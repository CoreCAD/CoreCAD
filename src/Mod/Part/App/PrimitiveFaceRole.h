// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef PART_PRIMITIVEFACEROLE_H
#define PART_PRIMITIVEFACEROLE_H

#include <string>
#include <Mod/Part/PartGlobal.h>

class TopoDS_Shape;
class gp_Trsf;

namespace Part
{

/**
 * Parametric intent role-name for a primitive box face, expressed in the
 * feature's own local frame.
 *
 * A primitive feature knows its faces by construction: an (additive or
 * subtractive) box always has a +X, -X, +Y, -Y, +Z and -Z face. That role is a
 * property of the parametric definition, not of where the solid sits in the
 * world, so it is *invariant under any rigid motion of the feature*. This is the
 * one identity a geometric signature cannot supply: a signature reads world
 * position and orientation, so a self-symmetry motion of the part (rotate a box
 * 180 degrees) aliases a face onto its partner; a role does not, because it is
 * read back in the local frame the motion cancels.
 *
 * Roles are therefore the primary, symmetry-proof identity for primitive leaves,
 * leaving the signature to cover only true imports (history-less external
 * geometry, where no parametric frame exists). Like subShapeSignature this is a
 * compute-seam routine: it reads OCCT geometry and returns plain, kernel-neutral
 * data to a contract the data layer owns.
 *
 * The side a face is on is read from its centroid relative to the solid's
 * centroid, in the local frame -- not from the kernel's face-orientation flag,
 * which is not reliably preserved across a rigid transform. Centroids transform
 * exactly, so the role is genuinely motion-invariant.
 *
 * @param face          a planar TopoDS_Face of the box solid, in world coordinates
 * @param solid         the whole box solid the face belongs to, same coordinates
 * @param localToWorld  the feature's placement (local frame -> world); its
 *                      rotation names which local axis the face faces along
 * @return "+X", "-X", "+Y", "-Y", "+Z" or "-Z"; empty if @p face is null, not a
 *         face, or not planar
 */
PartExport std::string primitiveBoxFaceRole(
    const TopoDS_Shape& face,
    const TopoDS_Shape& solid,
    const gp_Trsf& localToWorld
);

}  // namespace Part

#endif  // PART_PRIMITIVEFACEROLE_H
