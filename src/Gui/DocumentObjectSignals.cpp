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


#include <string>
#include <map>
#include <vector>
#include <QTimer>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Transactions.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Matrix.h>

#include "Document.h"
#include "Application.h"
#include "Control.h"
#include "MainWindow.h"
#include "Tree.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

//*****************************************************************************************************
// Document
//*****************************************************************************************************
void Document::slotNewObject(const App::DocumentObject& Obj)
{
    auto pcProvider = static_cast<ViewProviderDocumentObject*>(getViewProvider(&Obj));
    if (!pcProvider) {
        std::string cName = Obj.getViewProviderNameStored();
        for (;;) {
            if (cName.empty()) {
                // handle document object with no view provider specified
                FC_LOG(Obj.getFullName() << " has no view provider specified");
                return;
            }
            // The file states which view provider each object had, and importing from that
            // name made a document decide what code this session ran -- the same hole as the
            // object's own type, one layer up. Every installed module is loaded at startup, so
            // the name is looked up and never fetched.
            Base::Type type = Base::Type::getTypeIfDerivedFrom(
                cName.c_str(),
                ViewProviderDocumentObject::getClassTypeId(),
                false
            );
            pcProvider = static_cast<ViewProviderDocumentObject*>(type.createInstance());
            // createInstance could return a null pointer
            if (!pcProvider) {
                // type not derived from ViewProviderDocumentObject!!!
                FC_ERR("Invalid view provider type '" << cName << "' for " << Obj.getFullName());
                return;
            }
            else if (cName != Obj.getViewProviderName() && !pcProvider->allowOverride(Obj)) {
                FC_WARN("View provider type '" << cName << "' does not support " << Obj.getFullName());
                delete pcProvider;
                pcProvider = nullptr;
                cName = Obj.getViewProviderName();
            }
            else {
                break;
            }
        }

        setModified(true);
        d->_ViewProviderMap[&Obj] = pcProvider;
        d->_CoinMap[pcProvider->getRoot()] = pcProvider;
        pcProvider->setStatus(Gui::ViewStatus::TouchDocument, d->_changeViewTouchDocument);

        try {
            // if successfully created set the right name and calculate the view
            // FIXME: Consider to change argument of attach() to const pointer
            pcProvider->attach(const_cast<App::DocumentObject*>(&Obj));
            pcProvider->updateView();
            pcProvider->setActiveMode();
        }
        catch (const Base::MemoryException& e) {
            FC_ERR("Memory exception in " << Obj.getFullName() << " thrown: " << e.what());
        }
        catch (Base::Exception& e) {
            e.reportException();
        }
#ifndef FC_DEBUG
        catch (...) {
            FC_ERR("Unknown exception in Feature " << Obj.getFullName() << " thrown");
        }
#endif
    }
    else {
        try {
            pcProvider->reattach(const_cast<App::DocumentObject*>(&Obj));
        }
        catch (Base::Exception& e) {
            e.reportException();
        }
    }

    if (pcProvider) {
        // cycling to all views of the document
        for (auto* v : d->baseViews) {
            auto activeView = dynamic_cast<View3DInventor*>(v);
            if (activeView) {
                activeView->getViewer()->addViewProvider(pcProvider);
            }
        }

        // adding to the tree
        signalNewObject(*pcProvider);
        pcProvider->pcDocument = this;

        // it is possible that a new viewprovider already claims children
        handleChildren3D(pcProvider);
        // ... and that its arrival contests a child some other provider was parenting
        reconcileContested3D(pcProvider);
        if (d->_isTransacting) {
            d->_redoViewProviders.push_back(pcProvider);
        }
    }
}

void Document::slotDeletedObject(const App::DocumentObject& Obj)
{
    setModified(true);

    // cycling to all views of the document
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (!viewProvider) {
        return;
    }

    if (d->_editViewProvider == viewProvider || d->_editViewProviderParent == viewProvider) {
        _resetEdit();
    }
    else {
        Application::Instance->unsetEditDocumentIf([&viewProvider](Gui::Document* editdoc) {
            return editdoc->d->_editViewProvider == viewProvider
                || editdoc->d->_editViewProviderParent == viewProvider;
        });
    }

    handleChildren3D(viewProvider, true);

    if (viewProvider && viewProvider->isDerivedFrom(ViewProviderDocumentObject::getClassTypeId())) {
        // go through the views
        for (auto* v : d->baseViews) {
            auto activeView = dynamic_cast<View3DInventor*>(v);
            if (activeView) {
                activeView->getViewer()->removeViewProvider(viewProvider);
            }
        }

        // removing from tree
        signalDeletedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }

    viewProvider->beforeDelete();
}

void Document::beforeDelete()
{
    Application::Instance->unsetEditDocumentIf([this](Gui::Document* editDoc) {
        auto vp = freecad_cast<ViewProviderDocumentObject*>(editDoc->d->_editViewProvider);
        auto vpp = freecad_cast<ViewProviderDocumentObject*>(editDoc->d->_editViewProviderParent);

        return editDoc == this || (vp && vp->getDocument() == this)
            || (vpp && vpp->getDocument() == this);
    });
    for (auto& v : d->_ViewProviderMap) {
        v.second->beforeDelete();
    }
}

void Document::slotChangedObject(const App::DocumentObject& Obj, const App::Property& Prop)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider) {
        try {
            viewProvider->update(&Prop);
            if (d->_editingViewer && d->_editingObject && d->_editViewProviderParent
                && (Prop.isDerivedFrom<App::PropertyPlacement>()
                    // Issue ID 0004230 : getName() can return null in which case strstr() crashes
                    || (Prop.getName() && strstr(Prop.getName(), "Scale")))
                && d->_editObjs.contains(&Obj)) {
                Base::Matrix4D mat;
                auto sobj = d->_editViewProviderParent->getObject()
                                ->getSubObject(d->_editSubname.c_str(), nullptr, &mat);
                if (sobj == d->_editingObject && d->_editingTransform != mat) {
                    d->_editingTransform = mat;
                    d->_editingViewer->setEditingTransform(d->_editingTransform);
                }
            }
        }
        catch (const Base::MemoryException& e) {
            FC_ERR("Memory exception in " << Obj.getFullName() << " thrown: " << e.what());
        }
        catch (Base::Exception& e) {
            e.reportException();
        }
        catch (const std::exception& e) {
            FC_ERR("C++ exception in " << Obj.getFullName() << " thrown " << e.what());
        }
        catch (...) {
            FC_ERR("Cannot update representation for " << Obj.getFullName());
        }

        handleChildren3D(viewProvider);
        // Repointing a feature's input can contest or release a shared object elsewhere.
        reconcileContested3D(viewProvider);

        if (viewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
            signalChangedObject(static_cast<ViewProviderDocumentObject&>(*viewProvider), Prop);
        }
    }

    // a property of an object has changed
    if (!Prop.testStatus(App::Property::NoModify) && !isModified()) {
        FC_LOG(Prop.getFullName() << " modified");
        setModified(true);
    }

    getMainWindow()->updateActions(true);
    scheduleRebuild();
}

void Document::slotRelabelObject(const App::DocumentObject& Obj)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider && viewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
        signalRelabelObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }
}

void Document::slotTransactionAppend(const App::DocumentObject& obj, App::Transaction* transaction)
{
    ViewProvider* viewProvider = getViewProvider(&obj);
    if (viewProvider && viewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
        transaction->addObjectDel(viewProvider);
    }
}

void Document::slotTransactionRemove(const App::DocumentObject& obj, App::Transaction* transaction)
{
    std::map<const App::DocumentObject*, ViewProviderDocumentObject*>::const_iterator it
        = d->_ViewProviderMap.find(&obj);
    if (it != d->_ViewProviderMap.end()) {
        ViewProvider* viewProvider = it->second;

        auto itC = d->_CoinMap.find(viewProvider->getRoot());
        if (itC != d->_CoinMap.end()) {
            d->_CoinMap.erase(itC);
        }

        d->_ViewProviderMap.erase(&obj);
        // transaction being a nullptr indicates that undo/redo is off and the object
        // can be safely deleted
        if (transaction) {
            transaction->addObjectNew(viewProvider);
        }
        else {
            delete viewProvider;
        }
    }
}

void Document::slotActivatedObject(const App::DocumentObject& Obj)
{
    ViewProvider* viewProvider = getViewProvider(&Obj);
    if (viewProvider && viewProvider->isDerivedFrom<ViewProviderDocumentObject>()) {
        signalActivatedObject(*(static_cast<ViewProviderDocumentObject*>(viewProvider)));
    }
}

void Document::slotUndoDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }

    signalUndoDocument(*this);
    getMainWindow()->updateActions();
    scheduleRebuild();
}

void Document::slotRedoDocument(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }

    signalRedoDocument(*this);
    getMainWindow()->updateActions();
    scheduleRebuild();
}

void Document::slotRecomputed(const App::Document& doc)
{
    if (d->_pcDocument != &doc) {
        return;
    }
    getMainWindow()->updateActions();
    TreeWidget::updateStatus();
}

// This function is called when some asks to recompute a document that is marked
// as 'SkipRecompute'. We'll check if we are the current document, and if either
// not given an explicit recomputing object list, or the given single object is
// the eidting object or the active object. If the conditions are met, we'll
// force recompute only that object and all its dependent objects.
void Document::slotSkipRecompute(const App::Document& doc, const std::vector<App::DocumentObject*>& objs)
{
    if (d->_pcDocument != &doc) {
        return;
    }
    if (objs.size() > 1 || App::GetApplication().getActiveDocument() != &doc
        || !doc.testStatus(App::Document::AllowPartialRecompute)) {
        return;
    }
    App::DocumentObject* obj = nullptr;

    if (Gui::Application::Instance->isInEdit(this)) {
        auto vp = freecad_cast<ViewProviderDocumentObject*>(getInEdit());
        if (vp) {
            obj = vp->getObject();
        }
    }
    if (objs.size() > 1 || App::GetApplication().getActiveDocument() != &doc
        || !doc.testStatus(App::Document::AllowPartialRecompute)) {
        return;
    }

    auto editDoc = Application::Instance->editDocument();
    if (editDoc) {
        auto vp = freecad_cast<ViewProviderDocumentObject*>(editDoc->getInEdit());
        if (vp) {
            obj = vp->getObject();
        }
    }
    if (!obj) {
        obj = doc.getActiveObject();
    }
    if (!obj || !obj->isAttachedToDocument() || (!objs.empty() && objs.front() != obj)) {
        return;
    }
    obj->recomputeFeature(true);
}

void Document::slotTouchedObject(const App::DocumentObject& Obj)
{
    getMainWindow()->updateActions(true);
    if (!isModified()) {
        FC_LOG(Obj.getFullName() << " touched");
        setModified(true);
    }
    scheduleRebuild();
}

void Document::slotCommitTransaction(const App::Document&)
{
    scheduleRebuild();
}

void Document::scheduleRebuild()
{
    // What a rebuild itself changes must not schedule the next one, or a step that stays
    // stale after failing would be rebuilt forever.
    if (d->_rebuildScheduled || d->_isClosing
        || d->_pcDocument->testStatus(App::Document::Recomputing)) {
        return;
    }
    d->_rebuildScheduled = true;
    QTimer::singleShot(0, [name = std::string(d->_pcDocument->getName())] {
        auto* doc = App::GetApplication().getDocument(name.c_str());
        if (auto* guiDoc = doc ? Application::Instance->getDocument(doc) : nullptr) {
            guiDoc->rebuildIfStale();
        }
    });
}

void Document::rebuildIfStale()
{
    d->_rebuildScheduled = false;
    App::Document* doc = d->_pcDocument;
    // An open edit or command rebuilds on its own terms; its commit or close comes back here.
    if (d->_isClosing || doc->testStatus(App::Document::SkipRecompute)
        || doc->testStatus(App::Document::Restoring) || doc->testStatus(App::Document::Recomputing)
        || App::GetApplication().isRestoring() || doc->isPerformingTransaction()
        || d->_isTransacting || doc->hasPendingTransaction()
        || Application::Instance->editDocument() || Control().activeDialog()) {
        return;
    }
    if (!doc->mustExecute()) {
        return;
    }
    try {
        doc->recompute({}, false, nullptr, App::Document::DepNoCycle);
    }
    catch (const Base::BadGraphError&) {
        // A cycle stays stale and marked; the user resolves it through an explicit recompute.
    }
    catch (const Base::Exception& e) {
        e.reportException();
    }
}

void Document::slotChangePropertyEditor(const App::Document& doc, const App::Property& Prop)
{
    if (getDocument() == &doc) {
        FC_LOG(Prop.getFullName() << " editor changed");
        setModified(true);
    }
}
