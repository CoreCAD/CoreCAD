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
# include <optional>
# include <sstream>
# include <string>
# include <utility>
# include <vector>
#endif

#include <Base/Unit.h>

#include "ObjectRecipe.h"

#include "DocumentObject.h"
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

}  // namespace

RecipeNode App::emitObjectRecipe(const DocumentObject& obj)
{
    RecipeNode node;
    node.id = obj.Uid.getValueStr();
    node.type = obj.getTypeId().getName();

    // Derived and non-persisted state is never authored source; Label and Visibility are user
    // preferences, not geometry, so a rename or a show/hide toggle is never a merge conflict.
    const short excludedFlags = Prop_Output | Prop_Transient | Prop_NoPersist | Prop_Hidden;

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
            continue;  // links become refs-by-Uid — a follow-on increment
        }
        if (const std::optional<std::string> value = authoredFieldValue(prop)) {
            node.fields[propName] = *value;
        }
    }

    return node;
}
