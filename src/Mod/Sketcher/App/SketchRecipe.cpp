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

#include "Constraint.h"
#include "ExternalGeometryFacade.h"
#include "GeoEnum.h"
#include "GeometryFacade.h"
#include "SolverGeometryExtension.h"

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

/// The sketch's own frame -- the origin and the two axes. Every sketch has them, so a reference to
/// one is carried by name and can never dangle.
const std::map<std::string, int>& frameGeoIds()
{
    static const std::map<std::string, int> frame {
        {"RootPoint", GeoEnum::RtPnt},
        {"H_Axis", GeoEnum::HAxis},
        {"V_Axis", GeoEnum::VAxis},
    };
    return frame;
}

/// The frame name of a constraint element, or empty when it is not part of the frame. The origin
/// and the horizontal axis share a GeoId; only the point selector tells them apart.
std::string frameName(int geoId, int pos)
{
    if (geoId == GeoEnum::RtPnt && pos != static_cast<int>(PointPos::none)) {
        return "RootPoint";
    }
    if (geoId == GeoEnum::HAxis) {
        return "H_Axis";
    }
    if (geoId == GeoEnum::VAxis) {
        return "V_Axis";
    }
    return {};
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

/// The constraint holding `tag` in the first seed source that has one, or null.
const Constraint* findSeedConstraint(
    const std::string& tag,
    const std::vector<const SketchObject*>& seedSources
)
{
    for (const SketchObject* source : seedSources) {
        if (source == nullptr) {
            continue;
        }
        for (const Constraint* constraint : source->Constraints.getValues()) {
            if (tagToString(constraint->getTag()) == tag) {
                return constraint;
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

/// Reverse of Constraint::internalAlignmentTypeToString, on the same reasoning: the recipe
/// stores the authored role name, so a renumbering of the enum cannot silently turn a major
/// axis into a focus.
InternalAlignmentType alignmentTypeFromString(const std::string& name)
{
    for (int t = 0; t < InternalAlignmentType::NumInternalAlignmentType; ++t) {
        const auto role = static_cast<InternalAlignmentType>(t);
        if (Constraint::internalAlignmentTypeToString(role) == name) {
            return role;
        }
    }
    return InternalAlignmentType::Undef;
}

/// The datum a constraint's recipe value gives. A formula is taken as what it gave in the sides
/// that state it, each read in its own document; nullopt when two disagree, since the merged
/// document they would agree in does not exist yet. Anything else is a literal ("40 mm").
std::optional<double> datumFor(
    const std::string& tag,
    const std::string& value,
    const std::vector<const SketchObject*>& seedSources
)
{
    std::optional<double> given;
    for (const SketchObject* source : seedSources) {
        if (source == nullptr) {
            continue;
        }
        const std::vector<Constraint*>& constraints = source->Constraints.getValues();
        for (std::size_t i = 0; i < constraints.size(); ++i) {
            if (tagToString(constraints[i]->getTag()) != tag
                || source->getConstraintExpression(static_cast<int>(i)) != value) {
                continue;
            }
            if (given && *given != constraints[i]->getValue()) {
                return std::nullopt;
            }
            given = constraints[i]->getValue();
        }
    }
    return given ? given : std::strtod(value.c_str(), nullptr);
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


/// A coordinate as a person reads it. Deliberately *not* canonicalNumber: a coordinate is where
/// the solver put something, not a value anyone stated, and at seventeen digits an edit to one
/// dimension buries itself under the drift of every point that moved with it. Authored values --
/// a dimension's datum, a bound expression -- keep full precision, because those are statements.
///
/// The cost is real and accepted: two positions closer together than this round to one string,
/// so a sub-micron move goes unreported in the view. The document still holds both, and the
/// merge, which never sees coordinates at all, is unaffected.
std::string displayNumber(double value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(4) << value;

    std::string text = oss.str();
    const std::string::size_type lastKept = text.find_last_not_of('0');
    if (text.find('.') != std::string::npos && lastKept != std::string::npos) {
        text.erase(text[lastKept] == '.' ? lastKept : lastKept + 1);
    }
    if (text == "-0") {
        text = "0";
    }

    return text;
}

/// An angle as a person reads it: degrees, the unit the sketch's own editor shows, rounded the
/// same way a coordinate is. The kernel keeps radians; a file nobody can read in radians is a
/// file nobody checks.
std::string displayAngle(double radians)
{
    return displayNumber(Base::toDegrees(radians));
}

/// The two coordinates a person reads a sketch by. The merge deliberately does not carry these
/// (DESIGN §4 treats an undimensioned position as a regenerable seed, which is right for
/// reconciling two people's edits and wrong for one person asking what moved), so they are
/// added here, on the view's side of that line, and nowhere else -- rounded, per displayNumber,
/// because where the solver put a point is not a statement anyone made.
std::string canonicalPoint(const Base::Vector3d& point)
{
    return displayNumber(point.x) + " " + displayNumber(point.y);
}

/// A run of points on one line, each in brackets so the pairs stay apart: "(1 10) (4 14)".
std::string canonicalPointList(const std::vector<Base::Vector3d>& points)
{
    std::string out;
    for (const Base::Vector3d& point : points) {
        if (!out.empty()) {
            out += " ";
        }
        out += "(" + canonicalPoint(point) + ")";
    }
    return out;
}

/// A run of numbers on one line, rounded as a reader reads them.
std::string canonicalNumberList(const std::vector<double>& values)
{
    std::string out;
    for (const double value : values) {
        if (!out.empty()) {
            out += " ";
        }
        out += displayNumber(value);
    }
    return out;
}

/// A run of whole numbers on one line; no rounding is involved in a count.
std::string canonicalIntegerList(const std::vector<int>& values)
{
    std::string out;
    for (const int value : values) {
        if (!out.empty()) {
            out += " ";
        }
        out += std::to_string(value);
    }
    return out;
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

/// The turn of a conic's own axes within the sketch, always stated. Which way a conic faces is
/// half of what it is -- an ellipse on its side is a different feature from an upright one --
/// and unlike a line's construction flag, a conic is a rare entity, so the field costs a reader
/// almost nothing. An arc of a circle is deliberately not given one: its range already arrives
/// with the frame's rotation folded in, so a second number would say the same thing twice.
void addConicOrientation(double angleXU, App::RecipeNode& node)
{
    node.fields["angle"] = displayAngle(angleXU);
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
        node.fields["radius"] = displayNumber(arc->getRadius());
        node.fields["range"] = displayAngle(first) + " " + displayAngle(last);
        return;
    }
    if (const auto* arc = dynamic_cast<const Part::GeomArcOfEllipse*>(geo)) {
        double first = 0.0;
        double last = 0.0;
        arc->getRange(first, last, true);
        node.fields["center"] = canonicalPoint(arc->getCenter());
        node.fields["radius"] = displayNumber(arc->getMajorRadius()) + " x "
            + displayNumber(arc->getMinorRadius());
        node.fields["range"] = displayAngle(first) + " " + displayAngle(last);
        addConicOrientation(arc->getAngleXU(), node);
        return;
    }
    if (const auto* arc = dynamic_cast<const Part::GeomArcOfHyperbola*>(geo)) {
        double first = 0.0;
        double last = 0.0;
        arc->getRange(first, last, true);
        node.fields["center"] = canonicalPoint(arc->getCenter());
        node.fields["radius"] = displayNumber(arc->getMajorRadius()) + " x "
            + displayNumber(arc->getMinorRadius());
        // A hyperbola and a parabola are not swept by an angle: the trim bounds are
        // positions along the curve's own parameter, so they stay bare numbers.
        node.fields["range"] = displayNumber(first) + " " + displayNumber(last);
        addConicOrientation(arc->getAngleXU(), node);
        return;
    }
    if (const auto* arc = dynamic_cast<const Part::GeomArcOfParabola*>(geo)) {
        double first = 0.0;
        double last = 0.0;
        arc->getRange(first, last, true);
        node.fields["center"] = canonicalPoint(arc->getCenter());
        // A parabola has one shape number, not two: the focal distance is the whole of it.
        node.fields["focal"] = displayNumber(arc->getFocal());
        // A hyperbola and a parabola are not swept by an angle: the trim bounds are
        // positions along the curve's own parameter, so they stay bare numbers.
        node.fields["range"] = displayNumber(first) + " " + displayNumber(last);
        addConicOrientation(arc->getAngleXU(), node);
        return;
    }
    if (const auto* circle = dynamic_cast<const Part::GeomCircle*>(geo)) {
        node.fields["center"] = canonicalPoint(circle->getCenter());
        node.fields["radius"] = displayNumber(circle->getRadius());
        return;
    }
    if (const auto* ellipse = dynamic_cast<const Part::GeomEllipse*>(geo)) {
        node.fields["center"] = canonicalPoint(ellipse->getCenter());
        node.fields["radius"] = displayNumber(ellipse->getMajorRadius()) + " x "
            + displayNumber(ellipse->getMinorRadius());
        addConicOrientation(ellipse->getAngleXU(), node);
        return;
    }
    if (const auto* hyperbola = dynamic_cast<const Part::GeomHyperbola*>(geo)) {
        node.fields["center"] = canonicalPoint(hyperbola->getCenter());
        node.fields["radius"] = displayNumber(hyperbola->getMajorRadius()) + " x "
            + displayNumber(hyperbola->getMinorRadius());
        addConicOrientation(hyperbola->getAngleXU(), node);
        return;
    }
    if (const auto* parabola = dynamic_cast<const Part::GeomParabola*>(geo)) {
        node.fields["center"] = canonicalPoint(parabola->getCenter());
        node.fields["focal"] = displayNumber(parabola->getFocal());
        addConicOrientation(parabola->getAngleXU(), node);
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
        // The control points themselves, not a count of them. A count says the curve was
        // rebuilt; it cannot see a single pole being nudged, which is how a spline is actually
        // edited. A spline is a long line in the file because a spline is a long thing.
        node.fields["degree"] = std::to_string(spline->getDegree());
        node.fields["poles"] = canonicalPointList(spline->getPoles());
        if (spline->isPeriodic()) {
            node.fields["periodic"] = "true";
        }
        if (spline->isRational()) {
            // Only a rational curve has weights worth stating; on every other one they are all
            // the same number.
            node.fields["weights"] = canonicalNumberList(spline->getWeights());
        }
        // Degree and pole count do not pin down the curve on their own: the knots are where
        // its pieces meet, and the multiplicities are how sharply.
        node.fields["knots"] = canonicalNumberList(spline->getKnots());
        node.fields["multiplicities"] = canonicalIntegerList(spline->getMultiplicities());
        return;
    }
}

struct Borrowed
{
    int geoId;
    std::string id;
    const Part::Geometry* geometry;
};

/// Each piece of geometry the sketch borrows, by an identity that survives a branch: the durable
/// id of the object it comes from, the picked element's durable name, and which of the curves
/// that element projects to it is. The sketch's own two axes borrow nothing and are left out.
std::vector<Borrowed> borrowedGeometry(const SketchObject& sketch)
{
    // The sketch stamps each projected curve with the owner's name and the element's mapped
    // name; a name is not an identity, so it is translated to the owner's durable id here.
    std::map<std::string, std::string> durableByRef;
    const std::vector<App::DocumentObject*>& objects = sketch.ExternalGeometry.getValues();
    const std::vector<std::string>& subs = sketch.ExternalGeometry.getSubValues();
    const std::vector<App::PropertyLinkBase::ShadowSub>& shadows
        = sketch.ExternalGeometry.getShadowSubs();
    for (std::size_t i = 0; i < objects.size() && i < subs.size(); ++i) {
        if (objects[i] == nullptr || objects[i]->getNameInDocument() == nullptr) {
            continue;
        }
        const bool mapped = i < shadows.size() && !shadows[i].newName.empty();
        const std::string element = Data::newElementName(
            (mapped ? shadows[i].newName : subs[i]).c_str()
        );
        durableByRef[std::string(objects[i]->getNameInDocument()) + "." + element]
            = objects[i]->Uid.getValueStr() + "." + element;
    }

    std::vector<Borrowed> borrowed;
    std::map<std::string, int> curvesOfRef;
    const std::vector<Part::Geometry*>& external = sketch.getExternalGeometry();
    for (int index = 0; index < static_cast<int>(external.size()); ++index) {
        const std::string ref = ExternalGeometryFacade::getFacade(external[index])->getRef();
        if (ref.empty()) {
            continue;
        }
        const auto found = durableByRef.find(ref);
        const std::string owner = found != durableByRef.end() ? found->second : ref;
        borrowed.push_back(
            {-index - 1, "borrowed:" + owner + "#" + std::to_string(curvesOfRef[ref]++), external[index]}
        );
    }
    return borrowed;
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

    for (const Borrowed& piece : borrowedGeometry(sketch)) {
        geoIdToTag[piece.geoId] = piece.id;
        App::RecipeNode node;
        node.id = piece.id;
        node.type = piece.geometry->getTypeId().getName();
        recipe.external[piece.id] = std::move(node);
    }

    // Constraints: one node per constraint, keyed by its own durable Tag. References address
    // geometry by the geometry's durable Tag (never positional GeoId), and the sketch's frame by
    // name, and borrowed geometry by where it was borrowed from.
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

        // An internal alignment is one constraint type doing eleven different jobs: without the
        // role, an ellipse's major axis, its minor axis and its two foci are four identical
        // lines, and a rebuild cannot tell them apart either.
        if (constraint->Type == InternalAlignment) {
            node.fields["role"] = constraint->internalAlignmentTypeToString();
            if (constraint->InternalAlignmentIndex >= 0) {
                // Which pole or knot of a spline this one is; the role alone does not say.
                node.fields["index"] = std::to_string(constraint->InternalAlignmentIndex);
            }
        }

        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int geoId = constraint->getGeoId(static_cast<int>(i));
            const int pos = constraint->getPosIdAsInt(static_cast<int>(i));
            const auto it = geoIdToTag.find(geoId);
            App::RecipeRef ref;
            ref.target = it != geoIdToTag.end() ? it->second : frameName(geoId, pos);
            if (ref.target.empty()) {
                continue;  // an unused element place of the constraint
            }
            ref.pos = pos;
            node.refs.push_back(ref);
        }

        recipe.constraints[node.id] = std::move(node);
    }

    return recipe;
}

RegenResult Sketcher::regenerateSketch(
    SketchObject& target,
    const SketchRecipe& recipe,
    const std::vector<const SketchObject*>& seedSources,
    const std::map<std::string, const SketchObject*>& seedFor,
    const std::map<std::string, const Part::Geometry*>& seedGeometry
)
{
    RegenResult result;

    // Geometry: materialize each surviving entity from its seed coordinates, keeping a map from
    // the recipe's durable tag to the fresh GeoId so constraints can rebind by identity.
    std::map<std::string, int> tagToGeoId;
    for (const auto& [tag, node] : recipe.geometry) {
        const auto chosen = seedFor.find(tag);
        const auto given = seedGeometry.find(tag);
        const Part::Geometry* seed = given != seedGeometry.end() ? given->second
            : chosen != seedFor.end() ? findSeedGeometry(tag, {chosen->second})
                                      : findSeedGeometry(tag, seedSources);
        if (seed == nullptr) {
            result.fullyRealized = false;  // no seed for a surviving entity — cannot place it
            continue;
        }
        const auto it = node.fields.find("construction");
        const bool construction = it != node.fields.end() && it->second == "true";
        const int geoId = target.addGeometry(seed, construction);
        // The entity is the one the recipe names, so it keeps that identity (§4.7).
        target.Geometry.getValues()[geoId]->copyTagFrom(seed);
        tagToGeoId[tag] = geoId;
    }
    result.placed = tagToGeoId;

    // Borrowed geometry: the scratch sketch has nothing to borrow through, so each piece stands in
    // as construction geometry the solver may not move, which is what borrowed geometry is to it.
    for (const auto& [id, node] : recipe.external) {
        const Part::Geometry* seed = nullptr;
        for (const SketchObject* source : seedSources) {
            for (const Borrowed& piece :
                 source != nullptr ? borrowedGeometry(*source) : std::vector<Borrowed> {}) {
                if (piece.id == id) {
                    seed = piece.geometry;
                }
            }
            if (seed != nullptr) {
                break;
            }
        }
        if (seed == nullptr) {
            result.fullyRealized = false;
            continue;
        }
        const int geoId = target.addGeometry(seed, /*construction=*/true);
        Constraint fixed;
        fixed.Type = Block;
        fixed.setElement(0, GeoElementId(geoId, PointPos::none));
        target.addConstraint(&fixed);
        tagToGeoId[id] = geoId;
    }

    // Constraints: rebuild each from its authored type, literal datum, and refs-by-tag. A ref to
    // an entity no seed supplied (dropped by the merge) means the constraint cannot be placed
    // honestly, so it is skipped and the result flagged.
    for (const auto& [tag, node] : recipe.constraints) {
        const ConstraintType type = constraintTypeFromString(node.type);
        if (type == ConstraintType::None) {
            result.fullyRealized = false;
            continue;
        }

        // Started from the side's own constraint where one holds this tag, so it keeps its
        // identity and name; everything the recipe states is then set from the recipe.
        const Constraint* seedConstraint = findSeedConstraint(tag, seedSources);
        auto constraint = seedConstraint != nullptr
            ? std::unique_ptr<Constraint>(seedConstraint->clone())
            : std::make_unique<Constraint>();
        for (std::size_t slot = 0; slot < constraint->getElementsSize(); ++slot) {
            constraint->setElement(slot, GeoElementId());
        }
        constraint->truncateElements(0);
        constraint->Type = type;
        const auto valueIt = node.fields.find("value");
        if (valueIt != node.fields.end()) {
            const std::optional<double> datum = datumFor(tag, valueIt->second, seedSources);
            if (!datum) {
                result.fullyRealized = false;
            }
            constraint->setValue(datum.value_or(0.0));
        }

        const auto roleIt = node.fields.find("role");
        if (roleIt != node.fields.end()) {
            constraint->AlignmentType = alignmentTypeFromString(roleIt->second);
        }
        const auto indexIt = node.fields.find("index");
        if (indexIt != node.fields.end()) {
            constraint->InternalAlignmentIndex = std::atoi(indexIt->second.c_str());
        }

        bool placeable = true;
        for (size_t i = 0; i < node.refs.size(); ++i) {
            const std::string& target = node.refs[i].target;
            const auto geoIt = tagToGeoId.find(target);
            const auto frameIt = frameGeoIds().find(target);
            if (geoIt == tagToGeoId.end() && frameIt == frameGeoIds().end()) {
                placeable = false;
                break;
            }
            const int geoId = geoIt != tagToGeoId.end() ? geoIt->second : frameIt->second;
            constraint->setElement(
                static_cast<int>(i),
                GeoElementId(geoId, static_cast<PointPos>(node.refs[i].pos))
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

}  // namespace

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


namespace
{

/// The name of a sketch element that carries no durable tag of its own: the sketch's own axes
/// and root point, and an edge borrowed from another object. The merge cannot bind any of these
/// -- they are not entities of this sketch -- and so drops them, which left the view saying
/// "coincident" without ever saying with what. A person reading the file needs the other half
/// of the sentence, so the view names them. Returns empty for an element the sketch cannot
/// account for at all.
/// The readable name of every borrowed element, keyed by the reference the sketch stamps on the
/// projected curve. That stored reference carries the shape's mapped element name -- a hash no
/// one can read, which says nothing about what was picked -- while the link the person actually
/// made still holds "Pad" and "Edge1". Both are built from the same two halves, so the readable
/// one can be found by the other.
std::map<std::string, std::string> readableBorrowedNames(const SketchObject& sketch)
{
    std::map<std::string, std::string> names;

    const std::vector<App::DocumentObject*>& objects = sketch.ExternalGeometry.getValues();
    const std::vector<std::string>& subs = sketch.ExternalGeometry.getSubValues();
    const std::vector<App::PropertyLinkBase::ShadowSub>& shadows
        = sketch.ExternalGeometry.getShadowSubs();

    for (std::size_t i = 0; i < objects.size() && i < subs.size(); ++i) {
        if (objects[i] == nullptr || objects[i]->getNameInDocument() == nullptr) {
            continue;
        }
        const std::string owner = std::string(objects[i]->getNameInDocument()) + ".";
        const bool mapped = i < shadows.size() && !shadows[i].newName.empty();
        const std::string& sub = mapped ? shadows[i].newName : subs[i];

        // The same two halves the sketch itself puts together when it stamps a reference on the
        // projected curve, so the key here is the key there.
        const std::string plain = Data::oldElementName(subs[i].c_str());
        names[owner + Data::newElementName(sub.c_str())] = owner + (plain.empty() ? subs[i] : plain);
    }

    return names;
}

std::string borrowedElementName(
    const SketchObject& sketch,
    int geoId,
    const std::map<std::string, std::string>& readable
)
{
    if (geoId == GeoEnum::HAxis) {
        return "H_Axis";
    }
    if (geoId == GeoEnum::VAxis) {
        return "V_Axis";
    }
    // A negative GeoId is a position in the borrowed list, counting back from -1; anything
    // past its end is an unused constraint slot (a constraint carries a fixed number of
    // element places and leaves the spare ones undefined), which names nothing.
    const std::vector<Part::Geometry*>& borrowed = sketch.getExternalGeometry();
    const int index = -geoId - 1;
    if (index < 0 || index >= static_cast<int>(borrowed.size())) {
        return {};
    }

    const std::string reference = ExternalGeometryFacade::getFacade(borrowed[index])->getRef();
    const auto found = readable.find(reference);
    if (found != readable.end()) {
        return found->second;
    }
    // A reference the link list no longer accounts for: say so rather than print the hash.
    return reference.empty() ? std::string("external") : std::string("external (unresolved)");
}

/// The sketch's origin, addressed as the start point of the horizontal axis. The two share a
/// GeoId, so only the point selector tells them apart, and "RootPoint:1" would say the same
/// thing twice.
bool isRootPoint(int geoId, int pos)
{
    return geoId == GeoEnum::RtPnt && pos != static_cast<int>(PointPos::none);
}

}  // namespace

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
        // A flag left at its default is not something anyone did. The merge carries it on every
        // entity because it needs a value to rebuild with; the view prints it only where it
        // departs from ordinary geometry, the same way a constraint prints "driving" only when
        // the answer is no. Repeated once per line, an untouched flag would outnumber the
        // authored content of the sketch.
        const auto construction = node.fields.find("construction");
        if (construction != node.fields.end() && construction->second == "false") {
            node.fields.erase(construction);
        }
        addAuthoredCoordinates(geo, node);
        geometry.nodes.push_back(std::move(node));
    }

    // What the sketch borrows from elsewhere in the document. The link itself is already an
    // object-level reference, but that only names the object; which edge or face was picked is
    // the authored fact, and it lived nowhere in the view until now.
    App::RecipeDetailSection external;
    external.name = "external";
    const std::map<std::string, std::string> readable = readableBorrowedNames(*sketch);
    for (const Part::Geometry* geo : sketch->getExternalGeometry()) {
        const auto facade = ExternalGeometryFacade::getFacade(geo);
        if (facade->getRef().empty()) {
            continue;  // the sketch's own two axes, which are not borrowed from anywhere
        }
        App::RecipeNode node;
        const auto found = readable.find(facade->getRef());
        node.id = found != readable.end() ? found->second : facade->getRef();
        node.type = readableGeometryType(geo->getTypeId().getName());
        for (int flag = 0; flag < ExternalGeometryExtension::NumFlags; ++flag) {
            if (facade->testFlag(flag)) {
                // A reference the sketch has frozen, detached or lost is a state someone put it
                // in (or a breakage they need to see), never the ordinary case.
                node.fields[ExternalGeometryExtension::flag2str.at(flag)] = "true";
            }
        }
        external.nodes.push_back(std::move(node));
    }

    // The GeoId a constraint stores is a position in the sketch's own geometry list, so the
    // view needs the same index the merge built to turn one back into a durable tag.
    std::map<int, std::string> geoIdToTag;
    for (int geoId = 0; geoId < static_cast<int>(internals.size()); ++geoId) {
        geoIdToTag[geoId] = tagToString(internals[geoId]->getTag());
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

        // Rebuilt rather than inherited: the merge keeps only what it can bind by durable tag,
        // which silently dropped every reference to an axis, to the origin, or to a borrowed
        // edge -- so "point on object" arrived naming one side of a two-sided fact.
        node.refs.clear();
        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int element = static_cast<int>(i);
            const int geoId = constraint->getGeoId(element);
            const int pos = constraint->getPosIdAsInt(element);

            App::RecipeRef ref;
            const auto own = geoIdToTag.find(geoId);
            if (own != geoIdToTag.end()) {
                ref.target = own->second;
                ref.pos = pos;
            }
            else if (isRootPoint(geoId, pos)) {
                ref.target = "RootPoint";
            }
            else {
                ref.target = borrowedElementName(*sketch, geoId, readable);
                if (ref.target.empty()) {
                    continue;  // an element the sketch itself cannot account for
                }
                ref.pos = geoId <= GeoEnum::RefExt ? pos : 0;
            }
            node.refs.push_back(ref);
        }

        constraints.nodes.push_back(std::move(node));
    }

    detail.sections.push_back(std::move(geometry));
    if (!external.nodes.empty()) {
        detail.sections.push_back(std::move(external));
    }
    detail.sections.push_back(std::move(constraints));

    return detail;
}

namespace
{

/// The geometry named by a constraint that was added, removed or changed, on either side.
std::set<std::string> geometryOfChangedConstraints(
    const SketchObject& oldSketch,
    const SketchObject& newSketch
)
{
    const SketchRecipe was = emitSketchRecipe(oldSketch);
    const SketchRecipe now = emitSketchRecipe(newSketch);
    std::set<std::string> touched;
    for (const App::RecipeChange& change : App::RecipeDiff::twoWay(was.constraints, now.constraints)) {
        for (const App::RecipeSection* side : {&was.constraints, &now.constraints}) {
            const auto found = side->find(change.id);
            if (found == side->end()) {
                continue;
            }
            for (const App::RecipeRef& ref : found->second.refs) {
                touched.insert(ref.target);
            }
        }
    }
    return touched;
}

/// For each of the sketch's own geometry, by GeoId, one GeoId standing for everything its
/// constraints tie it to.
std::vector<int> connectedGroups(const SketchObject& sketch)
{
    std::vector<int> group(sketch.getInternalGeometry().size());
    std::iota(group.begin(), group.end(), 0);
    const auto root = [&group](int geoId) {
        while (group[geoId] != geoId) {
            group[geoId] = group[group[geoId]];
            geoId = group[geoId];
        }
        return geoId;
    };

    for (const Constraint* constraint : sketch.Constraints.getValues()) {
        int first = -1;
        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int geoId = constraint->getGeoId(static_cast<int>(i));
            if (geoId < 0 || geoId >= static_cast<int>(group.size())) {
                continue;  // an axis or borrowed edge holds things in place; it joins nothing
            }
            if (first < 0) {
                first = geoId;
            }
            else {
                group[root(geoId)] = root(first);
            }
        }
    }

    for (int geoId = 0; geoId < static_cast<int>(group.size()); ++geoId) {
        group[geoId] = root(geoId);
    }
    return group;
}

}  // namespace

App::RecipeConsequences Sketcher::sketchConsequences(
    const App::DocumentObject& before,
    const App::DocumentObject& after
)
{
    App::RecipeConsequences consequences;

    const auto* oldSketch = dynamic_cast<const SketchObject*>(&before);
    const auto* newSketch = dynamic_cast<const SketchObject*>(&after);
    if (oldSketch == nullptr || newSketch == nullptr) {
        return consequences;
    }

    std::map<std::string, const Part::Geometry*> oldByTag;
    for (const Part::Geometry* geo : oldSketch->getInternalGeometry()) {
        oldByTag[tagToString(geo->getTag())] = geo;
    }

    // Geometry tied to a changed constraint went where the solver put it, so its moves follow
    // from that change. Replaying the solve to tell a drag from a solver move cannot work: on
    // geometry the constraints do not fix, the solver lands in a different place from run to run.
    const std::set<std::string> touched = geometryOfChangedConstraints(*oldSketch, *newSketch);
    const std::vector<int> group = connectedGroups(*newSketch);
    const std::vector<Part::Geometry*>& internals = newSketch->getInternalGeometry();
    std::set<int> resolved;
    for (size_t i = 0; i < internals.size(); ++i) {
        if (touched.count(tagToString(internals[i]->getTag())) != 0) {
            resolved.insert(group[i]);
        }
    }

    for (size_t i = 0; i < internals.size(); ++i) {
        const std::string tag = tagToString(internals[i]->getTag());
        const auto found = oldByTag.find(tag);
        if (resolved.count(group[i]) == 0 || found == oldByTag.end()) {
            continue;
        }
        App::RecipeNode was;
        App::RecipeNode now;
        addAuthoredCoordinates(found->second, was);
        addAuthoredCoordinates(internals[i], now);
        for (const App::RecipeNode* side : {&was, &now}) {
            for (const auto& entry : side->fields) {
                consequences.emplace(tag, entry.first);
            }
        }
    }

    return consequences;
}

void Sketcher::registerSketchRecipeDetail()
{
    App::registerRecipeDetail(SketchObject::getClassTypeId(), &sketchRecipeDetail);
    App::registerRecipeConsequences(SketchObject::getClassTypeId(), &sketchConsequences);
    App::registerRecipeMerge(
        SketchObject::getClassTypeId(),
        {"Geometry", "Constraints", "ExternalGeo"},
        [](const App::DocumentObject& ancestor,
           const App::DocumentObject& branchA,
           const App::DocumentObject& branchB) {
            MergeReport report = mergeSketches(
                static_cast<const SketchObject&>(ancestor),
                static_cast<const SketchObject&>(branchA),
                static_cast<const SketchObject&>(branchB)
            );
            App::RecipeContentMerge merged {std::move(report.conflicts), {}};
            for (const OverriddenChoice& lost : report.overridden) {
                merged.overridden.push_back(nounFor(lost.type) + " (" + lost.field + ")");
            }
            for (const OpenPosition& open : report.undetermined) {
                merged.undetermined.push_back(nounFor(open.type));
            }
            return merged;
        },
        [](App::DocumentObject& into,
           const App::DocumentObject& ancestor,
           const App::DocumentObject& branchA,
           const App::DocumentObject& branchB) -> std::optional<std::string> {
            const auto& sketches = {
                &static_cast<const SketchObject&>(ancestor),
                &static_cast<const SketchObject&>(branchA),
                &static_cast<const SketchObject&>(branchB)
            };
            for (const SketchObject* sketch : sketches) {
                // Borrowed geometry rebuilds only as stand-ins, and a formula on a dimension is
                // bound by the dimension's place in a list the rebuild renumbers.
                if (!borrowedGeometry(*sketch).empty()) {
                    return "it borrows geometry from other objects";
                }
                if (!sketch->ExpressionEngine.getExpressions().empty()) {
                    return "a formula drives one of its dimensions";
                }
            }
            const MergeReport report = mergeSketches(
                static_cast<const SketchObject&>(ancestor),
                static_cast<const SketchObject&>(branchA),
                static_cast<const SketchObject&>(branchB),
                &static_cast<SketchObject&>(into)
            );
            if (!report.conflicts.empty() || !report.regen.fullyRealized) {
                return "it could not be rebuilt from the merged elements";
            }
            return std::nullopt;
        }
    );
}
