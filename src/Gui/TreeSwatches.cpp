// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QTreeWidgetItem>

#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "Selection.h"
#include "Tree.h"
#include "TreeSwatches.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;
using namespace Gui::TreeSwatches;

namespace Gui::TreeSwatches
{

QPixmap paintBodySwatches(const Gui::ViewProvider::TreeBodyColumn& column, std::vector<SwatchHit>* hits)
{
    if (column.builds.empty()) {
        return {};
    }
    constexpr int side = 10;
    constexpr int gap = 2;
    constexpr int ellipsis = 8;
    constexpr int maxShown = 3;
    QFont font;
    font.setPixelSize(side + 1);
    const QFontMetrics metrics(font);
    auto truncated = [](const Gui::ViewProvider::BodySwatch& swatch) {
        return swatch.colors.size() > static_cast<std::size_t>(maxShown);
    };
    auto countText = [](const Gui::ViewProvider::BodySwatch& swatch) {
        return QString::number(swatch.colors.size());
    };
    auto swatchWidth = [&](const Gui::ViewProvider::BodySwatch& swatch) {
        const int count = std::min<int>(static_cast<int>(swatch.colors.size()), maxShown);
        int width = count * side + std::max(count - 1, 0) * gap;
        if (truncated(swatch)) {
            width += 2 * gap + ellipsis + metrics.horizontalAdvance(countText(swatch));
        }
        return width;
    };
    auto groupWidth = [&](const std::vector<Gui::ViewProvider::BodySwatch>& swatches) {
        const int count = std::min<int>(static_cast<int>(swatches.size()), maxShown);
        int width = std::max(count - 1, 0) * gap;
        for (int i = 0; i < count; ++i) {
            width += swatchWidth(swatches[i]);
        }
        if (static_cast<int>(swatches.size()) > maxShown) {
            width += gap + ellipsis;
        }
        return width;
    };
    const int width = groupWidth(column.builds);

    QPixmap pixmap(width, side);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setFont(font);
    const QColor ink(128, 128, 128);  // readable on light and dark backgrounds
    int x = 0;
    auto drawSquare = [&](int left, const QColor& color) {
        painter.fillRect(left, 0, side, side, color);
        painter.setPen(color.darker(150));
        painter.drawRect(left, 0, side - 1, side - 1);
    };
    auto drawEllipsis = [&]() {
        for (int dot = 0; dot < 3; ++dot) {
            painter.fillRect(x + dot * 3, side - 2, 2, 2, ink);
        }
        x += ellipsis;
    };
    auto drawSwatch = [&](const Gui::ViewProvider::BodySwatch& swatch) {
        const std::size_t count = std::min<std::size_t>(swatch.colors.size(), maxShown);
        for (std::size_t i = 0; i < count; ++i) {
            drawSquare(x, swatch.colors[i]);
            if (hits && i < swatch.bodies.size()) {
                hits->push_back({QRect(x, 0, side, side), {swatch.bodies[i]}});
            }
            x += side + (i + 1 < count ? gap : 0);
        }
        if (!truncated(swatch)) {
            return;
        }
        const int tailStart = x;
        x += gap;
        drawEllipsis();
        x += gap;
        const QString text = countText(swatch);
        const int textWidth = metrics.horizontalAdvance(text);
        painter.setPen(ink);
        painter.drawText(QRect(x, 0, textWidth, side), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += textWidth;
        if (hits) {
            hits->push_back({QRect(tailStart, 0, x - tailStart, side), swatch.bodies});
        }
    };
    auto drawGroup = [&](const std::vector<Gui::ViewProvider::BodySwatch>& swatches) {
        const int count = std::min<int>(static_cast<int>(swatches.size()), maxShown);
        for (int i = 0; i < count; ++i) {
            drawSwatch(swatches[i]);
            x += i + 1 < count ? gap : 0;
        }
        if (static_cast<int>(swatches.size()) > maxShown) {
            x += gap;
            drawEllipsis();
        }
    };
    drawGroup(column.builds);
    painter.end();
    return pixmap;
}

QIcon bodySwatchIcon(const Gui::ViewProvider::TreeBodyColumn& column)
{
    QPixmap pixmap = paintBodySwatches(column);
    if (pixmap.isNull()) {
        return {};
    }
    // A hidden object's row is drawn greyed out; its swatch must keep its colour, or the column
    // stops matching the body it names.
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Disabled);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}

void applyBodyColumn(QTreeWidgetItem* item, const Gui::ViewProviderDocumentObject& vp)
{
    auto column = vp.getTreeBodyColumn();
    if (item->toolTip(BodyColumn) != column.tooltip) {
        item->setToolTip(BodyColumn, column.tooltip);
    }
    item->setIcon(BodyColumn, bodySwatchIcon(column));
}

}  // namespace Gui::TreeSwatches

std::vector<App::DocumentObject*> TreeWidget::bodiesAtSwatch(const QPoint& pos) const
{
    QModelIndex index = indexAt(pos);
    if (!index.isValid() || index.column() != BodyColumn) {
        return {};
    }
    auto item = itemFromIndex(index);
    if (!item || item->type() != ObjectType) {
        return {};
    }
    auto objItem = static_cast<DocumentObjectItem*>(item);
    if (!objItem->object() || !objItem->object()->getObject()) {
        return {};
    }
    std::vector<SwatchHit> hits;
    QPixmap pixmap = paintBodySwatches(objItem->object()->getTreeBodyColumn(), &hits);
    if (pixmap.isNull()) {
        return {};
    }

    // Find where the delegate drew the swatches, then map the click back onto the pixmap.
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.rect = visualRect(index);
    option.index = index;
    option.features |= QStyleOptionViewItem::HasDecoration;
    option.icon = item->icon(BodyColumn);
    option.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    option.decorationSize = QSize(pixmap.width() * iconSize() / pixmap.height(), iconSize());
    QRect decoration = style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, this);
    QSize drawn = option.icon.actualSize(decoration.size());
    QRect drawnRect
        = QStyle::alignedRect(layoutDirection(), option.decorationAlignment, drawn, decoration);
    if (!drawnRect.contains(pos) || drawn.isEmpty()) {
        return {};
    }
    QPoint onPixmap(
        (pos.x() - drawnRect.left()) * pixmap.width() / drawn.width(),
        (pos.y() - drawnRect.top()) * pixmap.height() / drawn.height()
    );
    for (const auto& hit : hits) {
        if (hit.rect.contains(onPixmap)) {
            return hit.bodies;
        }
    }
    return {};
}

std::vector<App::DocumentObject*> Gui::stepsUsingBody(App::DocumentObject* body)
{
    std::vector<App::DocumentObject*> steps;
    auto* guiDoc = Application::Instance->getDocument(body->getDocument());
    if (!guiDoc) {
        return steps;
    }
    for (auto* obj : body->getDocument()->getObjects()) {
        auto* vp = guiDoc->getViewProvider(obj);
        if (!vp) {
            continue;
        }
        for (const auto& swatch : vp->getTreeBodyColumn().references) {
            if (std::ranges::find(swatch.bodies, body) != swatch.bodies.end()) {
                steps.push_back(obj);
                break;
            }
        }
    }
    return steps;
}

void TreeWidget::showSwatchMenu(const std::vector<App::DocumentObject*>& bodies, const QPoint& at)
{
    QMenu menu;
    App::DocumentObject* single = bodies.size() == 1 ? bodies.front() : nullptr;
    if (single) {
        menu.addAction(tr("Rename..."), this, [this, single]() {
            bool ok = false;
            QString label = QInputDialog::getText(
                this,
                tr("Rename body"),
                tr("Name:"),
                QLineEdit::Normal,
                QString::fromUtf8(single->Label.getValue()),
                &ok
            );
            label = label.trimmed();
            if (!ok || label.isEmpty() || label == QString::fromUtf8(single->Label.getValue())) {
                return;
            }
            App::AutoTransaction committer(
                single->getDocument(),
                QT_TRANSLATE_NOOP("Command", "Rename")
            );
            single->Label.setValue(label.toUtf8().constData());
        });
    }

    const bool anyShown = std::ranges::any_of(bodies, [](App::DocumentObject* body) {
        return body->Visibility.getValue();
    });
    menu.addAction(anyShown ? tr("Hide") : tr("Show"), this, [anyShown]() {
        Application::Instance->commandManager().runCommandByName(
            anyShown ? "Std_HideSelection" : "Std_ShowSelection"
        );
    });

    std::vector<App::DocumentObject*> users;
    for (auto* body : bodies) {
        for (auto* step : stepsUsingBody(body)) {
            if (std::ranges::find(users, step) == users.end()) {
                users.push_back(step);
            }
        }
    }
    if (!users.empty()) {
        menu.addSeparator();
        QMenu* usedBy = menu.addMenu(tr("Used by"));
        for (auto* step : users) {
            QIcon icon;
            if (auto* vp = Application::Instance->getViewProvider(step)) {
                icon = vp->getIcon();
            }
            std::string docName = step->getDocument()->getName();
            std::string objName = step->getNameInDocument();
            usedBy->addAction(icon, QString::fromUtf8(step->Label.getValue()), this, [docName, objName]() {
                Selection().clearSelection();
                Selection().addSelection(docName.c_str(), objName.c_str());
            });
        }
    }
    menu.exec(at);
}
