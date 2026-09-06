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

#pragma once

#include <FCGlobal.h>

#include <functional>
#include <string>
#include <vector>

#include <Base/Type.h>

#include "Recipe.h"

namespace App
{

class DocumentObject;

/** Authored content an object holds *inside* a property, which the generic emitter can only
 *  see as an opaque value.
 *
 *  A sketch is the case that forced this. Its lines, circles and dimensions live in two
 *  properties the generic walk cannot read, so a document recipe listed them as unrecorded and
 *  a moved hole produced no line at all. The module that owns the type knows how to say what is
 *  in there; App does not, and must not learn — so the module registers a provider for its own
 *  types and App looks it up by type.
 */
struct RecipeDetailSection
{
    std::string name;              ///< "geometry", "constraints" — the reader's heading
    std::vector<RecipeNode> nodes; ///< in authoring order, which is the order a person expects
};

/** What one provider says about one object.
 *
 *  `coveredProperties` names the properties the sections account for, so the view stops
 *  reporting them as unrecorded — a provider must claim what it covers, or the file would go on
 *  saying a sketch's geometry is missing while printing it.
 */
struct RecipeDetail
{
    std::vector<std::string> coveredProperties;
    std::vector<RecipeDetailSection> sections;
};

using RecipeDetailProvider = std::function<RecipeDetail(const DocumentObject&)>;

/** Register a provider for `type` and every type derived from it. Called once per module, at
 *  module initialisation, by the module that owns the type.
 *
 *  A provider serves the **readable view**, and may report authored values the merge engine
 *  deliberately leaves out — a sketch's coordinates being exactly that case (§4 treats an
 *  undimensioned position as a regenerable seed, correct for reconciling two people's edits,
 *  wrong for a person asking what moved). Reading a change and reconciling two of them are
 *  different jobs; this serves the first.
 */
AppExport void registerRecipeDetail(Base::Type type, RecipeDetailProvider provider);

/// The detail for one object, from the provider registered against the nearest of its types, or
/// an empty detail when no module has claimed it.
AppExport RecipeDetail recipeDetail(const DocumentObject& obj);

}  // namespace App
