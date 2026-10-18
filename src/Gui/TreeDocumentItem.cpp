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

// One document's rows in the tree, kept in step with the document.

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

DocumentItem::DocumentItem(const Gui::Document* doc, QTreeWidgetItem* parent)
    : QTreeWidgetItem(parent, TreeWidget::DocumentType)
    , pDocument(const_cast<Gui::Document*>(doc))
{
    // NOLINTBEGIN
    //  Setup connections
    connectNewObject = doc->signalNewObject.connect(
        std::bind(&DocumentItem::slotNewObject, this, sp::_1)
    );
    connectDelObject = doc->signalDeletedObject.connect(
        std::bind(&TreeWidget::slotDeleteObject, getTree(), sp::_1)
    );
    if (!App::GetApplication().isRestoring()) {
        connectChgObject = doc->signalChangedObject.connect(
            std::bind(&TreeWidget::slotChangeObject, getTree(), sp::_1, sp::_2)
        );
        connectTouchedObject = doc->getDocument()->signalTouchedObject.connect(
            std::bind(&TreeWidget::slotTouchedObject, getTree(), sp::_1)
        );
    }
    connectEdtObject = doc->signalInEdit.connect(std::bind(&DocumentItem::slotInEdit, this, sp::_1));
    connectResObject = doc->signalResetEdit.connect(
        std::bind(&DocumentItem::slotResetEdit, this, sp::_1)
    );
    connectHltObject = doc->signalHighlightObject.connect(
        std::bind(&DocumentItem::slotHighlightObject, this, sp::_1, sp::_2, sp::_3, sp::_4, sp::_5)
    );
    connectExpObject = doc->signalExpandObject.connect(
        std::bind(&DocumentItem::slotExpandObject, this, sp::_1, sp::_2, sp::_3, sp::_4)
    );
    connectScrObject = doc->signalScrollToObject.connect(
        std::bind(&DocumentItem::slotScrollToObject, this, sp::_1)
    );
    auto adoc = doc->getDocument();
    connectRecomputed = adoc->signalRecomputed.connect(
        std::bind(&DocumentItem::slotRecomputed, this, sp::_1, sp::_2)
    );
    connectRecomputedObj = adoc->signalRecomputedObject.connect(
        std::bind(&DocumentItem::slotRecomputedObject, this, sp::_1)
    );
    // NOLINTEND

    setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable /*|Qt::ItemIsEditable*/);

    treeName = getTree()->getTreeName();
}

DocumentItem::~DocumentItem()
{
    connectNewObject.disconnect();
    connectDelObject.disconnect();
    connectChgObject.disconnect();
    connectTouchedObject.disconnect();
    connectEdtObject.disconnect();
    connectResObject.disconnect();
    connectHltObject.disconnect();
    connectExpObject.disconnect();
    connectScrObject.disconnect();
    connectRecomputed.disconnect();
    connectRecomputedObj.disconnect();
}

TreeWidget* DocumentItem::getTree() const
{
    return static_cast<TreeWidget*>(treeWidget());
}

const char* DocumentItem::getTreeName() const
{
    return treeName;
}

void DocumentItem::slotInEdit(const Gui::ViewProviderDocumentObject& v)
{
    (void)v;

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/TreeView"
    );
    unsigned long col = hGrp->GetUnsigned("TreeEditColor", 563609599);
    QColor color(Base::Color::fromPackedRGB<QColor>(col));

    if (!getTree()->editingItem) {
        // In which cases would this return? theo-vt
        if (!Application::Instance->isInEdit(document())) {
            return;
        }
        ViewProviderDocumentObject* parentVp = nullptr;
        std::string subname;
        auto vp = document()->getInEdit(&parentVp, &subname);
        if (!parentVp) {
            parentVp = freecad_cast<ViewProviderDocumentObject*>(vp);
        }
        if (parentVp) {
            getTree()->editingItem = findItemByObject(true, parentVp->getObject(), subname.c_str());
        }
    }

    if (getTree()->editingItem) {
        getTree()->editingItem->setBackground(0, color);
    }
    else {
        FOREACH_ITEM(item, v)
        item->setBackground(0, color);
        END_FOREACH_ITEM
    }
}

void DocumentItem::slotResetEdit(const Gui::ViewProviderDocumentObject& v)
{
    auto tree = getTree();
    FOREACH_ITEM_ALL(item)
    if (tree->editingItem) {
        if (item == tree->editingItem) {
            item->setData(0, Qt::BackgroundRole, QVariant());
            break;
        }
    }
    else if (item->object() == &v) {
        item->setData(0, Qt::BackgroundRole, QVariant());
    }
    END_FOREACH_ITEM
    tree->editingItem = nullptr;
}

void DocumentItem::slotNewObject(const Gui::ViewProviderDocumentObject& obj)
{
    if (!obj.getObject() || !obj.getObject()->isAttachedToDocument()) {
        FC_ERR("view provider not attached");
        return;
    }
    getTree()->NewObjects[pDocument->getDocument()->getName()].push_back(obj.getObject()->getID());
    getTree()->_updateStatus();
}

bool DocumentItem::createNewItem(
    const Gui::ViewProviderDocumentObject& obj,
    QTreeWidgetItem* parent,
    int index,
    DocumentObjectDataPtr data
)
{
    if (!obj.getObject() || !obj.getObject()->isAttachedToDocument()
        || obj.getObject()->testStatus(App::PartialObject)) {
        return false;
    }

    if (!data) {
        auto& pdata = ObjectMap[obj.getObject()];
        if (!pdata) {
            pdata = std::make_shared<DocumentObjectData>(
                this,
                const_cast<ViewProviderDocumentObject*>(&obj)
            );
            auto& entry = getTree()->ObjectTable[obj.getObject()];
            if (!entry.empty()) {
                pdata->updateChildren(*entry.begin());
            }
            else {
                pdata->updateChildren(true);
            }
            entry.insert(pdata);
        }
        else if (pdata->rootItem && !parent) {
            Base::Console().warning("DocumentItem::slotNewObject: Cannot add view provider twice.\n");
            return false;
        }
        data = pdata;
    }

    auto item = new DocumentObjectItem(this, data);
    if (!parent || parent == this) {
        parent = this;
        data->rootItem = item;
        if (index < 0) {
            index = findRootIndex(obj.getObject());
        }
    }
    if (index < 0) {
        parent->addChild(item);
    }
    else {
        parent->insertChild(index, item);
    }
    assert(item->parent() == parent);
    item->setText(0, QString::fromUtf8(data->label.c_str()));
    if (!data->label2.empty()) {
        item->setText(1, QString::fromUtf8(data->label2.c_str()));
    }
    item->setText(2, QString::fromUtf8(data->internalName.c_str()));
    applyBodyColumn(item, obj);
    if (!obj.showInTree() && !showHidden()) {
        item->setHidden(true);
    }
    item->testStatus(true);

    populateItem(item);
    return true;
}

ViewProviderDocumentObject* DocumentItem::getViewProvider(App::DocumentObject* obj)
{
    return freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(obj));
}

bool DocumentItem::populateObject(App::DocumentObject* obj)
{
    // make sure at least one of the item corresponding to obj is populated
    auto it = ObjectMap.find(obj);
    if (it == ObjectMap.end()) {
        return false;
    }
    auto& items = it->second->items;
    if (items.empty()) {
        return false;
    }
    for (auto item : items) {
        if (item->populated) {
            return true;
        }
    }
    TREE_LOG("force populate object " << obj->getFullName());
    auto item = *items.begin();
    item->populated = true;
    populateItem(item, true);
    return true;
}

void DocumentItem::populateItem(DocumentObjectItem* item, bool refresh, bool delay)
{
    (void)delay;

    if (item->populated && !refresh) {
        return;
    }

    // Lazy loading policy: We will create an item for each children object if
    // a) the item is expanded, or b) there is at least one free child, i.e.
    // child originally located at root.

    item->setChildIndicatorPolicy(
        item->myData->children.empty() ? QTreeWidgetItem::DontShowIndicator
                                       : QTreeWidgetItem::ShowIndicator
    );

    if (!item->populated && !item->isExpanded()) {
        bool doPopulate = false;

        bool external = item->object()->getDocument() != item->getOwnerDocument()->document();
        if (external) {
            return;
        }
        auto obj = item->object()->getObject();
        auto linked = obj->getLinkedObject(true);
        if (linked && linked->getDocument() != obj->getDocument()) {
            return;
        }
        for (auto child : item->myData->children) {
            auto it = ObjectMap.find(child);
            if (it == ObjectMap.end() || it->second->items.empty()) {
                auto vp = getViewProvider(child);
                if (!vp) {
                    continue;
                }
                doPopulate = true;
                break;
            }
            if (item->myData->removeChildrenFromRoot) {
                if (it->second->rootItem) {
                    doPopulate = true;
                    break;
                }
            }
        }

        if (!doPopulate) {
            return;
        }
    }

    item->populated = true;
    bool checkHidden = !showHidden();
    bool updated = false;

    int i = -1;
    // iterate through the claimed children, and try to synchronize them with the
    // children tree item with the same order of appearance.
    int childCount = item->childCount();
    for (auto child : item->myData->children) {

        ++i;  // the current index of the claimed child

        bool found = false;
        for (int j = i; j < childCount; ++j) {
            QTreeWidgetItem* ci = item->child(j);
            if (ci->type() != TreeWidget::ObjectType) {
                continue;
            }

            auto childItem = static_cast<DocumentObjectItem*>(ci);
            if (childItem->object()->getObject() != child) {
                continue;
            }

            found = true;
            if (j != i) {  // fix index if it is changed
                childItem->setHighlight(false);
                item->removeChild(ci);
                item->insertChild(i, ci);
                assert(ci->parent() == item);
                if (checkHidden) {
                    updateItemsVisibility(ci, false);
                }
            }

            // Check if the item just changed its policy of whether to remove
            // children item from the root.
            if (item->myData->removeChildrenFromRoot) {
                if (childItem->myData->rootItem) {
                    assert(childItem != childItem->myData->rootItem);
                    bool lock = getTree()->blockSelection(true);
                    delete childItem->myData->rootItem;
                    getTree()->blockSelection(lock);
                }
            }
            else if (childItem->requiredAtRoot()) {
                createNewItem(*childItem->object(), this, -1, childItem->myData);
                updated = true;
            }
            break;
        }

        if (found) {
            continue;
        }

        // This algo will be recursively applied to newly created child items
        // through slotNewObject -> populateItem

        auto it = ObjectMap.find(child);
        if (it == ObjectMap.end() || it->second->items.empty()) {
            auto vp = getViewProvider(child);
            if (
                !vp
                || !createNewItem(*vp, item, i, it == ObjectMap.end() ? DocumentObjectDataPtr() : it->second)
            ) {
                --i;
            }
            else {
                updated = true;
            }
            continue;
        }

        if (!item->myData->removeChildrenFromRoot || !it->second->rootItem) {
            DocumentObjectItem* childItem = *it->second->items.begin();
            if (!createNewItem(*childItem->object(), item, i, it->second)) {
                --i;
            }
            else {
                updated = true;
            }
        }
        else {
            DocumentObjectItem* childItem = it->second->rootItem;
            if (item == childItem || item->isChildOfItem(childItem)) {
                TREE_ERR(
                    "Cyclic dependency in " << item->object()->getObject()->getFullName() << '.'
                                            << childItem->object()->getObject()->getFullName()
                );
                --i;
                continue;
            }
            it->second->rootItem = nullptr;
            childItem->setHighlight(false);
            this->removeChild(childItem);
            item->insertChild(i, childItem);
            assert(childItem->parent() == item);
            if (checkHidden) {
                updateItemsVisibility(childItem, false);
            }
        }
    }

    for (++i; item->childCount() > i;) {
        QTreeWidgetItem* ci = item->child(i);
        if (ci->type() == TreeWidget::ObjectType) {
            auto childItem = static_cast<DocumentObjectItem*>(ci);
            if (childItem->requiredAtRoot()) {
                item->removeChild(childItem);
                auto index = findRootIndex(childItem->object()->getObject());
                if (index >= 0) {
                    this->insertChild(index, childItem);
                }
                else {
                    this->addChild(childItem);
                }
                assert(childItem->parent() == this);
                if (checkHidden) {
                    updateItemsVisibility(childItem, false);
                }
                childItem->myData->rootItem = childItem;
                continue;
            }
        }

        bool lock = getTree()->blockSelection(true);
        delete ci;
        getTree()->blockSelection(lock);
    }
    if (updated) {
        getTree()->_updateStatus();
    }
}

int DocumentItem::findRootIndex(App::DocumentObject* childObj)
{
    if (!TreeParams::getKeepRootOrder() || !childObj || !childObj->isAttachedToDocument()) {
        return -1;
    }

    int count = this->childCount();
    if (!count) {
        return -1;
    }

    int first, last;

    // The root is the timeline: a row stands where its step stands in the document's history.
    const auto position = historyPositions(document()->getDocument());
    auto positionOf = [&position](Gui::ViewProviderDocumentObject* vp) -> int {
        return positionIn(position, vp->getObject());
    };

    auto vpc = freecad_cast<ViewProviderDocumentObject*>(
        Application::Instance->getViewProvider(childObj)
    );
    int childPosition = positionOf(vpc);

    // find the last item
    for (last = count - 1; last >= 0; --last) {
        auto citem = this->child(last);
        if (citem->type() == TreeWidget::ObjectType) {
            auto vp = static_cast<DocumentObjectItem*>(citem)->object();
            if (positionOf(vp) <= childPosition) {
                return last + 1;
            }
            break;
        }
    }

    // find the first item
    for (first = 0; first < count; ++first) {
        auto citem = this->child(first);
        if (citem->type() == TreeWidget::ObjectType) {
            auto vp = static_cast<DocumentObjectItem*>(citem)->object();
            if (positionOf(vp) > childPosition) {
                return first;
            }
            break;
        }
    }

    // now do a binary search to find the lower bound, assuming the root level
    // object is already in order
    count = last - first;
    int pos;
    while (count > 0) {
        int step = count / 2;
        pos = first + step;
        for (; pos <= last; ++pos) {
            auto citem = this->child(pos);
            if (citem->type() != TreeWidget::ObjectType) {
                continue;
            }
            auto vp = static_cast<DocumentObjectItem*>(citem)->object();
            if (positionOf(vp) < childPosition) {
                first = ++pos;
                count -= step + 1;
            }
            else {
                count = step;
            }
            break;
        }
        if (pos > last) {
            return -1;
        }
    }
    if (first > last) {
        return -1;
    }
    return first;
}

void DocumentItem::sortObjectItems()
{
    QSignalBlocker guard(getTree());

    std::vector<DocumentObjectItem*> sortedItems;
    sortedItems.reserve(this->childCount());

    for (int i = 0; i < this->childCount(); ++i) {
        QTreeWidgetItem* treeItem = this->child(i);
        if (treeItem->type() == TreeWidget::ObjectType) {
            sortedItems.push_back(static_cast<DocumentObjectItem*>(treeItem));
        }
    }

    const auto position = historyPositions(document()->getDocument());
    std::ranges::stable_sort(sortedItems, [&position](DocumentObjectItem* a, DocumentObjectItem* b) {
        return positionIn(position, a->object()->getObject())
            < positionIn(position, b->object()->getObject());
    });

    int sortedIndex = 0;
    std::vector<bool> expansion;
    for (int i = 0; i < this->childCount(); ++i) {
        QTreeWidgetItem* treeItem = this->child(i);
        if (treeItem->type() != TreeWidget::ObjectType) {
            continue;
        }

        DocumentObjectItem* sortedItem = sortedItems[sortedIndex++];
        if (sortedItem == treeItem) {
            continue;
        }

        expansion.clear();
        sortedItem->getExpandedSnapshot(expansion);

        this->removeChild(sortedItem);
        this->insertChild(i, sortedItem);
        if (!showHidden()) {
            updateItemsVisibility(sortedItem, false);
        }

        std::vector<bool>::const_iterator expFrom = expansion.cbegin();
        sortedItem->applyExpandedSnapshot(expansion, expFrom);
    }
}

void DocumentItem::slotHighlightObject(
    const Gui::ViewProviderDocumentObject& obj,
    const Gui::HighlightMode& high,
    bool set,
    const App::DocumentObject* parent,
    const char* subname
)
{
    getTree()->_updateStatus(false);
    if (parent && parent->getDocument() != document()->getDocument()) {
        auto it = getTree()->DocumentMap.find(
            Application::Instance->getDocument(parent->getDocument())
        );
        if (it != getTree()->DocumentMap.end()) {
            it->second->slotHighlightObject(obj, high, set, parent, subname);
        }
        return;
    }
    FOREACH_ITEM(item, obj)
    if (parent) {
        App::DocumentObject* topParent = nullptr;
        std::ostringstream ss;
        item->getSubName(ss, topParent);
        if (!topParent) {
            if (parent != obj.getObject()) {
                continue;
            }
        }
    }
    item->setHighlight(set, high);
    if (parent) {
        return;
    }
    END_FOREACH_ITEM
}

void DocumentItem::slotExpandObject(
    const Gui::ViewProviderDocumentObject& obj,
    const Gui::TreeItemMode& mode,
    const App::DocumentObject* parent,
    const char* subname
)
{
    getTree()->_updateStatus(false);

    if ((mode == TreeItemMode::ExpandItem || mode == TreeItemMode::ExpandPath)
        && obj.getDocument()->getDocument()->testStatus(App::Document::Restoring)) {
        if (!_ExpandInfo) {
            _ExpandInfo.reset(new ExpandInfo);
        }
        _ExpandInfo->emplace(std::string("*") + obj.getObject()->getNameInDocument(), ExpandInfoPtr());
        return;
    }

    if (parent && parent->getDocument() != document()->getDocument()) {
        auto it = getTree()->DocumentMap.find(
            Application::Instance->getDocument(parent->getDocument())
        );
        if (it != getTree()->DocumentMap.end()) {
            it->second->slotExpandObject(obj, mode, parent, subname);
        }
        return;
    }

    FOREACH_ITEM(item, obj)
    // All document object items must always have a parent, either another
    // object item or document item. If not, then there is a bug somewhere
    // else.
    assert(item->parent());

    switch (mode) {
        case TreeItemMode::ExpandPath:
            if (!parent) {
                QTreeWidgetItem* parentItem = item->parent();
                while (parentItem) {
                    parentItem->setExpanded(true);
                    parentItem = parentItem->parent();
                }
                item->setExpanded(true);
                break;
            }
            // fall through
        case TreeItemMode::ExpandItem:
            if (!parent) {
                if (item->parent()->isExpanded()) {
                    item->setExpanded(true);
                }
            }
            else {
                App::DocumentObject* topParent = nullptr;
                std::ostringstream ss;
                item->getSubName(ss, topParent);
                if (!topParent) {
                    if (parent != obj.getObject()) {
                        continue;
                    }
                }
                else if (topParent != parent) {
                    continue;
                }
                showItem(item, false, true);
                item->setExpanded(true);
            }
            break;
        case TreeItemMode::CollapseItem:
            item->setExpanded(false);
            break;
        case TreeItemMode::ToggleItem:
            if (item->isExpanded()) {
                item->setExpanded(false);
            }
            else {
                item->setExpanded(true);
            }
            break;

        default:
            break;
    }
    if (item->isExpanded()) {
        populateItem(item);
    }
    if (parent) {
        return;
    }
    END_FOREACH_ITEM
}

void DocumentItem::slotScrollToObject(const Gui::ViewProviderDocumentObject& obj)
{
    if (!obj.getObject() || !obj.getObject()->isAttachedToDocument()) {
        return;
    }
    auto it = ObjectMap.find(obj.getObject());
    if (it == ObjectMap.end() || it->second->items.empty()) {
        return;
    }
    auto item = it->second->rootItem;
    if (!item) {
        item = *it->second->items.begin();
    }
    getTree()->_updateStatus(false);
    getTree()->scrollToItem(item);
}

void DocumentItem::slotRecomputedObject(const App::DocumentObject& obj)
{
    if (obj.isValid()) {
        return;
    }
    slotRecomputed(*obj.getDocument(), {const_cast<App::DocumentObject*>(&obj)});
}

void DocumentItem::slotRecomputed(const App::Document&, const std::vector<App::DocumentObject*>& objs)
{
    auto tree = getTree();
    for (auto obj : objs) {
        if (!obj->isValid()) {
            tree->ChangedObjects[obj].set(TreeWidget::CS_Error);
        }
    }
    if (!tree->ChangedObjects.empty()) {
        tree->_updateStatus();
    }
}

Gui::Document* DocumentItem::document() const
{
    return this->pDocument;
}

void DocumentItem::updateBodyColumn()
{
    for (const auto& v : ObjectMap) {
        for (auto* item : v.second->items) {
            applyBodyColumn(item, *v.second->viewObject);
        }
    }
}

namespace
{
bool standsAfter(const QTreeWidgetItem* row, const QTreeWidgetItem* step)
{
    const QTreeWidgetItem* parent = row->parent();
    if (!parent || parent != step->parent()) {
        return false;
    }
    int index = parent->indexOfChild(const_cast<QTreeWidgetItem*>(row)) - 1;
    while (index >= 0 && parent->child(index)->type() == TreeWidget::StopRowType) {
        --index;
    }
    return index >= 0 && parent->child(index) == step;
}
}  // namespace

void DocumentItem::updateHeldBack()
{
    for (const auto& [obj, data] : ObjectMap) {
        data->viewObject->updateShowable();
        for (auto* item : data->items) {
            applyBodyColumn(item, *data->viewObject);
            item->testStatus(false);
        }
    }
}

// The stop row sits only among the document's own rows, never inside an object's children.
void DocumentItem::updateStopRows()
{
    QTreeWidgetItem* step = nullptr;
    if (App::DocumentObject* marker = document()->getDocument()->getRollbackMarker()) {
        auto found = ObjectMap.find(marker);
        if (found != ObjectMap.end()) {
            for (auto* item : found->second->items) {
                if (item->parent() == this) {
                    step = item;
                    break;
                }
            }
        }
    }

    QTreeWidgetItem* existing = nullptr;
    for (int i = 0; i < childCount() && !existing; ++i) {
        if (child(i)->type() == TreeWidget::StopRowType) {
            existing = child(i);
        }
    }
    // With no marker the model is built to the end, and the row stands after the last row.
    const bool inPlace = existing
        && (step ? standsAfter(existing, step) : existing == child(childCount() - 1));
    if (inPlace) {
        return;
    }

    updateHeldBack();

    QSignalBlocker guard(treeWidget());
    if (existing) {
        if (getTree()->draggedStopRow == existing) {
            getTree()->draggedStopRow = nullptr;
            getTree()->stopRowTarget = nullptr;
        }
        delete existing;
    }
    auto* row = new QTreeWidgetItem(TreeWidget::StopRowType);
    row->setText(0, QObject::tr("End of Model"));
    row->setIcon(0, BitmapFactory().iconFromTheme("PartDesign_MoveTip"));
    row->setFlags(Qt::ItemIsEnabled);
    insertChild(step ? indexOfChild(step) + 1 : childCount(), row);
}

void DocumentItem::testStatus()
{
    for (const auto& v : ObjectMap) {
        v.second->testStatus();
    }
}

void DocumentItem::setData(int column, int role, const QVariant& value)
{
    if (role == Qt::EditRole) {
        QString label = value.toString();
        pDocument->getDocument()->Label.setValue((const char*)label.toUtf8());
    }

    QTreeWidgetItem::setData(column, role, value);
}

void DocumentItem::clearSelection(DocumentObjectItem* exclude)
{
    // Block signals here otherwise we get a recursion and quadratic runtime
    bool ok = treeWidget()->blockSignals(true);
    FOREACH_ITEM_ALL(item);
    _v.second->dirtyFlag = false;
    if (item == exclude) {
        if (item->selected > 0) {
            item->selected = -1;
        }
        else {
            item->selected = 0;
        }
        updateItemSelection(item);
        // The set has been changed while calling updateItemSelection
        // so that the iterator has become invalid -> Abort
        if (_v.second->dirtyFlag) {
            break;
        }
    }
    else {
        item->selected = 0;
        item->mySubs.clear();
        item->setSelected(false);
        item->setCheckState(false);
    }
    END_FOREACH_ITEM;
    treeWidget()->blockSignals(ok);
}

void DocumentItem::updateSelection(QTreeWidgetItem* ti, bool unselect)
{
    for (int i = 0, count = ti->childCount(); i < count; ++i) {
        auto child = ti->child(i);
        if (child && child->type() == TreeWidget::ObjectType) {
            auto childItem = static_cast<DocumentObjectItem*>(child);
            if (unselect) {
                childItem->setSelected(false);
                childItem->setCheckState(false);
            }
            updateItemSelection(childItem);
            if (unselect && childItem->isGroup()) {
                // If the child item being force unselected by its group parent
                // is itself a group, propagate the unselection to its own
                // children
                updateSelection(childItem, true);
            }
        }
    }

    if (unselect) {
        return;
    }
    for (int i = 0, count = ti->childCount(); i < count; ++i) {
        updateSelection(ti->child(i));
    }
}

void DocumentItem::updateItemSelection(DocumentObjectItem* item)
{
    // Note: In several places of this function the selection is altered and the notification of
    // the selection observers can trigger a recreation of all DocumentObjectItem so that the
    // passed 'item' can become a dangling pointer.
    // Thus,'item' mustn't be accessed any more after altering the selection.
    // For further details see the bug analysis of #13107
    bool selected = item->isSelected();
    bool checked = item->checkState(0) == Qt::Checked;

    if (selected && !checked) {
        item->setCheckState(true);
    }

    if (!selected && checked) {
        item->setCheckState(false);
    }

    if (!selected && !item->selected) {
        return;
    }
    if (item->selected != -1) {
        item->mySubs.clear();
    }
    item->selected = selected;

    auto obj = item->object()->getObject();
    if (!obj || !obj->isAttachedToDocument()) {
        return;
    }

    std::ostringstream str;
    App::DocumentObject* topParent = nullptr;
    item->getSubName(str, topParent);
    if (topParent) {
        if (!obj->redirectSubName(str, topParent, nullptr)) {
            str << obj->getNameInDocument() << '.';
        }
        obj = topParent;
    }
    const char* objname = obj->getNameInDocument();
    const char* docname = obj->getDocument()->getName();
    const auto& subname = str.str();

#ifdef FC_DEBUG
    if (!subname.empty()) {
        assert(item->getParentItem());
    }
#endif

    if (!selected) {
        // Handles deselection for same name object and subname
        bool keep = false;
        auto items = getTree()->selectedItems();
        for (auto it : items) {
            if (it->type() == TreeWidget::ObjectType) {
                auto docitem = static_cast<DocumentObjectItem*>(it);
                auto obj2 = docitem->object()->getObject();
                if (!obj2 || !obj2->isAttachedToDocument()) {
                    continue;
                }

                std::ostringstream str2;
                App::DocumentObject* topParent2 = nullptr;
                docitem->getSubName(str2, topParent2);

                if (topParent2) {
                    if (!obj2->redirectSubName(str2, topParent2, nullptr)) {
                        str2 << obj2->getNameInDocument() << '.';
                    }
                    obj2 = topParent2;
                }

                if (obj2 == obj && str2.str() == subname) {
                    keep = true;
                    break;
                }
            }
        }

        if (!keep) {
            Gui::Selection().rmvSelection(docname, objname, subname.c_str());
        }
        return;
    }

    auto vobj = item->object();
    selected = false;
    if (!item->mySubs.empty()) {
        for (auto& sub : item->mySubs) {
            if (Gui::Selection().isSelected(docname, objname, (subname + sub).c_str())
                || Gui::Selection().addSelection(docname, objname, (subname + sub).c_str())) {
                selected = true;
            }
        }
    }
    if (!selected) {
        item->mySubs.clear();
        if (!Gui::Selection().isSelected(docname, objname, subname.c_str())
            && !Gui::Selection().addSelection(docname, objname, subname.c_str())) {
            // Safely re-access the item
            DocumentObjectItem* item2 = findItem(vobj->getObject(), subname);
            if (item2) {
                item2->selected = 0;
                item2->setSelected(false);
                item2->setCheckState(false);
            }
            return;
        }
    }
    getTree()->syncView(vobj);
}

App::DocumentObject* DocumentItem::getTopParent(App::DocumentObject* obj, std::string& subname)
{
    auto it = ObjectMap.find(obj);
    if (it == ObjectMap.end() || it->second->items.empty()) {
        return nullptr;
    }

    // already a top parent
    if (it->second->rootItem) {
        return obj;
    }

    for (auto item : it->second->items) {
        // non group object do not provide a coordinate system, hence its
        // claimed child is still in the global coordinate space, so the
        // child can still be considered a top level object
        if (!item->isParentGroup()) {
            return obj;
        }
    }

    // If no top level item, find an item that is closest to the top level
    std::multimap<int, DocumentObjectItem*> items;
    for (auto item : it->second->items) {
        int i = 0;
        for (auto parent = item->parent(); parent; ++i, parent = parent->parent()) {
            if (parent->isHidden()) {
                i += 1000;
            }
            ++i;
        }
        items.emplace(i, item);
    }

    App::DocumentObject* topParent = nullptr;
    std::ostringstream ss;
    items.begin()->second->getSubName(ss, topParent);
    if (!topParent) {
        // this shouldn't happen
        FC_WARN("No top parent for " << obj->getFullName() << '.' << subname);
        return obj;
    }
    ss << obj->getNameInDocument() << '.' << subname;
    FC_LOG(
        "Subname correction " << obj->getFullName() << '.' << subname << " -> "
                              << topParent->getFullName() << '.' << ss.str()
    );
    subname = ss.str();
    return topParent;
}

DocumentObjectItem* DocumentItem::findItem(App::DocumentObject* obj, const std::string& subname) const
{
    auto it = ObjectMap.find(obj);
    if (it == ObjectMap.end()) {
        return nullptr;
    }

    // There is only one instance in the tree view
    if (it->second->items.size() == 1) {
        return *(it->second->items.begin());
    }

    // If there are multiple instances use the one with the same subname
    DocumentObjectItem* item {};
    for (auto jt : it->second->items) {
        std::ostringstream str;
        App::DocumentObject* topParent = nullptr;
        jt->getSubName(str, topParent);
        if (topParent) {
            if (!obj->redirectSubName(str, topParent, nullptr)) {
                str << obj->getNameInDocument() << '.';
            }
        }

        if (subname == str.str()) {
            item = jt;
            break;
        }
    }

    return item;
}

DocumentObjectItem* DocumentItem::findItemByObject(
    bool sync,
    App::DocumentObject* obj,
    const char* subname,
    bool select
)
{
    if (!subname) {
        subname = "";
    }

    auto it = ObjectMap.find(obj);
    if (it == ObjectMap.end() || it->second->items.empty()) {
        return nullptr;
    }

    // prefer top level item of this object
    if (it->second->rootItem) {
        return findItem(sync, it->second->rootItem, subname, select);
    }

    for (auto item : it->second->items) {
        // non group object do not provide a coordinate system, hence its
        // claimed child is still in the global coordinate space, so the
        // child can still be considered a top level object
        if (!item->isParentGroup()) {
            return findItem(sync, item, subname, select);
        }
    }

    // If no top level item, find an item that is closest to the top level
    std::multimap<int, DocumentObjectItem*> items;
    for (auto item : it->second->items) {
        int i = 0;
        for (auto parent = item->parent(); parent; ++i, parent = parent->parent()) {
            ++i;
        }
        items.emplace(i, item);
    }
    for (auto& v : items) {
        auto item = findItem(sync, v.second, subname, select);
        if (item) {
            return item;
        }
    }
    return nullptr;
}

DocumentObjectItem* DocumentItem::findItem(
    bool sync,
    DocumentObjectItem* item,
    const char* subname,
    bool select
)
{
    // A row kept out of the tree on purpose (a body, whose steps carry it) stays out when its
    // object is selected; revealing it would shift every row under the user's pointer.
    if (item->isHidden() && (showHidden() || item->object()->showInTree())) {
        item->setHidden(false);
    }

    if (!subname || *subname == 0) {
        if (select) {
            item->selected += 2;
            item->mySubs.clear();
        }
        return item;
    }

    TREE_TRACE("find next " << subname);

    // try to find the next level object name
    const char* nextsub = nullptr;
    const char* dot = nullptr;
    if ((dot = strchr(subname, '.'))) {
        nextsub = dot + 1;
    }
    else {
        if (select) {
            item->selected += 2;
            if (std::ranges::find(item->mySubs, subname) == item->mySubs.end()) {
                item->mySubs.emplace_back(subname);
            }
        }
        return item;
    }

    std::string name(subname, nextsub - subname);
    auto obj = item->object()->getObject();
    auto subObj = obj->getSubObject(name.c_str());
    if (!subObj || subObj == obj) {
        if (!subObj && !getTree()->searchDoc) {
            TREE_LOG("sub object not found " << item->getName() << '.' << name.c_str());
        }
        if (select) {
            item->selected += 2;
            if (std::ranges::find(item->mySubs, subname) == item->mySubs.end()) {
                item->mySubs.emplace_back(subname);
            }
        }
        return item;
    }

    if (select) {
        item->mySubs.clear();
    }

    if (!item->populated && sync) {
        // force populate the item
        item->populated = true;
        populateItem(item, true);
    }

    for (int i = 0, count = item->childCount(); i < count; ++i) {
        auto ti = item->child(i);
        if (!ti || ti->type() != TreeWidget::ObjectType) {
            continue;
        }
        auto child = static_cast<DocumentObjectItem*>(ti);

        if (child->object()->getObject() == subObj) {
            return findItem(sync, child, nextsub, select);
        }
    }

    // The sub object is not found. This could happen for geo group, since its
    // children may be in more than one hierarchy down.
    bool found = false;
    DocumentObjectItem* res = nullptr;
    auto it = ObjectMap.find(subObj);
    if (it != ObjectMap.end()) {
        for (auto child : it->second->items) {
            if (child->isChildOfItem(item)) {
                found = true;
                res = findItem(sync, child, nextsub, select);
                if (!select) {
                    return res;
                }
            }
        }
    }

    // Cruth #3 (ARCHITECTURE §8.1): a body lists no children in the tree, so a path through
    // one ("Body.Pad.Face6") names a step that has its own row at the document level. Continue
    // from that row. Not through a link: a face picked through a link belongs to the link's row.
    if (!found && obj->getLinkedObject(false) == obj && subObj->getDocument() == obj->getDocument()) {
        auto root = ObjectMap.find(subObj);
        if (root != ObjectMap.end() && root->second->rootItem) {
            return findItem(sync, root->second->rootItem, nextsub, select);
        }
    }

    if (select && !found) {
        // The sub object is still not found. Maybe it is a non-object sub-element.
        // Select the current object instead.
        TREE_TRACE("element " << subname << " not found");
        item->selected += 2;
        if (std::ranges::find(item->mySubs, subname) == item->mySubs.end()) {
            item->mySubs.emplace_back(subname);
        }
    }
    return res;
}

void DocumentItem::selectItems(SelectionReason reason)
{
    const auto& sels
        = Selection().getSelection(pDocument->getDocument()->getName(), ResolveMode::NoResolve);

    bool sync = (sels.size() > 50 || reason == SR_SELECT) ? false : true;

    for (const auto& sel : sels) {
        findItemByObject(sync, sel.pObject, sel.SubName, true);
    }

    DocumentObjectItem* newSelect = nullptr;
    DocumentObjectItem* oldSelect = nullptr;

    FOREACH_ITEM_ALL(item)
    if (item->selected == 1) {
        // this means it is the old selection and is not in the current
        // selection
        item->selected = 0;
        item->mySubs.clear();
        item->setSelected(false);
        item->setCheckState(false);
    }
    else if (item->selected) {
        if (sync) {
            if ((item->selected == 2 || reason == SR_FORCE_EXPAND)
                && showItem(item, false, reason == SR_FORCE_EXPAND)) {
                // This means newly selected and can auto expand
                if (!newSelect) {
                    newSelect = item;
                }
            }
            if (!newSelect && !oldSelect && !item->isHidden()) {
                bool visible = true;
                for (auto parent = item->parent(); parent; parent = parent->parent()) {
                    if (!parent->isExpanded() || parent->isHidden()) {
                        visible = false;
                        break;
                    }
                }
                if (visible) {
                    oldSelect = item;
                }
            }
        }
        item->selected = 1;
        item->setSelected(true);
        item->setCheckState(true);
    }
    END_FOREACH_ITEM;

    if (sync) {
        if (!newSelect) {
            newSelect = oldSelect;
        }
        else {
            getTree()->syncView(newSelect->object());
        }
        if (newSelect) {
            getTree()->scrollToItem(newSelect);
        }
    }
}

void DocumentItem::populateParents(const ViewProvider* vp, ViewParentMap& parentMap)
{
    auto it = parentMap.find(vp);
    if (it == parentMap.end()) {
        return;
    }
    for (auto parent : it->second) {
        auto it = ObjectMap.find(parent->getObject());
        if (it == ObjectMap.end()) {
            continue;
        }

        populateParents(parent, parentMap);
        for (auto item : it->second->items) {
            if (!item->isHidden() && !item->populated) {
                item->populated = true;
                populateItem(item, true);
            }
        }
    }
}

void DocumentItem::selectAllInstances(const ViewProviderDocumentObject& vpd)
{
    ViewParentMap parentMap;
    auto pObject = vpd.getObject();
    if (!ObjectMap.contains(pObject)) {
        return;
    }

    bool lock = getTree()->blockSelection(true);

    // We are trying to select all items corresponding to a given view
    // provider, i.e. all appearance of the object inside all its parent items
    //
    // Build a map of object to all its parent
    for (auto& v : ObjectMap) {
        if (v.second->viewObject == &vpd) {
            continue;
        }
        for (auto child : v.second->viewObject->claimChildren()) {
            auto vp = getViewProvider(child);
            if (!vp) {
                continue;
            }
            parentMap[vp].push_back(v.second->viewObject);
        }
    }

    // now make sure all parent items are populated. In order to do that, we
    // need to populate the oldest parent first
    populateParents(&vpd, parentMap);

    DocumentObjectItem* first = nullptr;
    FOREACH_ITEM(item, vpd);
    if (showItem(item, true) && !first) {
        first = item;
    }
    END_FOREACH_ITEM;

    getTree()->blockSelection(lock);
    if (first) {
        treeWidget()->scrollToItem(first);
        // updateSelection();  // commented out - it was incorrectly deselecting
        // ...newly selected items because their qt selection state wasn't
        // ...yet synchronized when updateItemSelection() checked them
    }
}

bool DocumentItem::showHidden() const
{
    return pDocument->getDocument()->ShowHidden.getValue();
}

void DocumentItem::setShowHidden(bool show)
{
    pDocument->getDocument()->ShowHidden.setValue(show);
}

bool DocumentItem::showItem(DocumentObjectItem* item, bool select, bool force)
{
    auto parent = item->parent();
    if (item->isHidden()) {
        if (!force) {
            return false;
        }
        item->setHidden(false);
    }

    if (parent->type() == TreeWidget::ObjectType) {
        if (!showItem(static_cast<DocumentObjectItem*>(parent), false, force)) {
            return false;
        }
        auto pitem = static_cast<DocumentObjectItem*>(parent);
        if (force || !pitem->object()->getObject()->testStatus(App::NoAutoExpand)) {
            parent->setExpanded(true);
        }
        else if (!select) {
            return false;
        }
    }
    else {
        parent->setExpanded(true);
    }

    if (select) {
        item->setSelected(true);
        item->setCheckState(true);
    }
    return true;
}

void DocumentItem::updateItemsVisibility(QTreeWidgetItem* item, bool show)
{
    if (item->type() == TreeWidget::ObjectType) {
        auto objitem = static_cast<DocumentObjectItem*>(item);
        objitem->setHidden(!show && !objitem->object()->showInTree());
    }
    for (int i = 0; i < item->childCount(); ++i) {
        updateItemsVisibility(item->child(i), show);
    }
}

void DocumentItem::updateSelection()
{
    bool lock = getTree()->blockSelection(true);
    updateSelection(this, false);
    getTree()->blockSelection(lock);
}

bool DocumentItem::isObjectShowable(App::DocumentObject* obj)
{
    auto itParents = _ParentMap.find(obj);
    if (itParents == _ParentMap.end() || itParents->second.empty()) {
        return true;
    }
    bool showable = true;
    for (auto parent : itParents->second) {
        if (parent->getDocument() != obj->getDocument()) {
            continue;
        }
        if (!parent->hasChildElement() && parent->getLinkedObject(false) == parent) {
            return true;
        }
        showable = false;
    }
    return showable;
}
