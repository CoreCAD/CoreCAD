// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef PART_SUBSHAPESIGNATURE_H
#define PART_SUBSHAPESIGNATURE_H

#include <string>
#include <Mod/Part/PartGlobal.h>

class TopoDS_Shape;

namespace Part
{

/**
 * Kernel-neutral geometric identity for an emergent sub-shape.
 *
 * A primitive's own faces/edges/vertices carry no construction history and so
 * no element-map name (`getElementName` returns the empty string, and resolving
 * that empty name silently yields the whole solid). Yet a cross-branch merge
 * must still bind a reference to "the same face". This function derives an
 * identity from what the sub-shape *is* -- surface/curve character, centroid,
 * orientation, size -- quantized to a tolerance so it is stable across
 * recompute and reproducible on any conforming kernel, not from the kernel's
 * positional leaf ordinal.
 *
 * It is the ratified "Stable Face Identity" signature (ARCHITECTURE.md) applied
 * to primitive leaves, which are the same case as imported geometry: emergent
 * from parameters, no history. It is a compute-seam routine: it reads OCCT
 * geometry and returns plain data to a contract the data layer owns; the kernel
 * adapter (this function) fills it. A signature is a deterministic function of
 * the geometry the document already stores, so it is computable with nothing
 * running and honours both the "data keeps its own identity" and the
 * "engines are replaceable" corollaries of the three-level split.
 *
 * @param sub a TopoDS_Face, TopoDS_Edge or TopoDS_Vertex
 * @return a canonical, locale-free signature string; empty if @p sub is null or
 *         not one of the supported sub-shape types
 */
PartExport std::string subShapeSignature(const TopoDS_Shape& sub);

}  // namespace Part

#endif  // PART_SUBSHAPESIGNATURE_H
