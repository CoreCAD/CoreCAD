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
