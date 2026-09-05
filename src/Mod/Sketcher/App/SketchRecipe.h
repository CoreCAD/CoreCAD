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

#ifndef SKETCHER_SKETCHRECIPE_H
#define SKETCHER_SKETCHRECIPE_H

#include <App/Recipe.h>
#include <App/RecipeDetail.h>

#include "SketchObject.h"

namespace Sketcher
{

/** The sketch's authored recipe — the first driver behind the general recipe-merge engine
 *  (App::RecipeMerge). Reads only authored/stored state; it never runs the solver, so the
 *  result can be diffed and merged with nothing running (the three-level split's Data-level
 *  guarantee). See corecad-strategy/DESIGN_recipe-merge.md §3.
 *
 *  Two homogeneous, durable-id-keyed sections:
 *   - geometry: one node per internal entity, keyed by its Part::Geometry Tag, carrying the
 *     authored type and construction flag. Coordinates are regenerable seeds (§4), not emitted
 *     as merge-diffed fields.
 *   - constraints: one node per constraint, keyed by its own Tag, carrying type, the authored
 *     value (an expression if one is bound, else a unit-typed literal; omitted for derived,
 *     non-driving reference dimensions), and references to geometry by the geometry's durable
 *     Tag — never by positional GeoId. Refs to sentinels with no durable identity (axes,
 *     external geometry) are not emitted, so App::RecipeMerge::checkReferences stays a pure
 *     "target exists" test.
 */
struct SketchRecipe
{
    App::RecipeSection geometry;
    App::RecipeSection constraints;
};

/// Emit the authored recipe of a sketch (§3). Pure read of authored/stored state; the solver
/// is not run.
SketcherExport SketchRecipe emitSketchRecipe(const SketchObject& sketch);

/// The sketch's contribution to a readable document recipe: its geometry and constraints, which
/// the generic emitter can only see as two opaque properties. Adds the authored coordinates the
/// merge recipe deliberately omits — a person asking what moved needs them, a merge does not.
SketcherExport App::RecipeDetail sketchRecipeDetail(const App::DocumentObject& obj);

/// Register sketchRecipeDetail against Sketcher::SketchObject. Called once, at module init.
SketcherExport void registerSketchRecipeDetail();

/** The outcome of regenerating a recipe onto a live sketch and re-solving with the existing
 *  solver — the CAD analogue of "does the merge compile?" (DESIGN_recipe-merge.md §6 slice 5,
 *  §10.3). The recipe layer merges text with nothing running; a textually-clean merge can still
 *  regenerate to invalid geometry, and only running the kernel/solver reveals it.
 */
struct RegenResult
{
    int solverStatus = 0;          ///< SketchObject::solve() return: 0 ok; -4 overconstrained,
                                   ///< -3 conflicting, -5 malformed, -1 solver error, -2 redundant
    bool hasConflicts = false;     ///< last solve reported conflicting constraints
    bool hasRedundancies = false;  ///< last solve reported redundant constraints
    bool hasMalformed = false;     ///< last solve reported malformed constraints
    int dof = -1;                  ///< remaining degrees of freedom after the solve
    bool fullyRealized = true;  ///< every node materialized (false if a seed/type/ref was missing)
};

/// Slice 5: regenerate. Materialize a (typically merged) recipe as live geometry and constraints
/// on `target`, then run the existing solver. Coordinates are not in the recipe (they are
/// regenerable seeds, §4): each surviving entity's seed geometry is pulled from `seedSources` by
/// durable tag, so the merged constraint set is re-solved against real starting coordinates.
/// Constraint values are taken as literal datums (a bound expression is not re-evaluated here —
/// a named deferral); refs with no durable identity (axes/external) were never emitted and so are
/// absent. Returns the solver's verdict: a clean merge that regenerates with hasConflicts is the
/// "merged but does not compile" case.
SketcherExport RegenResult regenerateSketch(
    SketchObject& target,
    const SketchRecipe& recipe,
    const std::vector<const SketchObject*>& seedSources
);

/** The user-facing outcome of merging three sketches — a common ancestor and two edited copies —
 *  the whole recipe-merge pipeline (emit → three-way merge → reference resolution → regenerate)
 *  run as one call so the result can be surfaced to a person (DESIGN_recipe-merge.md §7). This is
 *  the exploratory surfacing layer: a provisional trigger until the git-for-CAD merge driver
 *  exists. The three sketches need not share a document, but they must share a common ancestor's
 *  durable identity (a reload/copy of one sketch, not an independently re-drawn one), or the merge
 *  has no basis to align them.
 */
struct MergeReport
{
    App::RecipeSection mergedGeometry;     ///< geometry after the three-way merge
    App::RecipeSection mergedConstraints;  ///< constraints after the merge + reference resolution
    std::vector<App::MergeConflict> conflicts;  ///< value conflicts (both edits changed one thing)
    std::vector<App::RefResolution> resolutions;  ///< dropped-with-disclosure + stop-and-ask
    RegenResult regen;                            ///< does the merged sketch still compile?
};

/// Run the whole recipe-merge pipeline over three sketches and collect the outcome (§7). Emits the
/// three recipes, merges geometry and constraints, resolves dangling references through §4.7's
/// honest-retirement outcomes, then regenerates the merged recipe onto a throwaway scratch sketch
/// and re-solves — so the "does the merge compile?" verdict is included.
SketcherExport MergeReport
mergeSketches(const SketchObject& ancestor, const SketchObject& branchA, const SketchObject& branchB);

/// Render a MergeReport as plain language a person can read (Report View / Python console).
SketcherExport std::string formatMergeReport(const MergeReport& report);

}  // namespace Sketcher

#endif  // SKETCHER_SKETCHRECIPE_H
