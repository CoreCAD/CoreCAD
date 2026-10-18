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

// Dragging rows in the tree and dropping them: onto an object, into a document, or to reorder
// the document's history.

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


FC_LOG_LEVEL_INIT("Tree", false, true, true)

#define _TREE_PRINT(_level, _func, _msg) \
    _FC_PRINT(FC_LOG_INSTANCE, _level, _func, '[' << getTreeName() << "] " << _msg)
#define TREE_MSG(_msg) _TREE_PRINT(FC_LOGLEVEL_MSG, notify<Base::LogStyle::Message>, _msg)
#define TREE_WARN(_msg) _TREE_PRINT(FC_LOGLEVEL_WARN, notify<Base::LogStyle::Warning>, _msg)
#define TREE_ERR(_msg) _TREE_PRINT(FC_LOGLEVEL_ERR, notify<Base::LogStyle::Error>, _msg)
#define TREE_LOG(_msg) _TREE_PRINT(FC_LOGLEVEL_LOG, notify<Base::LogStyle::Log>, _msg)
#define TREE_TRACE(_msg) _TREE_PRINT(FC_LOGLEVEL_TRACE, notify<Base::LogStyle::Log>, _msg)

using namespace Gui;
using namespace Gui::TreeHistory;
using namespace Gui::TreeShared;

void TreeWidget::startDragging()
{
    if (state() != NoState) {
        return;
    }
    if (selectedItems().empty()) {
        return;
    }

    setState(DraggingState);
    startDrag(model()->supportedDragActions());
    setState(NoState);
    stopAutoScroll();
}

void TreeWidget::startDrag(Qt::DropActions supportedActions)
{
    Base::StateLocker guard(_DraggingActive);
    QTreeWidget::startDrag(supportedActions);
    if (_DragEventFilter) {
        _DragEventFilter = false;
        qApp->removeEventFilter(this);
    }
}

bool TreeWidget::dropMimeData(QTreeWidgetItem* parent, int index, const QMimeData* data, Qt::DropAction action)
{
    return QTreeWidget::dropMimeData(parent, index, data, action);
}

void TreeWidget::dragEnterEvent(QDragEnterEvent* event)
{
    QTreeWidget::dragEnterEvent(event);
}

void TreeWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    QTreeWidget::dragLeaveEvent(event);
}


struct ItemInfo
{
    std::string doc;
    std::string obj;
    std::string parentDoc;
    std::string parent;
    std::string ownerDoc;
    std::string owner;
    std::string subname;
    std::string topDoc;
    std::string topObj;
    std::string topSubname;
    std::vector<std::string> subs;
    bool dragging = false;
};

struct ItemInfo2
{
    std::string doc;
    std::string obj;
    std::string parentDoc;
    std::string parent;
    std::string topDoc;
    std::string topObj;
    std::string topSubname;
};

namespace
{
class DropHandler
{
public:
    static std::vector<std::pair<DocumentObjectItem*, std::vector<std::string>>> filterItems(
        const QList<QTreeWidgetItem*>& sels,
        QTreeWidgetItem* targetItem
    )
    {
        std::vector<std::pair<DocumentObjectItem*, std::vector<std::string>>> items;
        items.reserve(sels.size());
        for (auto ti : sels) {
            if (ti->type() != TreeWidget::ObjectType) {
                continue;
            }
            // ignore child elements if the parent is selected
            if (sels.contains(ti->parent())) {
                continue;
            }
            if (ti == targetItem) {
                continue;
            }
            auto item = static_cast<DocumentObjectItem*>(ti);
            items.emplace_back();
            auto& info = items.back();
            info.first = item;
            const auto& subnames = item->getSubNames();
            info.second.insert(info.second.end(), subnames.begin(), subnames.end());
        }

        return items;
    }
    static App::PropertyPlacement* getPlacement(
        const ItemInfo& info,
        const App::DocumentObject* obj,
        Base::Matrix4D& mat
    )
    {
        App::PropertyPlacement* propPlacement = nullptr;
        if (!info.topObj.empty()) {
            auto doc = App::GetApplication().getDocument(info.topDoc.c_str());
            if (doc) {
                auto topObj = doc->getObject(info.topObj.c_str());
                if (topObj) {
                    auto sobj = topObj->getSubObject(info.topSubname.c_str(), nullptr, &mat);
                    if (sobj == obj) {
                        propPlacement = freecad_cast<App::PropertyPlacement*>(
                            obj->getPropertyByName("Placement")
                        );
                    }
                }
            }
        }
        else {
            propPlacement = freecad_cast<App::PropertyPlacement*>(obj->getPropertyByName("Placement"));
            if (propPlacement) {
                mat = propPlacement->getValue().toMatrix();
            }
        }

        return propPlacement;
    }
};

QPoint getPos(QEvent* event)
{
    if (auto* dragMoveEvent = dynamic_cast<QDragMoveEvent*>(event)) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return dragMoveEvent->pos();
#else
        return dragMoveEvent->position().toPoint();
#endif
    }

    else if (auto* dropEvent = dynamic_cast<QDropEvent*>(event)) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return dropEvent->pos();
#else
        return dropEvent->position().toPoint();
#endif
    }

    // For unsupported event types or if casting fails
    return QPoint(-1, -1);
}

Qt::DropAction getDropAction(int size, const int type)
{
    if (QApplication::keyboardModifiers() == Qt::ControlModifier) {
        return Qt::CopyAction;
    }
    else if (
        QApplication::keyboardModifiers() == Qt::AltModifier
        && (size == 1 || type == TreeWidget::DocumentType)
    ) {
        return Qt::LinkAction;
    }
    else {
        return Qt::MoveAction;
    }
}
}  // namespace

namespace
{
std::vector<App::DocumentObject*> draggedObjectsOf(const QList<QTreeWidgetItem*>& items)
{
    std::vector<App::DocumentObject*> objects;
    for (auto* item : items) {
        if (item->type() == TreeWidget::ObjectType) {
            objects.push_back(static_cast<DocumentObjectItem*>(item)->object()->getObject());
        }
    }
    return objects;
}
}  // namespace

void TreeWidget::dragMoveEvent(QDragMoveEvent* event)
{
    // Qt5 does not change drag cursor in response to modifier key press,
    // because QDrag installs a event filter that eats up key event. We install
    // a filter after Qt and generate fake mouse move event in response to key
    // press event, which triggers QDrag to update its cursor
    if (!_DragEventFilter) {
        _DragEventFilter = true;
        qApp->installEventFilter(this);
    }

    QTreeWidget::dragMoveEvent(event);
    if (!event->isAccepted()) {
        // return;
        //  QTreeWidget::dragMoveEvent is rejecting the event when in between items
        //  at DocumentItem root level. Which is preventing reordering. To work around
        //  we accept for now, then reject below if targetItem not found.
        event->accept();
    }


    TargetItemInfo targetInfo = getTargetInfo(event);
    QTreeWidgetItem* targetItem = targetInfo.targetItem;
    if (!targetItem) {
        event->ignore();
        return;
    }

    auto items = selectedItems();

    auto da = getDropAction(items.size(), targetItem->type());
    auto visualDa = da;
    if (targetItem->type() == TreeWidget::ObjectType) {
        if (auto* vp = static_cast<DocumentObjectItem*>(targetItem)->object()) {
            visualDa = vp->getDropActionForTarget(da);
        }
    }
    event->setDropAction(visualDa);

    if (targetItem->type() == TreeWidget::DocumentType) {
        leaveEvent(nullptr);
    }
    else if (targetItem->type() == TreeWidget::ObjectType) {
        onItemEntered(targetItem);

        auto targetItemObj = static_cast<DocumentObjectItem*>(targetItem);
        Gui::ViewProviderDocumentObject* vp = targetItemObj->object();

        // if we are in between or if target doesn't accept drops then the target is the parent
        if (da == Qt::MoveAction && (targetInfo.inThresholdZone || !vp->canDropObjects())) {
            targetInfo.targetItem = targetInfo.targetItem->parent();
            if (targetInfo.targetItem->type() == TreeWidget::DocumentType) {
                leaveEvent(nullptr);
                if (!dropKeepsInputsFirst(targetInfo, draggedObjectsOf(items))) {
                    event->ignore();
                }
                return;
            }

            targetItemObj = static_cast<DocumentObjectItem*>(targetInfo.targetItem);
            vp = targetItemObj->object();

            if (!vp) {
                TREE_TRACE("cannot drop");
                return;
            }
        }
        try {
            if (da != Qt::LinkAction && !vp->canDropObjects()) {
                if (!(event->possibleActions() & Qt::LinkAction) || items.size() != 1) {
                    TREE_TRACE("Cannot drop here");
                    event->ignore();
                    return;
                }
            }
            for (auto ti : items) {
                if (ti->type() != TreeWidget::ObjectType) {
                    TREE_TRACE("cannot drop");
                    event->ignore();
                    return;
                }
                auto item = static_cast<DocumentObjectItem*>(ti);

                auto obj = item->object()->getObject();

                if (da == Qt::MoveAction) {
                    // Check if item can be dragged from his parent
                    auto parentItem = item->getParentItem();
                    if (parentItem
                        && !(
                            parentItem->object()->canDragObjects()
                            && parentItem->object()->canDragObject(obj)
                        )) {
                        TREE_TRACE("Cannot drag object");
                        event->ignore();
                        return;
                    }
                }

                std::ostringstream str;
                auto owner = item->getRelativeParent(str, targetItemObj);
                auto subname = str.str();

                // let the view provider decide to accept the object or ignore it
                if (da != Qt::LinkAction
                    && !vp->canDropObjectEx(obj, owner, subname.c_str(), item->mySubs)) {
                    TREE_TRACE(
                        "cannot drop " << obj->getFullName() << ' '
                                       << (owner ? owner->getFullName() : "<No Owner>") << '.'
                                       << subname
                    );
                    event->ignore();
                    return;
                }
            }
        }
        catch (Base::Exception& e) {
            e.reportException();
            event->ignore();
        }
        catch (std::exception& e) {
            FC_ERR("C++ exception: " << e.what());
            event->ignore();
        }
        catch (...) {
            FC_ERR("Unknown exception");
            event->ignore();
        }
    }
    else {
        leaveEvent(nullptr);
        event->ignore();
    }
}

TreeWidget::TargetItemInfo TreeWidget::getTargetInfo(QEvent* ev)
{
    TargetItemInfo targetInfo;

    QPoint pos = getPos(ev);
    if (pos == QPoint(-1, -1)) {
        return {};  // Return an empty struct
    }

    targetInfo.targetItem = itemAt(pos);
    // not dropped onto an item or one of the source items is also the destination item
    if (!targetInfo.targetItem || targetInfo.targetItem->isSelected()) {
        return {};
    }
    targetInfo.underMouseItem = targetInfo.targetItem;

    if (targetInfo.targetItem->type() == TreeWidget::ObjectType) {
        auto targetItemObj = static_cast<DocumentObjectItem*>(targetInfo.targetItem);
        targetInfo.targetDoc = targetItemObj->getOwnerDocument()->document()->getDocument();
    }
    else if (targetInfo.targetItem->type() == TreeWidget::DocumentType) {
        auto targetDocItem = static_cast<DocumentItem*>(targetInfo.targetItem);
        targetInfo.targetDoc = targetDocItem->document()->getDocument();
    }
    else {
        return {};
    }

    // Calculate the position of the mouse relative to the item's rectangle
    QRect itemRect = visualItemRect(targetInfo.targetItem);
    int mouseY = pos.y();
    int itemMidPoint = itemRect.top() + itemRect.height() / 2;
    int threshold = itemRect.height() * 0.20;  // 20% of the item's height as threshold

    targetInfo.inBottomHalf = mouseY > itemMidPoint;
    targetInfo.inThresholdZone = ((mouseY < itemRect.top() + threshold)
                                  && !targetInfo.targetItem->isExpanded())
        || (mouseY > itemRect.top() + itemRect.height() - threshold);
    return targetInfo;
}

bool TreeWidget::dropInDocument(
    QDropEvent* event,
    TargetItemInfo& targetInfo,
    std::vector<TreeWidget::ObjectItemSubname> items
)
{
    std::string errMsg;
    auto da = event->dropAction();
    bool touched = false;

    std::vector<ItemInfo2> infos;
    infos.reserve(items.size());
    bool syncPlacement = TreeParams::getSyncPlacement();

    int tid = 0;
    std::string transName = da == Qt::LinkAction ? "Link object"
        : da == Qt::CopyAction                   ? "Copy object"
                                                 : "Move object";

    // check if items can be dragged
    for (auto& v : items) {
        auto item = v.first;
        auto obj = item->object()->getObject();
        auto parentItem = item->getParentItem();

        tid = obj->getDocument()->openTransaction(
            transName,
            tid
        );  // If the same document already has this transaction opened, it is ignored
        if (parentItem) {
            bool allParentsOK = canDragFromParents(parentItem, obj, nullptr);

            if (!allParentsOK || !parentItem->object()->canDragObjects()
                || !parentItem->object()->canDragObject(obj)) {
                App::GetApplication().abortTransaction(tid);
                TREE_ERR(
                    "'" << obj->getFullName() << "' cannot be dragged out of '"
                        << parentItem->object()->getObject()->getFullName() << "'"
                );
                return false;
            }
        }
        else if (da != Qt::MoveAction || item->myOwner != targetInfo.targetItem) {
            // We will not drag item out of parent if either, 1) modifier
            // key is held, or 2) the dragging item is not inside the
            // dropping document tree.
            parentItem = nullptr;
        }
        infos.emplace_back();
        auto& info = infos.back();
        info.doc = obj->getDocument()->getName();
        info.obj = obj->getNameInDocument();
        if (parentItem) {
            auto parent = parentItem->object()->getObject();
            info.parentDoc = parent->getDocument()->getName();
            info.parent = parent->getNameInDocument();
        }
        if (syncPlacement) {
            std::ostringstream ss;
            App::DocumentObject* topParent = nullptr;
            item->getSubName(ss, topParent);
            if (topParent) {
                info.topDoc = topParent->getDocument()->getName();
                info.topObj = topParent->getNameInDocument();
                ss << obj->getNameInDocument() << '.';
                info.topSubname = ss.str();
            }
        }
    }
    // Because the existence of subname, we must de-select the drag the
    // object manually. Just do a complete clear here for simplicity
    Selection().selStackPush();
    Selection().clearCompleteSelection();

    // Open command
    auto manager = Application::Instance->macroManager();
    try {
        std::vector<App::DocumentObject*> droppedObjs;
        for (auto& info : infos) {
            auto doc = App::GetApplication().getDocument(info.doc.c_str());
            if (!doc) {
                continue;
            }
            auto obj = doc->getObject(info.obj.c_str());
            auto vpc = freecad_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(obj)
            );
            if (!vpc) {
                FC_WARN("Cannot find dragging object " << info.obj);
                continue;
            }

            Base::Matrix4D mat;
            App::PropertyPlacement* propPlacement = nullptr;
            if (syncPlacement) {
                if (!info.topObj.empty()) {
                    auto doc = App::GetApplication().getDocument(info.topDoc.c_str());
                    if (doc) {
                        auto topObj = doc->getObject(info.topObj.c_str());
                        if (topObj) {
                            auto sobj = topObj->getSubObject(info.topSubname.c_str(), nullptr, &mat);
                            if (sobj == obj) {
                                propPlacement = dynamic_cast<App::PropertyPlacement*>(
                                    obj->getPropertyByName("Placement")
                                );
                            }
                        }
                    }
                }
                else {
                    propPlacement = dynamic_cast<App::PropertyPlacement*>(
                        obj->getPropertyByName("Placement")
                    );
                    if (propPlacement) {
                        mat = propPlacement->getValue().toMatrix();
                    }
                }
            }

            if (da == Qt::LinkAction) {
                std::string name = targetInfo.targetDoc->getUniqueObjectName("Link");
                FCMD_DOC_CMD(
                    targetInfo.targetDoc,
                    "addObject('App::Link','" << name << "').setLink(" << Command::getObjectCmd(obj)
                                              << ")"
                );
                auto link = targetInfo.targetDoc->getObject(name.c_str());
                if (!link) {
                    continue;
                }
                FCMD_OBJ_CMD(link, "Label='" << obj->getLinkedObject(true)->Label.getValue() << "'");
                propPlacement = dynamic_cast<App::PropertyPlacement*>(
                    link->getPropertyByName("Placement")
                );
                if (propPlacement) {
                    propPlacement->setValueIfChanged(Base::Placement(mat));
                }
                droppedObjs.push_back(link);
            }
            else if (!info.parent.empty()) {
                auto parentDoc = App::GetApplication().getDocument(info.parentDoc.c_str());
                if (!parentDoc) {
                    FC_WARN("Canont find document " << info.parentDoc);
                    continue;
                }
                auto parent = parentDoc->getObject(info.parent.c_str());
                auto vpp = freecad_cast<ViewProviderDocumentObject*>(
                    Application::Instance->getViewProvider(parent)
                );
                if (!vpp) {
                    FC_WARN("Cannot find dragging object's parent " << info.parent);
                    continue;
                }

                std::ostringstream ss;
                ss << Command::getObjectCmd(vpp->getObject()) << ".ViewObject.dragObject("
                   << Command::getObjectCmd(obj) << ')';
                auto lines = manager->getLines();
                vpp->dragObject(obj);
                if (manager->getLines() == lines) {
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }

                // make sure it is not part of a geofeaturegroup anymore.
                // When this has happen we need to handle all removed
                // objects
                auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(obj);
                if (grp) {
                    FCMD_OBJ_CMD(grp, "removeObject(" << Command::getObjectCmd(obj) << ")");
                }

                // check if the object has been deleted
                obj = doc->getObject(info.obj.c_str());
                if (!obj || !obj->isAttachedToDocument()) {
                    continue;
                }
                droppedObjs.push_back(obj);
                if (propPlacement) {
                    propPlacement->setValueIfChanged(Base::Placement(mat));
                }
            }
            else {
                std::ostringstream ss;
                ss << "App.getDocument('" << targetInfo.targetDoc->getName() << "')."
                   << (da == Qt::CopyAction ? "copyObject(" : "moveObject(")
                   << Command::getObjectCmd(obj) << ", True)";
                App::DocumentObject* res = nullptr;
                if (da == Qt::CopyAction) {
                    auto copied = targetInfo.targetDoc->copyObject({obj}, true);
                    if (!copied.empty()) {
                        res = copied.back();
                    }
                }
                else if (da == Qt::MoveAction && obj->getDocument() == targetInfo.targetDoc) {
                    // Moving a object within the document root.
                    // Do not set 'res' as changing the placement is not desired: #13690
                    droppedObjs.push_back(obj);
                }
                else {
                    // Moving a object from another document.
                    res = targetInfo.targetDoc->moveObject(obj, true);
                }
                if (res) {
                    propPlacement = dynamic_cast<App::PropertyPlacement*>(
                        res->getPropertyByName("Placement")
                    );
                    if (propPlacement) {
                        propPlacement->setValueIfChanged(Base::Placement(mat));
                    }
                    droppedObjs.push_back(res);
                }
                manager->addLine(MacroManager::App, ss.str().c_str());
            }
        }
        touched = true;
        Base::FlagToggler<> guard(_DisableCheckTopParent);
        Selection().setSelection(targetInfo.targetDoc->getName(), droppedObjs);

        // If moved, then we sort objects properly.
        if (da == Qt::MoveAction) {
            sortDroppedObjects(targetInfo, droppedObjs);
        }
    }
    catch (const Base::Exception& e) {
        e.reportException();
        errMsg = e.what();
    }
    catch (std::exception& e) {
        FC_ERR("C++ exception: " << e.what());
        errMsg = e.what();
    }
    catch (...) {
        FC_ERR("Unknown exception");
        errMsg = "Unknown exception";
    }
    if (!errMsg.empty()) {
        App::GetApplication().abortTransaction(tid);
        QMessageBox::critical(
            getMainWindow(),
            QObject::tr("Drag & drop failed"),
            QString::fromUtf8(errMsg.c_str())
        );
        return false;
    }

    App::GetApplication().commitTransaction(tid);
    return touched;
}

bool TreeWidget::dropInObject(
    QDropEvent* event,
    TargetItemInfo& targetInfo,
    std::vector<TreeWidget::ObjectItemSubname> items
)
{
    std::string errMsg;
    auto da = event->dropAction();
    bool touched = false;

    // add object to group
    auto targetItemObj = static_cast<DocumentObjectItem*>(targetInfo.targetItem);
    Gui::ViewProviderDocumentObject* vp = targetItemObj->object();

    if (!vp || !vp->getObject() || !vp->getObject()->isAttachedToDocument()) {
        TREE_TRACE("invalid object");
        return false;
    }

    // if we are in between or if target doesn't accept drops then the target is the parent
    if (da == Qt::MoveAction && (targetInfo.inThresholdZone || !vp->canDropObjects())) {
        targetInfo.targetItem = targetInfo.targetItem->parent();
        if (targetInfo.targetItem->type() == TreeWidget::DocumentType) {
            return dropInDocument(event, targetInfo, items);
        }

        targetItemObj = static_cast<DocumentObjectItem*>(targetInfo.targetItem);
        vp = targetItemObj->object();

        if (!vp || !vp->getObject() || !vp->getObject()->isAttachedToDocument()) {
            TREE_TRACE("invalid object");
            return false;
        }
    }

    if (da != Qt::LinkAction && !vp->canDropObjects()) {
        if (!(event->possibleActions() & Qt::LinkAction) || items.size() != 1) {
            TREE_TRACE("Cannot drop objects");
            return false;  // no group like object
        }
    }

    App::DocumentObject* targetObj = targetItemObj->object()->getObject();
    std::ostringstream targetSubname;
    App::DocumentObject* targetParent = nullptr;
    targetItemObj->getSubName(targetSubname, targetParent);
    Selection().selStackPush();
    Selection().clearCompleteSelection();
    if (targetParent) {
        targetSubname << vp->getObject()->getNameInDocument() << '.';
        Selection().addSelection(
            targetParent->getDocument()->getName(),
            targetParent->getNameInDocument(),
            targetSubname.str().c_str()
        );
    }
    else {
        targetParent = targetObj;
        Selection().addSelection(
            targetParent->getDocument()->getName(),
            targetParent->getNameInDocument()
        );
    }

    bool syncPlacement = TreeParams::getSyncPlacement() && targetItemObj->isGroup();
    bool setSelection = true;
    std::vector<App::DocumentObject*> draggedObjects;
    std::vector<std::pair<App::DocumentObject*, std::string>> droppedObjects;
    std::vector<ItemInfo> infos;
    int tid = 0;
    // Only keep text names here, because you never know when doing drag
    // and drop some object may delete other objects.
    infos.reserve(items.size());
    for (auto& v : items) {
        infos.emplace_back();
        auto& info = infos.back();
        auto item = v.first;
        App::DocumentObject* obj = item->object()->getObject();
        tid = obj->getDocument()->openTransaction("Drop object");

        std::ostringstream str;
        App::DocumentObject* topParent = nullptr;
        auto owner = item->getRelativeParent(str, targetItemObj, &topParent, &info.topSubname);
        if (syncPlacement && topParent) {
            info.topDoc = topParent->getDocument()->getName();
            info.topObj = topParent->getNameInDocument();
        }
        info.subname = str.str();
        info.doc = obj->getDocument()->getName();
        info.obj = obj->getNameInDocument();
        if (owner) {
            info.ownerDoc = owner->getDocument()->getName();
            info.owner = owner->getNameInDocument();
        }

        info.subs.swap(v.second);

        // check if items can be dragged
        if (da == Qt::MoveAction && item->myOwner == targetItemObj->myOwner
            && vp->canDragAndDropObject(obj)) {
            auto parentItem = item->getParentItem();
            if (!parentItem) {
                info.dragging = true;
            }
            else {

                bool allParentsOK = canDragFromParents(parentItem, obj, targetObj);

                if (allParentsOK) {
                    auto vpp = parentItem->object();
                    info.dragging = true;
                    info.parent = vpp->getObject()->getNameInDocument();
                    info.parentDoc = vpp->getObject()->getDocument()->getName();
                }
                else {
                    App::GetApplication().abortTransaction(tid);
                    return false;
                }
            }
        }

        if (da != Qt::LinkAction
            && !vp->canDropObjectEx(obj, owner, info.subname.c_str(), item->mySubs)) {
            if (event->possibleActions() & Qt::LinkAction) {
                if (items.size() > 1) {
                    App::GetApplication().abortTransaction(tid);
                    TREE_TRACE("Cannot replace with more than one object");
                    return false;
                }
                auto ext = vp->getObject()->getExtensionByType<App::LinkBaseExtension>(true);
                if ((!ext || !ext->getLinkedObjectProperty()) && !targetItemObj->getParentItem()) {
                    App::GetApplication().abortTransaction(tid);
                    TREE_TRACE("Cannot replace without parent");
                    return false;
                }
                da = Qt::LinkAction;
            }
        }
    }

    try {
        std::set<App::DocumentObject*> inList;
        auto parentObj = targetObj;
        if (da == Qt::LinkAction && targetItemObj->getParentItem()) {
            parentObj = targetItemObj->getParentItem()->object()->getObject();
        }
        inList = parentObj->getInListEx(true);
        inList.insert(parentObj);

        std::string target = targetObj->getNameInDocument();
        auto targetDoc = targetObj->getDocument();
        for (auto& info : infos) {
            auto& subname = info.subname;
            targetObj = targetDoc->getObject(target.c_str());
            vp = freecad_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(targetObj)
            );
            if (!vp) {
                FC_ERR("Cannot find drop target object " << target);
                break;
            }

            auto doc = App::GetApplication().getDocument(info.doc.c_str());
            if (!doc) {
                FC_WARN("Cannot find document " << info.doc);
                continue;
            }
            auto obj = doc->getObject(info.obj.c_str());
            auto vpc = freecad_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(obj)
            );
            if (!vpc) {
                FC_WARN("Cannot find dragging object " << info.obj);
                continue;
            }

            ViewProviderDocumentObject* vpp = nullptr;
            if (da != Qt::LinkAction && !info.parentDoc.empty()) {
                auto parentDoc = App::GetApplication().getDocument(info.parentDoc.c_str());
                if (parentDoc) {
                    auto parent = parentDoc->getObject(info.parent.c_str());
                    vpp = freecad_cast<ViewProviderDocumentObject*>(
                        Application::Instance->getViewProvider(parent)
                    );
                }
                if (!vpp) {
                    FC_WARN("Cannot find dragging object's parent " << info.parent);
                    continue;
                }
            }

            App::DocumentObject* owner = nullptr;
            if (!info.ownerDoc.empty()) {
                auto ownerDoc = App::GetApplication().getDocument(info.ownerDoc.c_str());
                if (ownerDoc) {
                    owner = ownerDoc->getObject(info.owner.c_str());
                }
                if (!owner) {
                    FC_WARN("Cannot find dragging object's top parent " << info.owner);
                    continue;
                }
            }

            Base::Matrix4D mat;
            App::PropertyPlacement* propPlacement = nullptr;
            if (syncPlacement) {
                propPlacement = DropHandler::getPlacement(info, obj, mat);
            }

            auto dropParent = targetParent;

            auto manager = Application::Instance->macroManager();
            std::ostringstream ss;
            if (vpp) {
                auto lines = manager->getLines();
                ss << Command::getObjectCmd(vpp->getObject()) << ".ViewObject.dragObject("
                   << Command::getObjectCmd(obj) << ')';
                vpp->dragObject(obj);
                if (manager->getLines() == lines) {
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }
                owner = nullptr;
                subname.clear();
                ss.str("");

                obj = doc->getObject(info.obj.c_str());
                if (!obj || !obj->isAttachedToDocument()) {
                    FC_WARN("Dropping object deleted: " << info.doc << '#' << info.obj);
                    continue;
                }
            }

            if (da == Qt::MoveAction) {
                // Try to adjust relative links to avoid cyclic dependency, may
                // throw exception if failed
                ss.str("");
                ss << Command::getObjectCmd(obj) << ".adjustRelativeLinks("
                   << Command::getObjectCmd(targetObj) << ")";
                manager->addLine(MacroManager::Gui, ss.str().c_str());

                std::set<App::DocumentObject*> visited;
                if (obj->adjustRelativeLinks(inList, &visited)) {
                    inList = parentObj->getInListEx(true);
                    inList.insert(parentObj);

                    // TODO: link adjustment and placement adjustment does
                    // not work together at the moment.
                    propPlacement = nullptr;
                }
            }

            if (inList.contains(obj)) {
                FC_THROWM(Base::RuntimeError, "Dependency loop detected for " << obj->getFullName());
            }


            std::string dropName;
            ss.str("");
            if (da == Qt::LinkAction) {
                auto parentItem = targetItemObj->getParentItem();
                if (parentItem) {
                    ss << Command::getObjectCmd(
                        parentItem->object()->getObject(),
                        nullptr,
                        ".replaceObject(",
                        true
                    ) << Command::getObjectCmd(targetObj)
                       << "," << Command::getObjectCmd(obj) << ")";

                    std::ostringstream ss;

                    dropParent = nullptr;
                    parentItem->getSubName(ss, dropParent);
                    if (dropParent) {
                        ss << parentItem->object()->getObject()->getNameInDocument() << '.';
                    }
                    else {
                        dropParent = parentItem->object()->getObject();
                    }
                    ss << obj->getNameInDocument() << '.';
                    dropName = ss.str();
                }
                else {
                    TREE_WARN("ignore replace operation without parent");
                    continue;
                }

                Gui::Command::runCommand(Gui::Command::App, ss.str().c_str());
            }
            else {
                ss << Command::getObjectCmd(vp->getObject()) << ".ViewObject.dropObject("
                   << Command::getObjectCmd(obj);
                if (owner) {
                    ss << "," << Command::getObjectCmd(owner) << ",'" << subname << "',[";
                }
                else {
                    ss << ",None,'',[";
                }
                for (auto& sub : info.subs) {
                    ss << "'" << sub << "',";
                }
                ss << "])";
                auto lines = manager->getLines();
                dropName = vp->dropObjectEx(obj, owner, subname.c_str(), info.subs);
                if (manager->getLines() == lines) {
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }
                if (!dropName.empty()) {
                    dropName = targetSubname.str() + dropName;
                }
            }

            touched = true;

            // Construct the subname pointing to the dropped object
            if (dropName.empty()) {
                auto pos = targetSubname.tellp();
                targetSubname << obj->getNameInDocument() << '.' << std::ends;
                dropName = targetSubname.str();
                targetSubname.seekp(pos);
            }

            Base::Matrix4D newMat;
            auto sobj = dropParent->getSubObject(dropName.c_str(), nullptr, &newMat);
            if (!sobj) {
                FC_LOG(
                    "failed to find dropped object " << dropParent->getFullName() << '.' << dropName
                );
                setSelection = false;
                continue;
            }

            if (da != Qt::CopyAction && propPlacement) {
                // try to adjust placement
                if ((info.dragging && sobj == obj)
                    || (!info.dragging && sobj->getLinkedObject(false) == obj)) {
                    if (!info.dragging) {
                        propPlacement = freecad_cast<App::PropertyPlacement*>(
                            sobj->getPropertyByName("Placement")
                        );
                    }
                    if (propPlacement) {
                        newMat *= propPlacement->getValue().inverse().toMatrix();
                        newMat.inverseGauss();
                        Base::Placement pla(newMat * mat);
                        propPlacement->setValueIfChanged(pla);
                    }
                }
            }
            droppedObjects.emplace_back(dropParent, dropName);
            draggedObjects.push_back(obj);
        }
        Base::FlagToggler<> guard(_DisableCheckTopParent);
        if (setSelection && !droppedObjects.empty()) {
            Selection().selStackPush();
            Selection().clearCompleteSelection();
            for (auto& v : droppedObjects) {
                Selection().addSelection(
                    v.first->getDocument()->getName(),
                    v.first->getNameInDocument(),
                    v.second.c_str()
                );
            }
            Selection().selStackPush();
        }

        // If moved, then we sort objects properly.
        if (da == Qt::MoveAction && vp->acceptReorderingObjects()) {
            sortDroppedObjects(targetInfo, draggedObjects);
        }
    }
    catch (const Base::Exception& e) {
        e.reportException();
        errMsg = e.what();
    }
    catch (std::exception& e) {
        FC_ERR("C++ exception: " << e.what());
        errMsg = e.what();
    }
    catch (...) {
        FC_ERR("Unknown exception");
        errMsg = "Unknown exception";
    }
    if (!errMsg.empty()) {
        App::GetApplication().abortTransaction(tid);
        QMessageBox::critical(
            getMainWindow(),
            QObject::tr("Drag & drop failed"),
            QString::fromUtf8(errMsg.c_str())
        );
        return false;
    }
    App::GetApplication().commitTransaction(tid);
    return touched;
}

bool TreeWidget::canDragFromParents(
    DocumentObjectItem* parentItem,
    App::DocumentObject* obj,
    App::DocumentObject* target
)
{
    // We query all the parents recursively. (for cases like assembly/group/part)
    bool allParentsOK = true;
    while (parentItem) {
        if (!parentItem->object()->canDragObjectToTarget(obj, target)) {
            allParentsOK = false;
            break;
        }
        parentItem = parentItem->getParentItem();
    }

    return allParentsOK;
}

void TreeWidget::dropEvent(QDropEvent* event)
{
    // FIXME: This should actually be done inside dropMimeData

    TargetItemInfo targetInfo = getTargetInfo(event);
    if (!targetInfo.targetItem) {
        return;
    }

    // filter out the selected items we cannot handle
    std::vector<ObjectItemSubname> items;
    items = DropHandler::filterItems(selectedItems(), targetInfo.targetItem);
    if (items.empty()) {
        return;  // nothing needs to be done
    }

    event->setDropAction(getDropAction(items.size(), targetInfo.targetItem->type()));

    bool touched = false;
    if (targetInfo.targetItem->type() == TreeWidget::ObjectType) {
        touched = dropInObject(event, targetInfo, items);
    }
    else if (targetInfo.targetItem->type() == TreeWidget::DocumentType) {
        touched = dropInDocument(event, targetInfo, items);
    }

    if (touched && TreeParams::getRecomputeOnDrop()) {
        targetInfo.targetDoc->recompute();
    }
    if (touched && TreeParams::getSyncView()) {
        auto gdoc = Application::Instance->getDocument(targetInfo.targetDoc);
        if (gdoc) {
            gdoc->setActiveView();
        }
    }
}

std::vector<App::DocumentObject*> TreeWidget::rootOrderAfterDrop(
    const TargetItemInfo& targetInfo,
    const std::vector<App::DocumentObject*>& draggedObjects
) const
{
    Gui::Document* guiDoc = Gui::Application::Instance->getDocument(targetInfo.targetDoc);
    std::vector<App::DocumentObject*> objList = guiDoc->getTreeRootObjects();
    const auto position = historyPositions(targetInfo.targetDoc);
    std::ranges::stable_sort(objList, [&position](App::DocumentObject* a, App::DocumentObject* b) {
        return positionIn(position, a) < positionIn(position, b);
    });
    auto underMouse = static_cast<DocumentObjectItem*>(targetInfo.underMouseItem);
    return placeDropped(objList, draggedObjects, underMouse->object()->getObject(), targetInfo.inBottomHalf);
}

bool TreeWidget::dropKeepsInputsFirst(
    const TargetItemInfo& targetInfo,
    const std::vector<App::DocumentObject*>& draggedObjects
) const
{
    if (!targetInfo.targetDoc || !targetInfo.underMouseItem
        || targetInfo.underMouseItem->type() != ObjectType) {
        return true;
    }
    return keepsInputsFirst(rootOrderAfterDrop(targetInfo, draggedObjects));
}

void TreeWidget::sortDroppedObjects(
    TargetItemInfo& targetInfo,
    std::vector<App::DocumentObject*> draggedObjects
)
{
    if (targetInfo.targetItem == targetInfo.underMouseItem) {
        return;
    }
    auto underMouseItemObj = static_cast<DocumentObjectItem*>(targetInfo.underMouseItem);
    auto underMouseObj = underMouseItemObj->object()->getObject();

    if (targetInfo.targetItem->type() == TreeWidget::ObjectType) {
        // To update the order of items of groups such as App::Part, we just need to change the
        // order in the Group property
        auto targetItemObj = static_cast<DocumentObjectItem*>(targetInfo.targetItem);
        App::DocumentObject* targetObj = targetItemObj->object()->getObject();

        auto propGroup = freecad_cast<App::PropertyLinkList*>(targetObj->getPropertyByName("Group"));
        if (!propGroup) {
            return;
        }
        propGroup->setValue(
            placeDropped(propGroup->getValue(), draggedObjects, underMouseObj, targetInfo.inBottomHalf)
        );
    }
    else if (targetInfo.targetItem->type() == TreeWidget::DocumentType) {
        const auto sortedObjList = rootOrderAfterDrop(targetInfo, draggedObjects);
        if (!keepsInputsFirst(sortedObjList)) {
            return;
        }
        // The tree is the timeline: the drop moves the steps in the document's history itself,
        // which is what the roll-back marker and the saved file read.
        targetInfo.targetDoc->arrangeObjects(sortedObjList);
        for (auto* obj : draggedObjects) {
            obj->touch();
        }

        auto* docItem = static_cast<DocumentItem*>(targetInfo.targetItem);
        docItem->sortObjectItems();
        docItem->updateHeldBack();
    }
}
