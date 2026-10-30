// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <Mod/Part/PartGlobal.h>

#include "TopoShape.h"

namespace App
{
class Document;
}

namespace Part
{

/// Two shapes compared as material: what both hold, what only the later one holds, and what
/// only the earlier one held. Any of the three may be a null shape, meaning none.
struct ShapeComparison
{
    TopoShape shared;
    TopoShape added;
    TopoShape removed;
};

/** The solids a person sees in a document, in the world frame, as one compound: every object
 *  carrying a solid that no other solid-carrying object builds on. A pad inside a body is left
 *  out because the body carries its result; a sketch or a plane carries no solid. A null shape
 *  when the document holds none.
 *
 *  Reads the shapes as they stand; a document read from a file has to be recomputed first.
 */
PartExport TopoShape documentSolids(const App::Document& doc);

/// Compare `before` with `after` as material.
PartExport ShapeComparison compareShapes(const TopoShape& before, const TopoShape& after);

}  // namespace Part
