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

// Keeping the tree's selection and the program's selection in step, and the lineage a selection tints.

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

// RAII guard for safely tracking select all operation state
struct SelectAllGuard
{
    bool* flag;
    explicit SelectAllGuard(bool& f)
        : flag(&f)
    {
        *flag = true;
    }
    ~SelectAllGuard()
    {
        *flag = false;
    }
    SelectAllGuard(const SelectAllGuard&) = delete;
    SelectAllGuard& operator=(const SelectAllGuard&) = delete;
    SelectAllGuard(SelectAllGuard&&) = delete;
    SelectAllGuard& operator=(SelectAllGuard&&) = delete;
};

void TreeWidget::selectAll()
{
    auto& selection = Gui::Selection();

    const QTreeWidgetItem* current = currentItem();
    if (!current) {
        auto selItems = selectedItems();
        if (!selItems.isEmpty()) {
            current = selItems.back();
        }
    }

    if (!current) {
        SelectAllGuard guard(inSelectAllOperation);
        clearSelectAllContext();
        selectAllDocumentLevel();
        return;
    }

    // if we already did a group select, the second press expand to document
    if (lastSelectAllParent) {
        SelectAllGuard guard(inSelectAllOperation);
        if (TreeParams::getRecordSelection()) {
            selection.selStackPush();
        }
        selection.clearSelection();
        selectAllDocumentLevel();
        clearSelectAllContext();
        return;
    }

    bool isGrp = false;
    if (current->type() == ObjectType) {
        isGrp = (static_cast<const DocumentObjectItem*>(current)->isGroup() != 0);
    }

    const QTreeWidgetItem* parent = current->parent();

    // if its not a group and got no parent, just select whole document immediately
    if (!isGrp && (!parent || parent->type() == DocumentType)) {
        SelectAllGuard guard(inSelectAllOperation);
        selectAllDocumentLevel();
        clearSelectAllContext();
        return;
    }

    const QTreeWidgetItem* targetNode = isGrp ? current : parent;

    SelectAllGuard guard(inSelectAllOperation);
    lastSelectAllParent = true;
    selectAllGroupLevel(targetNode, isGrp);
}

void TreeWidget::selectAllGroupLevel(const QTreeWidgetItem* targetNode, bool isGroup)
{
    auto& selection = Gui::Selection();

    if (TreeParams::getRecordSelection()) {
        selection.selStackPush();
    }
    selection.clearSelection();

    // If current item is a group, also select the group itself along with its children
    if (isGroup) {
        const auto* item = static_cast<const DocumentObjectItem*>(targetNode);
        const auto* vp = item->object();
        if (vp && vp->isSelectable()) {
            const auto* obj = vp->getObject();
            if (obj && obj->getDocument()) {
                selection.addSelection(obj->getDocument()->getName(), obj->getNameInDocument());
            }
        }
    }

    selectGroupItems(targetNode, false);
}

void TreeWidget::selectGroupItems(const QTreeWidgetItem* group, bool recursive)
{
    auto& selection = Gui::Selection();
    if (!group) {
        return;
    }

    const int childCount = group->childCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* child = group->child(i);
        if (child->type() != TreeWidget::ObjectType) {
            continue;
        }

        const auto* childItem = static_cast<DocumentObjectItem*>(child);
        const auto* vp = childItem->object();
        if (!vp || !vp->isSelectable()) {
            continue;
        }

        const auto* obj = vp->getObject();
        if (!obj || !obj->getDocument()) {
            continue;
        }

        selection.addSelection(obj->getDocument()->getName(), obj->getNameInDocument());

        if (recursive && child->childCount() > 0) {
            selectGroupItems(child, true);
        }
    }
}

void TreeWidget::selectAllDocumentLevel()
{
    auto gdoc = Application::Instance->getDocument(App::GetApplication().getActiveDocument());
    if (!gdoc) {
        return;
    }

    auto itDoc = DocumentMap.find(gdoc);
    if (itDoc == DocumentMap.end()) {
        return;
    }

    auto& selection = Gui::Selection();
    if (TreeParams::getRecordSelection()) {
        selection.selStackPush();
    }
    selection.clearSelection();
    selection.setSelection(gdoc->getDocument()->getName(), gdoc->getDocument()->getObjects());
}

void TreeWidget::clearSelectAllContext()
{
    lastSelectAllParent = false;
}

// check if selection has dependent objects
bool TreeWidget::CheckForDependents()
{
    // if the selected object is a document
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        return true;
    }
    // it can be an object
    else {
        QList<QTreeWidgetItem*> items = this->selectedItems();
        for (QList<QTreeWidgetItem*>::iterator it = items.begin(); it != items.end(); ++it) {
            if ((*it)->type() == ObjectType) {
                auto objitem = static_cast<DocumentObjectItem*>(*it);
                App::DocumentObject* obj = objitem->object()->getObject();
                // get dependents
                auto subObjectList = obj->getOutList();
                if (!subObjectList.empty()) {
                    return true;
                }
            }
        }
    }

    return false;
}

// adds an App::DocumentObject* and its dependent objects to the selection
void TreeWidget::addDependentToSelection(App::Document* doc, App::DocumentObject* docObject)
{
    // add the docObject to the selection
    Selection().addSelection(doc->getName(), docObject->getNameInDocument());
    // get the dependent
    auto subObjectList = docObject->getOutList();
    for (auto itDepend : subObjectList) {
        if (!Selection().isSelected(itDepend)) {
            addDependentToSelection(doc, itDepend);
        }
    }
}

// add dependents of the selected tree object to selection
void TreeWidget::onSelectDependents()
{
    // We only have this context menu entry if the selection is within one document but it
    // might be not the active document. Therefore get the document not here but later by casting.
    App::Document* doc;

    // if the selected object is a document
    if (this->contextItem && this->contextItem->type() == DocumentType) {
        auto docitem = static_cast<DocumentItem*>(this->contextItem);
        doc = docitem->document()->getDocument();
        std::vector<App::DocumentObject*> obj = doc->getObjects();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            Selection().addSelection(doc->getName(), (*it)->getNameInDocument());
        }
    }
    // it can be an object
    else {
        QList<QTreeWidgetItem*> items = this->selectedItems();
        for (QList<QTreeWidgetItem*>::iterator it = items.begin(); it != items.end(); ++it) {
            if ((*it)->type() == ObjectType) {
                auto objitem = static_cast<DocumentObjectItem*>(*it);
                doc = objitem->object()->getObject()->getDocument();
                App::DocumentObject* obj = objitem->object()->getObject();
                // the dependents can also have dependents, thus add them recursively via a separate void
                addDependentToSelection(doc, obj);
            }
        }
    }
}

void TreeWidget::selectAllInstances(const ViewProviderDocumentObject& vpd)
{
    if (!isSelectionAttached()) {
        return;
    }

    if (selectTimer->isActive()) {
        onSelectTimer();
    }
    else {
        _updateStatus(false);
    }

    for (const auto& v : DocumentMap) {
        v.second->selectAllInstances(vpd);
    }
}

TreeWidget* TreeWidget::getTreeForSelection()
{
    TreeWidget* tree = instance();
    if (!tree || !tree->isSelectionAttached()) {
        for (auto pTree : Instances) {
            if (pTree->isSelectionAttached()) {
                tree = pTree;
                break;
            }
        }
    }
    if (!tree) {
        return nullptr;
    }

    if (tree->selectTimer->isActive()) {
        tree->onSelectTimer();
    }
    else {
        tree->_updateStatus(false);
    }

    return tree;
}

std::vector<Document*> TreeWidget::getSelectedDocuments()
{
    std::vector<Document*> ret;
    TreeWidget* tree = getTreeForSelection();

    if (!tree) {
        return ret;
    }

    const auto items = tree->selectedItems();
    for (auto ti : items) {
        if (ti->type() != DocumentType) {
            continue;
        }
        auto item = static_cast<DocumentItem*>(ti);
        auto doc = item->document();
        if (!doc || !doc->getDocument()) {
            FC_WARN("skip invalid document");
            continue;
        }
        ret.push_back(doc);
    }
    return ret;
}

std::vector<TreeWidget::SelInfo> TreeWidget::getSelection(App::Document* doc)
{
    std::vector<SelInfo> ret;
    TreeWidget* tree = getTreeForSelection();

    if (!tree) {
        return ret;
    }

    const auto items = tree->selectedItems();
    for (auto ti : items) {
        if (ti->type() != ObjectType) {
            continue;
        }
        auto item = static_cast<DocumentObjectItem*>(ti);
        auto vp = item->object();
        auto obj = vp->getObject();
        if (!obj || !obj->isAttachedToDocument()) {
            FC_WARN("skip invalid object");
            continue;
        }
        if (doc && obj->getDocument() != doc) {
            FC_LOG("skip objects not from current document");
            continue;
        }
        ViewProviderDocumentObject* parentVp = nullptr;
        auto parent = item->getParentItem();
        if (parent) {
            parentVp = parent->object();
            if (!parentVp->getObject()->isAttachedToDocument()) {
                FC_WARN("skip '" << obj->getFullName() << "' with invalid parent");
                continue;
            }
        }
        ret.emplace_back();
        auto& sel = ret.back();
        sel.topParent = nullptr;
        std::ostringstream ss;
        item->getSubName(ss, sel.topParent);
        if (!sel.topParent) {
            sel.topParent = obj;
        }
        else {
            ss << obj->getNameInDocument() << '.';
        }
        sel.subname = ss.str();
        sel.parentVp = parentVp;
        sel.vp = vp;
    }
    return ret;
}

void TreeWidget::selectAllLinks(App::DocumentObject* obj)
{
    if (!isSelectionAttached()) {
        return;
    }

    if (!obj || !obj->isAttachedToDocument()) {
        TREE_ERR("invalid object");
        return;
    }

    if (selectTimer->isActive()) {
        onSelectTimer();
    }
    else {
        _updateStatus(false);
    }

    for (auto link : App::GetApplication().getLinksTo(obj, App::GetLinkRecursive)) {
        if (!link || !link->isAttachedToDocument()) {
            TREE_ERR("invalid linked object");
            continue;
        }
        auto vp = freecad_cast<ViewProviderDocumentObject*>(
            Application::Instance->getViewProvider(link)
        );
        if (!vp) {
            TREE_ERR("invalid view provider of the linked object");
            continue;
        }
        for (auto& v : DocumentMap) {
            v.second->selectAllInstances(*vp);
        }
    }
}

void TreeWidget::onItemEntered(QTreeWidgetItem* item)
{
    if (item && item->type() == TreeWidget::ObjectType) {
        auto objItem = static_cast<DocumentObjectItem*>(item);
        objItem->displayStatusInfo();

        if (TreeParams::getPreSelection()) {
            int timeout = TreeParams::getPreSelectionDelay();
            if (timeout < 0) {
                timeout = 1;
            }
            if (preselectTime.elapsed() < timeout) {
                onPreSelectTimer();
            }
            else {
                timeout = TreeParams::getPreSelectionTimeout();
                if (timeout < 0) {
                    timeout = 1;
                }
                preselectTimer->start(timeout);
                Selection().rmvPreselect();
            }
        }
    }
    else if (TreeParams::getPreSelection()) {
        Selection().rmvPreselect();
    }
}

void TreeWidget::leaveEvent(QEvent* event)
{
    Q_UNUSED(event)
    if (!updateBlocked && TreeParams::getPreSelection()) {
        preselectTimer->stop();
        Selection().rmvPreselect();
    }
}

void TreeWidget::onPreSelectTimer()
{
    if (!TreeParams::getPreSelection()) {
        return;
    }
    auto item = itemAt(viewport()->mapFromGlobal(QCursor::pos()));
    if (!item || item->type() != TreeWidget::ObjectType) {
        return;
    }

    preselectTime.restart();
    auto objItem = static_cast<DocumentObjectItem*>(item);
    auto vp = objItem->object();
    auto obj = vp->getObject();
    std::ostringstream ss;
    App::DocumentObject* parent = nullptr;
    objItem->getSubName(ss, parent);
    if (!parent) {
        parent = obj;
    }
    else if (!obj->redirectSubName(ss, parent, nullptr)) {
        ss << obj->getNameInDocument() << '.';
    }
    Selection().setPreselect(
        parent->getDocument()->getName(),
        parent->getNameInDocument(),
        ss.str().c_str(),
        0,
        0,
        0,
        SelectionChanges::MsgSource::TreeView
    );
}

void TreeWidget::onItemSelectionChanged()
{
    if (!this->isSelectionAttached() || this->isSelectionBlocked() || updateBlocked) {
        return;
    }

    _LastSelectedTreeWidget = this;

    if (!inSelectAllOperation && lastSelectAllParent) {
        clearSelectAllContext();
    }

    // block tmp. the connection to avoid to notify us ourself
    bool lock = this->blockSelection(true);

    if (preselectTimer->isActive()) {
        // block preselect after selecting
        preselectTimer->stop();
    }

    if (selectTimer->isActive()) {
        onSelectTimer();
    }
    else {
        _updateStatus(false);
    }

    auto selItems = selectedItems();

    // do not allow document item multi-selection
    if (!selItems.empty()) {
        auto firstType = selItems.back()->type();
        for (auto it = selItems.begin(); it != selItems.end();) {
            auto item = *it;
            if ((firstType == ObjectType && item->type() != ObjectType)
                || (firstType == DocumentType && item != selItems.back())) {
                item->setSelected(false);
                it = selItems.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    if (selItems.size() <= 1) {
        if (TreeParams::getRecordSelection()) {
            Gui::Selection().selStackPush();
        }

        // This special handling to deal with possible discrepancy of
        // Gui.Selection and Tree view selection because of newly added
        // DocumentObject::redirectSubName()
        Selection().clearCompleteSelection();
        DocumentObjectItem* item = nullptr;
        if (!selItems.empty()) {
            if (selItems.front()->type() == ObjectType) {
                item = static_cast<DocumentObjectItem*>(selItems.front());
            }
            else if (selItems.front()->type() == DocumentType) {
                auto ditem = static_cast<DocumentItem*>(selItems.front());
                if (TreeParams::getSyncView()) {
                    bool focus = hasFocus();
                    ditem->document()->setActiveView();
                    if (focus) {
                        setFocus();
                    }
                }
                // For triggering property editor refresh
                Gui::Selection().signalSelectionChanged(SelectionChanges());
            }
        }
        for (auto& v : DocumentMap) {
            currentDocItem = v.second;
            v.second->clearSelection(item);
            currentDocItem = nullptr;
        }
        if (TreeParams::getRecordSelection()) {
            Gui::Selection().selStackPush();
        }
    }
    else {
        for (auto pos = DocumentMap.begin(); pos != DocumentMap.end(); ++pos) {
            currentDocItem = pos->second;
            pos->second->updateSelection(pos->second);
            currentDocItem = nullptr;
        }
        if (TreeParams::getRecordSelection()) {
            Gui::Selection().selStackPush(true, true);
        }
    }

    this->blockSelection(lock);
}

void TreeWidget::synchronizeSelectionCheckBoxes()
{
    const bool useCheckBoxes = isSelectionCheckBoxesEnabled();
    for (auto tree : TreeWidget::Instances) {
        QSignalBlocker blocker(tree);
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto item = *it;
            if (item->type() == ObjectType) {
                if (useCheckBoxes) {
                    item->setCheckState(0, item->isSelected() ? Qt::Checked : Qt::Unchecked);
                }
                else {
                    item->setData(0, Qt::CheckStateRole, QVariant());
                }
            }
        }
        tree->resizeColumnToContents(0);
    }
}

void TreeWidget::onSelectTimer()
{

    _updateStatus(false);

    bool syncSelect = TreeParams::getSyncSelection();
    bool locked = this->blockSelection(true);
    if (Selection().hasSelection()) {
        for (auto& v : DocumentMap) {
            v.second->setSelected(false);
            currentDocItem = v.second;
            v.second->selectItems(syncSelect ? DocumentItem::SR_EXPAND : DocumentItem::SR_SELECT);
            currentDocItem = nullptr;
        }
    }
    else {
        for (auto& v : DocumentMap) {
            v.second->clearSelection();
        }
    }
    this->blockSelection(locked);
    selectTimer->stop();
    updateLineage();
    return;
}

void TreeWidget::updateLineage()
{
    // Cruth #3 (ARCHITECTURE §8.2): selecting a step tints the rows of every step it came from:
    // its sketch, the steps it builds on, their sketches, and so on back to the start. The tree
    // no longer nests a sketch under its feature, so this is how the link shows. A step records
    // only its inputs, so following them never reaches a later step.
    std::set<App::DocumentObject*> selected;
    for (const auto& sel : Selection().getCompleteSelection()) {
        // A face picked in 3D arrives through its body ("Body.Pad.Face6"); the step is the object
        // that owns the element.
        App::DocumentObject* step = sel.pObject;
        if (step && sel.SubName && *sel.SubName) {
            if (auto sub = step->getSubObject(sel.SubName)) {
                step = sub;
            }
        }
        if (step) {
            selected.insert(step);
        }
    }

    auto lineage = TreeLineage::trace(
        std::move(selected),
        [](App::DocumentObject* obj) {
            auto vp = Application::Instance->getViewProvider(obj);
            return vp ? vp->getTreeBodyColumn() : ViewProvider::TreeBodyColumn {};
        },
        palette().color(QPalette::Highlight)
    );
    if (lineage.tints != lineageTints || lineage.selected != lineageSelected) {
        lineageTints = std::move(lineage.tints);
        lineageSelected = std::move(lineage.selected);
        viewport()->update();
    }
}

void TreeWidget::onSelectionChanged(const SelectionChanges& msg)
{
    // When running from a different thread Qt will raise a warning
    // when trying to start the QTimer
    if (Q_UNLIKELY(thread() != QThread::currentThread())) {
        return;
    }

    switch (msg.Type) {
        case SelectionChanges::AddSelection:
        case SelectionChanges::RmvSelection:
        case SelectionChanges::SetSelection:
        case SelectionChanges::ClrSelection: {
            int timeout = TreeParams::getSelectionTimeout();
            if (timeout <= 0) {
                timeout = 1;
            }
            selectTimer->start(timeout);
            break;
        }
        default:
            break;
    }
}

void TreeWidget::selectLinkedObject(App::DocumentObject* linked)
{
    if (!isSelectionAttached() || isSelectionBlocked()) {
        return;
    }

    auto linkedVp = freecad_cast<ViewProviderDocumentObject*>(
        Application::Instance->getViewProvider(linked)
    );
    if (!linkedVp) {
        TREE_ERR("invalid linked view provider");
        return;
    }
    auto linkedDoc = getDocumentItem(linkedVp->getDocument());
    if (!linkedDoc) {
        TREE_ERR("cannot find document of linked object");
        return;
    }

    if (selectTimer->isActive()) {
        onSelectTimer();
    }
    else {
        _updateStatus(false);
    }

    auto it = linkedDoc->ObjectMap.find(linked);
    if (it == linkedDoc->ObjectMap.end()) {
        TREE_ERR("cannot find tree item of linked object");
        return;
    }
    auto linkedItem = it->second->rootItem;
    if (!linkedItem) {
        linkedItem = *it->second->items.begin();
    }

    if (linkedDoc->showItem(linkedItem, true)) {
        scrollToItem(linkedItem);
    }

    if (linkedDoc->document()->getDocument() != App::GetApplication().getActiveDocument()) {
        bool focus = hasFocus();
        linkedDoc->document()->setActiveView(linkedItem->object());
        if (focus) {
            setFocus();
        }
    }
}
