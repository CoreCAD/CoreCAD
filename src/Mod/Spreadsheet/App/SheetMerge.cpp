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


#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <App/Document.h>
#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/Property.h>
#include <App/PropertyExpressionEngine.h>
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

Ids inverted(const std::map<int, std::string>& byIndex)
{
    Ids ids;
    for (const auto& [index, id] : byIndex) {
        ids[id] = index;
    }
    return ids;
}

PropertySheet& cellsOf(const App::DocumentObject& sheet)
{
    return *const_cast<Sheet&>(static_cast<const Sheet&>(sheet)).getCells();
}

Ids rowsOf(const App::DocumentObject& sheet)
{
    return inverted(cellsOf(sheet).rowIdMap());
}

Ids columnsOf(const App::DocumentObject& sheet)
{
    return inverted(cellsOf(sheet).columnIdMap());
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

bool moved(const App::DocumentObject& ancestor, const App::DocumentObject& side)
{
    return moved(rowsOf(ancestor), rowsOf(side)) || moved(columnsOf(ancestor), columnsOf(side));
}

/// Everything a cell states, by its address.
std::map<App::CellAddress, std::string> statedCells(const App::DocumentObject& sheet)
{
    std::map<App::CellAddress, std::string> stated;
    const PropertySheet& cells = cellsOf(sheet);
    for (const App::CellAddress& address : cells.getUsedCells()) {
        std::ostringstream saved;
        cells.getValue(address)->save(saved, "", false);
        const std::string line = saved.str();
        const std::string quoted = "address=\"" + address.toString() + "\" ";
        stated[address] = line.substr(0, line.find(quoted))
            + line.substr(line.find(quoted) + quoted.size());
    }
    return stated;
}

App::MergeConflict conflict(const std::string& id, const std::string& detail)
{
    return App::MergeConflict {App::MergeConflict::Kind::Value, id, "Spreadsheet", detail, {}, {}, {}};
}

const App::DocumentObject* withUid(const App::Document& doc, const std::string& uid)
{
    for (const App::DocumentObject* obj : doc.getObjects()) {
        if (obj->Uid.getValueStr() == uid) {
            return obj;
        }
    }
    return nullptr;
}

/// The rows (or columns) one side inserted and removed, stated against the ancestor's numbering.
/// A row that held a cell carries an id, so where it went is known. A row that never held one, or
/// whose id is gone (removed, or only emptied: a saved sheet keeps no id for an empty row), is
/// only counted, so a stretch of them that grew or shrank leaves where in it the change was open.
struct AxisMoves
{
    /// (index, count) applied in order, bottom first: a positive count inserts that many before
    /// the index, a negative one removes that many from it.
    std::vector<std::pair<int, int>> steps;
    /// Inclusive stretches whose rows moved by an amount nothing pins down.
    std::vector<std::pair<int, int>> unknown;
    bool reordered {false};

    bool unplaced(int index) const
    {
        for (const auto& [first, last] : unknown) {
            if (index >= first && index <= last) {
                return true;
            }
        }
        return false;
    }
};

AxisMoves movesOf(const Ids& ancestor, const Ids& moving)
{
    AxisMoves moves;
    std::vector<std::pair<int, int>> kept;  // (ancestor index, moving side's index)
    int lastGone = -1;
    for (const auto& [id, index] : ancestor) {
        const auto found = moving.find(id);
        if (found != moving.end()) {
            kept.emplace_back(index, found->second);
        }
        else {
            lastGone = std::max(lastGone, index);
        }
    }
    std::sort(kept.begin(), kept.end());
    for (std::size_t i = 1; i < kept.size(); ++i) {
        if (kept[i].second <= kept[i - 1].second) {
            moves.reordered = true;
            return moves;
        }
    }

    // Below the last kept row nothing pins the layout; once a row there lost its id, whatever
    // lies below it may have moved.
    const int lastKept = kept.empty() ? -1 : kept.back().first;
    if (lastGone > lastKept) {
        moves.unknown.emplace_back(lastKept + 1, std::numeric_limits<int>::max());
    }
    for (std::size_t i = kept.size(); i-- > 0;) {
        const auto [above, aboveThere] = i > 0 ? kept[i - 1] : std::pair {-1, -1};
        const auto [below, belowThere] = kept[i];
        const int stretch = below - above - 1;
        const int grown = (belowThere - aboveThere - 1) - stretch;
        if (grown != 0) {
            moves.steps.emplace_back(above + 1, grown);
            if (stretch > 0) {
                moves.unknown.emplace_back(above + 1, below - 1);
            }
        }
    }
    return moves;
}

bool lostAnId(const Ids& ancestor, const Ids& side)
{
    return std::any_of(ancestor.begin(), ancestor.end(), [&side](const auto& entry) {
        return side.count(entry.first) == 0;
    });
}

bool lostAnId(const App::DocumentObject& ancestor, const App::DocumentObject& side)
{
    return lostAnId(rowsOf(ancestor), rowsOf(side)) || lostAnId(columnsOf(ancestor), columnsOf(side));
}

void replay(Sheet& sheet, const AxisMoves& rows, const AxisMoves& columns)
{
    for (const auto& [index, count] : rows.steps) {
        if (count > 0) {
            sheet.insertRows(index, count);
        }
        else {
            sheet.removeRows(index, -count);
        }
    }
    for (const auto& [index, count] : columns.steps) {
        if (count > 0) {
            sheet.insertColumns(index, count);
        }
        else {
            sheet.removeColumns(index, -count);
        }
    }
}

std::map<std::string, std::string> formulasOf(const App::DocumentObject& obj)
{
    std::map<std::string, std::string> formulas;
    std::vector<App::Property*> properties;
    obj.getPropertyList(properties);
    for (const App::Property* prop : properties) {
        if (const auto* container = dynamic_cast<const App::PropertyExpressionContainer*>(prop)) {
            for (const auto& [path, expression] : container->getExpressions()) {
                formulas[path.toString()] = expression->toString();
            }
        }
    }
    return formulas;
}

/// What the side that kept the sheet's layout wrote that names a row or column the replay cannot
/// place, each named for a person.
std::vector<std::string> unplacedWrites(
    const App::Document& ancestor,
    const App::Document& placed,
    const App::DocumentObject& sheet,
    const AxisMoves& rows,
    const AxisMoves& columns
)
{
    std::vector<std::string> found;
    const auto unplaced = [&](const App::CellAddress& address) {
        return rows.unplaced(address.row()) || columns.unplaced(address.col());
    };

    const App::DocumentObject* sheetBefore = withUid(ancestor, sheet.Uid.getValueStr());
    const auto before = statedCells(*sheetBefore);
    for (const auto& [address, stated] : statedCells(sheet)) {
        const auto was = before.find(address);
        if ((was == before.end() || was->second != stated) && unplaced(address)) {
            found.push_back(
                "cell " + address.toString()
                + " changed where the other side inserted or removed rows or columns"
            );
        }
    }
    for (const auto& [address, stated] : before) {
        (void)stated;
        if (cellsOf(sheet).getValue(address) == nullptr && unplaced(address)) {
            found.push_back(
                "cell " + address.toString()
                + " cleared where the other side inserted or removed rows or columns"
            );
        }
    }

    for (const App::DocumentObject* obj : placed.getObjects()) {
        const App::DocumentObject* objBefore = withUid(ancestor, obj->Uid.getValueStr());
        const auto formulasBefore = objBefore ? formulasOf(*objBefore)
                                              : std::map<std::string, std::string> {};
        std::vector<App::Property*> properties;
        obj->getPropertyList(properties);
        for (const App::Property* prop : properties) {
            const auto* container = dynamic_cast<const App::PropertyExpressionContainer*>(prop);
            if (container == nullptr) {
                continue;
            }
            for (const auto& [path, expression] : container->getExpressions()) {
                const auto was = formulasBefore.find(path.toString());
                if (was != formulasBefore.end() && was->second == expression->toString()) {
                    continue;
                }
                for (const auto& [named, hidden] : expression->getIdentifiers()) {
                    (void)hidden;
                    if (named.getDocumentObject() != &sheet
                        || !cellsOf(sheet).isValidCellAddressName(named.getPropertyName())) {
                        continue;
                    }
                    const App::CellAddress address(named.getPropertyName());
                    if (unplaced(address)) {
                        found.push_back(
                            std::string(obj->Label.getValue()) + " " + path.toString() + " names "
                            + address.toString()
                            + ", where the other side inserted or removed rows or "
                              "columns"
                        );
                    }
                }
            }
        }
    }
    return found;
}

struct SheetReplay
{
    std::string uid;
    bool placedIsB;
    AxisMoves rows;
    AxisMoves columns;
};

App::MergeRebase rebaseSheets(
    const App::Document& ancestor,
    const App::Document& branchA,
    const App::Document& branchB
)
{
    App::MergeRebase rebase;
    std::vector<SheetReplay> replays;
    for (const App::DocumentObject* sheetO : ancestor.getObjectsOfType(Sheet::getClassTypeId())) {
        const std::string uid = sheetO->Uid.getValueStr();
        const App::DocumentObject* sheetA = withUid(branchA, uid);
        const App::DocumentObject* sheetB = withUid(branchB, uid);
        if (sheetA == nullptr || sheetB == nullptr) {
            continue;
        }
        // The side that moved rows leads. A side that only lost ids may have removed rows below
        // the last one it kept, so it leads too, unless the other side moved some.
        const bool aShifted = moved(*sheetO, *sheetA);
        const bool bShifted = moved(*sheetO, *sheetB);
        const bool aLeads = aShifted || (!bShifted && lostAnId(*sheetO, *sheetA));
        const bool bLeads = bShifted || (!aShifted && lostAnId(*sheetO, *sheetB));
        if (aLeads == bLeads) {
            continue;
        }
        const bool aMoved = aLeads;
        const App::DocumentObject& leading = aMoved ? *sheetA : *sheetB;
        const App::DocumentObject& placedSheet = aMoved ? *sheetB : *sheetA;
        SheetReplay replayed {
            uid,
            aMoved,
            movesOf(rowsOf(*sheetO), rowsOf(leading)),
            movesOf(columnsOf(*sheetO), columnsOf(leading))
        };
        const std::string label = sheetO->Label.getValue();
        if (replayed.rows.reordered || replayed.columns.reordered) {
            rebase.conflicts.push_back(conflict(uid, label + ": one side reordered rows or columns"));
            continue;
        }
        for (const std::string& why : unplacedWrites(
                 ancestor,
                 aMoved ? branchB : branchA,
                 placedSheet,
                 replayed.rows,
                 replayed.columns
             )) {
            rebase.conflicts.push_back(conflict(uid, label + ": " + why));
        }
        if (!replayed.rows.steps.empty() || !replayed.columns.steps.empty()) {
            replays.push_back(std::move(replayed));
        }
    }
    if (rebase.conflicts.empty() && !replays.empty()) {
        rebase.apply = [replays](App::Document& o, App::Document& a, App::Document& b) {
            for (const SheetReplay& sheet : replays) {
                App::Document& placed = sheet.placedIsB ? b : a;
                for (App::Document* doc : {&o, &placed}) {
                    auto* copy = const_cast<App::DocumentObject*>(withUid(*doc, sheet.uid));
                    replay(static_cast<Sheet&>(*copy), sheet.rows, sheet.columns);
                }
            }
        };
    }
    return rebase;
}

struct TakenCell
{
    std::string stated;
    const App::DocumentObject* from;
};

struct SheetMergeResult
{
    std::vector<App::MergeConflict> conflicts;
    std::map<App::CellAddress, TakenCell> cells;
};

std::optional<std::string> attributeOf(const std::string& stated, const std::string& name)
{
    const std::string opening = " " + name + "=\"";
    const auto start = stated.find(opening);
    if (start == std::string::npos) {
        return std::nullopt;
    }
    return stated.substr(
        start + opening.size(),
        stated.find('"', start + opening.size()) - start - opening.size()
    );
}

/// The cells one merged cell covers, its own address first.
struct Span
{
    App::CellAddress anchor;
    int rows;
    int cols;
    const App::DocumentObject* from;

    bool covers(const App::CellAddress& address) const
    {
        return address.row() >= anchor.row() && address.row() < anchor.row() + rows
            && address.col() >= anchor.col() && address.col() < anchor.col() + cols;
    }

    bool overlaps(const Span& other) const
    {
        return anchor.row() < other.anchor.row() + other.rows
            && other.anchor.row() < anchor.row() + rows
            && anchor.col() < other.anchor.col() + other.cols
            && other.anchor.col() < anchor.col() + cols;
    }

    std::string named() const
    {
        return App::CellAddress(anchor.row(), anchor.col()).toString() + ":"
            + App::CellAddress(anchor.row() + rows - 1, anchor.col() + cols - 1).toString();
    }
};

/// Cells merged by address. A merge replays one side's inserted and removed rows on the other
/// side first (rebaseSheets), so by now an address names the same cell in all three versions.
SheetMergeResult mergeSheets(
    const App::DocumentObject& ancestor,
    const App::DocumentObject& branchA,
    const App::DocumentObject& branchB
)
{
    SheetMergeResult result;
    const std::string uid = ancestor.Uid.getValueStr();
    const bool aMoved = moved(ancestor, branchA);
    const bool bMoved = moved(ancestor, branchB);
    if (aMoved && bMoved) {
        result.conflicts.push_back(conflict(uid, "both sides inserted or removed rows or columns"));
    }
    if (aMoved || bMoved) {
        return result;
    }

    const auto o = statedCells(ancestor);
    const auto a = statedCells(branchA);
    const auto b = statedCells(branchB);
    std::set<App::CellAddress> addresses;
    for (const auto* version : {&o, &a, &b}) {
        for (const auto& entry : *version) {
            addresses.insert(entry.first);
        }
    }
    const auto find = [](const std::map<App::CellAddress, std::string>& version,
                         const App::CellAddress& address) {
        const auto found = version.find(address);
        return found != version.end() ? std::optional(found->second) : std::nullopt;
    };
    for (const App::CellAddress& address : addresses) {
        const auto inO = find(o, address);
        const auto inA = find(a, address);
        const auto inB = find(b, address);
        const std::optional<std::string> taken = inA == inB || inB == inO ? inA
            : inA == inO                                                  ? inB
                                                                          : std::nullopt;
        if (inA != inB && inA != inO && inB != inO) {
            result.conflicts.push_back(
                conflict(uid, "cell " + address.toString() + ": both sides changed it differently")
            );
        }
        else if (taken) {
            result.cells[address] = {*taken, taken == inA ? &branchA : &branchB};
        }
    }

    std::map<std::string, App::CellAddress> aliases;
    for (const auto& [address, cell] : result.cells) {
        if (const auto alias = attributeOf(cell.stated, "alias")) {
            const auto [named, fresh] = aliases.emplace(*alias, address);
            if (!fresh) {
                result.conflicts.push_back(conflict(
                    uid,
                    "the alias " + *alias + " names both " + named->second.toString() + " and "
                        + address.toString()
                ));
            }
        }
    }

    // Cells merged on one side must not swallow what the other side merged or wrote.
    std::vector<Span> spans;
    for (const auto& [address, cell] : result.cells) {
        const auto rows = attributeOf(cell.stated, "rowSpan");
        const auto cols = attributeOf(cell.stated, "colSpan");
        if (rows && cols && (std::stoi(*rows) > 1 || std::stoi(*cols) > 1)) {
            spans.push_back({address, std::stoi(*rows), std::stoi(*cols), cell.from});
        }
    }
    for (std::size_t i = 0; i < spans.size(); ++i) {
        for (std::size_t j = i + 1; j < spans.size(); ++j) {
            if (spans[i].overlaps(spans[j])) {
                result.conflicts.push_back(conflict(
                    uid,
                    "the merged cells " + spans[i].named() + " and " + spans[j].named() + " overlap"
                ));
            }
        }
        const auto& sameSide = spans[i].from == &branchA ? a : b;
        for (const auto& [address, cell] : result.cells) {
            const bool anchorsASpan = std::any_of(spans.begin(), spans.end(), [&](const Span& span) {
                return span.anchor == address;
            });
            if (anchorsASpan || !spans[i].covers(address)) {
                continue;
            }
            const auto there = sameSide.find(address);
            if (there == sameSide.end() || there->second != cell.stated) {
                result.conflicts.push_back(conflict(
                    uid,
                    "cell " + address.toString() + " lies under the merged cells "
                        + spans[i].named() + " the other side made"
                ));
            }
        }
    }
    return result;
}

/// The id of the row or column at `index` in the first version that has one.
std::string idAt(int index, std::initializer_list<const std::map<int, std::string>*> versions)
{
    for (const auto* ids : versions) {
        const auto found = ids->find(index);
        if (found != ids->end()) {
            return found->second;
        }
    }
    return {};
}

std::optional<std::string> applySheetMerge(
    App::DocumentObject& into,
    const App::DocumentObject& ancestor,
    const App::DocumentObject& branchA,
    const App::DocumentObject& branchB
)
{
    if (moved(ancestor, branchA) || moved(ancestor, branchB)) {
        return "its rows or columns moved and were not replayed";
    }
    const SheetMergeResult merged = mergeSheets(ancestor, branchA, branchB);
    if (!merged.conflicts.empty()) {
        return "its cells conflict";
    }

    // A row or column keeps the id of the version its first cell was taken from.
    std::map<int, const PropertySheet*> rows;
    std::map<int, const PropertySheet*> columns;
    std::ostringstream cellLines;
    for (const auto& [address, cell] : merged.cells) {
        rows.emplace(address.row(), &cellsOf(*cell.from));
        columns.emplace(address.col(), &cellsOf(*cell.from));
        const std::string opening = "<Cell ";
        cellLines << opening << "address=\"" << address.toString() << "\" "
                  << cell.stated.substr(cell.stated.find(opening) + opening.size());
    }
    const auto& o = cellsOf(ancestor);
    const auto& a = cellsOf(branchA);
    const auto& b = cellsOf(branchB);
    std::ostringstream file;
    file << "<root><Cells Count=\"" << merged.cells.size() << "\" rows=\"" << rows.size()
         << "\" columns=\"" << columns.size() << "\">";
    for (const auto& [row, from] : rows) {
        file << "<Row row=\"" << App::CellAddress(row, 0).toString(App::CellAddress::Cell::ShowRow)
             << "\" id=\""
             << idAt(row, {&from->rowIdMap(), &o.rowIdMap(), &a.rowIdMap(), &b.rowIdMap()})
             << "\"/>";
    }
    for (const auto& [column, from] : columns) {
        file << "<Column column=\""
             << App::CellAddress(0, column).toString(App::CellAddress::Cell::ShowColumn) << "\" id=\""
             << idAt(
                    column,
                    {&from->columnIdMap(), &o.columnIdMap(), &a.columnIdMap(), &b.columnIdMap()}
                )
             << "\"/>";
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
    App::registerMergeRebase(&rebaseSheets);
}
