// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
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
# include <array>
# include <charconv>
# include <cstring>
# include <filesystem>
# include <fstream>
# include <functional>
# include <iterator>
# include <limits>
# include <map>
# include <memory>
# include <optional>
# include <sstream>
# include <string>
# include <vector>
#endif

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

#include <Base/FileInfo.h>
#include <Base/Persistence.h>
#include <Base/Uuid.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "StoredRecipe.h"

#include <Base/Console.h>

#include "Document.h"
#include "DynamicProperty.h"
#include "Extension.h"
#include "ExpressionParser.h"
#include "DocumentObject.h"
#include "GeoFeature.h"
#include "Property.h"
#include "PropertyContainer.h"
#include "PropertyExpressionEngine.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "Services.h"

#include <Base/ServiceProvider.h>

using namespace App;

namespace fs = std::filesystem;

namespace
{

/// Derived and non-persisted state is never authored source. Shared with the readable view
/// (ObjectRecipe.cpp) so the two agree on what counts as authored.
constexpr short excludedPropertyFlags = Prop_Output | Prop_Transient | Prop_NoPersist;

/// Authored despite its flags. `Label` is `Prop_Output` only in the sense "changing it needs no
/// recompute"; a name a person typed is still source.
bool isAuthoredDespiteItsFlags(const std::string& name)
{
    return name == "Label";
}

/// Bulk the object itself builds, so the recipe leaves it out and a rebuild makes it again.
///
/// The object answers, never the property's class (Amendment 18 Clause 18.2): the same kind of
/// value is output on a feature and authored content on an import.
bool theObjectProducedIt(const Property& prop, const PropertyContainer& owner)
{
    if (!prop.holdsOpaqueBulk()) {
        return false;
    }
    const auto* object = dynamic_cast<const DocumentObject*>(&owner);
    return object != nullptr && object->producesContentOf(prop);
}

}  // namespace

bool App::theRecipeCarries(const Property& prop, const PropertyContainer& owner)
{
    const std::string name = owner.getPropertyName(&prop) != nullptr
        ? std::string(owner.getPropertyName(&prop))
        : std::string();
    if ((owner.getPropertyType(&prop) & excludedPropertyFlags) != 0
        && !isAuthoredDespiteItsFlags(name)) {
        return false;
    }
    if (prop.testStatus(Property::PropNoPersist)) {
        return false;
    }
    return !theObjectProducedIt(prop, owner);
}

std::vector<Property*> App::rebuiltProperties(const PropertyContainer& owner)
{
    std::map<std::string, Property*> properties;
    owner.getPropertyMap(properties);

    std::vector<Property*> rebuilt;
    for (const auto& [name, prop] : properties) {
        if (prop == nullptr) {
            continue;
        }
        // A transient value is never stored at all, so no store can claim to hold it.
        if (prop->testStatus(Property::PropNoPersist) || prop->testStatus(Property::Transient)
            || (owner.getPropertyType(prop) & Prop_Transient) != 0) {
            continue;
        }
        if (theObjectProducedIt(*prop, owner)
            || ((owner.getPropertyType(prop) & Prop_Output) != 0
                && !isAuthoredDespiteItsFlags(name))) {
            rebuilt.push_back(prop);
        }
    }
    return rebuilt;
}
