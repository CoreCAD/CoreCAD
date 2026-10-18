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

#pragma once

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <QBrush>
#include <QStyledItemDelegate>
#include <QTreeView>

#include <fastsignals/signal.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Reader.h>

#include "Application.h"
#include "Document.h"
#include "Tree.h"
#include "TreeParams.h"
#include "ViewProviderDocumentObject.h"

FC_LOG_LEVEL_INIT("Tree", false, true, true)

#define _TREE_PRINT(_level, _func, _msg) \
    _FC_PRINT(FC_LOG_INSTANCE, _level, _func, '[' << getTreeName() << "] " << _msg)
#define TREE_MSG(_msg) _TREE_PRINT(FC_LOGLEVEL_MSG, notify<Base::LogStyle::Message>, _msg)
#define TREE_WARN(_msg) _TREE_PRINT(FC_LOGLEVEL_WARN, notify<Base::LogStyle::Warning>, _msg)
#define TREE_ERR(_msg) _TREE_PRINT(FC_LOGLEVEL_ERR, notify<Base::LogStyle::Error>, _msg)
#define TREE_LOG(_msg) _TREE_PRINT(FC_LOGLEVEL_LOG, notify<Base::LogStyle::Log>, _msg)
#define TREE_TRACE(_msg) _TREE_PRINT(FC_LOGLEVEL_TRACE, notify<Base::LogStyle::Log>, _msg)

#define FOREACH_ITEM(_item, _obj) \
    auto _it = ObjectMap.end(); \
    if (_obj.getObject() && _obj.getObject()->isAttachedToDocument()) \
        _it = ObjectMap.find(_obj.getObject()); \
    if (_it != ObjectMap.end()) { \
        for (auto _item : _it->second->items) {

#define FOREACH_ITEM_ALL(_item) \
    for (const auto& _v : ObjectMap) { \
        for (auto _item : _v.second->items) {

#define END_FOREACH_ITEM \
    } \
    }


/// State the tree's source files share; private to the tree, not for use elsewhere.
namespace Gui::TreeShared
{
extern bool _DraggingActive;
extern bool _DragEventFilter;
extern bool _DisableCheckTopParent;
extern QBrush _TreeItemBackground;
extern TreeWidget* _LastSelectedTreeWidget;

inline bool isVisibilityIconEnabled()
{
    return TreeParams::getVisibilityIcon();
}

inline bool isOnlyNameColumnDisplayed()
{
    return TreeParams::getHideInternalNames() && TreeParams::getHideColumn();
}

inline bool isSelectionCheckBoxesEnabled()
{
    return TreeParams::getCheckBoxesSelection();
}
}  // namespace Gui::TreeShared

namespace Gui
{
/// Which rows of a document were expanded when it was saved, read back from its file.
class DocumentItem::ExpandInfo: public std::unordered_map<std::string, DocumentItem::ExpandInfoPtr>
{
public:
    void restore(Base::XMLReader& reader)
    {
        int level = reader.level();
        int count = reader.getAttribute<long>("count");
        for (int i = 0; i < count; ++i) {
            reader.readElement("Expand");
            auto& entry = (*this)[reader.getAttribute<const char*>("name")];
            if (!reader.hasAttribute("count")) {
                continue;
            }
            entry.reset(new ExpandInfo);
            entry->restore(reader);
        }
        reader.readEndElement("Expand", level - 1);
    }
};
}  // namespace Gui

namespace Gui
{
using DocumentObjectItems = std::set<DocumentObjectItem*>;
}  // namespace Gui

class Gui::DocumentObjectData
{
public:
    bool dirtyFlag {};
    DocumentItem* docItem;
    DocumentObjectItems items;
    ViewProviderDocumentObject* viewObject;
    DocumentObjectItem* rootItem {nullptr};
    std::vector<App::DocumentObject*> children;
    std::set<App::DocumentObject*> childSet;
    bool removeChildrenFromRoot;
    bool itemHidden;
    std::string label;
    std::string label2;
    std::string internalName;

    using Connection = fastsignals::scoped_connection;

    Connection connectIcon;
    Connection connectTool;
    Connection connectStat;
    Connection connectHl;

    DocumentObjectData(DocumentItem* docItem, ViewProviderDocumentObject* vpd)
        : docItem(docItem)
        , viewObject(vpd)
    {
        // NOLINTBEGIN
        //  Setup connections
        connectIcon = viewObject->signalChangeIcon.connect(
            std::bind(&DocumentObjectData::slotChangeIcon, this)
        );
        connectTool = viewObject->signalChangeToolTip.connect(
            std::bind(&DocumentObjectData::slotChangeToolTip, this, std::placeholders::_1)
        );
        connectStat = viewObject->signalChangeStatusTip.connect(
            std::bind(&DocumentObjectData::slotChangeStatusTip, this, std::placeholders::_1)
        );
        connectHl = viewObject->signalChangeHighlight.connect(
            std::bind(
                &DocumentObjectData::slotChangeHighlight,
                this,
                std::placeholders::_1,
                std::placeholders::_2
            )
        );
        // NOLINTEND

        removeChildrenFromRoot = viewObject->canRemoveChildrenFromRoot();
        itemHidden = !viewObject->showInTree();
        label = viewObject->getObject()->Label.getValue();
        label2 = viewObject->getObject()->Label2.getValue();
        internalName = viewObject->getObject()->getNameInDocument();
    }

    void insertItem(DocumentObjectItem* item)
    {
        items.insert(item);
        if (const QString tip = viewObject->getToolTip(); !tip.isEmpty()) {
            item->setToolTip(0, tip);
        }
        dirtyFlag = true;
    }

    void removeItem(DocumentObjectItem* item)
    {
        auto it = items.find(item);
        if (it == items.end()) {
            assert(0);
        }
        else {
            items.erase(it);
            dirtyFlag = true;
        }
    }

    const char* getTreeName() const
    {
        return docItem->getTreeName();
    }

    void updateChildren(DocumentObjectDataPtr other)
    {
        children = other->children;
        childSet = other->childSet;
    }

    bool updateChildren(bool checkVisibility)
    {
        auto newChildren = viewObject->claimChildren();
        auto obj = viewObject->getObject();
        std::set<App::DocumentObject*> newSet;
        bool updated = false;
        for (auto child : newChildren) {
            auto childVp = docItem->getViewProvider(child);
            if (!childVp) {
                continue;
            }
            if (child && child->isAttachedToDocument()) {
                if (!newSet.insert(child).second) {
                    TREE_WARN(
                        "duplicate child item " << obj->getFullName() << '.'
                                                << child->getNameInDocument()
                    );
                }
                else if (!childSet.erase(child)) {
                    // this means new child detected
                    updated = true;
                    if (child->getDocument() == obj->getDocument()
                        && child->getDocument() == docItem->document()->getDocument()) {
                        auto& parents = docItem->_ParentMap[child];
                        if (parents.insert(obj).second && child->Visibility.getValue()) {
                            bool showable = false;
                            for (auto parent : parents) {
                                if (!parent->hasChildElement()
                                    && parent->getLinkedObject(false) == parent) {
                                    showable = true;
                                    break;
                                }
                            }
                            childVp->setShowable(showable);
                        }
                    }
                }
            }
        }
        for (auto child : childSet) {
            if (newSet.find(child) == newSet.end()) {
                // this means old child removed
                updated = true;
                auto mapIt = docItem->_ParentMap.find(child);

                // If 'child' is not part of the map then it has already been deleted
                // in _slotDeleteObject.
                if (mapIt != docItem->_ParentMap.end()) {
                    docItem->_ParentMap[child].erase(obj);

                    auto childVp = docItem->getViewProvider(child);
                    if (childVp && child->getDocument() == obj->getDocument()) {
                        childVp->setShowable(docItem->isObjectShowable(child));
                    }
                }
            }
        }
        // We still need to check the order of the children
        updated = updated || children != newChildren;
        children.swap(newChildren);
        childSet.swap(newSet);

        if (updated && checkVisibility) {
            for (auto child : children) {
                auto childVp = docItem->getViewProvider(child);
                if (childVp && child->getDocument() == obj->getDocument()) {
                    childVp->setShowable(docItem->isObjectShowable(child));
                }
            }
        }
        return updated;
    }

    void testStatus(bool resetStatus = false)
    {
        QIcon icon, icon2;
        for (auto item : items) {
            item->testStatus(resetStatus, icon, icon2);
        }
    }

    void slotChangeIcon()
    {
        testStatus(true);
    }

    void slotChangeToolTip(const QString& tip)
    {
        for (auto item : items) {
            item->setToolTip(0, tip);
        }
    }

    void slotChangeStatusTip(const QString& tip)
    {
        for (auto item : items) {
            item->setStatusTip(0, tip);
        }
    }

    void slotChangeHighlight(bool set, Gui::HighlightMode mode)
    {
        for (auto item : items) {
            item->setHighlight(set, mode);
        }
    }
};

namespace Gui
{
/**
 * TreeWidget item delegate for editing
 */
class TreeWidgetItemDelegate: public QStyledItemDelegate
{
    typedef QStyledItemDelegate inherited;

    // Beware, big scary hack incoming!
    //
    // This is artificial QTreeWidget that is not rendered and its sole goal is to be the source
    // of style information that can be manipulated using QSS. From Qt6.5 tree branches also
    // have rendered background using ::item sub-control. Whole row also gets background from
    // the same sub-control. Only way to prevent this is to disable background of ::item,
    // this however limits our ability to style tree items. As solution we create this widget
    // that will be for painter to read information and draw proper backgrounds only when asked.
    //
    // More information: https://github.com/FreeCAD/FreeCAD/pull/13807
    QTreeView* artificial;

    QRect calculateItemRect(const QStyleOptionViewItem& option) const;

public:
    explicit TreeWidgetItemDelegate(QObject* parent = nullptr);

    virtual QWidget* createEditor(
        QWidget* parent,
        const QStyleOptionViewItem&,
        const QModelIndex& index
    ) const;

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    virtual void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const;

    virtual void paint(
        QPainter* painter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index
    ) const;
};

}  // namespace Gui
