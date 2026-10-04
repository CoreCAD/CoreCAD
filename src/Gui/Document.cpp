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


#include <list>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Transactions.h>
#include <Base/Matrix.h>
#include <Base/Tools.h>

#include "Document.h"
#include "DocumentPy.h"
#include "Application.h"
#include "MDIView.h"
#include "Tree.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

using namespace Gui;
namespace sp = std::placeholders;

/* TRANSLATOR Gui::Document */

/// @namespace Gui @class Document

int Document::_iDocCount = 0;

Document::Document(App::Document* pcDocument, Application* app)
{
    d = new DocumentP;
    d->_iWinCount = 1;
    // new instance
    d->_iDocId = (++_iDocCount);
    d->_isClosing = false;
    d->_isModified = false;
    d->_isTransacting = false;
    d->_isActive = false;
    d->_pcAppWnd = app;
    d->_pcDocument = pcDocument;
    d->_editViewProvider = nullptr;
    d->_editViewProviderPrevious = nullptr;
    d->_editingObject = nullptr;
    d->_editViewProviderParent = nullptr;
    d->_editingViewer = nullptr;
    d->_editMode = 0;
    d->_editModePrevious = 0;
    d->_editWantsRestore = false;
    d->_editWantsRestorePrevious = false;

    // NOLINTBEGIN
    //  Setup the connections
    d->connectNewObject = pcDocument->signalNewObject.connect(
        std::bind(&Gui::Document::slotNewObject, this, sp::_1)
    );
    d->connectDelObject = pcDocument->signalDeletedObject.connect(
        std::bind(&Gui::Document::slotDeletedObject, this, sp::_1)
    );
    d->connectCngObject = pcDocument->signalChangedObject.connect(
        std::bind(&Gui::Document::slotChangedObject, this, sp::_1, sp::_2)
    );
    d->connectRenObject = pcDocument->signalRelabelObject.connect(
        std::bind(&Gui::Document::slotRelabelObject, this, sp::_1)
    );
    d->connectActObject = pcDocument->signalActivatedObject.connect(
        std::bind(&Gui::Document::slotActivatedObject, this, sp::_1),
        fastsignals::advanced_tag()
    );
    d->connectActObjectBlocker = fastsignals::shared_connection_block(d->connectActObject, false);
    d->connectSaveDocument = pcDocument->signalSaveDocument.connect(
        std::bind(&Gui::Document::Save, this, sp::_1)
    );
    d->connectFinishSaveDocument = pcDocument->signalFinishSave.connect(
        std::bind(&Gui::Document::slotFinishSaveDocument, this, sp::_1, sp::_2)
    );
    d->connectRestDocument = pcDocument->signalRestoreDocument.connect(
        std::bind(&Gui::Document::Restore, this, sp::_1)
    );
    d->connectStartLoadDocument = App::GetApplication().signalStartRestoreDocument.connect(
        std::bind(&Gui::Document::slotStartRestoreDocument, this, sp::_1)
    );
    d->connectFinishLoadDocument = App::GetApplication().signalFinishRestoreDocument.connect(
        std::bind(&Gui::Document::slotFinishRestoreDocument, this, sp::_1)
    );
    d->connectShowHidden = App::GetApplication().signalShowHidden.connect(
        std::bind(&Gui::Document::slotShowHidden, this, sp::_1)
    );

    d->connectChangePropertyEditor = pcDocument->signalChangePropertyEditor.connect(
        std::bind(&Gui::Document::slotChangePropertyEditor, this, sp::_1, sp::_2)
    );
    d->connectChangeDocument
        = d->_pcDocument->signalChanged.connect  // use the same slot function
          (std::bind(&Gui::Document::slotChangePropertyEditor, this, sp::_1, sp::_2),
           fastsignals::advanced_tag());
    d->connectChangeDocumentBlocker
        = fastsignals::shared_connection_block(d->connectChangeDocument, true);
    d->connectFinishRestoreObject = pcDocument->signalFinishRestoreObject.connect(
        std::bind(&Gui::Document::slotFinishRestoreObject, this, sp::_1)
    );
    d->connectExportObjects = pcDocument->signalExportViewObjects.connect(
        std::bind(&Gui::Document::exportObjects, this, sp::_1, sp::_2)
    );
    d->connectImportObjects = pcDocument->signalImportViewObjects.connect(
        std::bind(&Gui::Document::importObjects, this, sp::_1, sp::_2, sp::_3)
    );
    d->connectFinishImportObjects = pcDocument->signalFinishImportObjects.connect(
        std::bind(&Gui::Document::slotFinishImportObjects, this, sp::_1)
    );

    d->connectUndoDocument = pcDocument->signalUndo.connect(
        std::bind(&Gui::Document::slotUndoDocument, this, sp::_1)
    );
    d->connectRedoDocument = pcDocument->signalRedo.connect(
        std::bind(&Gui::Document::slotRedoDocument, this, sp::_1)
    );
    d->connectRecomputed = pcDocument->signalRecomputed.connect(
        std::bind(&Gui::Document::slotRecomputed, this, sp::_1)
    );
    d->connectSkipRecompute = pcDocument->signalSkipRecompute.connect(
        std::bind(&Gui::Document::slotSkipRecompute, this, sp::_1, sp::_2)
    );
    d->connectTouchedObject = pcDocument->signalTouchedObject.connect(
        std::bind(&Gui::Document::slotTouchedObject, this, sp::_1)
    );
    d->connectCommitTransaction = pcDocument->signalCommitTransaction.connect(
        std::bind(&Gui::Document::slotCommitTransaction, this, sp::_1)
    );

    d->connectTransactionAppend = pcDocument->signalTransactionAppend.connect(
        std::bind(&Gui::Document::slotTransactionAppend, this, sp::_1, sp::_2)
    );
    d->connectTransactionRemove = pcDocument->signalTransactionRemove.connect(
        std::bind(&Gui::Document::slotTransactionRemove, this, sp::_1, sp::_2)
    );
    // NOLINTEND

    // pointer to the python class
    // NOTE: As this Python object doesn't get returned to the interpreter we
    // mustn't increment it (Werner Jan-12-2006)
    Base::PyGILStateLocker lock;
    _pcDocPy = new Gui::DocumentPy(this);

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Document"
    );
    if (hGrp->GetBool("UsingUndo", true)) {
        d->_pcDocument->setUndoMode(1);
        // set the maximum stack size
        d->_pcDocument->setMaxUndoStackSize(hGrp->GetInt("MaxUndoSize", 20));
    }

    d->_changeViewTouchDocument = hGrp->GetBool("ChangeViewProviderTouchDocument", true);
}

Document::~Document()
{
    // disconnect everything to avoid to be double-deleted
    // in case an exception is raised somewhere
    d->connectNewObject.disconnect();
    d->connectDelObject.disconnect();
    d->connectCngObject.disconnect();
    d->connectRenObject.disconnect();
    d->connectActObject.disconnect();
    d->connectSaveDocument.disconnect();
    d->connectFinishSaveDocument.disconnect();
    d->connectRestDocument.disconnect();
    d->connectStartLoadDocument.disconnect();
    d->connectFinishLoadDocument.disconnect();
    d->connectShowHidden.disconnect();
    d->connectFinishRestoreObject.disconnect();
    d->connectExportObjects.disconnect();
    d->connectImportObjects.disconnect();
    d->connectFinishImportObjects.disconnect();
    d->connectUndoDocument.disconnect();
    d->connectRedoDocument.disconnect();
    d->connectRecomputed.disconnect();
    d->connectSkipRecompute.disconnect();
    d->connectTransactionAppend.disconnect();
    d->connectTransactionRemove.disconnect();
    d->connectTouchedObject.disconnect();
    d->connectCommitTransaction.disconnect();
    d->connectChangePropertyEditor.disconnect();
    d->connectChangeDocument.disconnect();

    // e.g. if document gets closed from within a Python command
    d->_isClosing = true;
    // calls Document::detachView() and alter the view list
    std::list<Gui::BaseView*> temp = d->baseViews;
    for (auto& it : temp) {
        it->deleteSelf();
    }

    for (const auto& vp : d->_ViewProviderMap) {
        delete vp.second;
    }

    for (const auto& va : d->_ViewProviderMapAnnotation) {
        delete va.second;
    }

    // remove the reference from the object
    Base::PyGILStateLocker lock;
    _pcDocPy->setInvalid();
    _pcDocPy->DecRef();
    delete d;
}

void Document::setAnnotationViewProvider(const char* name, ViewProvider* pcProvider)
{
    // already in ?
    std::map<std::string, ViewProvider*>::iterator it = d->_ViewProviderMapAnnotation.find(name);
    if (it != d->_ViewProviderMapAnnotation.end()) {
        removeAnnotationViewProvider(name);
    }

    // add
    d->_ViewProviderMapAnnotation[name] = pcProvider;

    // cycling to all views of the document
    for (auto* v : d->baseViews) {
        auto activeView = dynamic_cast<View3DInventor*>(v);
        if (activeView) {
            activeView->getViewer()->addViewProvider(pcProvider);
        }
    }
}

ViewProvider* Document::getAnnotationViewProvider(const char* name) const
{
    std::map<std::string, ViewProvider*>::const_iterator it = d->_ViewProviderMapAnnotation.find(name);
    return ((it != d->_ViewProviderMapAnnotation.end()) ? it->second : 0);
}

bool Document::isAnnotationViewProvider(const ViewProvider* vp) const
{
    for (const auto& va : d->_ViewProviderMapAnnotation) {
        if (va.second == vp) {
            return true;
        }
    }
    return false;
}

ViewProvider* Document::takeAnnotationViewProvider(const char* name)
{
    auto it = d->_ViewProviderMapAnnotation.find(name);
    if (it == d->_ViewProviderMapAnnotation.end()) {
        return nullptr;
    }

    ViewProvider* vp = it->second;
    d->_ViewProviderMapAnnotation.erase(it);

    // cycling to all views of the document
    for (auto vIt : d->baseViews) {
        if (auto activeView = dynamic_cast<View3DInventor*>(vIt)) {
            activeView->getViewer()->removeViewProvider(vp);
        }
    }

    return vp;
}

void Document::removeAnnotationViewProvider(const char* name)
{
    delete takeAnnotationViewProvider(name);
}

ViewProvider* Document::getViewProvider(const App::DocumentObject* Feat) const
{
    std::map<const App::DocumentObject*, ViewProviderDocumentObject*>::const_iterator it
        = d->_ViewProviderMap.find(Feat);
    return ((it != d->_ViewProviderMap.end()) ? it->second : 0);
}

std::vector<ViewProvider*> Document::getViewProvidersOfType(const Base::Type& typeId) const
{
    std::vector<ViewProvider*> Objects;
    for (const auto& vp : d->_ViewProviderMap) {
        if (vp.second->isDerivedFrom(typeId)) {
            Objects.push_back(vp.second);
        }
    }
    return Objects;
}

ViewProvider* Document::getViewProviderByName(const char* name) const
{
    // first check on feature name
    App::DocumentObject* pcFeat = getDocument()->getObject(name);

    if (pcFeat) {
        std::map<const App::DocumentObject*, ViewProviderDocumentObject*>::const_iterator it
            = d->_ViewProviderMap.find(pcFeat);

        if (it != d->_ViewProviderMap.end()) {
            return it->second;
        }
    }
    else {
        // then try annotation name
        std::map<std::string, ViewProvider*>::const_iterator it2
            = d->_ViewProviderMapAnnotation.find(name);

        if (it2 != d->_ViewProviderMapAnnotation.end()) {
            return it2->second;
        }
    }

    return nullptr;
}

bool Document::isShow(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);
    return pcProv ? pcProv->isShow() : false;
}

/// put the feature in show
void Document::setShow(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);

    if (pcProv && pcProv->isDerivedFrom<ViewProviderDocumentObject>()) {
        static_cast<ViewProviderDocumentObject*>(pcProv)->Visibility.setValue(true);
    }
}

/// set the feature in Noshow
void Document::setHide(const char* name)
{
    ViewProvider* pcProv = getViewProviderByName(name);

    if (pcProv && pcProv->isDerivedFrom<ViewProviderDocumentObject>()) {
        static_cast<ViewProviderDocumentObject*>(pcProv)->Visibility.setValue(false);
    }
}

/// set the feature in Noshow
void Document::setPos(const char* name, const Base::Matrix4D& rclMtrx)
{
    ViewProvider* pcProv = getViewProviderByName(name);
    if (pcProv) {
        pcProv->setTransformation(rclMtrx);
    }
}

void Document::addViewProvider(Gui::ViewProviderDocumentObject* vp)
{
    // Hint: The undo/redo first adds the view provider to the Gui
    // document before adding the objects to the App document.

    // the view provider is added by TransactionViewProvider and an
    // object can be there only once
    assert(d->_ViewProviderMap.find(vp->getObject()) == d->_ViewProviderMap.end());
    vp->setStatus(Detach, false);
    d->_ViewProviderMap[vp->getObject()] = vp;
    d->_CoinMap[vp->getRoot()] = vp;
}

void Document::setModified(bool b)
{
    if (d->_isModified == b) {
        return;
    }
    d->_isModified = b;

    std::list<MDIView*> mdis = getMDIViews();
    for (auto& mdi : mdis) {
        mdi->setWindowModified(b);
    }
}

bool Document::isModified() const
{
    return d->_isModified;
}

void Document::setWorkbench(const std::string& name)
{
    d->_workbenchName = name;
}

std::string Document::workbench() const
{
    return d->_workbenchName;
}

bool Document::isAboutToClose() const
{
    return d->_isClosing;
}

ViewProviderDocumentObject* Document::getViewProviderByPathFromTail(SoPath* path) const
{
    // Get the lowest root node in the pick path!
    for (int i = 0; i < path->getLength(); i++) {
        SoNode* node = path->getNodeFromTail(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if (it != d->_CoinMap.end()) {
                return it->second;
            }
        }
    }

    return nullptr;
}

ViewProviderDocumentObject* Document::getViewProviderByPathFromHead(SoPath* path) const
{
    for (int i = 0; i < path->getLength(); i++) {
        SoNode* node = path->getNode(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if (it != d->_CoinMap.end()) {
                return it->second;
            }
        }
    }

    return nullptr;
}

ViewProviderDocumentObject* Document::getViewProvider(SoNode* node) const
{
    if (!node || !node->isOfType(SoSeparator::getClassTypeId())) {
        return nullptr;
    }
    auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
    if (it != d->_CoinMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::pair<ViewProviderDocumentObject*, int>> Document::getViewProvidersByPath(
    SoPath* path
) const
{
    std::vector<std::pair<ViewProviderDocumentObject*, int>> ret;
    for (int i = 0; i < path->getLength(); i++) {
        SoNode* node = path->getNodeFromTail(i);
        if (node->isOfType(SoSeparator::getClassTypeId())) {
            auto it = d->_CoinMap.find(static_cast<SoSeparator*>(node));
            if (it != d->_CoinMap.end()) {
                ret.emplace_back(it->second, i);
            }
        }
    }
    return ret;
}

App::Document* Document::getDocument() const
{
    return d->_pcDocument;
}

void Document::setIsActive(bool active)
{
    d->_isActive = active;
    if (d->_editViewProvider) {
        d->_editViewProvider->setActive(active);
    }
}

bool Document::isActive() const
{
    return d->_isActive;
}

PyObject* Document::getPyObject()
{
    _pcDocPy->IncRef();
    return _pcDocPy;
}

std::vector<App::DocumentObject*> Document::getTreeRootObjects() const
{
    std::vector<App::DocumentObject*> docObjects = d->_pcDocument->getObjects();
    std::unordered_map<App::DocumentObject*, bool> rootMap;
    for (auto it : docObjects) {
        rootMap[it] = true;
    }

    for (auto obj : docObjects) {
        ViewProvider* vp = Application::Instance->getViewProvider(obj);
        if (!vp) {
            continue;
        }

        std::vector<App::DocumentObject*> children = vp->claimChildren();
        for (auto child : children) {
            rootMap[child] = false;
        }
    }

    std::vector<App::DocumentObject*> rootObjs;
    for (const auto& it : rootMap) {
        if (it.second) {
            rootObjs.push_back(it.first);
        }
    }
    return rootObjs;
}
