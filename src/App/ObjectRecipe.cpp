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

/// The authored value of one property as a canonical string, or nullopt if the property carries
/// no value this driver can yet canonicalize (richer value types — placements, vectors, lists —
/// are follow-on increments, each gated by its own test). A quantity keeps its unit token so a
/// bare-number change and a unit change are both visible. Most-derived types are matched first
/// (PropertyQuantity derives from PropertyFloat).
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
    std::vector<std::pair<const char*, Property*>> properties;
    obj.getPropertyNamedList(properties);
    for (const auto& [name, prop] : properties) {
        if (name == nullptr || prop == nullptr) {
            continue;
        }
        const std::string propName = name;
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
