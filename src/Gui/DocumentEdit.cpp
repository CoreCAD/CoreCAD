/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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


#include <tuple>
#include <string>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Transactions.h>
#include <App/ElementNamingUtils.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Matrix.h>

#include "Document.h"
#include "Application.h"
#include "Control.h"
#include "MainWindow.h"
#include "Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

namespace Gui
{

ViewProviderDocumentObject* DocumentP::throwIfCastFails(ViewProvider* p)
{
    if (auto vp = freecad_cast<ViewProviderDocumentObject*>(p)) {
        return vp;
    }

    throw Base::RuntimeError("cannot edit non ViewProviderDocumentObject");
}

App::DocumentObject* DocumentP::tryGetObject(ViewProviderDocumentObject* vp)
{
    auto obj = vp->getObject();
    if (!obj->isAttachedToDocument()) {
        throw Base::RuntimeError("cannot edit detached object");
    }

    return obj;
}

void DocumentP::throwIfNotInMap(App::DocumentObject* obj, App::Document* doc) const
{
    if (_ViewProviderMap.find(obj) == _ViewProviderMap.end()) {
        // We can actually support editing external object, by calling
        // View3DInventViewer::setupEditingRoot() before exiting from
        // ViewProvider::setEditViewer(), which transfer all child node of the view
        // provider into an editing node inside the viewer of this document. And
        // that's may actually be the case, as the subname referenced sub object
        // is allowed to be in other documents.
        //
        // We just disabling editing external parent object here, for bug
        // tracking purpose. Because, bringing an unrelated external object to
        // the current view for editing will confuse user, and is certainly a
        // bug. By right, the top parent object should always belong to the
        // editing document, and the actually editing sub object can be
        // external.
        //
        // So, you can either call setEdit() with subname set to 0, which cause
        // the code above to auto detect selection context, and dispatch the
        // editing call to the correct document. Or, supply subname yourself,
        // and make sure you get the document right.
        //
        std::stringstream str;
        str << "cannot edit object '" << obj->getNameInDocument() << "': not found in document "
            << "'" << doc->getName() << "'";
        throw Base::RuntimeError(str.str());
    }
}

App::DocumentObject* DocumentP::tryGetSubObject(App::DocumentObject* obj, const char* subname)
{
    _editingTransform = Base::Matrix4D();
    auto sobj = obj->getSubObject(subname, nullptr, &_editingTransform);
    if (!sobj || !sobj->isAttachedToDocument()) {
        std::stringstream str;
        str << "Invalid sub object '" << obj->getFullName() << '.' << (subname ? subname : "") << "'";
        throw Base::RuntimeError(str.str());
    }

    return sobj;
}

ViewProviderDocumentObject* DocumentP::tryGetSubViewProvider(
    ViewProviderDocumentObject* vp,
    App::DocumentObject* obj,
    App::DocumentObject* sobj
) const
{
    auto svp = vp;
    if (sobj != obj) {
        svp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(sobj));
        if (!svp) {
            std::stringstream str;
            str << "Cannot edit '" << sobj->getFullName() << "' without view provider";
            throw Base::RuntimeError(str.str());
        }
    }

    return svp;
}

void DocumentP::setParentViewProvider(ViewProviderDocumentObject* vp)
{
    _editViewProviderParent = vp;
}

void DocumentP::clearSubElement()
{
    _editSubElement.clear();
    _editSubname.clear();
}

void DocumentP::findElementName(const char* subname)
{
    if (subname) {
        const char* element = Data::findElementName(subname);
        if (element) {
            _editSubname = std::string(subname, element - subname);
            _editSubElement = element;
        }
        else {
            _editSubname = subname;
        }
    }
}

void DocumentP::findSubObjectList(App::DocumentObject* obj, const char* subname)
{
    auto sobjs = obj->getSubObjectList(subname);
    _editObjs.clear();
    _editObjs.insert(sobjs.begin(), sobjs.end());
}

bool DocumentP::tryStartEditing(
    ViewProviderDocumentObject* vp,
    App::DocumentObject* obj,
    const char* subname,
    int ModNum
)
{
    auto sobj = tryGetSubObject(obj, subname);
    auto svp = tryGetSubViewProvider(vp, obj, sobj);

    setParentViewProvider(vp);
    clearSubElement();
    findElementName(subname);
    findSubObjectList(obj, subname);
    return tryStartEditing(svp, sobj, ModNum);
}

bool DocumentP::tryStartEditing(ViewProviderDocumentObject* svp, App::DocumentObject* sobj, int ModNum)
{
    _editingObject = sobj;
    _editMode = ModNum;
    _editViewProvider = svp;  // Used to resolve start editing (find the document in edit from
                              // within the viewprovider)
    _editViewProvider = svp->startEditing(ModNum);
    if (!_editViewProvider) {
        _editViewProviderParent = nullptr;
        _editObjs.clear();
        _editingObject = nullptr;
        FC_LOG("object '" << sobj->getFullName() << "' refuse to edit");
        return false;
    }

    return true;
}

void DocumentP::setEditingViewerIfPossible(View3DInventor* view3d, int ModNum)
{
    if (view3d) {
        view3d->getViewer()->setEditingViewProvider(_editViewProvider, ModNum);
        _editingViewer = view3d->getViewer();
    }
}

void DocumentP::signalEditMode()
{
    if (auto vpd = freecad_cast<ViewProviderDocumentObject*>(_editViewProvider)) {
        vpd->getDocument()->signalInEdit(*vpd);
    }
}

void DocumentP::setDocumentNameOfTaskDialog(App::Document* doc)
{
    Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog(_pcDocument);
    if (dlg) {
        dlg->setDocumentName(doc->getName());
    }
}

class ParentFinder
{
public:
    ParentFinder(App::DocumentObject* obj, ViewProviderDocumentObject* vp, const std::string& subname)
        : obj {obj}
        , vp {vp}
        , subname {subname}
    {}

    App::DocumentObject* getObject() const
    {
        return obj;
    }

    ViewProviderDocumentObject* getViewProvider() const
    {
        return vp;
    }

    std::string getSubname() const
    {
        return subname;
    }

    bool findParent()
    {
        auto result = findParentAndSubName(obj);
        App::DocumentObject* parentObj = std::get<0>(result);

        if (parentObj) {
            subname = std::get<1>(result);
            obj = parentObj;
            vp = findParentObject(parentObj, subname.c_str());
            return true;
        }

        return false;
    }

private:
    static std::tuple<App::DocumentObject*, std::string> findParentAndSubName(App::DocumentObject* obj)
    {
        // No subname reference is given, we try to extract one from the current
        // selection in order to obtain the correct transformation matrix below
        auto sels = Gui::Selection().getCompleteSelection(ResolveMode::NoResolve);
        App::DocumentObject* parentObj = nullptr;
        std::string _subname;
        for (auto& sel : sels) {
            if (!sel.pObject || !sel.pObject->isAttachedToDocument()) {
                continue;
            }
            if (!parentObj) {
                parentObj = sel.pObject;
            }
            else if (parentObj != sel.pObject) {
                FC_LOG("Cannot deduce subname for editing, more than one parent?");
                parentObj = nullptr;
                break;
            }

            auto sobj = parentObj->getSubObject(sel.SubName);
            if (!sobj || (sobj != obj && sobj->getLinkedObject(true) != obj)) {
                FC_LOG("Cannot deduce subname for editing, subname mismatch");
                parentObj = nullptr;
                break;
            }

            _subname = sel.SubName;
        }

        return std::make_tuple(parentObj, _subname);
    }

    static Gui::ViewProviderDocumentObject* findParentObject(
        App::DocumentObject* parentObj,
        const char* subname
    )
    {
        FC_LOG("deduced editing reference " << parentObj->getFullName() << '.' << subname);
        auto vp = freecad_cast<ViewProviderDocumentObject*>(
            Application::Instance->getViewProvider(parentObj)
        );
        if (!vp || !vp->getDocument()) {
            throw Base::RuntimeError("invalid view provider for parent object");
        }

        return vp;
    }

private:
    App::DocumentObject* obj;
    ViewProviderDocumentObject* vp;
    std::string subname;
};

}  // namespace Gui

//*****************************************************************************************************
// 3D viewer handling
//*****************************************************************************************************

bool Document::setEdit(Gui::ViewProvider* p, int ModNum, const char* subname)
{
    try {
        return trySetEdit(p, ModNum, subname);
    }
    catch (const Base::Exception& e) {
        FC_ERR("" << e.what());
        return false;
    }
}

void Document::resetIfEditing()
{
    // Fix regression: https://forum.freecad.org/viewtopic.php?f=19&t=43629&p=371972#p371972
    // When an object is already in edit mode a subsequent call for editing is only possible
    // when resetting the currently edited object.
    if (d->_editViewProvider) {
        _resetEdit();
    }
}

View3DInventor* Document::openEditingView3D(const ViewProviderDocumentObject* vp)
{
    auto view3d = dynamic_cast<View3DInventor*>(getActiveView());
    // if the currently active view is not the 3d view search for it and activate it
    if (view3d) {
        getMainWindow()->setActiveWindow(view3d);
    }
    else {
        view3d = dynamic_cast<View3DInventor*>(setActiveView(vp));
    }

    return view3d;
}

View3DInventor* Document::openEditingView3D(const App::DocumentObject* obj)
{
    if (auto vp
        = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(obj))) {
        return openEditingView3D(vp);
    }

    return nullptr;
}

bool Document::trySetEdit(Gui::ViewProvider* p, int ModNum, const char* subname)
{
    auto vp = DocumentP::throwIfCastFails(p);

    auto obj = DocumentP::tryGetObject(vp);

    std::string _subname = subname ? subname : "";
    if (_subname.empty()) {
        ParentFinder finder(obj, vp, _subname);
        if (finder.findParent()) {
            _subname = finder.getSubname();
            obj = finder.getObject();
            vp = finder.getViewProvider();
            if (vp->getDocument() != this) {
                resetIfEditing();

                return vp->getDocument()->setEdit(vp, ModNum, _subname.c_str());
            }
        }
    }

    // Fix for #13852: When switching edit directly between sketches, resetIfEditing()
    // triggers unsetEdit() on the previous sketch which restores its selection.
    // This clobbers the selection of the new sketch that ParentFinder relies on.
    // Moving resetIfEditing() after ParentFinder ensures we resolve the parent context correctly
    // using the current selection before closing the previous edit.
    resetIfEditing();

    d->throwIfNotInMap(obj, getDocument());

    Application::Instance->setEditDocument(this);

    if (!d->tryStartEditing(vp, obj, _subname.c_str(), ModNum)) {
        d->setDocumentNameOfTaskDialog(getDocument());
        return false;
    }

    d->setDocumentNameOfTaskDialog(getDocument());

    auto view3d = openEditingView3D(vp);
    d->setEditingViewerIfPossible(view3d, ModNum);
    d->signalEditMode();

    return true;
}

const Base::Matrix4D& Document::getEditingTransform() const
{
    return d->_editingTransform;
}

void Document::setEditingTransform(const Base::Matrix4D& mat)
{
    d->_editObjs.clear();
    d->_editingTransform = mat;
    auto activeView = dynamic_cast<View3DInventor*>(getActiveView());
    if (activeView) {
        activeView->getViewer()->setEditingTransform(mat);
    }
}

void Document::resetEdit()
{
    bool vpIsNotNull = d->_editViewProvider != nullptr;
    bool vpHasChanged = d->_editViewProvider != d->_editViewProviderPrevious;
    int modeToRestore = d->_editModePrevious;
    Gui::ViewProvider* vpToRestore = d->_editViewProviderPrevious;
    bool shouldRestorePrevious = d->_editWantsRestorePrevious;

    Application::Instance->unsetEditDocument(this);

    if (vpIsNotNull && vpHasChanged && shouldRestorePrevious) {
        setEdit(vpToRestore, modeToRestore);
    }
    scheduleRebuild();
}

void Document::_resetEdit()
{
    if (d->_editViewProvider) {
        for (auto* v : d->baseViews) {
            auto activeView = dynamic_cast<View3DInventor*>(v);
            if (activeView) {
                activeView->getViewer()->resetEditingViewProvider();
            }
        }

        d->_editViewProvider->finishEditing();

        d->_editViewProviderPrevious = d->_editViewProvider;
        d->_editModePrevious = d->_editMode;
        d->_editWantsRestorePrevious = d->_editWantsRestore;
        d->_editWantsRestore = false;

        // Have to check d->_editViewProvider below, because there is a chance
        // the editing object gets deleted inside the above call to
        // 'finishEditing()', which will trigger our slotDeletedObject(), which
        // nullifies _editViewProvider.
        if (d->_editViewProvider
            && d->_editViewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
            auto vpd = static_cast<ViewProviderDocumentObject*>(d->_editViewProvider);
            vpd->getDocument()->signalResetEdit(*vpd);
        }
        d->_editViewProvider = nullptr;

        // The logic below is not necessary anymore, because this method is
        // changed into a private one,  _resetEdit(). And the exposed
        // resetEdit() above calls into Application->unsetEditDocument() which
        // will prevent recursive calling.

        App::GetApplication().commitTransaction(getDocument()->getBookedTransactionID());
    }
    d->_editViewProviderParent = nullptr;
    d->_editingViewer = nullptr;
    d->_editObjs.clear();
    d->_editingObject = nullptr;
    Application::Instance->unsetEditDocument(this);
}

ViewProvider* Document::getInEdit(
    ViewProviderDocumentObject** parentVp,
    std::string* subname,
    int* mode,
    std::string* subelement
) const
{
    if (parentVp) {
        *parentVp = d->_editViewProviderParent;
    }
    if (subname) {
        *subname = d->_editSubname;
    }
    if (subelement) {
        *subelement = d->_editSubElement;
    }
    if (mode) {
        *mode = d->_editMode;
    }

    if (d->_editViewProvider) {
        // there is only one 3d view which is in edit mode
        auto activeView = dynamic_cast<View3DInventor*>(getActiveView());
        if (activeView && activeView->getViewer()->isEditingViewProvider()) {
            return d->_editViewProvider;
        }
    }

    return nullptr;
}

ViewProvider* Document::getEditViewProvider() const
{
    return d->_editViewProvider;
}

void Document::setInEdit(ViewProviderDocumentObject* parentVp, const char* subname)
{
    if (d->_editViewProvider) {
        d->_editViewProviderParent = parentVp;
        d->_editSubname = subname ? subname : "";
    }
}

void Document::setEditRestore(bool askRestore)
{
    d->_editWantsRestore = askRestore;
}
