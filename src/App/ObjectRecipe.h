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

#include <string>
#include <vector>

#include "Recipe.h"

namespace App
{

class Document;
class DocumentObject;

/** Emit one document object's authored recipe node — the generic, type-blind driver behind
 *  the recipe-merge engine (App::RecipeMerge), the counterpart to the sketch's bespoke
 *  emitter. Reads only authored/stored state; it never runs a recompute, so the result can be
 *  diffed and merged with nothing running (the three-level split's Data-level guarantee).
 *
 *  The node is keyed by the object's durable Uid (never its in-document name, which is
 *  positional). `type` is the object's type name. `fields` carries each authored property that
 *  survives the emit filter, as a string. `refs` express the object's links to other objects
 *  by the target's durable Uid — never by name. Computed output (the kernel-derived Shape and
 *  other Prop_Output/Prop_Transient state) is not emitted; a merged recipe is regenerated
 *  afterwards, so emergent geometry never has to be matched across branches.
 *
 *  This is the atom a future whole-document emitter calls once per object to build a
 *  RecipeSection for model-wide three-way merge.
 */
AppExport RecipeNode emitObjectRecipe(const DocumentObject& obj);

/** Emit a whole document's authored recipe — one id-keyed section holding every object's node,
 *  built by calling emitObjectRecipe once per object. This is the section App::RecipeMerge
 *  reconciles for model-wide, object-granular three-way merge: an object untouched on both
 *  branches compares equal as a whole node and is taken with no field-level work, so the
 *  unchanged majority costs a single comparison. (Finer, field-level reconciliation of the few
 *  objects that both branches edit is a later refinement pass layered over this, not a change
 *  to it.)
 */
AppExport RecipeSection emitDocumentRecipe(const Document& doc);

/** The outcome of merging three versions of a model — a common ancestor and two edited copies —
 *  at object granularity. `merged` is the reconciled section; `conflicts` lists objects both
 *  branches changed differently; `resolutions` lists objects whose references the merge left
 *  dangling (an object kept on one branch whose referent was deleted on the other), routed
 *  through §4.7's honest-retirement outcomes.
 *
 *  `conflicts` is a flat list by design: the later field-level refinement pass consumes each
 *  object-level entry and either dissolves it (the branches touched disjoint fields, auto-merged)
 *  or replaces it with finer per-field entries — extending this same list, never reshaping it.
 */
struct DocumentMergeReport
{
    RecipeSection merged;
    std::vector<MergeConflict> conflicts;
    std::vector<RefResolution> resolutions;
};

/// Merge three versions of a model — a common ancestor and two edited copies of it — into a
/// DocumentMergeReport. The three documents must share a common ancestor's durable identities
/// (branches copied/reloaded from one origin, not independently rebuilt), or the merge has no
/// basis to align their objects. Emergent geometry is never matched: only authored recipes are
/// reconciled, and a merged model is regenerated afterwards.
AppExport DocumentMergeReport
mergeDocuments(const Document& ancestor, const Document& branchA, const Document& branchB);

/// Render a DocumentMergeReport as plain language a person can read (Report View / console).
AppExport std::string formatDocumentReport(const DocumentMergeReport& report);

}  // namespace App
