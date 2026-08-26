// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef PART_NEUTRALREF_H
#define PART_NEUTRALREF_H

#include <string>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

class Feature;

/**
 * A kernel-neutral reference to a sub-shape of a feature -- the stored form a
 * durable reference takes, in our own schema rather than a kernel's dialect.
 *
 * This is the leaf regime of the two-regime identity model (the derived regime,
 * a neutralized provenance name, is a later bite and is not carried here yet). A
 * leaf sub-shape -- one with no authored construction history -- is identified
 * two ways, in order of strength:
 *
 *  - role: for a primitive feature that knows its faces parametrically (a box has
 *    a +X, -X, ... face by construction), the symmetry-proof intent name. Empty
 *    when the owning feature is not a role-bearing primitive.
 *  - signature: the geometric signature (Part::subShapeSignature), the identity
 *    for a history-less import leaf, and a cross-check otherwise. Always recorded.
 *
 * Resolution prefers the role when present (it survives a self-symmetry that the
 * world-read signature aliases) and falls back to the signature otherwise.
 */
struct PartExport NRef
{
    std::string featureUid;  ///< owning feature's durable Uid
    std::string kind;        ///< "face" (the only supported grain for now); empty = null ref
    std::string role;        ///< leaf role ("+X".."-Z") for a primitive; empty otherwise
    std::string signature;   ///< geometric signature of the sub-shape (feature-local frame)
};

/**
 * Capture a durable NRef for the sub-element @p subName of @p feature.
 *
 * Records the owning feature's Uid and, in the feature-local frame, the
 * sub-shape's geometric signature; and, when @p feature is a role-bearing
 * primitive (a box), its parametric role. The signature is always taken so an
 * import leaf still has an identity; the role is the primary handle when present.
 *
 * @param feature the owning feature
 * @param subName a positional face sub-name, e.g. "Face3"
 * @return the captured NRef; a null ref (empty @c kind) if @p subName names no
 *         sub-shape of @p feature's shape
 */
PartExport NRef captureFaceRef(const Feature& feature, const std::string& subName);

/**
 * Resolve an NRef against @p feature to the positional sub-name that now carries
 * it -- the inverse of captureFaceRef.
 *
 * Two-regime: a ref with a role resolves through the role (symmetry-proof); a ref
 * without one resolves by a unique geometric-signature match. Either way a lost
 * or ambiguous target yields the empty string, never a guessed binding.
 *
 * @param ref     an NRef as produced by captureFaceRef
 * @param feature the feature to resolve against (recomputed or moved)
 * @return the current sub-name of the referenced face; empty if @p ref is not a
 *         face ref, or the target is gone or not uniquely matched
 */
PartExport std::string resolveFaceRef(const NRef& ref, const Feature& feature);

}  // namespace Part

#endif  // PART_NEUTRALREF_H
