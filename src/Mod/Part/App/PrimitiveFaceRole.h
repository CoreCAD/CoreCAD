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

/**
 * Resolve a stored box-face role back to the live face that now carries it.
 *
 * This is the inverse of primitiveBoxFaceRole and the resolution half of the
 * leaf-regime reference contract: capture stores the role a picked face plays in
 * the feature's parametric frame; resolve, run against a possibly-recomputed or
 * rigidly-moved solid, hands back the face that plays that same role. Because the
 * role is read in the local frame, resolution carries a reference correctly
 * across any rigid motion -- including a self-symmetry of the body, the case
 * where matching by geometric signature would alias onto the wrong face.
 *
 * The match is required to be unique: a box has exactly one face per axis, so a
 * healthy solid resolves unambiguously. Zero matches (the role is gone) or more
 * than one (a malformed solid) both return null rather than bind a guess -- the
 * stop-and-ask degrade the merge contract assumes.
 *
 * @param solid         the box solid to resolve against, in world coordinates
 * @param localToWorld  the feature's placement (local frame -> world), the same
 *                      frame the role was captured in
 * @param role          a role name as produced by primitiveBoxFaceRole
 *                      ("+X", "-X", "+Y", "-Y", "+Z", "-Z")
 * @return the unique planar face of @p solid playing @p role; null if @p role is
 *         empty/unknown, @p solid is null, or the match is not unique
 */
PartExport TopoDS_Shape
resolveBoxFaceByRole(const TopoDS_Shape& solid, const gp_Trsf& localToWorld, const std::string& role);

}  // namespace Part

#endif  // PART_PRIMITIVEFACEROLE_H
