// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <vector>

#include <QIcon>
#include <QPixmap>
#include <QRect>

#include "ViewProvider.h"

class QTreeWidgetItem;

namespace Gui
{
class ViewProviderDocumentObject;
}

namespace Gui::TreeSwatches
{

constexpr int BodyColumn = 3;

/// The Bodies column (Cruth ARCHITECTURE §8.7): a swatch per body the object builds. The bodies
/// it only references are not drawn. The bodies one pattern emits sit side by side when
/// there are three or fewer; past that, the first three are followed by an ellipsis and the
/// count, so "12" reads as twelve different bodies (§5.5). At most three groups of each
/// are drawn, with an ellipsis when there are more; the tooltip names them all.
/// Where each square lands, and the bodies a click there means.
struct GuiExport SwatchHit
{
    QRect rect;
    std::vector<App::DocumentObject*> bodies;
};

GuiExport QPixmap paintBodySwatches(
    const Gui::ViewProvider::TreeBodyColumn& column,
    std::vector<SwatchHit>* hits = nullptr
);

GuiExport QIcon bodySwatchIcon(const Gui::ViewProvider::TreeBodyColumn& column);

GuiExport void applyBodyColumn(QTreeWidgetItem* item, const Gui::ViewProviderDocumentObject& vp);

}  // namespace Gui::TreeSwatches
