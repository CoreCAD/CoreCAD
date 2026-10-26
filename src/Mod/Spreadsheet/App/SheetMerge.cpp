// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   Cruth is free software: you can redistribute it and/or modify it       *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Cruth is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with Cruth. If not, see                                  *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/Property.h>
#include <App/Recipe.h>
#include <App/RecipeDetail.h>
#include <Base/Reader.h>

#include "Cell.h"
#include "PropertySheet.h"
#include "Sheet.h"
#include "SheetMerge.h"

using namespace Spreadsheet;

namespace
{

using Ids = std::map<std::string, int>;  // id -> index

struct CellText
{
    std::optional<std::string> content;
    std::string attributes;  // everything saved but address and content
    bool refersByAddress {false};

    bool operator==(const CellText& other) const
    {
        return content == other.content && attributes == other.attributes;
    }
};

struct Version
{
    Ids rows;
    Ids columns;
    std::map<std::pair<std::string, std::string>, CellText> cells;
};

Ids inverted(const std::map<int, std::string>& byIndex)
{
    Ids ids;
    for (const auto& [index, id] : byIndex) {
        ids[id] = index;
    }
    return ids;
}

bool refersByAddress(const Cell& cell, const PropertySheet& sheet)
{
    const App::Expression* expression = cell.getExpression();
    if (expression == nullptr) {
        return false;
    }
    for (const auto& entry : expression->getIdentifiers()) {
        const App::ObjectIdentifier& path = entry.first;
        if (path.getDocumentObject() == sheet.getContainer()
            && const_cast<PropertySheet&>(sheet).isValidCellAddressName(path.getPropertyName())) {
            return true;
        }
    }
    return false;
}

Version versionOf(const App::DocumentObject& obj)
{
    const auto& sheet = static_cast<const Sheet&>(obj);
    const PropertySheet& cells = *const_cast<Sheet&>(sheet).getCells();
    Version version {inverted(cells.rowIdMap()), inverted(cells.columnIdMap()), {}};
    for (const App::CellAddress& address : cells.getUsedCells()) {
        const Cell* cell = cells.getValue(address);
        CellText text;
        std::string content;
        if (cell->getStringContent(content, true)) {
            text.content = content;
        }
        std::ostringstream saved;
        cell->save(saved, "", true);
        const std::string line = saved.str();
        text.attributes = line.substr(6, line.size() - 6 - 2);  // inside "<Cell " ... "/>"
        text.refersByAddress = refersByAddress(*cell, cells);
        version.cells[{cells.rowId(address.row()), cells.columnId(address.col())}] = text;
    }
    return version;
}

bool moved(const Ids& ancestor, const Ids& side)
{
    for (const auto& [id, index] : ancestor) {
        const auto found = side.find(id);
        if (found != side.end() && found->second != index) {
            return true;
        }
    }
    return false;
}

/// Where each row (or column) lands in the merged sheet, and which id a row new on the placed
/// side takes when it is the same row as one new on the leading side. Empty on failure, with
/// `why` set.
struct Placement
{
    Ids merged;
    std::map<std::string, std::string> sameAs;
    std::string why;
};

Placement place(const Ids& ancestor, const Ids& leading, const Ids& placed, const char* noun)
{
    Placement result {leading, {}, {}};
    std::map<int, std::string> occupied;
    for (const auto& [id, index] : leading) {
        occupied[index] = id;
    }
    for (const auto& [id, index] : ancestor) {
        if (!leading.count(id) && !moved(ancestor, leading)) {
            result.merged[id] = index;
            occupied.emplace(index, id);
        }
    }

    for (const auto& [id, at] : placed) {
        if (result.merged.count(id) || ancestor.count(id)) {
            continue;
        }
        // Anchor on the nearest kept neighbours: the placed side did not move them, so their
        // distance to the new one holds as long as the leading side kept the gap between them.
        std::optional<std::pair<int, int>> before;
        std::optional<std::pair<int, int>> after;
        for (const auto& [anchor, index] : ancestor) {
            const auto kept = leading.find(anchor);
            if (kept == leading.end()) {
                continue;
            }
            if (index < at && (!before || index > before->first)) {
                before = std::pair {index, kept->second};
            }
            if (index > at && (!after || index < after->first)) {
                after = std::pair {index, kept->second};
            }
        }
        if (before && after && after->second - before->second != after->first - before->first) {
            result.why = std::string("one side changed the ") + noun
                + "s around one the other side filled, so where it goes is not known";
            return result;
        }
        const int index = before ? before->second + (at - before->first)
            : after              ? after->second - (after->first - at)
                                 : at;
        const auto taken = occupied.find(index);
        if (taken == occupied.end()) {
            result.merged[id] = index;
            occupied[index] = id;
        }
        else if (!ancestor.count(taken->second)) {
            result.sameAs[id] = taken->second;
        }
        else {
            result.why = std::string("a new ") + noun + " lands on one the other side kept";
            return result;
        }
    }
    return result;
}

struct SheetMergeResult
{
    std::vector<App::MergeConflict> conflicts;
    Ids rows;
    Ids columns;
    std::map<std::pair<std::string, std::string>, CellText> cells;
};

App::MergeConflict conflict(const std::string& id, const std::string& detail)
{
    return App::MergeConflict {App::MergeConflict::Kind::Value, id, "Spreadsheet cell", detail, {}, {}, {}};
}

std::string readable(const std::pair<std::string, std::string>& key, const Ids& rows, const Ids& columns)
{
    const auto row = rows.find(key.first);
    const auto column = columns.find(key.second);
    if (row == rows.end() || column == columns.end()) {
        return "a cell";
    }
    return "cell " + App::CellAddress(row->second, column->second).toString();
}

SheetMergeResult mergeSheets(
    const App::DocumentObject& ancestorObj,
    const App::DocumentObject& branchAObj,
    const App::DocumentObject& branchBObj
)
{
    const Version ancestor = versionOf(ancestorObj);
    Version a = versionOf(branchAObj);
    Version b = versionOf(branchBObj);
    SheetMergeResult result;

    const bool aMoved = moved(ancestor.rows, a.rows) || moved(ancestor.columns, a.columns);
    const bool bMoved = moved(ancestor.rows, b.rows) || moved(ancestor.columns, b.columns);
    if (aMoved && bMoved) {
        result.conflicts.push_back(
            conflict("layout", "both sides inserted or removed rows or columns")
        );
        return result;
    }
    Version& leading = bMoved ? b : a;
    Version& placed = bMoved ? a : b;

    const Placement rows = place(ancestor.rows, leading.rows, placed.rows, "row");
    const Placement columns = place(ancestor.columns, leading.columns, placed.columns, "column");
    for (const std::string& why : {rows.why, columns.why}) {
        if (!why.empty()) {
            result.conflicts.push_back(conflict("layout", why));
        }
    }
    if (!result.conflicts.empty()) {
        return result;
    }
    result.rows = rows.merged;
    result.columns = columns.merged;

    std::map<std::pair<std::string, std::string>, CellText> placedCells;
    for (auto& [key, text] : placed.cells) {
        const auto row = rows.sameAs.find(key.first);
        const auto column = columns.sameAs.find(key.second);
        placedCells[{
            row != rows.sameAs.end() ? row->second : key.first,
            column != columns.sameAs.end() ? column->second : key.second
        }] = text;
    }
    placed.cells = std::move(placedCells);

    std::set<std::pair<std::string, std::string>> keys;
    for (const Version* version :
         {&ancestor, static_cast<const Version*>(&a), static_cast<const Version*>(&b)}) {
        for (const auto& entry : version->cells) {
            keys.insert(entry.first);
        }
    }
    auto find = [](const Version& version, const std::pair<std::string, std::string>& key) {
        const auto found = version.cells.find(key);
        return found != version.cells.end() ? std::optional(found->second) : std::nullopt;
    };
    for (const auto& key : keys) {
        const std::optional<CellText> o = find(ancestor, key);
        const std::optional<CellText> l = find(leading, key);
        const std::optional<CellText> p = find(placed, key);
        const std::string where = readable(key, result.rows, result.columns);
        std::optional<CellText> taken;
        if (l == p || p == o) {
            taken = l;
        }
        else if (l == o) {
            taken = p;
            if (p && p->refersByAddress && (aMoved || bMoved)) {
                result.conflicts.push_back(conflict(
                    key.first + "/" + key.second,
                    where
                        + ": its formula names cells by address, and the other side moved "
                          "rows or columns"
                ));
                continue;
            }
        }
        else {
            result.conflicts.push_back(
                conflict(key.first + "/" + key.second, where + ": both sides changed it differently")
            );
            continue;
        }
        if (!taken) {
            continue;
        }
        if (!result.rows.count(key.first) || !result.columns.count(key.second)) {
            result.conflicts.push_back(conflict(
                key.first + "/" + key.second,
                "one side changed a cell in a row or column the other side removed"
            ));
            continue;
        }
        result.cells[key] = *taken;
    }
    return result;
}

std::optional<std::string> applySheetMerge(
    App::DocumentObject& into,
    const App::DocumentObject& ancestor,
    const App::DocumentObject& branchA,
    const App::DocumentObject& branchB
)
{
    const SheetMergeResult merged = mergeSheets(ancestor, branchA, branchB);
    if (!merged.conflicts.empty()) {
        return "its cells conflict";
    }

    std::set<std::string> usedRows;
    std::set<std::string> usedColumns;
    std::ostringstream cellLines;
    for (const auto& [key, text] : merged.cells) {
        usedRows.insert(key.first);
        usedColumns.insert(key.second);
        const App::CellAddress address(merged.rows.at(key.first), merged.columns.at(key.second));
        cellLines << "<Cell address=\"" << address.toString() << "\" ";
        if (text.content) {
            cellLines << "content=\"" << App::Property::encodeAttribute(*text.content) << "\" ";
        }
        cellLines << text.attributes << "/>";
    }
    std::ostringstream file;
    file << "<root><Cells Count=\"" << merged.cells.size() << "\" rows=\"" << usedRows.size()
         << "\" columns=\"" << usedColumns.size() << "\">";
    for (const std::string& id : usedRows) {
        file << "<Row row=\""
             << App::CellAddress(merged.rows.at(id), 0).toString(App::CellAddress::Cell::ShowRow)
             << "\" id=\"" << id << "\"/>";
    }
    for (const std::string& id : usedColumns) {
        file << "<Column column=\""
             << App::CellAddress(0, merged.columns.at(id)).toString(App::CellAddress::Cell::ShowColumn)
             << "\" id=\"" << id << "\"/>";
    }
    file << cellLines.str() << "</Cells></root>";

    std::istringstream stream(file.str());
    Base::XMLReader reader("Cells", stream);
    auto& sheet = static_cast<Sheet&>(into);
    sheet.clearAll();
    sheet.getCells()->Restore(reader);
    return std::nullopt;
}

}  // namespace

void Spreadsheet::registerSheetMerge()
{
    App::registerRecipeMerge(
        Sheet::getClassTypeId(),
        {"cells"},
        [](const App::DocumentObject& ancestor,
           const App::DocumentObject& branchA,
           const App::DocumentObject& branchB) {
            return App::RecipeContentMerge {mergeSheets(ancestor, branchA, branchB).conflicts, {}, {}};
        },
        &applySheetMerge
    );
}
