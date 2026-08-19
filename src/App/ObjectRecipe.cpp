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
# include <iomanip>
# include <limits>
# include <locale>
# include <map>
# include <optional>
# include <set>
# include <sstream>
# include <string>
# include <utility>
# include <vector>
#endif

#include <Base/Unit.h>

#include "ObjectRecipe.h"

#include "Document.h"
#include "DocumentObject.h"
#include "Expression.h"
#include "ObjectIdentifier.h"
#include "PropertyGeo.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "PropertyUnits.h"

using namespace App;

namespace
{

/// Full-precision, locale-free rendering of a double, so a field changes iff the authored value
/// changes (display formatting would round distinct values into one string). Mirrors the sketch
/// emitter's canonicalNumber, so the two drivers speak the same value language.
std::string canonicalNumber(double value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return oss.str();
}

/// A full-precision, locale-free rendering of a 3-vector's components, so a change to any
/// component changes the field. Shared by the vector and placement canonicalizers.
std::string canonicalVector(const Base::Vector3d& vec)
{
    return canonicalNumber(vec.x) + " " + canonicalNumber(vec.y) + " " + canonicalNumber(vec.z);
}

/// Position then rotation as a quaternion, full precision, so a move OR a rotation changes the
/// field. Shared by the placement scalar and the placement-list canonicalizers.
std::string canonicalPlacement(const Base::Placement& value)
{
    double q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0;
    value.getRotation().getValue(q0, q1, q2, q3);
    return canonicalVector(value.getPosition()) + " " + canonicalNumber(q0) + " "
        + canonicalNumber(q1) + " " + canonicalNumber(q2) + " " + canonicalNumber(q3);
}

/// A list value type canonicalized as its elements joined by "; ", so a change to any element,
/// to the count, or to the order changes the field. Each element is rendered by the same
/// per-value canonicalizer the scalar path uses, so a list and its scalar sibling speak one
/// value language. An empty list is a valid authored state and renders as the empty string
/// (present-but-empty, distinct from a type this driver does not handle).
template<class ListT, class Render>
std::string canonicalList(const ListT& values, Render render)
{
    std::string out;
    bool first = true;
    for (const auto& element : values) {
        if (!first) {
            out += "; ";
        }
        out += render(element);
        first = false;
    }
    return out;
}

/// The authored value of one property as a canonical string, or nullopt if the property carries
/// no value this driver can yet canonicalize (list value types are a follow-on increment, each
/// gated by its own test). A quantity keeps its unit token so a bare-number change and a unit
/// change are both visible. Most-derived types are matched first (PropertyQuantity derives from
/// PropertyFloat; PropertyPosition/PropertyVectorDistance derive from PropertyVector).
std::optional<std::string> authoredFieldValue(const Property* prop)
{
    if (const auto* quantity = dynamic_cast<const PropertyQuantity*>(prop)) {
        std::string literal = canonicalNumber(quantity->getValue());
        const std::string unit = quantity->getUnit().getString();
        if (!unit.empty()) {
            literal += " " + unit;
        }
        return literal;
    }
    if (const auto* number = dynamic_cast<const PropertyFloat*>(prop)) {
        return canonicalNumber(number->getValue());
    }
    if (const auto* integer = dynamic_cast<const PropertyInteger*>(prop)) {
        return std::to_string(integer->getValue());
    }
    if (const auto* flag = dynamic_cast<const PropertyBool*>(prop)) {
        return std::string(flag->getValue() ? "true" : "false");
    }
    if (const auto* choice = dynamic_cast<const PropertyEnumeration*>(prop)) {
        const char* current = choice->getValueAsString();
        return current != nullptr ? std::string(current) : std::string();
    }
    if (const auto* text = dynamic_cast<const PropertyString*>(prop)) {
        const char* value = text->getValue();
        return value != nullptr ? std::string(value) : std::string();
    }
    if (const auto* placement = dynamic_cast<const PropertyPlacement*>(prop)) {
        // A move OR a rotation of a datum/attachment placement — previously invisible to the
        // merge — changes the field.
        return canonicalPlacement(placement->getValue());
    }
    if (const auto* vector = dynamic_cast<const PropertyVector*>(prop)) {
        return canonicalVector(vector->getValue());
    }
    if (const auto* matrix = dynamic_cast<const PropertyMatrix*>(prop)) {
        double cells[16] = {};
        matrix->getValue().getMatrix(cells);
        std::string out;
        for (int i = 0; i < 16; ++i) {
            if (i != 0) {
                out += " ";
            }
            out += canonicalNumber(cells[i]);
        }
        return out;
    }
    // List value types. Each derives from PropertyListsT (not from its scalar sibling), so these
    // are matched independently of the scalar cases above; every one renders via canonicalList so
    // a change to any element, the count, or the order is visible.
    if (const auto* list = dynamic_cast<const PropertyFloatList*>(prop)) {
        return canonicalList(list->getValues(), [](double v) { return canonicalNumber(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyIntegerList*>(prop)) {
        return canonicalList(list->getValues(), [](long v) { return std::to_string(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyStringList*>(prop)) {
        return canonicalList(list->getValues(), [](const std::string& v) { return v; });
    }
    if (const auto* list = dynamic_cast<const PropertyBoolList*>(prop)) {
        // Stored as a boost::dynamic_bitset (no range iterators), so index it directly.
        const auto& bits = list->getValues();
        std::string out;
        for (std::size_t i = 0; i < bits.size(); ++i) {
            if (i != 0) {
                out += "; ";
            }
            out += bits[i] ? "true" : "false";
        }
        return out;
    }
    if (const auto* list = dynamic_cast<const PropertyVectorList*>(prop)) {
        return canonicalList(list->getValues(),
                             [](const Base::Vector3d& v) { return canonicalVector(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyPlacementList*>(prop)) {
        return canonicalList(list->getValues(),
                             [](const Base::Placement& v) { return canonicalPlacement(v); });
    }
    return std::nullopt;
}

/// A short, readable stand-in for a durable Uid in a report line (the full uuid is exact but
/// unreadable). Never used for identity — only for a person scanning the summary.
std::string shortId(const std::string& id)
{
    return id.size() > 8 ? id.substr(0, 8) : id;
}

}  // namespace

RecipeNode App::emitObjectRecipe(const DocumentObject& obj)
{
    RecipeNode node;
    node.id = obj.Uid.getValueStr();
    node.type = obj.getTypeId().getName();

    // Derived and non-persisted state is never authored source; Label and Visibility are user
    // preferences, not geometry, so a rename or a show/hide toggle is never a merge conflict.
    const short excludedFlags = Prop_Output | Prop_Transient | Prop_NoPersist | Prop_Hidden;

    // A property driven by an expression is authored by that expression, not by its resolved
    // number; index the bindings by property so the field records the expression text.
    std::map<const Property*, std::string> expressionByProperty;
    for (const auto& [identifier, expression] : obj.ExpressionEngine.getExpressions()) {
        if (expression == nullptr) {
            continue;
        }
        if (const Property* bound = identifier.getProperty()) {
            expressionByProperty.emplace(bound, expression->toString());
        }
    }

    std::set<std::string> seenRefs;
    // getPropertyMap (unlike getPropertyNamedList) is overridden by ExtensionContainer to include
    // properties an extension contributes — Placement chief among them, which Amendment 4 moved
    // off GeoFeature into App::PlacementExtension. Enumerating the map keeps those in the recipe
    // (and orders deterministically by name).
    std::map<std::string, Property*> properties;
    obj.getPropertyMap(properties);
    for (const auto& [propName, prop] : properties) {
        if (prop == nullptr) {
            continue;
        }
        if (propName == "Label" || propName == "Visibility") {
            continue;
        }
        if ((obj.getPropertyType(prop) & excludedFlags) != 0) {
            continue;
        }
        if (prop->isDerivedFrom(PropertyLinkBase::getClassTypeId())) {
            // A link addresses another object; the recipe references it by the target's durable
            // Uid, never by name. Whole-object only (pos 0): a link's sub-element string (Face3,
            // Edge1) names emergent geometry with no durable identity, so it is deliberately not
            // encoded here.
            std::vector<DocumentObject*> targets;
            static_cast<const PropertyLinkBase*>(prop)->getLinks(targets, /*all=*/true);
            for (const DocumentObject* target : targets) {
                if (target == nullptr) {
                    continue;
                }
                RecipeRef ref;
                ref.target = target->Uid.getValueStr();
                ref.pos = 0;
                if (seenRefs.insert(ref.target).second) {
                    node.refs.push_back(ref);
                }
            }
            continue;
        }
        if (const std::optional<std::string> value = authoredFieldValue(prop)) {
            const auto bound = expressionByProperty.find(prop);
            node.fields[propName] =
                bound != expressionByProperty.end() ? bound->second : *value;
        }
    }

    return node;
}

RecipeSection App::emitDocumentRecipe(const Document& doc)
{
    RecipeSection section;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        section[obj->Uid.getValueStr()] = emitObjectRecipe(*obj);
    }
    return section;
}

DocumentMergeReport App::mergeDocuments(const Document& ancestor,
                                        const Document& branchA,
                                        const Document& branchB)
{
    DocumentMergeReport report;

    const RecipeSection base = emitDocumentRecipe(ancestor);
    const RecipeSection a = emitDocumentRecipe(branchA);
    const RecipeSection b = emitDocumentRecipe(branchB);

    report.merged = RecipeMerge::threeWay(base, a, b, report.conflicts);

    // Refine each whole-object conflict to field granularity: where the two branches touched
    // disjoint fields (or links, or type) the edits are auto-merged into report.merged and the
    // conflict dissolves; only genuinely overlapping same-field edits remain. Runs before the
    // referential pass so that pass resolves against the refined links.
    report.conflicts = RecipeMerge::refineConflicts(report.conflicts, base, a, b, report.merged);

    // Objects reference other objects in the same section, so a reference dangles when the merge
    // kept an object whose referent the other branch deleted. Resolve against a snapshot of the
    // survivors (a copy, so dropping a node cannot perturb the target set mid-pass).
    const RecipeSection survivors = report.merged;
    report.resolutions = RecipeMerge::resolveReferences(report.merged, survivors);

    return report;
}

std::string App::formatDocumentReport(const DocumentMergeReport& report)
{
    std::ostringstream out;
    const std::size_t objectCount = report.merged.size();

    if (report.conflicts.empty() && report.resolutions.empty()) {
        out << "Merged cleanly: " << objectCount << " objects, no conflicts.\n";
        return out.str();
    }

    out << "Merged " << objectCount << " objects.\n";

    if (!report.conflicts.empty()) {
        out << "\nConflicts — both branches changed the same object differently:\n";
        for (const MergeConflict& conflict : report.conflicts) {
            out << "  - " << (conflict.type.empty() ? "object" : conflict.type) << " "
                << shortId(conflict.id) << ": " << conflict.detail << "\n";
        }
    }

    if (!report.resolutions.empty()) {
        out << "\nReferences the merge had to settle:\n";
        for (const RefResolution& resolution : report.resolutions) {
            const char* label = "kept";
            if (resolution.outcome == RefResolution::Outcome::Drop) {
                label = "dropped";
            }
            else if (resolution.outcome == RefResolution::Outcome::StopAsk) {
                label = "needs a decision";
            }
            out << "  - " << (resolution.type.empty() ? "object" : resolution.type) << " "
                << shortId(resolution.id) << " " << label << ": " << resolution.detail << "\n";
        }
    }

    return out.str();
}
