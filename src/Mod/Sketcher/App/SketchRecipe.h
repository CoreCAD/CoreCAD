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

}  // namespace Sketcher

#endif  // SKETCHER_SKETCHRECIPE_H
