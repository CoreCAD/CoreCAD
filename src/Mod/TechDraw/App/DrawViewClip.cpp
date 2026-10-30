/***************************************************************************
 *   Copyright (c) 2012 Yorik van Havre <yorik@uncreated.net>              *
 *   Copyright (c) 2015 WandererFan <wandererfan@gmail.com>                *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include <App/Document.h>
#include <App/Link.h>

#include "DrawViewClip.h"
#include "DrawPage.h"
#include <Mod/TechDraw/App/DrawViewClipPy.h>  // generated from DrawViewClipPy.xml


using namespace TechDraw;

//===========================================================================
// DrawViewClip
//===========================================================================

PROPERTY_SOURCE(TechDraw::DrawViewClip, TechDraw::DrawView)

DrawViewClip::DrawViewClip()
{
    static const char *group = "Clip Group";
    //App::PropertyType hidden = (App::PropertyType)(App::Prop_Hidden);

    ADD_PROPERTY_TYPE(Height     ,(100), group, App::Prop_None, "The height of the view area of this clip");
    ADD_PROPERTY_TYPE(Width      ,(100), group, App::Prop_None, "The width of the view area of this clip");
    ADD_PROPERTY_TYPE(ShowFrame  ,(0) ,group, App::Prop_None, "Specifies if the clip frame appears on the page or not");
    // Membership is not stored: a clipped view names this clip group through DrawView::ClipGroup,
    // and getViews() derives the list. There is no Views property to declare.

    // hide N/A properties
    ScaleType.setStatus(App::Property::ReadOnly, true);
    ScaleType.setStatus(App::Property::Hidden, true);
    Scale.setStatus(App::Property::ReadOnly, true);
    Scale.setStatus(App::Property::Hidden, true);
}

void DrawViewClip::onChanged(const App::Property* prop)
{
    if ((prop == &Height) ||
        (prop == &Width) ||
        (prop == &ShowFrame)) {
        requestPaint();
    }
    DrawView::onChanged(prop);
}

void DrawViewClip::addView(App::DocumentObject* docObj)
{
    if(docObj->isDerivedFrom<App::Link>()) {
        auto* link = static_cast<App::Link*>(docObj);
        docObj = link->getLinkedObject();
    }

    if (!docObj->isDerivedFrom<DrawView>()) {
        return;
    }
    auto* view = static_cast<DrawView*>(docObj);

    // The view joins the clip by naming it; the clip stores no member list.
    view->ClipGroup.setValue(this);
    touch();
    QRectF viewRect = view->getRectAligned();
    QPointF clipPoint(X.getValue(), Y.getValue());
    if (viewRect.contains(clipPoint)) {
        //position so the part of view that is overlapped by clip frame
        //stays in the clip frame
        double deltaX = view->X.getValue() - X.getValue();
        double deltaY = view->Y.getValue() - Y.getValue();
        view->X.setValue(deltaX);
        view->Y.setValue(deltaY);
    } else {
        //position in centre of clip group frame
        view->X.setValue(0.0);
        view->Y.setValue(0.0);
    }

    //reparent view to clip in tree
    auto page = findParentPage();
    if (page) {
        page->notifyMembershipChanged();
    }
}

void DrawViewClip::removeView(App::DocumentObject* docObj)
{
    if (docObj->isDerivedFrom<App::Link>()) {
        docObj = static_cast<App::Link*>(docObj)->getLinkedObject();
    }
    auto* view = freecad_cast<DrawView*>(docObj);
    if (view && view->ClipGroup.getValue() == this) {
        view->ClipGroup.setValue(nullptr);
        touch();
    }
}

std::vector<App::DocumentObject*> DrawViewClip::getViews() const
{
    // Derived, never stored: the members are the views naming this clip group.
    std::vector<App::DocumentObject*> allViews;
    if (!isAttachedToDocument()) {
        return allViews;
    }
    for (auto* obj : getDocument()->getObjectsOfType<DrawView>()) {
        auto* view = static_cast<DrawView*>(obj);
        if (view->getClipGroup() == this) {
            allViews.push_back(view);
        }
    }
    return allViews;
}

App::DocumentObjectExecReturn *DrawViewClip::execute()
{
    if (!keepUpdated()) {
        return App::DocumentObject::StdReturn;
    }

    std::vector<App::DocumentObject*> children = getViews();
    for (auto* obj : getViews()) {
        if (obj->isDerivedFrom<DrawView>()) {
            auto* view = static_cast<TechDraw::DrawView*>(obj);
            view->requestPaint();
        }
    }

    requestPaint();
    overrideKeepUpdated(false);
    return DrawView::execute();
}

//NOTE: DocumentObject::mustExecute returns 1/0 and not true/false
short DrawViewClip::mustExecute() const
{
    if (!isRestoring()) {
        if (Height.isTouched() ||
            Width.isTouched()) {
            return 1;
        }
    }
    return TechDraw::DrawView::mustExecute();
}

std::vector<std::string> DrawViewClip::getChildViewNames()
{
    std::vector<std::string> childNames;
    for (auto* obj : getViews()) {
        if (obj->isDerivedFrom<DrawView>()) {
            std::string name = obj->getNameInDocument();
            childNames.push_back(name);
        }
    }
    return childNames;
}

bool DrawViewClip::isViewInClip(App::DocumentObject* view)
{
    for (auto* obj : getViews()) {
        if (obj == view) {
            return true;
        }
    }
    return false;
}

PyObject *DrawViewClip::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new DrawViewClipPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}


// Python Drawing feature ---------------------------------------------------------

namespace App {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(TechDraw::DrawViewClipPython, TechDraw::DrawViewClip)
template<> const char* TechDraw::DrawViewClipPython::getViewProviderName() const {
    return "TechDrawGui::ViewProviderViewClip";
}
/// @endcond

// explicit template instantiation
template class TechDrawExport FeaturePythonT<TechDraw::DrawViewClip>;
}
