// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <functional>
#include <map>
#include <set>

#include <QColor>

#include "ViewProvider.h"

namespace App
{
class DocumentObject;
}

namespace Gui
{

/// Which rows the tree tints for a selection, and in which colour (Cruth #3, ARCHITECTURE §8.2).
struct GuiExport TreeLineage
{
    /// The bodies an object builds, as its row's Bodies column reports them.
    using ColumnOf = std::function<ViewProvider::TreeBodyColumn(App::DocumentObject*)>;

    std::map<App::DocumentObject*, QColor> tints;
    std::set<App::DocumentObject*> selected;

    /// The lineage of @p selected; a row with no body colour to go by takes @p highlight.
    static TreeLineage trace(
        std::set<App::DocumentObject*> selected,
        const ColumnOf& lookUpColumn,
        const QColor& highlight
    );
};

}  // namespace Gui
