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
// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// The tree's right-click menu and the actions it offers.

#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>


#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Sequencer.h>
#include <Base/Tools.h>
#include <Base/Writer.h>

#include <Base/Color.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Datums.h>
#include <App/DocumentObjectGroup.h>
#include <App/AutoTransaction.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Link.h>
#include <App/Project.h>
#include <App/Origin.h>
#include <App/SuppressibleExtension.h>

#include "Tree.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "Document.h"
#include "ExpressionCompleter.h"
#include "Macro.h"
#include "MainWindow.h"
#include "MenuManager.h"
#include "ProjectPanel.h"
#include "TreeHistory.h"
#include "TreeLineage.h"
#include "TreeShared.h"
#include "TreeSwatches.h"
#include "TreeParams.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"
#include "Widgets.h"
#include "Workbench.h"

using namespace Gui;
using namespace Gui::TreeHistory;
using namespace Gui::TreeSwatches;
using namespace Gui::TreeShared;
namespace sp = std::placeholders;

void TreeWidget::contextMenuEvent(QContextMenuEvent* e)
{
    if (isHeldBackRowAt(e->pos())) {
        return;
    }
    QTreeWidgetItem* pressed = itemAt(e->pos());
    if (pressed && pressed->type() == StopRowType) {
        showStopRowMenu(pressed, e->globalPos());
        return;
    }

    auto swatchBodies = bodiesAtSwatch(e->pos());
    if (!swatchBodies.empty()) {
        showSwatchMenu(swatchBodies, e->globalPos());
        return;
    }

    // ask workbenches and view provider, ...
    MenuItem view;
    Gui::Application::Instance->setupContextMenu("Tree", &view);

    // Every workbench works on the same history, so every workbench can roll it back.
    const auto picked = Selection().getObjectsOfType(App::DocumentObject::getClassTypeId());
    if (picked.size() == 1 && picked.front()->isRollbackStop()) {
        view << "Std_RollBackHere";
    }
    view << "Std_Properties";
    Workbench::createLinkMenu(&view);

    QMenu contextMenu;

    QMenu subMenu;
    QMenu editMenu;
    QActionGroup subMenuGroup(&subMenu);
    subMenuGroup.setExclusive(true);
    connect(&subMenuGroup, &QActionGroup::triggered, this, &TreeWidget::onActivateDocument);
    MenuManager::getInstance()->setupContextMenu(&view, contextMenu);

    // get the current item
    this->contextItem = itemAt(e->pos());

    if (this->contextItem && this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();

        // It's better to let user decide whether and how to activate
        // the current document, such as by double-clicking.
        // App::GetApplication().setActiveDocument(doc);

        contextMenu.addAction(this->openFileLocationAction);
        contextMenu.addAction(this->searchObjectsAction);
        contextMenu.addAction(this->closeDocAction);
        if (doc->testStatus(App::Document::PartialDoc)) {
            contextMenu.addAction(this->reloadDocAction);
        }
        else {
            for (auto d : doc->getDependentDocuments()) {
                if (d->testStatus(App::Document::PartialDoc)) {
                    contextMenu.addAction(this->reloadDocAction);
                    break;
                }
            }
            contextMenu.addAction(this->selectDependentsAction);
            if (doc == App::GetApplication().getActiveDocument() && this->skipRecomputeCommand) {
                // if active document is selected, use Command
                this->skipRecomputeCommand->addTo(&contextMenu);
            }
            else {
                // if other document is selected or Command load fails, edit selected Document directly
                this->skipRecomputeAction->setChecked(doc->testStatus(App::Document::SkipRecompute));
                contextMenu.addAction(this->skipRecomputeAction);
            }
            this->allowPartialRecomputeAction->setChecked(
                doc->testStatus(App::Document::AllowPartialRecompute)
            );
            if (doc->testStatus(App::Document::SkipRecompute)) {
                contextMenu.addAction(this->allowPartialRecomputeAction);
            }
            contextMenu.addAction(this->createGroupAction);
        }
        contextMenu.addSeparator();
    }
    else if (this->contextItem && this->contextItem->type() == ObjectType) {
        auto objitem = static_cast<DocumentObjectItem*>(this->contextItem);

        // check that the selection is not across several documents
        bool acrossDocuments = false;
        auto SelectedObjectsList = Selection().getCompleteSelection();
        // get the object's document as reference
        App::Document* doc = objitem->object()->getObject()->getDocument();
        for (auto it = SelectedObjectsList.begin(); it != SelectedObjectsList.end(); ++it) {
            if ((*it).pDoc != doc) {
                acrossDocuments = true;
                break;
            }
        }

        if (!acrossDocuments  // is only sensible for selections within one document
            && objitem->object()->getObject()->isDerivedFrom<App::DocumentObjectGroup>()) {
            contextMenu.addAction(this->createGroupAction);
        }
        contextMenu.addSeparator();

        // relabeling is only possible for a single selected document
        if (SelectedObjectsList.size() == 1) {
            contextMenu.addAction(this->relabelObjectAction);
        }

        auto selItems = this->selectedItems();
        // if only one item is selected, setup the edit menu
        if (selItems.size() == 1) {
            objitem->object()->setupContextMenu(&editMenu, this, SLOT(onStartEditing()));
            QList<QAction*> editAct = editMenu.actions();
            if (!editAct.isEmpty()) {
                QAction* topact = contextMenu.actions().constFirst();
                for (QList<QAction*>::iterator it = editAct.begin(); it != editAct.end(); ++it) {
                    contextMenu.insertAction(topact, *it);
                }
                QAction* first = editAct.front();
                contextMenu.setDefaultAction(first);
                if (objitem->object()->isEditing()) {
                    contextMenu.insertAction(topact, this->finishEditingAction);
                }
                contextMenu.insertSeparator(topact);
            }
        }
    }


    // add a submenu to active a document if two or more exist
    std::vector<App::Document*> docs = App::GetApplication().getDocuments();
    if (docs.size() >= 2) {
        contextMenu.addSeparator();
        App::Document* activeDoc = App::GetApplication().getActiveDocument();
        subMenu.setTitle(tr("Activate Document"));
        contextMenu.addMenu(&subMenu);
        QAction* active = nullptr;
        for (auto it = docs.begin(); it != docs.end(); ++it) {
            QString label = QString::fromUtf8((*it)->Label.getValue());
            QAction* action = subMenuGroup.addAction(label);
            action->setCheckable(true);
            action->setStatusTip(tr("Activates document %1").arg(label));
            action->setData(QByteArray((*it)->getName()));
            if (*it == activeDoc) {
                active = action;
            }
        }

        if (active) {
            active->setChecked(true);
        }
        subMenu.addActions(subMenuGroup.actions());
    }

    // add a submenu to present the settings of the tree.
    QMenu settingsMenu;
    settingsMenu.setTitle(tr("Tree Settings"));
    // A tree preference, not an action on a step: offered on documents and empty space only.
    if (!this->contextItem || this->contextItem->type() != ObjectType) {
        contextMenu.addSeparator();
        contextMenu.addMenu(&settingsMenu);
    }

    QAction* action = new QAction(tr("Show Description"), this);
    QAction* internalNameAction = new QAction(tr("Show Internal Name"), this);
    action->setStatusTip(
        tr("Shows a description column for items. An item's description can be set by editing the "
           "'label2' property.")
    );
    action->setCheckable(true);

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/TreeView"
    );
    action->setChecked(!hGrp->GetBool("HideColumn", true));

    settingsMenu.addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, action, internalNameAction, hGrp]() {
        bool show = action->isChecked();
        hGrp->SetBool("HideColumn", !show);
        setColumnHidden(1, !show);
        header()->setVisible(action->isChecked() || internalNameAction->isChecked());
    });


    internalNameAction->setStatusTip(tr("Shows an internal name column for items."));
    internalNameAction->setCheckable(true);

    internalNameAction->setChecked(!hGrp->GetBool("HideInternalNames", true));

    settingsMenu.addAction(internalNameAction);

    QObject::connect(
        internalNameAction,
        &QAction::triggered,
        this,
        [this, action, internalNameAction, hGrp]() {
            bool show = internalNameAction->isChecked();
            hGrp->SetBool("HideInternalNames", !show);
            setColumnHidden(2, !show);
            header()->setVisible(action->isChecked() || internalNameAction->isChecked());
        }
    );

    if (!contextMenu.actions().isEmpty()) {
        try {
            contextMenu.exec(QCursor::pos());
        }
        catch (Base::Exception& e) {
            e.reportException();
        }
        catch (std::exception& e) {
            FC_ERR("C++ exception: " << e.what());
        }
        catch (...) {
            FC_ERR("Unknown exception");
        }
        contextItem = nullptr;
    }
}

void TreeWidget::onCreateGroup()
{
    QString name = tr("Group");
    if (this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        App::AutoTransaction trans(doc, "Create group");
        QString cmd = QStringLiteral(
                          "App.getDocument(\"%1\").addObject"
                          "(\"App::DocumentObjectGroup\",\"Group\").Label=\"%2\""
        )
                          .arg(QString::fromUtf8(doc->getName()), name);
        Gui::Command::runCommand(Gui::Command::App, cmd.toUtf8());
    }
    else if (this->contextItem->type() == ObjectType) {
        auto objitem = static_cast<DocumentObjectItem*>(this->contextItem);
        App::DocumentObject* obj = objitem->object()->getObject();
        App::Document* doc = obj->getDocument();
        App::AutoTransaction trans(doc, "Create group");
        QString cmd = QStringLiteral(
                          "App.getDocument(\"%1\").getObject(\"%2\")"
                          ".newObject(\"App::DocumentObjectGroup\",\"Group\").Label=\"%3\""
        )
                          .arg(
                              QString::fromUtf8(doc->getName()),
                              QString::fromUtf8(obj->getNameInDocument()),
                              name
                          );
        Gui::Command::runCommand(Gui::Command::App, cmd.toUtf8());
    }
}

void TreeWidget::onRelabelObject()
{
    QTreeWidgetItem* item = currentItem();
    if (item) {
        editItem(item);
    }
}

void TreeWidget::onStartEditing()
{
    auto action = qobject_cast<QAction*>(sender());
    if (action) {
        if (this->contextItem && this->contextItem->type() == ObjectType) {
            auto objitem = static_cast<DocumentObjectItem*>(this->contextItem);
            int edit = action->data().toInt();

            App::DocumentObject* obj = objitem->object()->getObject();
            if (!obj || !obj->isAttachedToDocument()) {
                return;
            }
            auto doc = const_cast<Document*>(objitem->getOwnerDocument()->document());
            MDIView* view = doc->getActiveView();
            if (view) {
                getMainWindow()->setActiveWindow(view);
            }

            editingItem = objitem;
            if (!doc->setEdit(objitem->object(), edit)) {
                editingItem = nullptr;
            }
        }
    }
}

void TreeWidget::onFinishEditing()
{
    if (this->contextItem && this->contextItem->type() == ObjectType) {
        auto objitem = static_cast<DocumentObjectItem*>(this->contextItem);
        App::DocumentObject* obj = objitem->object()->getObject();
        if (!obj) {
            return;
        }
        Gui::Document* doc = Gui::Application::Instance->getDocument(obj->getDocument());
        doc->commitCommand();
        doc->resetEdit();
        doc->getDocument()->recompute();
    }
}

void TreeWidget::onSkipRecompute(bool on)
{
    // if a document item is selected then touch all objects
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        doc->setStatus(App::Document::SkipRecompute, on);
    }
}

void TreeWidget::onAllowPartialRecompute(bool on)
{
    // if a document item is selected then touch all objects
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        doc->setStatus(App::Document::AllowPartialRecompute, on);
    }
}

void TreeWidget::onMarkRecompute()
{
    // if a document item is selected then touch all objects
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        App::Document* doc = docitem->document()->getDocument();
        std::vector<App::DocumentObject*> obj = doc->getObjects();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            (*it)->enforceRecompute();
        }
    }
    // mark all selected objects
    else {
        QList<QTreeWidgetItem*> items = this->selectedItems();
        for (QList<QTreeWidgetItem*>::iterator it = items.begin(); it != items.end(); ++it) {
            if ((*it)->type() == ObjectType) {
                auto objitem = static_cast<DocumentObjectItem*>(*it);
                App::DocumentObject* obj = objitem->object()->getObject();
                obj->enforceRecompute();
            }
        }
    }
}

void TreeWidget::onRecomputeObject()
{
    if (auto cmd = Gui::Application::Instance->commandManager().getCommandByName("Std_Recompute")) {
        cmd->invoke(0, Command::TriggerAction);
        return;
    }

    Base::Console().error("TreeWidget::onRecomputeObject: Std_Recompute command not found\n");
}

void TreeWidget::onSearchObjects()
{
    Q_EMIT emitSearchObjects();
}

void TreeWidget::onActivateDocument(QAction* active)
{
    // activate the specified document
    QByteArray docname = active->data().toByteArray();
    Gui::Document* doc = Application::Instance->getDocument((const char*)docname);
    if (doc && !doc->setActiveView()) {
        doc->setActiveView(nullptr, View3DInventor::getClassTypeId());
    }
}

void TreeWidget::showStopRowMenu(QTreeWidgetItem* row, const QPoint& globalPos)
{
    auto* docItem = dynamic_cast<DocumentItem*>(row->parent());
    if (!docItem) {
        return;
    }
    App::Document* doc = docItem->document()->getDocument();
    QMenu menu;
    QAction* toEnd = menu.addAction(tr("Roll to End"));
    toEnd->setEnabled(doc->getRollbackMarker() != nullptr);
    if (menu.exec(globalPos) != toEnd) {
        return;
    }
    Command::doCommand(Command::Doc, "App.getDocument('%s').rollBackTo(None)", doc->getName());
    Command::updateActive();
}

void TreeWidget::onReloadDoc()
{
    if (!this->contextItem || this->contextItem->type() != DocumentType) {
        return;
    }
    auto docitem = static_cast<DocumentItem*>(this->contextItem);
    App::Document* doc = docitem->document()->getDocument();
    std::string name = doc->FileName.getValue();
    Application::Instance->reopen(doc);
    for (auto& v : DocumentMap) {
        if (name == v.first->getDocument()->FileName.getValue()) {
            scrollToItem(v.second);
            App::GetApplication().setActiveDocument(v.first->getDocument());
            break;
        }
    }
}

void TreeWidget::onCloseDoc()
{
    if (!this->contextItem || this->contextItem->type() != DocumentType) {
        return;
    }
    try {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        Gui::Document* gui = docitem->document();
        App::Document* doc = gui->getDocument();
        if (gui->canClose(true, true)) {
            Command::doCommand(Command::Doc, "App.closeDocument(\"%s\")", doc->getName());
        }
    }
    catch (const Base::Exception& e) {
        e.reportException();
    }
    catch (std::exception& e) {
        FC_ERR("C++ exception: " << e.what());
    }
    catch (...) {
        FC_ERR("Unknown exception");
    }
}

void TreeWidget::onOpenFileLocation()
{
    auto docitem = static_cast<DocumentItem*>(this->contextItem);
    App::Document* doc = docitem->document()->getDocument();
    std::string name = doc->FileName.getValue();

    const QFileInfo fileInfo(QString::fromStdString(name));
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, tr("Error"), tr("File does not exist."));
        return;
    }

    const QString filePath = fileInfo.canonicalPath();
    bool success = false;

#if defined(Q_OS_MAC)
    success = QProcess::startDetached(QStringLiteral("open"), {filePath});
#elif defined(Q_OS_WIN)
    QStringList param;
    if (!fileInfo.isDir()) {
        param += QStringLiteral("/select,");
    }
    param += QDir::toNativeSeparators(filePath);
    success = QProcess::startDetached(QStringLiteral("explorer.exe"), param);
#else
    success = QProcess::startDetached(QStringLiteral("xdg-open"), {filePath});
#endif

    if (!success) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to open directory."));
    }
}

void TreeWidget::onShowHidden()
{
    if (!this->contextItem) {
        return;
    }
    DocumentItem* docItem = nullptr;
    if (this->contextItem->type() == DocumentType) {
        docItem = static_cast<DocumentItem*>(contextItem);
    }
    else if (this->contextItem->type() == ObjectType) {
        docItem = static_cast<DocumentObjectItem*>(contextItem)->getOwnerDocument();
    }
    if (docItem) {
        docItem->setShowHidden(showHiddenAction->isChecked());
    }
}

void TreeWidget::onToggleVisibilityInTree()
{
    const auto items = selectedItems();
    for (auto item : items) {
        if (item->type() == ObjectType) {
            auto objectItem = static_cast<DocumentObjectItem*>(item);
            auto object = objectItem->object();

            // toggle value
            bool showInTree = !object->showInTree();

            // update object
            object->ShowInTree.setValue(showInTree);

            // update GUI
            auto ownerDocument = objectItem->getOwnerDocument();
            bool hidden = !ownerDocument->showHidden() && !showInTree;
            objectItem->setHidden(hidden);
            if (hidden) {
                objectItem->setSelected(false);
            }
        }
    }
}
