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

namespace Sketcher::SketchRecipeInternal
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
/// coordinates; a surviving entity's starting coordinates are pulled from a branch that
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
/// (it treats an undimensioned position as a regenerable seed, which is right for
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

}  // namespace Sketcher::SketchRecipeInternal

SketchRecipe Sketcher::emitSketchRecipe(const SketchObject& sketch)
{
    SketchRecipe recipe;

    // Geometry: one node per internal entity, keyed by its durable Part::Geometry Tag. The
    // GeoId (== list index) is a readable position, never the identity, so a renumber is
    // invisible to the merge. Coordinates are regenerable seeds, not emitted.
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
        // The entity is the one the recipe names, so it keeps that identity.
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
