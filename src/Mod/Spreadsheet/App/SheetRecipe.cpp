// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <string>
# include <vector>
#endif

#include "SheetRecipe.h"

#include <App/DocumentObject.h>
#include <App/Range.h>

#include "Cell.h"
#include "PropertySheet.h"
#include "Sheet.h"

using namespace Spreadsheet;

namespace
{

/// A cell's authored text -- the literal or the formula exactly as a person typed it. Asked for
/// in its persistent form, which is the form that survives a rename of whatever it references:
/// the same reason the generic emitter records an expression rather than the number it resolves
/// to. The computed result is never emitted; the sheet recomputes it.
void addContent(const Cell& cell, App::RecipeNode& node)
{
    std::string content;
    if (cell.getStringContent(content, /*persistent=*/true) && !content.empty()) {
        node.fields["content"] = content;
    }
}

/// The name other expressions in the document bind to. Recorded next to the content because it
/// is the closest a cell comes to a durable identity -- a formula elsewhere survives the cell
/// moving only if it went through the alias.
void addAlias(const Cell& cell, App::RecipeNode& node)
{
    std::string alias;
    if (cell.getAlias(alias) && !alias.empty()) {
        node.fields["alias"] = alias;
    }
}

/// Presentation is deliberately not emitted -- not the display unit, not the alignment, style,
/// colours or spans. A recipe records what a person designed, and how a value is shown is not
/// part of that: the architecture files "metric vs imperial annotations" as display-only,
/// alongside colour and visibility, and the generic emitter already leaves Label and Visibility
/// out for exactly this reason. They are out of scope by declaration, not missing -- the same
/// standing as a label, which is why nothing reports them as a gap.

}  // namespace

App::RecipeDetail Spreadsheet::sheetRecipeDetail(const App::DocumentObject& obj)
{
    App::RecipeDetail detail;

    const auto* sheet = dynamic_cast<const Sheet*>(&obj);
    if (sheet == nullptr) {
        return detail;
    }

    // The property the generic emitter walked as a link and drew nothing out of. Claimed here,
    // now that the view prints what is in it.
    detail.coveredProperties = {"cells"};

    std::vector<App::CellAddress> used;
    for (const std::string& address : sheet->getUsedCells()) {
        used.emplace_back(address);
    }

    // Reading order -- down the columns of each row, as a person reads a sheet -- rather than
    // whatever order the store happens to hold. Two sheets that differ in one cell must also
    // differ in one line, so the order has to come from the addresses and not from memory.
    std::sort(used.begin(), used.end(), [](App::CellAddress lhs, App::CellAddress rhs) {
        return lhs.row() != rhs.row() ? lhs.row() < rhs.row() : lhs.col() < rhs.col();
    });

    App::RecipeDetailSection cells;
    cells.name = "cells";
    for (const App::CellAddress address : used) {
        const Cell* cell = sheet->getCell(address);
        if (cell == nullptr) {
            continue;
        }

        App::RecipeNode node;
        // The address, not a durable tag: a cell has none. It is a position, and inserting a row
        // moves it -- stated plainly here rather than dressed up as an identity it does not have.
        node.id = address.toString();
        node.type = "cell";
        addContent(*cell, node);
        addAlias(*cell, node);

        // A cell that ended up holding nothing at all is not something anyone authored: the
        // store keeps addresses alive after a clear, and they are not facts about the design.
        if (node.fields.empty()) {
            continue;
        }
        cells.nodes.push_back(std::move(node));
    }

    if (!cells.nodes.empty()) {
        detail.sections.push_back(std::move(cells));
    }

    return detail;
}

void Spreadsheet::registerSheetRecipeDetail()
{
    App::registerRecipeDetail(Sheet::getClassTypeId(), &sheetRecipeDetail);
}
