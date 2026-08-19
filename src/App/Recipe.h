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

#include <map>
#include <string>
#include <vector>

namespace App
{

/** The recipe layer — a part's authored "source", separated from its kernel-derived
 *  output so it can be diffed and merged with nothing running (the three-level split's
 *  Data-level guarantee). This is the general, type-blind substrate: every document
 *  object emits its authored recipe as these nodes, and one three-way merge engine
 *  reconciles them regardless of type. See DESIGN_recipe-merge.md.
 *
 *  Nothing here runs a kernel. Coordinates and other solver/derived output are not
 *  represented; a merged recipe is regenerated (re-solved/recomputed) afterwards.
 */

/// A reference from one node to another authored entity, by durable identity — never
/// by position. `pos` is an optional sub-part selector (e.g. a constraint's PointPos);
/// 0 means "the whole entity". Refs to non-authored sentinels (sketch axes) are simply
/// not emitted, so the referential check stays a pure "target exists" test.
struct RecipeRef
{
    std::string target;  ///< durable id (UUID) of the referenced node
    int pos = 0;         ///< sub-part selector, 0 = whole entity

    bool operator==(const RecipeRef& o) const
    {
        return target == o.target && pos == o.pos;
    }
    bool operator!=(const RecipeRef& o) const
    {
        return !(*this == o);
    }
};

/// One authored entity. `fields` holds authored values as strings — an expression
/// (`Length*2`) or a unit-typed literal (`40 mm`), never a resolved number, so a diff
/// over a parametric model does not lie. Identity is `id` (a durable UUID); a list of
/// nodes is matched by id, so a positional renumber is invisible.
struct RecipeNode
{
    std::string id;                             ///< durable UUID — the unit of diff/merge
    std::string type;                           ///< authored kind, e.g. "LineSegment"
    std::map<std::string, std::string> fields;  ///< authored name -> value/expression
    std::vector<RecipeRef> refs;                ///< references to other entities, by id

    bool operator==(const RecipeNode& o) const
    {
        return id == o.id && type == o.type && fields == o.fields && refs == o.refs;
    }
    bool operator!=(const RecipeNode& o) const
    {
        return !(*this == o);
    }
};

/// A homogeneous, id-keyed collection of nodes (e.g. a sketch's geometry, or its
/// constraints). The merge runs per section, then a referential pass checks across them.
using RecipeSection = std::map<std::string, RecipeNode>;

/// One unresolved outcome of a three-way merge, left for a higher layer (or the user)
/// to settle. Value conflicts come from the three-way rule; referential conflicts come
/// from the cross-section check and are handed to §4.7's honest-retirement outcomes.
struct MergeConflict
{
    enum class Kind
    {
        Value,       ///< both branches changed the same entity differently
        Referential  ///< a surviving reference points at an entity the merge deleted
    };

    Kind kind;
    std::string id;       ///< the conflicted node's id
    std::string type;     ///< the node's authored type token, opaque here — a driver (e.g. the
                          ///< sketch layer) turns it into a readable label so a report need not
                          ///< print the raw id
    std::string detail;   ///< human-readable summary (which field / which dangling target)
};

/// The honest-retirement outcome for one node the merge left with a dangling reference,
/// decided on §4.7's "who can decide?" axis (ratified as Amendment 15). The engine
/// applies these three outcomes; it does not invent new ones.
struct RefResolution
{
    enum class Outcome
    {
        Carry,    ///< target still present — geometry determines it; no decision needed
        Drop,     ///< no surviving reference remains — the subject genuinely ceased to
                  ///< exist; nothing survives to hold it, so the node is dropped and
                  ///< disclosed (the whole-subject-dimension case)
        StopAsk   ///< a live participant survives — a satisfiable target remains but
                  ///< re-targeting it is the user's choice, so the node is kept
    };

    Outcome outcome;
    std::string id;       ///< the referencing node's id
    std::string type;     ///< the node's authored type token, opaque here — a driver turns it
                          ///< into a readable label; carried so a dropped node (erased from the
                          ///< merged section) can still be described without its id
    std::string detail;   ///< which target dangled / what survives
};

/** The generic three-way merge — id-keyed, knows nothing about any document type. */
class AppExport RecipeMerge
{
public:
    /// Three-way reconcile one section against a common ancestor. For each id: both
    /// sides equal -> take it (covers both-added-equal and agreed-delete); only one side
    /// moved off the ancestor -> take that side (including its delete); both moved
    /// differently -> a Value conflict is recorded and A's side is kept provisionally so
    /// the result stays inspectable. Appends any conflicts to `conflicts`.
    static RecipeSection threeWay(const RecipeSection& base,
                                  const RecipeSection& a,
                                  const RecipeSection& b,
                                  std::vector<MergeConflict>& conflicts);

    /// The CAD-specific check with no text analogy: every ref in `withRefs` must point at
    /// an id present in `liveTargets`. A ref left dangling by the merge (one branch kept a
    /// constraint, the other deleted its target) is a Referential conflict — "merges clean,
    /// regenerates broken". Appends any to `conflicts`. This is the low-level detector;
    /// `resolveReferences` turns each dangling node into a §4.7 honest-retirement outcome.
    static void checkReferences(const RecipeSection& withRefs,
                                const RecipeSection& liveTargets,
                                std::vector<MergeConflict>& conflicts);

    /// Resolve references the merge left dangling, routed through §4.7's three honest-
    /// retirement outcomes rather than a bespoke rule. For each node in `withRefs`, its
    /// authored refs are re-resolved against the merged `liveTargets`:
    ///   - every ref still present            -> Carry (not reported; nothing to decide);
    ///   - at least one ref dangling, but a
    ///     live participant survives           -> StopAsk (node KEPT for the user);
    ///   - every authored ref dangling         -> Drop (node ERASED from `withRefs`, since
    ///                                            nothing survives to hold it honestly).
    /// Mutates `withRefs` for the Drop case. Returns one RefResolution per affected node
    /// (Carry nodes are omitted). Sentinel refs (empty target — sketch axes/external) are
    /// not authored targets and never dangle.
    static std::vector<RefResolution> resolveReferences(RecipeSection& withRefs,
                                                        const RecipeSection& liveTargets);

    /// Refine object-granular Value conflicts to field granularity — the layered pass that
    /// makes `threeWay`'s results finer without changing them. For each Value conflict, the
    /// same three-way rule is re-run over the object's base/A/B nodes one dimension at a time
    /// (each authored field, plus the object's type and its links as single dimensions). When
    /// the two branches touched disjoint dimensions, the edits are auto-merged: the object's
    /// node in `merged` is rebuilt to carry both sides and the conflict dissolves (no entry is
    /// returned for it). Only genuinely overlapping same-dimension edits remain, as one refined
    /// Value conflict per clashing dimension (the dimension named in `detail`); `merged` still
    /// takes A's side there, matching object-granular. A delete-vs-edit conflict (one branch
    /// dropped the object) has no two field sets to reconcile and passes through unchanged, as
    /// does any Referential conflict. The returned list is the same flat MergeConflict list,
    /// only finer — the object-granular pass and its data are not touched.
    static std::vector<MergeConflict> refineConflicts(const std::vector<MergeConflict>& conflicts,
                                                      const RecipeSection& base,
                                                      const RecipeSection& a,
                                                      const RecipeSection& b,
                                                      RecipeSection& merged);
};

}  // namespace App
