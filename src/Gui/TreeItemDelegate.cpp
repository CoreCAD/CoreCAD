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

// How a tree row is drawn and edited.

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

TreeWidgetItemDelegate::TreeWidgetItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
    artificial = new QTreeView(qobject_cast<QWidget*>(parent));
    artificial->setObjectName(QStringLiteral("DocumentTreeItems"));
    artificial->setFixedSize(0, 0);  // ensure that it does not render
}

QRect TreeWidgetItemDelegate::calculateItemRect(const QStyleOptionViewItem& option) const
{
    auto tree = static_cast<TreeWidget*>(parent());
    auto style = tree->style();

    QRect rect = option.rect;

    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, artificial) + 1;

    // 2 margin for text, 2 margin for decoration (icon) = 4 times margin
    int width = 4 * margin + option.fontMetrics.boundingRect(option.text).width()
        + option.decorationSize.width() + TreeParams::getItemBackgroundPadding();

    if (TreeParams::getCheckBoxesSelection()) {
        // another 2 margin for checkbox
        width += 2 * margin + style->pixelMetric(QStyle::PM_IndicatorWidth)
            + style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
    }

    if (width < rect.width()) {
        rect.setWidth(width);
    }

    return rect;
}

void TreeWidgetItemDelegate::paint(
    QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index
) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    auto tree = static_cast<TreeWidget*>(parent());
    auto style = tree->style();

    // If only the first column is shown, we'll trim the color background when
    // rendering as transparent overlay.
    bool trimColumnSize = isOnlyNameColumnDisplayed();

    if (index.column() == 0) {
        if (tree->testAttribute(Qt::WA_NoSystemBackground)
            && (trimColumnSize
                || (opt.backgroundBrush.style() == Qt::NoBrush
                    && _TreeItemBackground.style() != Qt::NoBrush))) {
            QRect rect = calculateItemRect(option);

            if (trimColumnSize && opt.backgroundBrush.style() == Qt::NoBrush) {
                painter->fillRect(rect, _TreeItemBackground);
            }
            else if (!opt.state.testFlag(QStyle::State_Selected)) {
                painter->fillRect(rect, _TreeItemBackground);
            }
        }
    }
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, artificial);
}

void TreeWidgetItemDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    inherited::initStyleOption(option, index);

    auto tree = static_cast<TreeWidget*>(parent());
    auto item = tree->itemFromIndex(index);

    if (!item) {
        return;
    }

    // Clear State_Enabled for hidden objects so QSS ::item:disabled rules can
    // override the overlay stylesheet's blanket ::item { color } for text fading.
    if (item->type() == TreeWidget::ObjectType) {
        if (auto* docItem = static_cast<DocumentObjectItem*>(item);
            docItem->object() && !docItem->object()->isShow()) {
            option->state &= ~QStyle::State_Enabled;
        }
        // Cruth #3: a selected step is shown by the tree's row wash (TreeWidget::drawRow), in
        // the same family as the rows it came from, not by the style's selection highlight.
        option->state &= ~QStyle::State_Selected;
    }

    // The Bodies column has no text, so Qt would centre its swatches; rows of different widths
    // then fail to line up. Start them all at the left edge.
    if (index.column() == BodyColumn) {
        option->decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    }

    option->textElideMode = Qt::ElideMiddle;
    auto mousePos = option->widget->mapFromGlobal(QCursor::pos());
    auto isHovered = option->rect.contains(mousePos);
    if (!isHovered) {
        option->state &= ~QStyle::State_MouseOver;
    }

    QSize size = option->icon.actualSize(QSize(0xffff, 0xffff));

    if (size.height() > 0) {
        option->decorationSize
            = QSize(size.width() * TreeWidget::iconSize() / size.height(), TreeWidget::iconSize());
    }

    if (isOnlyNameColumnDisplayed()) {
        option->rect = calculateItemRect(*option);

        // we need to extend this shape a bit, 3px on each side
        // this value was obtained experimentally
        option->rect.setWidth(option->rect.width() + 3 * 2);
    }
}

class DynamicQLineEdit: public ExpLineEdit
{
public:
    DynamicQLineEdit(QWidget* parent = nullptr)
        : ExpLineEdit(parent)
    {}

    QSize sizeHint() const override
    {
        QSize size = QLineEdit::sizeHint();
        QFontMetrics fm(font());
        int availableWidth = parentWidget()->width() - geometry().x();  // Calculate available width
        int margin = 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1)
            + 2 * style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing)
            + TreeParams::getItemBackgroundPadding();
        size.setWidth(std::min(fm.horizontalAdvance(text()) + margin, availableWidth));
        return size;
    }

    // resize on key presses
    void keyPressEvent(QKeyEvent* event) override
    {
        ExpLineEdit::keyPressEvent(event);
        setMinimumWidth(sizeHint().width());
    }
};

QWidget* TreeWidgetItemDelegate::createEditor(
    QWidget* parent,
    const QStyleOptionViewItem&,
    const QModelIndex& index
) const
{
    auto ti = static_cast<QTreeWidgetItem*>(index.internalPointer());
    if (ti->type() != TreeWidget::ObjectType || index.column() > 1) {
        return nullptr;
    }
    auto item = static_cast<DocumentObjectItem*>(ti);
    App::DocumentObject* obj = item->object()->getObject();
    auto& prop = index.column() ? obj->Label2 : obj->Label;

    DynamicQLineEdit* editor;
    if (TreeParams::getLabelExpression()) {
        DynamicQLineEdit* le = new DynamicQLineEdit(parent);
        le->setAutoApply(true);
        le->setFrame(false);
        le->bind(App::ObjectIdentifier(prop));
        editor = le;
    }
    else {
        editor = new DynamicQLineEdit(parent);
    }
    editor->setReadOnly(prop.isReadOnly());
    return editor;
}

QSize TreeWidgetItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    int spacing = std::max(0, static_cast<int>(TreeParams::getItemSpacing()));
    size.setHeight(size.height() + spacing);
    return size;
}
