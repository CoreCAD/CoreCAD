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

// One object's row in the tree.

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

enum GroupType
{
    NotGroup = 0,
    LinkGroup = 1,
    PartGroup = 2,
    SuperGroup = 3,  // reversed for future
};

static int countItems;

DocumentObjectItem::DocumentObjectItem(DocumentItem* ownerDocItem, DocumentObjectDataPtr data)
    : QTreeWidgetItem(TreeWidget::ObjectType)
    , myOwner(ownerDocItem)
    , myData(data)
    , previousStatus(-1)
    , selected(0)
    , populated(false)
{
    setFlags(flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    setCheckState(false);

    myData->insertItem(this);
    ++countItems;
    TREE_LOG("Create item: " << countItems << ", " << object()->getObject()->getFullName());
}

DocumentObjectItem::~DocumentObjectItem()
{
    --countItems;
    TREE_LOG("Delete item: " << countItems << ", " << object()->getObject()->getFullName());
    myData->removeItem(this);

    if (myData->rootItem == this) {
        myData->rootItem = nullptr;
    }

    if (myOwner && myData->items.empty()) {
        auto it = myOwner->_ParentMap.find(object()->getObject());
        if (it != myOwner->_ParentMap.end() && !it->second.empty()) {
            myOwner->PopulateObjects.push_back(*it->second.begin());
            myOwner->getTree()->_updateStatus();
        }
    }
}

void DocumentObjectItem::restoreBackground()
{
    this->setBackground(0, this->bgBrush);
}

void DocumentObjectItem::setHighlight(bool set, Gui::HighlightMode high)
{
    QFont f = this->font(0);
    auto highlight = [this, set](const QColor& col) {
        if (set) {
            this->setBackground(0, col);
        }
        else {
            this->setBackground(0, QBrush());
        }
        this->bgBrush = this->background(0);
    };

    switch (high) {
        case HighlightMode::Bold:
            f.setBold(set);
            break;
        case HighlightMode::Italic:
            f.setItalic(set);
            break;
        case HighlightMode::Underlined:
            f.setUnderline(set);
            break;
        case HighlightMode::Overlined:
            f.setOverline(set);
            break;
        case HighlightMode::StrikeOut:
            f.setStrikeOut(set);
            break;
        case HighlightMode::Blue:
            highlight(QColor(200, 200, 255));
            break;
        case HighlightMode::LightBlue:
            highlight(QColor(230, 230, 255));
            break;
        case HighlightMode::UserDefined: {
            QColor color(230, 230, 255);
            if (set) {
                ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/TreeView"
                );
                bool bold = hGrp->GetBool("TreeActiveBold", true);
                bool italic = hGrp->GetBool("TreeActiveItalic", false);
                bool underlined = hGrp->GetBool("TreeActiveUnderlined", false);
                bool overlined = hGrp->GetBool("TreeActiveOverlined", false);
                f.setBold(bold);
                f.setItalic(italic);
                f.setUnderline(underlined);
                f.setOverline(overlined);

                unsigned long col = hGrp->GetUnsigned("TreeActiveColor", 1538528255);
                color = Base::Color::fromPackedRGB<QColor>(col);
            }
            else {
                f.setBold(false);
                f.setItalic(false);
                f.setUnderline(false);
                f.setOverline(false);
            }
            highlight(color);
        } break;
        default:
            break;
    }
    this->setFont(0, f);
}

const char* DocumentObjectItem::getTreeName() const
{
    return myData->getTreeName();
}

Gui::ViewProviderDocumentObject* DocumentObjectItem::object() const
{
    return myData->viewObject;
}

void DocumentObjectItem::testStatus(bool resetStatus)
{
    QIcon icon, icon2;
    testStatus(resetStatus, icon, icon2);
    // check if the object is suppressed and apply strikethrough
    auto docObj = object()->getObject();
    bool suppressed = false;
    if (docObj && docObj->hasExtension(App::SuppressibleExtension::getExtensionClassTypeId())) {
        suppressed = docObj->getExtensionByType<App::SuppressibleExtension>()->Suppressed.getValue();
    }
    QFont f = font(0);
    if (f.strikeOut() != suppressed) {
        f.setStrikeOut(suppressed);
        setFont(0, f);
    }
}

namespace
{
enum Status
{
    Visible = 1 << 0,
    Recompute = 1 << 1,
    Error = 1 << 2,
    Hidden = 1 << 3,
    External = 1 << 4
};
}  // namespace

void DocumentObjectItem::testStatus(bool resetStatus, QIcon& icon1, QIcon& icon2)
{
    // guard against calling this during destruction when tree widget may be nullptr
    if (!treeWidget()) {
        return;
    }

    App::DocumentObject* pObject = object()->getObject();

    int visible = -1;
    auto parentItem = getParentItem();
    if (parentItem) {
        auto parent = parentItem->object()->getObject();
        auto ext = parent->getExtensionByType<App::GroupExtension>(true, false);
        if (!ext) {
            visible = parent->isElementVisible(pObject->getNameInDocument());
        }
        else {
            // We are dealing with a plain group. It has special handling when
            // linked, which allows it to have indpenedent visibility control.
            // We need to go up the hierarchy and see if there is any link to
            // it.
            for (auto pp = parentItem->getParentItem(); pp; pp = pp->getParentItem()) {
                auto obj = pp->object()->getObject();
                if (!obj->hasExtension(App::GroupExtension::getExtensionClassTypeId(), false)) {
                    visible = pp->object()->getObject()->isElementVisible(pObject->getNameInDocument());
                    break;
                }
            }
        }
    }

    if (visible < 0) {
        visible = object()->isShow() ? 1 : 0;
    }

    auto obj = object()->getObject();
    auto linked = obj->getLinkedObject(false);
    bool external = object()->getDocument() != getOwnerDocument()->document()
        || (linked && linked->getDocument() != obj->getDocument());
    int currentStatus = ((external ? 1 : 0) << 4) | ((object()->showInTree() ? 0 : 1) << 3)
        | ((pObject->isError() ? 1 : 0) << 2)
        | ((pObject->isTouched() || pObject->mustExecute() == 1 ? 1 : 0) << 1) | (visible ? 1 : 0);


    if (!resetStatus && previousStatus == currentStatus) {
        return;
    }

    previousStatus = currentStatus;

    QIcon::Mode mode = QIcon::Normal;
    if (currentStatus & Status::Visible) {
        // Note: By default the foreground, i.e. text color is invalid
        // to make use of the default color of the tree widget's palette.
        // If we temporarily set this color to dark and reset to an invalid
        // color again we cannot do it with setTextColor() or setForeground(),
        // respectively, because for any reason the color would always switch
        // to black which will lead to unreadable text if the system background
        // hss already a dark color.
        // However, it works if we set the appropriate role to an empty QVariant().
        for (int column = 0; column < this->columnCount(); ++column) {
            this->setData(column, Qt::ForegroundRole, QVariant());
        }
    }
    else {  // invisible
        QStyleOptionViewItem opt;
        // it can happen that a tree item is not attached to the tree widget (#0003025)
        if (this->treeWidget()) {
            opt.initFrom(this->treeWidget());
        }
        for (int column = 0; column < this->columnCount(); ++column) {
            this->setForeground(column, opt.palette.color(QPalette::Disabled, QPalette::Text));
        }
        mode = QIcon::Disabled;
    }

    QIcon& icon = mode == QIcon::Normal ? icon1 : icon2;

    if (icon.isNull()) {
        QPixmap px;
        if (currentStatus & Status::Error) {
            static QPixmap pxError;
            if (pxError.isNull()) {
                // object is in error state
                pxError = Gui::BitmapFactory().pixmapFromSvg("overlay_error", QSize(10, 10));
            }
            px = pxError;
        }
        else if (currentStatus & Status::Recompute) {
            static QPixmap pxRecompute;
            if (pxRecompute.isNull()) {
                // object must be recomputed
                pxRecompute = Gui::BitmapFactory().pixmapFromSvg("overlay_recompute", QSize(10, 10));
            }
            px = pxRecompute;
        }

        // get the original icon set
        QIcon icon_org = object()->getIcon();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        int w = getTree()->viewOptions().decorationSize.width();
#else
        QStyleOptionViewItem opt;
        getTree()->initViewItemOption(&opt);
        int w = opt.decorationSize.width();
#endif

        QPixmap pxOn, pxOff;

        // if needed show small pixmap inside
        if (!px.isNull()) {
            pxOff = BitmapFactory().merge(
                icon_org.pixmap(w, w, mode, QIcon::Off),
                px,
                BitmapFactoryInst::TopRight
            );
            pxOn = BitmapFactory().merge(
                icon_org.pixmap(w, w, mode, QIcon::On),
                px,
                BitmapFactoryInst::TopRight
            );
        }
        else {
            pxOff = icon_org.pixmap(w, w, mode, QIcon::Off);
            pxOn = icon_org.pixmap(w, w, mode, QIcon::On);
        }

        if (currentStatus & Status::Hidden) {
            static QPixmap pxHidden;
            if (pxHidden.isNull()) {
                pxHidden = Gui::BitmapFactory().pixmapFromSvg("TreeItemVisible", QSize(10, 10));
            }
            pxOff = BitmapFactory().merge(pxOff, pxHidden, BitmapFactoryInst::TopLeft);
            pxOn = BitmapFactory().merge(pxOn, pxHidden, BitmapFactoryInst::TopLeft);
        }

        if (currentStatus & Status::External) {
            static QPixmap pxExternal;
            constexpr int px = 12;
            if (pxExternal.isNull()) {
                pxExternal = Gui::BitmapFactory().pixmapFromSvg("LinkOverlay", QSize(px, px));
            }
            pxOff = BitmapFactory().merge(pxOff, pxExternal, BitmapFactoryInst::BottomRight);
            pxOn = BitmapFactory().merge(pxOn, pxExternal, BitmapFactoryInst::BottomRight);
        }

        icon.addPixmap(pxOn, QIcon::Normal, QIcon::On);
        icon.addPixmap(pxOff, QIcon::Normal, QIcon::Off);

        icon = object()->mergeColorfulOverlayIcons(icon);

        if (isVisibilityIconEnabled()) {
            static QPixmap pxVisible, pxInvisible;
            if (pxVisible.isNull()) {
                pxVisible = BitmapFactory().pixmap("TreeItemVisible");
            }
            if (pxInvisible.isNull()) {
                pxInvisible = BitmapFactory().pixmap("TreeItemInvisible");
            }

            // Prepend the visibility pixmap to the final icon pixmaps and use these as the icon.
            QIcon new_icon;
            auto style = this->getTree()->style();
            int const spacing = style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
            for (auto state : {QIcon::On, QIcon::Off}) {
                QPixmap px_org = icon.pixmap(0xFFFF, 0xFFFF, QIcon::Normal, state);

                QPixmap px(2 * px_org.width() + spacing, px_org.height());
                px.fill(Qt::transparent);

                QPainter pt;
                pt.begin(&px);
                pt.setPen(Qt::NoPen);
                if (object()->canToggleVisibility()) {
                    pt.drawPixmap(
                        0,
                        0,
                        px_org.width(),
                        px_org.height(),
                        (currentStatus & Status::Visible) ? pxVisible : pxInvisible
                    );
                }
                pt.drawPixmap(px_org.width() + spacing, 0, px_org.width(), px_org.height(), px_org);
                pt.end();

                new_icon.addPixmap(px, QIcon::Normal, state);
            }
            icon = new_icon;
        }
    }

    this->setIcon(0, icon);
}

void DocumentObjectItem::displayStatusInfo()
{
    App::DocumentObject* Obj = object()->getObject();

    QString info = QApplication::translate(Obj->getTypeId().getName(), Obj->getStatusString());

    if (Obj->mustExecute() == 1 && !Obj->isError()) {
        info += TreeWidget::tr(" (but must be executed)");
    }

    QString status = TreeWidget::tr("%1, Internal name: %2")
                         .arg(info, QString::fromLatin1(Obj->getNameInDocument()));

    if (!Obj->isError()) {
        getMainWindow()->showMessage(status);
    }
    else {
        getMainWindow()->showStatus(MainWindow::Err, status);
        QTreeWidget* tree = this->treeWidget();
        QPoint pos = tree->visualItemRect(this).topRight();
        QToolTip::showText(tree->mapToGlobal(pos), info);
    }
}

void DocumentObjectItem::setExpandedStatus(bool on)
{
    if (getOwnerDocument()->document() == object()->getDocument()) {
        object()->getObject()->setStatus(App::Expand, on);
    }
}

void DocumentObjectItem::setData(int column, int role, const QVariant& value)
{
    QVariant myValue(value);
    if (role == Qt::EditRole && column <= 1) {
        auto obj = object()->getObject();
        auto& label = column ? obj->Label2 : obj->Label;

        std::ostringstream str;
        str << TreeWidget::tr("Rename").toStdString() << ' ' << getName() << '.' << label.getName();

        // Explicitly open and commit a transaction since this is a single change here
        // For more details: https://forum.freecad.org/viewtopic.php?f=3&t=72351
        App::Document* doc = obj->getDocument();
        doc->openTransaction(str.str().c_str());
        label.setValue(value.toString().toUtf8().constData());
        doc->commitTransaction();

        myValue = QString::fromUtf8(label.getValue());
    }
    QTreeWidgetItem::setData(column, role, myValue);
}

bool DocumentObjectItem::isChildOfItem(DocumentObjectItem* item)
{
    for (auto pitem = parent(); pitem; pitem = pitem->parent()) {
        if (pitem == item) {
            return true;
        }
    }
    return false;
}

bool DocumentObjectItem::requiredAtRoot(bool excludeSelf) const
{
    if (myData->rootItem || object()->getDocument() != getOwnerDocument()->document()) {
        return false;
    }
    bool checkMap = true;
    for (auto item : myData->items) {
        if (excludeSelf && item == this) {
            continue;
        }
        auto pi = item->getParentItem();
        if (!pi || pi->myData->removeChildrenFromRoot) {
            return false;
        }
        checkMap = false;
    }
    if (checkMap && myOwner) {
        auto it = myOwner->_ParentMap.find(object()->getObject());
        if (it != myOwner->_ParentMap.end()) {
            // Reaching here means all items of this corresponding object is
            // going to be deleted, but the object itself is not deleted and
            // still being referred to by some parent item that is not expanded
            // yet. So, we force populate at least one item of the parent
            // object to make sure that there is at least one corresponding
            // item for each object.
            //
            // PS: practically speaking, it won't hurt much to delete all the
            // items, because the item will be auto created once the user
            // expand its parent item. It only causes minor problems, such as,
            // tree scroll to object command won't work properly.

            for (auto parent : it->second) {
                if (getOwnerDocument()->populateObject(parent)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool DocumentObjectItem::isLink() const
{
    auto obj = object()->getObject();
    auto linked = obj->getLinkedObject(false);
    return linked && obj != linked;
}

bool DocumentObjectItem::isLinkFinal() const
{
    auto obj = object()->getObject();
    auto linked = obj->getLinkedObject(false);
    return linked && linked == linked->getLinkedObject(true);
}

bool DocumentObjectItem::isParentLink() const
{
    auto pi = getParentItem();
    return pi && pi->isLink();
}

int DocumentObjectItem::isGroup() const
{
    auto obj = object()->getObject();
    auto linked = obj->getLinkedObject(true);
    if (linked && linked->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) {
        return PartGroup;
    }
    if (obj->hasChildElement()) {
        return LinkGroup;
    }
    if (obj->hasExtension(App::GroupExtension::getExtensionClassTypeId(), false)) {
        for (auto parent = getParentItem(); parent; parent = parent->getParentItem()) {
            auto pobj = parent->object()->getObject();
            if (pobj->hasExtension(App::GroupExtension::getExtensionClassTypeId(), false)) {
                continue;
            }
            if (pobj->isElementVisible(obj->getNameInDocument()) >= 0) {
                return LinkGroup;
            }
        }
    }
    return NotGroup;
}

int DocumentObjectItem::isParentGroup() const
{
    auto pi = getParentItem();
    return pi ? pi->isGroup() : 0;
}

DocumentObjectItem* DocumentObjectItem::getParentItem() const
{
    if (parent()->type() != TreeWidget::ObjectType) {
        return nullptr;
    }
    return static_cast<DocumentObjectItem*>(parent());
}

DocumentObjectItem* DocumentObjectItem::getNextSibling() const
{
    QTreeWidgetItem* parent = this->parent();
    if (parent) {
        int index = parent->indexOfChild(const_cast<DocumentObjectItem*>(this));
        if (index >= 0) {
            while (++index < parent->childCount()) {
                QTreeWidgetItem* sibling = parent->child(index);
                if (sibling->type() == TreeWidget::ObjectType) {
                    return static_cast<DocumentObjectItem*>(sibling);
                }
            }
        }
    }

    return nullptr;
}

DocumentObjectItem* DocumentObjectItem::getPreviousSibling() const
{
    QTreeWidgetItem* parent = this->parent();
    if (parent) {
        int index = parent->indexOfChild(const_cast<DocumentObjectItem*>(this));
        while (index > 0) {
            QTreeWidgetItem* sibling = parent->child(--index);
            if (sibling->type() == TreeWidget::ObjectType) {
                return static_cast<DocumentObjectItem*>(sibling);
            }
        }
    }

    return nullptr;
}

const char* DocumentObjectItem::getName() const
{
    const char* name = object()->getObject()->getNameInDocument();
    return name ? name : "";
}

int DocumentObjectItem::getSubName(std::ostringstream& str, App::DocumentObject*& topParent) const
{
    auto parent = getParentItem();
    if (!parent) {
        return NotGroup;
    }
    int ret = parent->getSubName(str, topParent);
    if (ret != SuperGroup) {
        int group = parent->isGroup();
        if (group == NotGroup) {
            if (ret != PartGroup) {
                // Handle this situation,
                //
                // LinkGroup
                //    |--PartExtrude
                //           |--Sketch
                //
                // This function traverse from top down, so, when seeing a
                // non-group object 'PartExtrude', its following children should
                // not be grouped, so must reset any previous parents here.
                topParent = nullptr;
                str.str("");  // reset the current subname
                return NotGroup;
            }
            group = PartGroup;
        }
        ret = group;
    }

    auto obj = parent->object()->getObject();
    if (!obj || !obj->isAttachedToDocument()) {
        topParent = nullptr;
        str.str("");
        return NotGroup;
    }
    if (!topParent) {
        topParent = obj;
    }
    else if (!obj->redirectSubName(str, topParent, object()->getObject())) {
        str << obj->getNameInDocument() << '.';
    }
    return ret;
}

App::DocumentObject* DocumentObjectItem::getFullSubName(
    std::ostringstream& str,
    DocumentObjectItem* parent
) const
{
    auto pi = getParentItem();
    if (this == parent || !pi || (!parent && !pi->isGroup())) {
        return object()->getObject();
    }

    auto ret = pi->getFullSubName(str, parent);
    str << getName() << '.';
    return ret;
}

App::DocumentObject* DocumentObjectItem::getRelativeParent(
    std::ostringstream& str,
    DocumentObjectItem* cousin,
    App::DocumentObject** topParent,
    std::string* topSubname
) const
{
    std::ostringstream str2;
    App::DocumentObject *top = nullptr, *top2 = nullptr;
    getSubName(str, top);
    if (topParent) {
        *topParent = top;
    }
    if (!top) {
        return nullptr;
    }
    if (topSubname) {
        *topSubname = str.str() + getName() + '.';
    }
    cousin->getSubName(str2, top2);
    if (top != top2) {
        str << getName() << '.';
        return top;
    }

    auto subname = str.str();
    auto subname2 = str2.str();
    const char* sub = subname.c_str();
    const char* sub2 = subname2.c_str();
    while (true) {
        const char* dot = strchr(sub, '.');
        if (!dot) {
            str.str("");
            return nullptr;
        }
        const char* dot2 = strchr(sub2, '.');
        if (!dot2 || dot - sub != dot2 - sub2 || strncmp(sub, sub2, dot - sub) != 0) {
            auto substr = subname.substr(0, dot - subname.c_str() + 1);
            auto ret = top->getSubObject(substr.c_str());
            if (!top) {
                FC_ERR("invalid subname " << top->getFullName() << '.' << substr);
                str.str("");
                return nullptr;
            }
            str.str("");
            str << dot + 1 << getName() << '.';
            return ret;
        }
        sub = dot + 1;
        sub2 = dot2 + 1;
    }
    str.str("");
    return nullptr;
}

void DocumentObjectItem::setCheckState(bool checked)
{
    if (isSelectionCheckBoxesEnabled()) {
        QTreeWidgetItem::setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
    else {
        setData(0, Qt::CheckStateRole, QVariant());
    }
}

DocumentItem* DocumentObjectItem::getParentDocument() const
{
    return getTree()->getDocumentItem(object()->getDocument());
}

DocumentItem* DocumentObjectItem::getOwnerDocument() const
{
    return myOwner;
}

TreeWidget* DocumentObjectItem::getTree() const
{
    return static_cast<TreeWidget*>(treeWidget());
}

void DocumentObjectItem::getExpandedSnapshot(std::vector<bool>& snapshot) const
{
    snapshot.push_back(isExpanded());

    for (int i = 0; i < childCount(); ++i) {
        static_cast<const DocumentObjectItem*>(child(i))->getExpandedSnapshot(snapshot);
    }
}

void DocumentObjectItem::applyExpandedSnapshot(
    const std::vector<bool>& snapshot,
    std::vector<bool>::const_iterator& from
)
{
    setExpanded(*from++);

    for (int i = 0; i < childCount(); ++i) {
        static_cast<DocumentObjectItem*>(child(i))->applyExpandedSnapshot(snapshot, from);
    }
}
