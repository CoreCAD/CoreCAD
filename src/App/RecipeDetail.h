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

#pragma once

#include <FCGlobal.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <Base/Type.h>

#include "Recipe.h"

namespace App
{

class Document;
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

/** Values in an object's detail that changed only because other edits did, as (node id, field).
 *
 *  A sketch's line ends are stored, but when a dimension changes the solver moves them, and
 *  nobody moved them. A history lists what was done, so these are left out of it; a line end
 *  someone dragged is not a consequence and stays.
 */
using RecipeConsequences = std::set<std::pair<std::string, std::string>>;

using RecipeConsequenceProvider
    = std::function<RecipeConsequences(const DocumentObject& before, const DocumentObject& after)>;

/// Register a provider for `type` and every type derived from it, as registerRecipeDetail does.
AppExport void registerRecipeConsequences(Base::Type type, RecipeConsequenceProvider provider);

/// The consequences between two versions of one object, or none when no module has claimed its
/// type. Neither object is changed.
AppExport RecipeConsequences recipeConsequences(const DocumentObject& before,
                                                const DocumentObject& after);

/** What a module's own merge found inside one object both sides changed.
 *
 *  The document merge sees a sketch's lines and dimensions only as two whole values, so two
 *  people editing different lines of one sketch would conflict, and the same dimension set two
 *  ways would read as one change. The module that owns the type merges that content by its own
 *  identities instead.
 */
struct RecipeContentMerge
{
    std::vector<MergeConflict> conflicts;
    /// Each value a side chose that the rebuilt content no longer holds, named for a person.
    std::vector<std::string> overridden;
    /// Each part of the rebuilt content that lands elsewhere when rebuilt from one version's own
    /// state, named for a person.
    std::vector<std::string> undetermined;
};

using RecipeContentMerger = std::function<RecipeContentMerge(
    const DocumentObject& ancestor, const DocumentObject& branchA, const DocumentObject& branchB)>;

/// Writes the merged content into `into`, a copy of the object in the merged document. Returns
/// why it could not, or nothing when it did.
using RecipeContentApplier = std::function<std::optional<std::string>(DocumentObject& into,
                                                                      const DocumentObject& ancestor,
                                                                      const DocumentObject& branchA,
                                                                      const DocumentObject& branchB)>;

struct RegisteredContentMerge
{
    std::vector<std::string> coveredProperties;
    RecipeContentMerger merge;
    RecipeContentApplier apply;
};

/// Register a merger for `type` and every type derived from it, for the properties it covers.
AppExport void registerRecipeMerge(Base::Type type,
                                   std::vector<std::string> coveredProperties,
                                   RecipeContentMerger merger,
                                   RecipeContentApplier applier = {});

/// The merger registered against the nearest of the object's types, or null when none is.
AppExport const RegisteredContentMerge* recipeMerger(const DocumentObject& obj);

/** What one side did to a layout the other side's statements are written against.
 *
 *  A formula names a sheet cell by its address. When one side inserts a row, the other side's
 *  formulas still count rows the old way, and a merge that took them as written would read the
 *  wrong cells without any conflict. A rebase replays the moving side's insertions and removals
 *  on copies of the ancestor and the other side, which rewrites that side's formulas the way the
 *  insertion rewrote the moving side's own, so the merge then compares like with like.
 */
struct MergeRebase
{
    /// What cannot be replayed, such as an address in rows that moved by an unknown amount.
    std::vector<MergeConflict> conflicts;
    /// Replays the moves on copies of the three versions; null when nothing moved.
    std::function<void(Document& ancestor, Document& branchA, Document& branchB)> apply;
};

using MergeRebaser = std::function<MergeRebase(
    const Document& ancestor, const Document& branchA, const Document& branchB)>;

AppExport void registerMergeRebase(MergeRebaser rebaser);
AppExport const std::vector<MergeRebaser>& mergeRebasers();

}  // namespace App
