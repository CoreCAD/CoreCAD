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
#include <vector>
#include <cctype>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "Document.h"
#include "Application.h"
#include "Control.h"
#include "MainWindow.h"
#include "MDIView.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

using namespace Gui;

MDIView* Document::createView(const Base::Type& typeId, CreateViewMode mode)
{
    if (!typeId.isDerivedFrom(MDIView::getClassTypeId())) {
        return nullptr;
    }

    std::list<MDIView*> theViews = this->getMDIViewsOfType(typeId);
    if (typeId == View3DInventor::getClassTypeId()) {

        QOpenGLWidget* shareWidget = nullptr;
        // VBO rendering doesn't work correctly when we don't share the OpenGL widgets
        if (!theViews.empty()) {
            auto firstView = static_cast<View3DInventor*>(theViews.front());
            shareWidget = qobject_cast<QOpenGLWidget*>(firstView->getViewer()->getGLWidget());

            const std::string& camera = firstView->getCamera();
            saveCameraSettings(camera.c_str());
        }

        auto view3D = new View3DInventor(this, getMainWindow(), shareWidget);
        if (!theViews.empty()) {
            auto firstView = static_cast<View3DInventor*>(theViews.front());
            std::string overrideMode = firstView->getViewer()->getOverrideMode();
            view3D->getViewer()->setOverrideMode(overrideMode);
        }

        // attach the viewproviders. we need to make sure that we only attach the toplevel ones
        // and not viewproviders which are claimed by other providers. To ensure this we first
        // add all providers and then remove the ones already claimed

        std::vector<App::DocumentObject*> child_vps;
        for (const auto& vp : d->_ViewProviderMap) {
            view3D->getViewer()->addViewProvider(vp.second);
            std::vector<App::DocumentObject*> children = vp.second->claimChildren3D();
            child_vps.insert(child_vps.end(), children.begin(), children.end());
        }

        for (const auto& va : d->_ViewProviderMapAnnotation) {
            view3D->getViewer()->addViewProvider(va.second);
            std::vector<App::DocumentObject*> children = va.second->claimChildren3D();
            child_vps.insert(child_vps.end(), children.begin(), children.end());
        }

        for (App::DocumentObject* obj : child_vps) {
            view3D->getViewer()->removeViewProvider(getViewProvider(obj));
        }

        // When cloning the view, don't increment the window counter as the old view will be deleted
        // shortly after.
        if (mode != CreateViewMode::Clone) {
            const char* name = getDocument()->Label.getValue();
            QString title
                = QStringLiteral("%1 : %2[*]").arg(QString::fromUtf8(name)).arg(d->_iWinCount++);

            view3D->setWindowTitle(title);
        }

        view3D->setWindowModified(this->isModified());
        view3D->resize(400, 300);

        if (!cameraSettings.empty()) {
            view3D->setCamera(cameraSettings.c_str());
        }

        // When cloning the view, don't add the view to the main window. The whole purpose of the
        // workaround using cloned views is that the view can be shown in undocked/fullscreen mode
        // without having been docked before.
        if (mode != CreateViewMode::Clone) {
            getMainWindow()->addWindow(view3D);
        }

        view3D->getViewer()->redraw();
        return view3D;
    }
    return nullptr;
}

const char* Document::getCameraSettings() const
{
    return cameraSettings.c_str();
}

bool Document::saveCameraSettings(const char* settings) const
{
    if (!settings) {
        return false;
    }

    // skip starting comment lines
    bool skipping = false;
    char c = *settings;
    for (; c; c = *(++settings)) {
        if (skipping) {
            if (c == '\n') {
                skipping = false;
            }
        }
        else if (c == '#') {
            skipping = true;
        }
        else if (!std::isspace(c)) {
            break;
        }
    }

    if (!c) {
        return false;
    }

    cameraSettings = settings;
    return true;
}

void Document::attachView(Gui::BaseView* pcView, bool bPassiv)
{
    if (!bPassiv) {
        d->baseViews.push_back(pcView);
    }
    else {
        d->passiveViews.push_back(pcView);
    }
}

void Document::detachView(Gui::BaseView* pcView, bool bPassiv)
{
    if (bPassiv) {
        if (find(d->passiveViews.begin(), d->passiveViews.end(), pcView) != d->passiveViews.end()) {
            d->passiveViews.remove(pcView);
        }
    }
    else {
        if (find(d->baseViews.begin(), d->baseViews.end(), pcView) != d->baseViews.end()) {
            d->baseViews.remove(pcView);
        }

        // last view?
        if (d->baseViews.empty()) {
            // decouple a passive view
            std::list<Gui::BaseView*>::iterator it = d->passiveViews.begin();
            while (it != d->passiveViews.end()) {
                (*it)->setDocument(nullptr);
                it = d->passiveViews.begin();
            }

            // is already closing the document, and is not linked by other documents
            if (!d->_isClosing && App::PropertyXLink::getDocumentInList(getDocument()).empty()) {
                d->_pcAppWnd->onLastWindowClosed(this);
            }
        }
    }
}

void Document::onUpdate()
{
#ifdef FC_LOGUPDATECHAIN
    Base::Console().log("Acti: Gui::Document::onUpdate()");
#endif

    for (auto* v : d->baseViews) {
        v->onUpdate();
    }

    for (auto* v : d->passiveViews) {
        v->onUpdate();
    }
}

void Document::onRelabel()
{
#ifdef FC_LOGUPDATECHAIN
    Base::Console().log("Acti: Gui::Document::onRelabel()");
#endif

    for (auto* v : d->baseViews) {
        v->onRelabel(this);
    }

    for (auto* v : d->passiveViews) {
        v->onRelabel(this);
    }

    d->connectChangeDocumentBlocker.unblock();
}

bool Document::isLastView()
{
    if (d->baseViews.size() <= 1) {
        return true;
    }
    return false;
}

/**
 *  This method checks if the document can be closed. It checks on
 *  the save state of the document and is able to abort the closing.
 */
bool Document::canClose(bool checkModify, bool checkLink)
{
    if (d->_isClosing) {
        return true;
    }
    if (!getDocument()->isClosable()) {
        QMessageBox::warning(
            getActiveView(),
            QObject::tr("Document not closable"),
            QObject::tr("The document is not closable for the moment.")
        );
        return false;
    }
    // else if (!Gui::Control().isAllowedAlterDocument()) {
    //     std::string name = Gui::Control().activeDialog()->getDocumentName();
    //     if (name == this->getDocument()->getName()) {
    //         QMessageBox::warning(getActiveView(),
    //             QObject::tr("Document not closable"),
    //             QObject::tr("The document is in editing mode and thus cannot be closed for the
    //             moment.\n"
    //                         "You either have to finish or cancel the editing in the task panel."));
    //         Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog();
    //         if (dlg) Gui::Control().showDialog(dlg);
    //         return false;
    //     }
    // }

    if (checkLink && !App::PropertyXLink::getDocumentInList(getDocument()).empty()) {
        return true;
    }

    if (getDocument()->testStatus(App::Document::TempDoc)) {
        return true;
    }

    bool ok = true;
    if (checkModify && isModified() && !getDocument()->testStatus(App::Document::PartialDoc)) {
        int res = getMainWindow()->confirmSave(getDocument(), getActiveView());
        switch (res) {
            case MainWindow::ConfirmSaveResult::Cancel:
                ok = false;
                break;
            case MainWindow::ConfirmSaveResult::SaveAll:
            case MainWindow::ConfirmSaveResult::Save:
                ok = save();
                if (!ok) {
                    const QString docName = QString::fromStdString(getDocument()->Label.getStrValue());
                    const QString text
                        = (!docName.isEmpty()
                               ? QObject::tr("Failed to save document '%1'. Would you like to cancel the closure?")
                                     .arg(docName)
                               : QObject::tr(
                                     "Document saving failed. Would you like to cancel the closure?"
                                 ));
                    int ret = QMessageBox::question(
                        getActiveView(),
                        QObject::tr("Unable to save document"),
                        text,
                        QMessageBox::Discard | QMessageBox::Cancel,
                        QMessageBox::Discard
                    );
                    if (ret == QMessageBox::Discard) {
                        ok = true;
                    }
                }
                break;
            case MainWindow::ConfirmSaveResult::DiscardAll:
            case MainWindow::ConfirmSaveResult::Discard:
                ok = true;
                break;
        }
    }

    if (ok) {
        // If a task dialog is open that doesn't allow other commands to modify
        // the document it must be closed by resetting the edit mode of the
        // corresponding view provider.
        if (!Gui::Control().isAllowedAlterDocument(getDocument())) {
            std::string name = Gui::Control().activeDialog(getDocument())->getDocumentName();
            if (name == this->getDocument()->getName()) {
                // getInEdit() only checks if the currently active MDI view is
                // a 3D view and that it is in edit mode. However, when closing a
                // document then the edit mode must be reset independent of the
                // active view.
                if (d->_editViewProvider) {
                    this->_resetEdit();
                }
            }
        }
    }

    return ok;
}

std::list<MDIView*> Document::getMDIViews(bool includePassive) const
{
    std::list<MDIView*> views;
    for (auto* v : d->baseViews) {
        auto view = dynamic_cast<MDIView*>(v);
        if (view) {
            views.push_back(view);
        }
    }

    if (includePassive) {
        for (auto* v : d->passiveViews) {
            auto view = dynamic_cast<MDIView*>(v);
            if (view) {
                views.push_back(view);
            }
        }
    }

    return views;
}

std::list<MDIView*> Document::getMDIViewsOfType(const Base::Type& typeId, bool includePassive) const
{
    std::list<MDIView*> views;
    for (auto* v : d->baseViews) {
        auto view = dynamic_cast<MDIView*>(v);
        if (view && view->isDerivedFrom(typeId)) {
            views.push_back(view);
        }
    }

    if (includePassive) {
        for (auto* v : d->passiveViews) {
            auto view = dynamic_cast<MDIView*>(v);
            if (view && view->isDerivedFrom(typeId)) {
                views.push_back(view);
            }
        }
    }

    return views;
}

/// send messages to the active view
bool Document::sendMsgToViews(const char* pMsg)
{
    for (auto* v : d->baseViews) {
        if (v->onMsg(pMsg)) {
            return true;
        }
    }

    for (auto* v : d->passiveViews) {
        if (v->onMsg(pMsg)) {
            return true;
        }
    }

    return false;
}

bool Document::sendMsgToFirstView(const Base::Type& typeId, const char* pMsg)
{
    // first try the active view
    Gui::MDIView* view = getActiveView();
    if (view && view->isDerivedFrom(typeId)) {
        if (view->onMsg(pMsg)) {
            return true;
        }
    }

    // now try the other views
    std::list<Gui::MDIView*> views = getMDIViewsOfType(typeId);
    for (const auto& it : views) {
        if ((it != view) && it->onMsg(pMsg)) {
            return true;
        }
    }

    return false;
}

/// Getter for the active view
MDIView* Document::getActiveView() const
{
    // get the main window's active view
    MDIView* active = getMainWindow()->activeWindow();

    // get all MDI views of the document
    std::list<MDIView*> mdis = getMDIViews(true);

    // check whether the active view is part of this document
    bool ok = false;
    for (const auto& mdi : mdis) {
        if (mdi == active) {
            ok = true;
            break;
        }
    }

    if (ok) {
        return active;
    }

    // the active view is not part of this document, just use the last view
    const auto& windows = Gui::getMainWindow()->windows();
    for (auto rit = mdis.rbegin(); rit != mdis.rend(); ++rit) {
        // Some view is removed from window list for some reason, e.g. TechDraw
        // hidden page has view but not in the list. By right, the view will
        // self delete, but not the case for TechDraw, especially during
        // document restore.
        if (windows.contains(*rit) || (*rit)->isDerivedFrom<View3DInventor>()) {
            return *rit;
        }
    }
    return nullptr;
}

MDIView* Document::setActiveView(const ViewProviderDocumentObject* vp, Base::Type typeId)
{
    MDIView* view = nullptr;
    if (!vp) {
        view = getActiveView();
    }
    else {
        view = vp->getMDIView();
        if (!view) {
            auto obj = vp->getObject();
            if (!obj) {
                view = getActiveView();
            }
            else {
                auto linked = obj->getLinkedObject(true);
                if (linked != obj) {
                    auto vpLinked = freecad_cast<ViewProviderDocumentObject*>(
                        Application::Instance->getViewProvider(linked)
                    );
                    if (vpLinked) {
                        view = vpLinked->getMDIView();
                    }
                }

                if (!view && typeId.isBad()) {
                    MDIView* active = getActiveView();
                    if (active && active->containsViewProvider(vp)) {
                        view = active;
                    }
                    else {
                        typeId = View3DInventor::getClassTypeId();
                    }
                }
            }
        }
    }

    if (!view || (!typeId.isBad() && !view->isDerivedFrom(typeId))) {
        view = nullptr;
        for (auto* v : d->baseViews) {
            if (v->isDerivedFrom<MDIView>() && (typeId.isBad() || v->isDerivedFrom(typeId))) {
                view = static_cast<MDIView*>(v);
                break;
            }
        }
    }

    if (!view && !typeId.isBad()) {
        view = createView(typeId);
    }

    if (view) {
        getMainWindow()->setActiveWindow(view);
    }

    return view;
}

/**
 * @brief Document::setActiveWindow
 * If this document is active and the view is part of it then it will be
 * activated. If the document is not active or if the view is already active
 * nothing is done.
 * @param view
 */
void Document::setActiveWindow(Gui::MDIView* view)
{
    // get the main window's active view
    MDIView* active = getMainWindow()->activeWindow();

    // view is already active
    if (active == view) {
        return;
    }

    // get all MDI views of the document
    std::list<MDIView*> mdis = getMDIViews(true);

    // this document is not active
    if (std::ranges::find(mdis, active) == mdis.end()) {
        return;
    }

    // the view is not part of the document
    if (std::ranges::find(mdis, view) == mdis.end()) {
        return;
    }

    getMainWindow()->setActiveWindow(view);
}

Gui::MDIView* Document::getViewOfNode(SoNode* node) const
{
    std::list<MDIView*> mdis = getMDIViewsOfType(View3DInventor::getClassTypeId());
    for (const auto& mdi : mdis) {
        auto view = static_cast<View3DInventor*>(mdi);
        if (view->getViewer()->searchNode(node)) {
            return mdi;
        }
    }

    return nullptr;
}

Gui::MDIView* Document::getViewOfViewProvider(const Gui::ViewProvider* vp) const
{
    return getViewOfNode(vp->getRoot());
}

Gui::MDIView* Document::getEditingViewOfViewProvider(Gui::ViewProvider* vp) const
{
    std::list<MDIView*> mdis = getMDIViewsOfType(View3DInventor::getClassTypeId());
    for (const auto& mdi : mdis) {
        auto view = static_cast<View3DInventor*>(mdi);
        View3DInventorViewer* viewer = view->getViewer();
        // there is only one 3d view which is in edit mode
        if (viewer->hasViewProvider(vp) && viewer->isEditingViewProvider()) {
            return mdi;
        }
    }

    return nullptr;
}
