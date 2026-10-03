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

/// Derived and non-persisted state is never authored source. The same declaration the recipe
/// emitter reads (ObjectRecipe.cpp), so the stored form and the readable view agree about what
/// counts as authored — two answers to that question would be two definitions of the document.
constexpr short excludedPropertyFlags = Prop_Output | Prop_Transient | Prop_NoPersist;

/// Properties that ARE authored even though their declared flags say otherwise.
///
/// An object's name for the user is declared `Prop_Output`, which in this vocabulary means
/// "changing it need not recompute anything" and not "the program computed it" — but the stored
/// form reads that same flag as the line between source and rebuilt output, so a name a person
/// typed was being dropped. The flags cannot answer a question they were not asked; until the
/// document model says outright which properties are authored, the exceptions are named here
/// rather than inferred, so the list is visible and short instead of silent.
bool isAuthoredDespiteItsFlags(const std::string& name)
{
    return name == "Label";
}

/// Content the recipe PRODUCES, as opposed to content the document was handed.
///
/// A part's shape is declared `Prop_None` -- it claims to be neither output nor transient -- so
/// the flags do not keep it out, and asking the writer to inline bulky values put the whole solid
/// into the recipe as text. That is not a big file, it is a wrong one: for a feature the recipe
/// is the source and the shape is what the source builds, and a file carrying both can disagree
/// with itself. Measured: with the shape inline, a test that rebuilt a part from the recipe with
/// none of its references restored still produced the right solid -- the file was answering with
/// the old geometry instead of rebuilding.
///
/// An imported solid, a scanned mesh, a measured point cloud, the bytes of a file a person handed
/// over are the opposite case: no step of the document produces them, so that content IS the
/// authored content. Excluding those by kind dropped them without a word -- an imported part came
/// back empty.
///
/// **The object answers, and the property's class is never tested** (Amendment 18 Clause 18.2).
/// It used to be tested: only a `PropertyGeometry` could be output, so every other kind of bulk
/// was authored content whatever produced it. Measured: a machine toolpath -- produced by the job
/// above it, and reproducible from it -- was written into the source store, visible and versioned
/// and kept for ever, beside the imported bodies a person chose.
///
/// Which leaves one question for the property, and it is a different question: whether the value
/// can be stated in a file meant to be read at all (`holdsOpaqueBulk`). A colour is not placed
/// anywhere; it is simply written down.
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
        // Nothing the archive itself refuses to keep. A value declared transient is regenerated
        // by whatever produces it, and a store that claimed to hold it would be lying about a
        // value that was never written.
        if (prop->testStatus(Property::PropNoPersist) || prop->testStatus(Property::Transient)
            || (owner.getPropertyType(prop) & Prop_Transient) != 0) {
            continue;
        }
        // Output, in the two ways an object says it: content it says it produced, and a value
        // it declares is a result rather than a setting. `Label` says the second and means
        // neither -- it is the one authored value wearing the output flag, and the recipe keeps
        // it.
        if (theObjectProducedIt(*prop, owner)
            || ((owner.getPropertyType(prop) & Prop_Output) != 0
                && !isAuthoredDespiteItsFlags(name))) {
            rebuilt.push_back(prop);
        }
    }
    return rebuilt;
}
