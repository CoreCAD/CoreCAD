// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef PART_BOXFACEROLEREF_H
#define PART_BOXFACEROLEREF_H

#include <string>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

class Feature;

/**
 * Capture the parametric role a referenced box face plays, at the level a real
 * document reference speaks: a feature plus a positional sub-name.
 *
 * This is the reference-seam wiring of primitiveBoxFaceRole. A PropertyLinkSub
 * holds a link to the owning feature and a sub-name like "Face3"; today that
 * sub-name is a bare kernel ordinal that a recompute can reassign to a different
 * face. This reads the face that ordinal currently denotes and returns the
 * durable role it plays ("+X", "-Z", ...) -- the payload a reference should
 * store in place of the ordinal.
 *
 * The role is read in the feature's own placement frame, so it is invariant to
 * where the feature sits in the world; the frame is taken from the feature's
 * placement (getLocation()) applied to its stored shape.
 *
 * @param feature the owning box feature (its Shape and placement)
 * @param subName a positional face sub-name, e.g. "Face3"
 * @return the role of that face ("+X".."-Z"); empty if @p subName does not name
 *         a planar face of @p feature's shape
 */
PartExport std::string captureBoxFaceRole(const Feature& feature, const std::string& subName);

/**
 * Resolve a stored box-face role back to the positional sub-name that now
 * carries it on @p feature -- the inverse of captureBoxFaceRole.
 *
 * Run against a recomputed or rigidly-moved feature, it hands back the current
 * sub-name (e.g. "Face5") of the face that plays @p role, so a reference stored
 * as a role can be handed to the kernel as the ordinal it presently needs. The
 * match is required to be unique (a box has one face per axis); a role that is
 * gone or a malformed solid yields the empty string rather than a guess.
 *
 * @param feature the owning box feature to resolve against
 * @param role    a role name as produced by captureBoxFaceRole
 * @return the current positional sub-name of the face playing @p role; empty if
 *         @p role is unknown/gone or the match is not unique
 */
PartExport std::string resolveBoxFaceRole(const Feature& feature, const std::string& role);

}  // namespace Part

#endif  // PART_BOXFACEROLEREF_H
