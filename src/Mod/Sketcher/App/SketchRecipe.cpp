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
# include <cctype>
# include <cstdlib>
# include <iomanip>
# include <limits>
# include <locale>
# include <map>
# include <memory>
# include <sstream>

# include <boost/uuid/uuid.hpp>
# include <boost/uuid/uuid_io.hpp>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/RecipeDetail.h>
#include <Mod/Part/App/Geometry.h>

#include "SketchRecipe.h"

#include "Constraint.h"
#include "GeometryFacade.h"

using namespace Sketcher;

namespace
{

std::string tagToString(const boost::uuids::uuid& tag)
{
    return boost::uuids::to_string(tag);
}

/// Full-precision, locale-free rendering of a double, so a field changes iff the authored
/// value changes (display formatting would round distinct values into one string).
std::string canonicalNumber(double value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return oss.str();
}

/// The live geometry carrying `tag` among the seed sources, or nullptr. The recipe holds no
/// coordinates (§4); a surviving entity's starting coordinates are pulled from a branch that
/// still has it, keyed by the same durable tag the merge reasoned over.
const Part::Geometry* findSeedGeometry(
    const std::string& tag,
    const std::vector<const SketchObject*>& seedSources
)
{
    for (const SketchObject* source : seedSources) {
        if (source == nullptr) {
            continue;
        }
        for (const Part::Geometry* geo : source->getInternalGeometry()) {
            if (tagToString(geo->getTag()) == tag) {
                return geo;
            }
        }
    }
    return nullptr;
}

/// Reverse of Constraint::typeToString — the recipe stores the authored type name, never the
/// enum value, so a renumbering of the enum cannot silently re-map a stored recipe.
ConstraintType constraintTypeFromString(const std::string& name)
{
    for (int t = 0; t < ConstraintType::NumConstraintTypes; ++t) {
        const auto type = static_cast<ConstraintType>(t);
        if (Constraint::typeToString(type) == name) {
            return type;
        }
    }
    return ConstraintType::None;
}

/// Parse a recipe value field ("40 mm", "40") to a literal datum. A bound expression is not
/// re-evaluated here (a named deferral, §4); it parses to 0 and the constraint regenerates with
/// a placeholder datum.
double parseDatum(const std::string& value)
{
    return std::strtod(value.c_str(), nullptr);
}

/// The authored value of a constraint, or empty if the constraint carries no authored value
/// (non-dimensional, or a derived non-driving reference dimension). A bound expression is the
/// authored thing (a resolved-value diff would lie); otherwise a unit-typed literal.
std::string authoredValue(const SketchObject& sketch, const Constraint* constraint, int constNum)
{
    if (!constraint->isDimensional()) {
        return {};
    }
    if (!constraint->isDriving) {
        // A reference dimension's value is solver output — derived, not authored.
        return {};
    }
    const std::string expr = sketch.getConstraintExpression(constNum);
    if (!expr.empty()) {
        return expr;
    }
    const Base::Quantity quantity = constraint->getPresentationValue();
    const std::string unit = quantity.getUnit().getString();
    std::string literal = canonicalNumber(quantity.getValue());
    if (!unit.empty()) {
        literal += " " + unit;
    }
    return literal;
}


/// The two authored coordinates a person reads a sketch by. The merge deliberately does not
/// carry these (DESIGN §4 treats an undimensioned position as a regenerable seed, which is
/// right for reconciling two people's edits and wrong for one person asking what moved), so
/// they are added here, on the view's side of that line, and nowhere else.
std::string canonicalPoint(const Base::Vector3d& point)
{
    return canonicalNumber(point.x) + " " + canonicalNumber(point.y);
}

/// The reader's name for a geometry type: "Part::GeomLineSegment" is the factory key, "LineSegment"
/// is what the entity is. A view may rename what it shows; the recipe node keeps the real type,
/// which is what a merge report has to quote.
std::string readableGeometryType(const std::string& typeName)
{
    const std::string::size_type sep = typeName.rfind("::");
    std::string leaf = sep == std::string::npos ? typeName : typeName.substr(sep + 2);
    if (leaf.rfind("Geom", 0) == 0) {
        leaf = leaf.substr(4);
    }
    return leaf;
}

void addAuthoredCoordinates(const Part::Geometry* geo, App::RecipeNode& node)
{
    // Most-derived first: an arc of a circle is not a circle in the type system, but an
    // ellipse's arc does derive from its conic, so the trimmed forms are matched before the
    // whole ones either way.
    if (const auto* arc = dynamic_cast<const Part::GeomArcOfCircle*>(geo)) {
        double first = 0.0;
        double last = 0.0;
        arc->getRange(first, last, true);
        node.fields["center"] = canonicalPoint(arc->getCenter());
        node.fields["radius"] = canonicalNumber(arc->getRadius());
        node.fields["range"] = canonicalNumber(first) + " " + canonicalNumber(last);
        return;
    }
    if (const auto* circle = dynamic_cast<const Part::GeomCircle*>(geo)) {
        node.fields["center"] = canonicalPoint(circle->getCenter());
        node.fields["radius"] = canonicalNumber(circle->getRadius());
        return;
    }
    if (const auto* ellipse = dynamic_cast<const Part::GeomEllipse*>(geo)) {
        node.fields["center"] = canonicalPoint(ellipse->getCenter());
        node.fields["radius"] = canonicalNumber(ellipse->getMajorRadius()) + " x "
            + canonicalNumber(ellipse->getMinorRadius());
        return;
    }
    if (const auto* line = dynamic_cast<const Part::GeomLineSegment*>(geo)) {
        node.fields["from"] = canonicalPoint(line->getStartPoint());
        node.fields["to"] = canonicalPoint(line->getEndPoint());
        return;
    }
    if (const auto* point = dynamic_cast<const Part::GeomPoint*>(geo)) {
        node.fields["at"] = canonicalPoint(point->getPoint());
        return;
    }
    if (const auto* spline = dynamic_cast<const Part::GeomBSplineCurve*>(geo)) {
        // A control point list is too long to read on one line; its size is the fact that
        // tells a reader the curve was rebuilt rather than nudged.
        node.fields["poles"] = std::to_string(spline->countPoles());
        node.fields["from"] = canonicalPoint(spline->getStartPoint());
        node.fields["to"] = canonicalPoint(spline->getEndPoint());
        return;
    }
}

}  // namespace

SketchRecipe Sketcher::emitSketchRecipe(const SketchObject& sketch)
{
    SketchRecipe recipe;

    // Geometry: one node per internal entity, keyed by its durable Part::Geometry Tag. The
    // GeoId (== list index) is a readable position, never the identity, so a renumber is
    // invisible to the merge. Coordinates are regenerable seeds (DESIGN §4), not emitted.
    const std::vector<Part::Geometry*>& geometry = sketch.getInternalGeometry();
    std::map<int, std::string> geoIdToTag;
    for (int geoId = 0; geoId < static_cast<int>(geometry.size()); ++geoId) {
        const Part::Geometry* geo = geometry[geoId];
        const std::string id = tagToString(geo->getTag());
        geoIdToTag[geoId] = id;

        App::RecipeNode node;
        node.id = id;
        node.type = geo->getTypeId().getName();
        node.fields["construction"] = GeometryFacade::getConstruction(geo) ? "true" : "false";
        recipe.geometry[id] = std::move(node);
    }

    // Constraints: one node per constraint, keyed by its own durable Tag. References address
    // geometry by the geometry's durable Tag (never positional GeoId); refs to sentinels with
    // no durable identity (axes, external geometry) are simply not emitted.
    const std::vector<Constraint*>& constraints = sketch.Constraints.getValues();
    for (int constNum = 0; constNum < static_cast<int>(constraints.size()); ++constNum) {
        const Constraint* constraint = constraints[constNum];

        App::RecipeNode node;
        node.id = tagToString(constraint->getTag());
        node.type = constraint->typeToString();

        const std::string value = authoredValue(sketch, constraint, constNum);
        if (!value.empty()) {
            node.fields["value"] = value;
        }

        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int geoId = constraint->getGeoId(static_cast<int>(i));
            const auto it = geoIdToTag.find(geoId);
            if (it == geoIdToTag.end()) {
                continue;  // axis / external / undefined — no durable identity to reference
            }
            App::RecipeRef ref;
            ref.target = it->second;
            ref.pos = constraint->getPosIdAsInt(static_cast<int>(i));
            node.refs.push_back(ref);
        }

        recipe.constraints[node.id] = std::move(node);
    }

    return recipe;
}

RegenResult Sketcher::regenerateSketch(
    SketchObject& target,
    const SketchRecipe& recipe,
    const std::vector<const SketchObject*>& seedSources
)
{
    RegenResult result;

    // Geometry: materialize each surviving entity from its seed coordinates, keeping a map from
    // the recipe's durable tag to the fresh GeoId so constraints can rebind by identity.
    std::map<std::string, int> tagToGeoId;
    for (const auto& [tag, node] : recipe.geometry) {
        const Part::Geometry* seed = findSeedGeometry(tag, seedSources);
        if (seed == nullptr) {
            result.fullyRealized = false;  // no seed for a surviving entity — cannot place it
            continue;
        }
        const auto it = node.fields.find("construction");
        const bool construction = it != node.fields.end() && it->second == "true";
        tagToGeoId[tag] = target.addGeometry(seed, construction);
    }

    // Constraints: rebuild each from its authored type, literal datum, and refs-by-tag. A ref to
    // an entity no seed supplied (dropped by the merge, or an un-emitted axis/external) means the
    // constraint cannot be placed honestly, so it is skipped and the result flagged.
    for (const auto& [tag, node] : recipe.constraints) {
        const ConstraintType type = constraintTypeFromString(node.type);
        if (type == ConstraintType::None) {
            result.fullyRealized = false;
            continue;
        }

        auto constraint = std::make_unique<Constraint>();
        constraint->Type = type;
        const auto valueIt = node.fields.find("value");
        if (valueIt != node.fields.end()) {
            constraint->setValue(parseDatum(valueIt->second));
        }

        bool placeable = true;
        for (size_t i = 0; i < node.refs.size(); ++i) {
            const auto geoIt = tagToGeoId.find(node.refs[i].target);
            if (geoIt == tagToGeoId.end()) {
                placeable = false;
                break;
            }
            constraint->setElement(
                static_cast<int>(i),
                GeoElementId(geoIt->second, static_cast<PointPos>(node.refs[i].pos))
            );
        }
        if (!placeable) {
            result.fullyRealized = false;
            continue;
        }

        target.addConstraint(std::move(constraint));
    }

    // Regenerate = the existing solver. Its verdict is the CAD "does the merge compile?".
    result.solverStatus = target.solve();
    result.hasConflicts = target.getLastHasConflicts();
    result.hasRedundancies = target.getLastHasRedundancies();
    result.hasMalformed = target.getLastHasMalformedConstraints();
    result.dof = target.getLastDoF();
    return result;
}

MergeReport Sketcher::mergeSketches(
    const SketchObject& ancestor,
    const SketchObject& branchA,
    const SketchObject& branchB
)
{
    MergeReport report;

    const SketchRecipe base = emitSketchRecipe(ancestor);
    const SketchRecipe a = emitSketchRecipe(branchA);
    const SketchRecipe b = emitSketchRecipe(branchB);

    // Merge each section by durable identity (renumber-invisible); value conflicts accumulate.
    report.mergedGeometry
        = App::RecipeMerge::threeWay(base.geometry, a.geometry, b.geometry, report.conflicts);
    report.mergedConstraints
        = App::RecipeMerge::threeWay(base.constraints, a.constraints, b.constraints, report.conflicts);

    // Turn any dangling reference into a §4.7 honest-retirement outcome (Drop erases the node;
    // StopAsk keeps it for the user; Carry is not reported).
    report.resolutions
        = App::RecipeMerge::resolveReferences(report.mergedConstraints, report.mergedGeometry);

    // Regenerate onto a hidden, throwaway scratch sketch and re-solve: the merge is text until the
    // kernel runs, and only the solver reveals a clean merge that does not compile.
    App::DocumentInitFlags flags;
    flags.createView = false;
    flags.temporary = true;
    App::Document* scratch = App::GetApplication().newDocument("recipeMergeScratch", nullptr, flags);
    auto* target = static_cast<SketchObject*>(
        scratch->addObject("Sketcher::SketchObject", "MergeScratch")
    );
    const SketchRecipe merged {report.mergedGeometry, report.mergedConstraints};
    report.regen = regenerateSketch(*target, merged, {&branchA, &branchB});
    App::GetApplication().closeDocument(scratch);

    return report;
}

namespace
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
/// the value-conflict / stop-and-ask lines where the node (and its targets) remain in the merge.
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

}  // namespace

std::string Sketcher::formatMergeReport(const MergeReport& report)
{
    std::ostringstream out;
    out << "Sketch merge report\n";
    out << "===================\n\n";
    out << "Combined: " << report.mergedGeometry.size() << " geometry element(s), "
        << report.mergedConstraints.size() << " constraint(s).\n\n";

    // Value conflicts — both edits changed the same authored thing in different ways.
    if (report.conflicts.empty()) {
        out << "No value conflicts: the two edits never changed the same thing differently.\n\n";
    }
    else {
        out << report.conflicts.size()
            << " value conflict(s) — both versions changed the same thing differently, so you "
               "must choose:\n";
        for (const App::MergeConflict& c : report.conflicts) {
            out << "  - " << nounFor(c.type) << describeTargets(c.id, report) << "\n";
        }
        out << "\n";
    }

    // Reference resolutions — dropped-with-disclosure and stop-and-ask.
    std::vector<const App::RefResolution*> drops;
    std::vector<const App::RefResolution*> asks;
    for (const App::RefResolution& r : report.resolutions) {
        if (r.outcome == App::RefResolution::Outcome::Drop) {
            drops.push_back(&r);
        }
        else if (r.outcome == App::RefResolution::Outcome::StopAsk) {
            asks.push_back(&r);
        }
    }
    if (drops.empty() && asks.empty()) {
        out << "No dangling references: every constraint still points at geometry that "
               "survived.\n\n";
    }
    if (!drops.empty()) {
        out << drops.size()
            << " constraint(s) removed — the geometry they described is gone from the merged "
               "result, so nothing was left to hold them:\n";
        for (const App::RefResolution* r : drops) {
            out << "  - " << nounFor(r->type) << "\n";
        }
        out << "\n";
    }
    if (!asks.empty()) {
        out << asks.size()
            << " constraint(s) need your decision — they point at deleted geometry while a related "
               "element survives, so where they should attach is your call:\n";
        for (const App::RefResolution* r : asks) {
            out << "  - " << nounFor(r->type) << describeTargets(r->id, report) << "\n";
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


App::RecipeDetail Sketcher::sketchRecipeDetail(const App::DocumentObject& obj)
{
    App::RecipeDetail detail;

    const auto* sketch = dynamic_cast<const SketchObject*>(&obj);
    if (sketch == nullptr) {
        return detail;
    }

    // The two properties the generic emitter can only see as opaque values. Claimed here, so
    // the view stops reporting them missing now that it prints what is in them.
    detail.coveredProperties = {"Geometry", "Constraints"};

    const SketchRecipe recipe = emitSketchRecipe(*sketch);

    // Walked in the sketch's own order rather than the recipe's id order: the merge is keyed by
    // durable tag and indifferent to sequence, but a person reads a sketch in the order it was
    // drawn.
    App::RecipeDetailSection geometry;
    geometry.name = "geometry";
    const std::vector<Part::Geometry*>& internals = sketch->getInternalGeometry();
    for (const Part::Geometry* geo : internals) {
        const auto found = recipe.geometry.find(tagToString(geo->getTag()));
        if (found == recipe.geometry.end()) {
            continue;
        }
        App::RecipeNode node = found->second;
        node.type = readableGeometryType(node.type);
        addAuthoredCoordinates(geo, node);
        geometry.nodes.push_back(std::move(node));
    }

    App::RecipeDetailSection constraints;
    constraints.name = "constraints";
    for (const Constraint* constraint : sketch->Constraints.getValues()) {
        const auto found = recipe.constraints.find(tagToString(constraint->getTag()));
        if (found == recipe.constraints.end()) {
            continue;
        }
        App::RecipeNode node = found->second;
        if (!constraint->Name.empty()) {
            // A named constraint is the handle an expression binds to, so the name is authored
            // content and a rename is a real change to the model.
            node.fields["name"] = constraint->Name;
        }
        if (!constraint->isDriving) {
            node.fields["driving"] = "false";
        }
        constraints.nodes.push_back(std::move(node));
    }

    detail.sections.push_back(std::move(geometry));
    detail.sections.push_back(std::move(constraints));

    return detail;
}

void Sketcher::registerSketchRecipeDetail()
{
    App::registerRecipeDetail(SketchObject::getClassTypeId(), &sketchRecipeDetail);
}
