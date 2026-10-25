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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <cctype>
# include <cstdlib>
# include <iomanip>
# include <limits>
# include <locale>
# include <map>
# include <memory>
# include <numeric>
# include <set>
# include <sstream>

# include <boost/uuid/uuid.hpp>
# include <boost/uuid/uuid_io.hpp>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <App/ElementNamingUtils.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyLinks.h>
#include <App/RecipeDetail.h>
#include <Mod/Part/App/Geometry.h>

#include "SketchRecipe.h"
#include "SketchRecipeInternal.h"

#include "Constraint.h"
#include "ExternalGeometryFacade.h"
#include "GeoEnum.h"
#include "GeometryFacade.h"
#include "SolverGeometryExtension.h"

using namespace Sketcher;
using namespace Sketcher::SketchRecipeInternal;

namespace
{

/// Whether the side's own solve fixed this coordinate, so its constraints produced it. A coordinate
/// the solve left free, or one it cannot speak for, counts as a person's choice: where the sketch
/// cannot tell, a drag must never be lost to it.
bool fixedBySolver(const Part::Geometry* geo, const std::string& field)
{
    if (!geo->hasExtension(SolverGeometryExtension::getClassTypeId())) {
        return false;
    }
    const auto solved = std::static_pointer_cast<const SolverGeometryExtension>(
        geo->getExtension(SolverGeometryExtension::getClassTypeId()).lock()
    );
    if (solved->getGeometry() == SolverGeometryExtension::FullyConstraint) {
        return true;
    }
    const bool line = geo->is<Part::GeomLineSegment>();
    if ((line && field == "from") || (geo->is<Part::GeomPoint>() && field == "at")) {
        return solved->getStart() == SolverGeometryExtension::Independent;
    }
    if (line && field == "to") {
        return solved->getEnd() == SolverGeometryExtension::Independent;
    }
    return false;
}

/// A coordinate a side's solve may have moved, as (tag, field), with where that side left it.
using HeldBack = std::map<std::pair<std::string, std::string>, std::string>;

/// A side's geometry with each entity's coordinates where a person on that side chose them.
/// Coordinates its solve fixed stand at the ancestor's, so they never count as that side's change.
/// So do coordinates tied to a constraint the side changed, which the history also reads as the
/// solver's work; those are kept in `heldBack`, since a drag among them cannot be told apart.
App::RecipeSection chosenGeometry(
    const SketchObject& ancestor,
    const SketchObject& side,
    const App::RecipeSection& geometry,
    HeldBack* heldBack = nullptr
)
{
    const App::RecipeConsequences consequences = sketchConsequences(ancestor, side);

    std::map<std::string, const Part::Geometry*> sideByTag;
    for (const Part::Geometry* geo : side.getInternalGeometry()) {
        sideByTag[tagToString(geo->getTag())] = geo;
    }
    std::map<std::string, const Part::Geometry*> ancestorByTag;
    for (const Part::Geometry* geo : ancestor.getInternalGeometry()) {
        ancestorByTag[tagToString(geo->getTag())] = geo;
    }

    App::RecipeSection chosen = geometry;
    for (auto& [tag, node] : chosen) {
        const Part::Geometry* geo = sideByTag[tag];
        addAuthoredCoordinates(geo, node);
        const auto was = ancestorByTag.find(tag);
        if (was == ancestorByTag.end()) {
            continue;
        }
        App::RecipeNode before;
        addAuthoredCoordinates(was->second, before);
        for (auto& [field, value] : node.fields) {
            if (before.fields.count(field) == 0) {
                continue;
            }
            if (fixedBySolver(geo, field)) {
                value = before.fields.at(field);
            }
            else if (consequences.count({tag, field}) != 0 && value != before.fields.at(field)) {
                if (heldBack != nullptr) {
                    (*heldBack)[{tag, field}] = value;
                }
                value = before.fields.at(field);
            }
        }
    }
    return chosen;
}

/// Put back a held-back coordinate wherever the other side chose that same coordinate: it may
/// have been dragged, and taking the other side's choice would lose the drag without a word.
void restoreContested(
    App::RecipeSection& chosen,
    const HeldBack& heldBack,
    const App::RecipeSection& ancestor,
    const App::RecipeSection& otherChosen
)
{
    for (const auto& [where, value] : heldBack) {
        const auto& [tag, field] = where;
        const auto other = otherChosen.find(tag);
        const auto was = ancestor.find(tag);
        if (other == otherChosen.end() || was == ancestor.end()
            || other->second.fields.count(field) == 0 || was->second.fields.count(field) == 0) {
            continue;
        }
        if (other->second.fields.at(field) != was->second.fields.at(field)) {
            chosen[tag].fields[field] = value;
        }
    }
}

/// A copy of `geo` with the coordinates named in `fields` taken from `other`, or null where its
/// type's coordinates cannot be taken one at a time.
std::unique_ptr<Part::Geometry> composeCoordinates(
    const Part::Geometry* geo,
    const Part::Geometry* other,
    const std::vector<std::string>& fields
)
{
    const auto takes = [&fields](const char* field) {
        return std::find(fields.begin(), fields.end(), field) != fields.end();
    };
    std::unique_ptr<Part::Geometry> composed(geo->copy());
    if (auto* line = dynamic_cast<Part::GeomLineSegment*>(composed.get())) {
        const auto* from = dynamic_cast<const Part::GeomLineSegment*>(other);
        line->setPoints(
            takes("from") ? from->getStartPoint() : line->getStartPoint(),
            takes("to") ? from->getEndPoint() : line->getEndPoint()
        );
        return composed;
    }
    if (auto* arc = dynamic_cast<Part::GeomArcOfCircle*>(composed.get())) {
        const auto* from = dynamic_cast<const Part::GeomArcOfCircle*>(other);
        if (takes("center")) {
            arc->setCenter(from->getCenter());
        }
        if (takes("radius")) {
            arc->setRadius(from->getRadius());
        }
        if (takes("range")) {
            double first = 0.0;
            double last = 0.0;
            from->getRange(first, last, true);
            arc->setRange(first, last, true);
        }
        return composed;
    }
    if (auto* circle = dynamic_cast<Part::GeomCircle*>(composed.get())) {
        const auto* from = dynamic_cast<const Part::GeomCircle*>(other);
        if (takes("center")) {
            circle->setCenter(from->getCenter());
        }
        if (takes("radius")) {
            circle->setRadius(from->getRadius());
        }
        return composed;
    }
    return nullptr;
}

/// The coordinates of `tag` a side chose differently from the ancestor; all of them for an entity
/// the side added. Empty where the side removed it or left its position alone.
std::vector<std::string> choiceOf(
    const std::string& tag,
    const App::RecipeSection& ancestor,
    const App::RecipeSection& side
)
{
    std::vector<std::string> fields;
    const auto now = side.find(tag);
    if (now == side.end()) {
        return fields;
    }
    const auto was = ancestor.find(tag);
    for (const auto& [field, value] : now->second.fields) {
        if (field == "construction") {
            continue;
        }
        if (was == ancestor.end() || was->second.fields.count(field) == 0
            || was->second.fields.at(field) != value) {
            fields.push_back(field);
        }
    }
    return fields;
}

/// Whether two readings of one entity's coordinates agree. Each number is rounded for reading, so
/// two solves a hair apart can round one step apart; that much is the same position.
bool samePosition(const App::RecipeNode& one, const App::RecipeNode& other)
{
    if (one.fields.size() != other.fields.size()) {
        return false;
    }
    constexpr double roundingStep = 2e-4;
    for (const auto& [field, text] : one.fields) {
        const auto it = other.fields.find(field);
        if (it == other.fields.end()) {
            return false;
        }
        std::istringstream left(text);
        std::istringstream right(it->second);
        std::string a;
        std::string b;
        while (left >> a) {
            if (!(right >> b)) {
                return false;
            }
            if (a == b) {
                continue;
            }
            const auto number = [](std::string token) {
                std::erase(token, '(');
                std::erase(token, ')');
                std::istringstream in(token);
                in.imbue(std::locale::classic());
                double value = 0.0;
                in >> value;
                return in && in.peek() == std::char_traits<char>::eof() ? std::optional(value)
                                                                        : std::nullopt;
            };
            const std::optional<double> x = number(a);
            const std::optional<double> y = number(b);
            if (!x || !y || std::abs(*x - *y) > roundingStep) {
                return false;
            }
        }
        if (right >> b) {
            return false;
        }
    }
    return true;
}

/// Whether a side left an entity somewhere other than the ancestor did, or added it.
bool movedFromAncestor(const std::string& tag, const SketchObject& ancestor, const SketchObject& side)
{
    const Part::Geometry* there = findSeedGeometry(tag, {&side});
    if (there == nullptr) {
        return false;
    }
    const Part::Geometry* before = findSeedGeometry(tag, {&ancestor});
    if (before == nullptr) {
        return true;
    }
    App::RecipeNode was;
    App::RecipeNode now;
    addAuthoredCoordinates(before, was);
    addAuthoredCoordinates(there, now);
    return !samePosition(was, now);
}

}  // namespace

MergeReport Sketcher::mergeSketches(
    const SketchObject& ancestor,
    const SketchObject& branchA,
    const SketchObject& branchB,
    SketchObject* into
)
{
    MergeReport report;

    const SketchRecipe base = emitSketchRecipe(ancestor);
    const SketchRecipe a = emitSketchRecipe(branchA);
    const SketchRecipe b = emitSketchRecipe(branchB);

    // Positions are merged too, but only where a person chose them: a side's change is what it
    // moved by hand, never where its solver carried the geometry.
    const App::RecipeSection baseChosen = chosenGeometry(ancestor, ancestor, base.geometry);
    HeldBack aHeldBack;
    HeldBack bHeldBack;
    App::RecipeSection aChosen = chosenGeometry(ancestor, branchA, a.geometry, &aHeldBack);
    App::RecipeSection bChosen = chosenGeometry(ancestor, branchB, b.geometry, &bHeldBack);
    const App::RecipeSection aFirm = aChosen;
    restoreContested(aChosen, aHeldBack, baseChosen, bChosen);
    restoreContested(bChosen, bHeldBack, baseChosen, aFirm);

    // Merge each section by durable identity (renumber-invisible), then each element both sides
    // changed by its values, as the document merge does an object.
    const auto mergeByValue = [&report](
                                  const App::RecipeSection& o,
                                  const App::RecipeSection& x,
                                  const App::RecipeSection& y
                              ) {
        std::vector<App::MergeConflict> whole;
        App::RecipeSection merged = App::RecipeMerge::threeWay(o, x, y, whole);
        const std::vector<App::MergeConflict> refined
            = App::RecipeMerge::refineConflicts(whole, o, x, y, merged);
        report.conflicts.insert(report.conflicts.end(), refined.begin(), refined.end());
        return merged;
    };
    report.mergedGeometry = mergeByValue(baseChosen, aChosen, bChosen);
    report.mergedConstraints = mergeByValue(base.constraints, a.constraints, b.constraints);
    const App::RecipeSection mergedExternal = mergeByValue(base.external, a.external, b.external);
    const auto drawnAndBorrowed = [](const SketchRecipe& recipe) {
        App::RecipeSection targets = recipe.geometry;
        targets.insert(recipe.external.begin(), recipe.external.end());
        return targets;
    };

    // A constraint one side added or changed on geometry the other deleted is a conflict, kept for
    // the person settling it. One neither side touched is kept as it was; the solve judges it.
    App::RecipeMerge::presenceConflicts(
        base.constraints,
        a.constraints,
        b.constraints,
        drawnAndBorrowed(base),
        drawnAndBorrowed(a),
        drawnAndBorrowed(b),
        report.mergedConstraints,
        report.conflicts
    );
    // Each entity starts the solve where the side that chose its position left it.
    std::map<std::string, const SketchObject*> seedFor;
    // Where neither side chose it, it starts where B's solve alone moved it, since that may be a
    // drag the merge cannot tell apart; otherwise A's copy seeds it.
    const auto movedBySolveOnly = [](const HeldBack& heldBack, const std::string& tag) {
        const auto first = heldBack.lower_bound({tag, std::string()});
        return first != heldBack.end() && first->first.first == tag;
    };
    // Where a side chose an entity's position, every solve starts it there.
    std::map<std::string, const SketchObject*> chosenSeedFor;
    // Where both sides chose different coordinates of one entity, it starts from both.
    std::vector<std::unique_ptr<Part::Geometry>> composed;
    std::map<std::string, const Part::Geometry*> seedGeometry;
    for (const auto& [tag, node] : report.mergedGeometry) {
        const std::vector<std::string> aFields = choiceOf(tag, baseChosen, aChosen);
        const std::vector<std::string> bFields = choiceOf(tag, baseChosen, bChosen);
        const bool aChose = !aFields.empty();
        const bool bChose = !bFields.empty();
        if (aChose || bChose) {
            chosenSeedFor[tag] = aChose ? &branchA : &branchB;
        }
        std::vector<std::string> bOnly;
        for (const std::string& field : bFields) {
            if (std::find(aFields.begin(), aFields.end(), field) == aFields.end()) {
                bOnly.push_back(field);
            }
        }
        if (aChose && !bOnly.empty()) {
            const Part::Geometry* fromA = findSeedGeometry(tag, {&branchA});
            const Part::Geometry* fromB = findSeedGeometry(tag, {&branchB});
            std::unique_ptr<Part::Geometry> both = fromA != nullptr && fromB != nullptr
                ? composeCoordinates(fromA, fromB, bOnly)
                : nullptr;
            if (both) {
                seedGeometry[tag] = both.get();
                composed.push_back(std::move(both));
            }
            else {
                App::MergeConflict conflict {
                    App::MergeConflict::Kind::Value,
                    tag,
                    node.type,
                    "both sides moved different parts of a curve that moves as one"
                };
                conflict.ancestor = baseChosen.count(tag) != 0
                    ? std::optional<App::RecipeNode>(baseChosen.at(tag))
                    : std::nullopt;
                conflict.branchA = aChosen.at(tag);
                conflict.branchB = bChosen.at(tag);
                report.conflicts.push_back(std::move(conflict));
            }
            continue;
        }
        if (bChose && !aChose) {
            seedFor[tag] = &branchB;
        }
        else if (
            !aChose && !bChose && movedBySolveOnly(bHeldBack, tag) && !movedBySolveOnly(aHeldBack, tag)
        ) {
            seedFor[tag] = &branchB;
        }
    }

    // Regenerate onto a hidden, throwaway scratch sketch and re-solve: the merge is text until the
    // kernel runs, and only the solver reveals a clean merge that does not compile.
    App::Document* scratch = nullptr;
    SketchObject* target = into;
    if (target == nullptr) {
        App::DocumentInitFlags flags;
        flags.createView = false;
        flags.temporary = true;
        scratch = App::GetApplication().newDocument("recipeMergeScratch", nullptr, flags);
        target = static_cast<SketchObject*>(
            scratch->addObject("Sketcher::SketchObject", "MergeScratch")
        );
    }
    else {
        target->deleteAllConstraints();
        target->deleteAllGeometry();
    }
    const SketchRecipe merged {report.mergedGeometry, report.mergedConstraints, mergedExternal};
    report.regen = regenerateSketch(*target, merged, {&branchA, &branchB}, seedFor, seedGeometry);

    // A position a side chose that the re-solve moved: the rest of the merge overrode it.
    const std::vector<Part::Geometry*>& solved = target->getInternalGeometry();
    for (const auto& [tag, geoId] : report.regen.placed) {
        App::RecipeNode& now = report.solvedPositions[tag];
        now.id = tag;
        addAuthoredCoordinates(solved[geoId], now);
        for (const App::RecipeSection* side : {&aChosen, &bChosen}) {
            for (const std::string& field : choiceOf(tag, baseChosen, *side)) {
                const std::string& choice = side->at(tag).fields.at(field);
                const bool reported = std::any_of(
                    report.overridden.begin(),
                    report.overridden.end(),
                    [&](const OverriddenChoice& o) { return o.id == tag && o.field == field; }
                );
                if (!reported && report.mergedGeometry.at(tag).fields.at(field) == choice
                    && now.fields[field] != choice) {
                    report.overridden.push_back({tag, side->at(tag).type, field});
                }
            }
        }
    }

    // Solved again from each side's own positions, wherever that side's differ from the
    // ancestor's and no side chose them. A position that comes out elsewhere is decided by where
    // the solve starts, not by the merged values.
    App::DocumentInitFlags seededFlags;
    seededFlags.createView = false;
    seededFlags.temporary = true;
    App::Document* seededScratch
        = App::GetApplication().newDocument("recipeMergeSeeded", nullptr, seededFlags);
    const auto starts = report.regen.solverStatus == 0
        ? std::vector<const SketchObject*> {&branchA, &branchB}
        : std::vector<const SketchObject*> {};
    for (const SketchObject* side : starts) {
        std::map<std::string, const SketchObject*> startFor = seedFor;
        for (const auto& [tag, node] : report.mergedGeometry) {
            if (chosenSeedFor.count(tag) == 0 && movedFromAncestor(tag, ancestor, *side)) {
                startFor[tag] = side;
            }
        }
        auto* seeded = static_cast<SketchObject*>(
            seededScratch->addObject("Sketcher::SketchObject", "Seeded")
        );
        const RegenResult again
            = regenerateSketch(*seeded, merged, {&branchA, &branchB}, startFor, seedGeometry);
        if (again.solverStatus != 0) {
            continue;
        }
        const std::vector<Part::Geometry*>& resolved = seeded->getInternalGeometry();
        for (const auto& [tag, geoId] : again.placed) {
            App::RecipeNode there;
            addAuthoredCoordinates(resolved[geoId], there);
            const bool reported = std::any_of(
                report.undetermined.begin(),
                report.undetermined.end(),
                [&](const OpenPosition& p) { return p.id == tag; }
            );
            const auto solved = report.solvedPositions.find(tag);
            if (!reported && solved != report.solvedPositions.end()
                && !samePosition(there, solved->second)) {
                report.undetermined.push_back({tag, report.mergedGeometry.at(tag).type});
            }
        }
    }
    App::GetApplication().closeDocument(seededScratch->getName());

    if (scratch != nullptr) {
        App::GetApplication().closeDocument(scratch->getName());
    }

    return report;
}

namespace Sketcher::SketchRecipeInternal
{

/// Prefix the noun with "a"/"an" by the first letter's sound (a coarse vowel test — good enough
/// for the fixed vocabulary of sketch nouns; "B-spline" reads "a", "arc" reads "an").
std::string withArticle(const std::string& noun)
{
    if (noun.empty()) {
        return noun;
    }
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(noun.front())));
    const bool vowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    return (vowel ? "an " : "a ") + noun;
}

/// A short noun for a geometry node's authored type token (Part::GeomLineSegment -> "line").
/// Unmapped tokens fall back to the bare token so nothing is silently lost.
std::string geometryNoun(const std::string& type)
{
    static const std::map<std::string, std::string> nouns = {
        {"Part::GeomLineSegment", "line"},
        {"Part::GeomCircle", "circle"},
        {"Part::GeomArcOfCircle", "arc"},
        {"Part::GeomEllipse", "ellipse"},
        {"Part::GeomArcOfEllipse", "elliptical arc"},
        {"Part::GeomArcOfHyperbola", "hyperbolic arc"},
        {"Part::GeomArcOfParabola", "parabolic arc"},
        {"Part::GeomBSplineCurve", "B-spline"},
        {"Part::GeomPoint", "point"},
    };
    const auto it = nouns.find(type);
    return it != nouns.end() ? it->second : type;
}

/// A noun phrase for a constraint node's authored type token ("Horizontal" -> "horizontal
/// constraint"). A few tokens read poorly lower-cased and are spelled out; the rest are just
/// lower-cased. Unknown tokens still read as "<token> constraint".
std::string constraintNoun(const std::string& type)
{
    static const std::map<std::string, std::string> special = {
        {"DistanceX", "horizontal-distance"},
        {"DistanceY", "vertical-distance"},
        {"PointOnObject", "point-on-object"},
        {"InternalAlignment", "internal-alignment"},
    };
    const auto it = special.find(type);
    std::string word = it->second;
    if (it == special.end()) {
        word = type;
        for (char& c : word) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return word + " constraint";
}

/// True when a type token names sketch geometry (its driver emits "Part::Geom..."), as opposed
/// to a constraint. Lets one report line describe either kind from the token alone.
bool isGeometryType(const std::string& type)
{
    return type.rfind("Part::Geom", 0) == 0;
}

/// The affected node named with its article ("a line", "a horizontal constraint"). Works from the
/// type token alone, so a dropped node (already erased from the merged section) still reads well.
std::string nounFor(const std::string& type)
{
    if (type.empty()) {
        return "an element";
    }
    return withArticle(isGeometryType(type) ? geometryNoun(type) : constraintNoun(type));
}

/// " on a line and a circle" — the surviving geometry a still-present constraint attaches to, for
/// the conflict lines where the node (and its targets) remain in the merge.
/// Empty when the node was dropped, has no resolvable targets, or is itself geometry.
std::string describeTargets(const std::string& id, const MergeReport& report)
{
    const auto it = report.mergedConstraints.find(id);
    if (it == report.mergedConstraints.end()) {
        return {};
    }
    std::vector<std::string> nouns;
    for (const App::RecipeRef& ref : it->second.refs) {
        const auto geo = report.mergedGeometry.find(ref.target);
        if (geo != report.mergedGeometry.end()) {
            nouns.push_back(withArticle(geometryNoun(geo->second.type)));
        }
    }
    if (nouns.empty()) {
        return {};
    }
    std::string joined = nouns[0];
    for (size_t i = 1; i < nouns.size(); ++i) {
        joined += (i + 1 == nouns.size() ? " and " : ", ") + nouns[i];
    }
    return " on " + joined;
}

}  // namespace Sketcher::SketchRecipeInternal

std::string Sketcher::formatMergeReport(const MergeReport& report)
{
    std::ostringstream out;
    out << "Sketch merge report\n";
    out << "===================\n\n";
    out << "Combined: " << report.mergedGeometry.size() << " geometry element(s), "
        << report.mergedConstraints.size() << " constraint(s).\n\n";

    // Value conflicts — both edits changed the same authored thing in different ways.
    std::vector<const App::MergeConflict*> values;
    std::vector<const App::MergeConflict*> onDeleted;
    for (const App::MergeConflict& c : report.conflicts) {
        (c.kind == App::MergeConflict::Kind::Value ? values : onDeleted).push_back(&c);
    }
    if (values.empty()) {
        out << "No value conflicts: the two edits never changed the same thing differently.\n\n";
    }
    else {
        out << values.size()
            << " value conflict(s) — both versions changed the same thing differently, so you "
               "must choose:\n";
        for (const App::MergeConflict* c : values) {
            out << "  - " << nounFor(c->type) << describeTargets(c->id, report) << "\n";
        }
        out << "\n";
    }

    if (!onDeleted.empty()) {
        out << onDeleted.size()
            << " constraint(s) one version added or changed on geometry the other version "
               "deleted, so you must choose between them:\n";
        for (const App::MergeConflict* c : onDeleted) {
            out << "  - " << nounFor(c->type) << describeTargets(c->id, report) << "\n";
        }
        out << "\n";
    }

    if (!report.overridden.empty()) {
        out << report.overridden.size()
            << " position(s) one version moved by hand that the merged sketch no longer holds — "
               "the other version's changes now put them elsewhere, so you must choose:\n";
        for (const OverriddenChoice& o : report.overridden) {
            out << "  - " << nounFor(o.type) << " (" << o.field << ")\n";
        }
        out << "\n";
    }

    if (!report.undetermined.empty()) {
        out << report.undetermined.size()
            << " element(s) the merged dimensions do not place — started from either version, "
               "the solve puts them somewhere else, so you must decide the shape:\n";
        for (const OpenPosition& open : report.undetermined) {
            out << "  - " << nounFor(open.type) << "\n";
        }
        out << "\n";
    }

    // Compile verdict — the CAD-specific outcome with no text analogy.
    out << "Does the merged sketch hold together? ";
    if (report.regen.solverStatus == 0 && !report.regen.hasConflicts) {
        out << "YES — it solves cleanly (" << report.regen.dof << " degree(s) of freedom remain).";
    }
    else {
        out << "NO — it does not compile:";
        if (report.regen.hasConflicts) {
            out << " conflicting constraints;";
        }
        if (report.regen.hasRedundancies) {
            out << " redundant constraints;";
        }
        if (report.regen.hasMalformed) {
            out << " malformed constraints;";
        }
        out << " (solver status " << report.regen.solverStatus << ").";
    }
    if (!report.regen.fullyRealized) {
        out << "\n(Note: some elements could not be rebuilt from the merged recipe.)";
    }
    out << "\n";

    return out.str();
}
