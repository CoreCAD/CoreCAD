/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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


# include <sstream>


#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Link.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>

#include "DrawViewCollection.h"


using namespace TechDraw;

//===========================================================================
// DrawViewCollection
//===========================================================================

PROPERTY_SOURCE(TechDraw::DrawViewCollection, TechDraw::DrawView)

DrawViewCollection::DrawViewCollection()
{
    nowUnsetting = false;
    // Membership is not stored: a member view names this collection through DrawView::Collection,
    // and getViews() derives the list. There is no Views property to declare.
}

DrawViewCollection::~DrawViewCollection()
{
}

void DrawViewCollection::onChanged(const App::Property* prop)
{
    TechDraw::DrawView::onChanged(prop);
}

short DrawViewCollection::mustExecute() const
{
    // Membership changes touch this object directly (see addView/removeView); there is no
    // stored Views list to watch.
    return TechDraw::DrawView::mustExecute();
}

App::DocumentObjectExecReturn *DrawViewCollection::execute()
{
    if (!keepUpdated()) {
        return App::DocumentObject::StdReturn;
    }

    lockChildren();

    overrideKeepUpdated(false);
    return DrawView::execute();
}

int DrawViewCollection::addView(App::DocumentObject* docObj)
{
    // A view joins the collection by naming it. The collection stores no member list; the edge
    // lives on the item (DrawView::Collection), the same direction a view names its page.
    auto* view = freecad_cast<DrawView*>(docObj);
    if (!view) {
        return -1;
    }

    view->Collection.setValue(this);

    // Membership changed. There is no property on this object to touch, so touch the object
    // itself to re-run execute() (arrangement), as the old Views.isTouched() trigger did.
    touch();

    return (int)getViews().size();
}

int DrawViewCollection::removeView(App::DocumentObject* docObj)
{
    // Taking a view out of the collection is the view forgetting the collection.
    auto* view = freecad_cast<DrawView*>(docObj);
    if (view && view->Collection.getValue() == this) {
        view->Collection.setValue(nullptr);
        touch();
    }

    return (int)getViews().size();
}

std::vector<App::DocumentObject*> DrawViewCollection::getViews() const
{
    // Derived, never stored: the members are the views naming this collection.
    std::vector<App::DocumentObject*> allViews;
    if (!isAttachedToDocument()) {
        return allViews;
    }
    for (auto* obj : getDocument()->getObjectsOfType<DrawView>()) {
        auto* view = static_cast<DrawView*>(obj);
        if (view->getCollection() == this) {
            allViews.push_back(view);
        }
    }
    return allViews;
}

int DrawViewCollection::countChildren()
{
    //Count the children recursively if needed
    int numChildren = 0;

    for(auto* view : getViews()) {
        if(view->isDerivedFrom<DrawViewCollection>()) {
            auto *viewCollection = static_cast<DrawViewCollection *>(view);
            numChildren += viewCollection->countChildren() + 1;
        }
        else {
            numChildren += 1;
        }
    }
    return numChildren;
}

void DrawViewCollection::onDocumentRestored()
{
    DrawView::execute();
}

void DrawViewCollection::lockChildren()
{
    for (auto& v : getViews()) {
        auto *view = dynamic_cast<DrawView *>(v);
        if (!view) {
            throw Base::ValueError("DrawViewCollection::lockChildren bad View\n");
        }
        view->handleXYLock();
    }
}

void DrawViewCollection::unsetupObject()
{
    nowUnsetting = true;

    // Remove the collection's views from document. Membership is derived, so read it before
    // anything is deleted; there is no stored list to clear afterwards.
    std::string docName = getDocument()->getName();

    for (auto* view : getViews()) {
        if (view->isAttachedToDocument()) {
            std::string viewName = view->getNameInDocument();
            Base::Interpreter().runStringArg("App.getDocument(\"%s\").removeObject(\"%s\")",
                                              docName.c_str(), viewName.c_str());
        }
    }
}

QRectF DrawViewCollection::getRect() const
{
    QRectF result;
    for (auto& v : getViews()) {
        auto *view = freecad_cast<DrawView*>(v);
        if (!view) {
            throw Base::ValueError("DrawViewCollection::getRect bad View\n");
        }

        result = result.united(view->getRect().translated(view->X.getValue(), view->Y.getValue()));
    }
    return { 0, 0, getScale() * result.width(), getScale() * result.height()};
}
