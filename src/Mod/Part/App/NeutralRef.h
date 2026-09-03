// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef PART_NEUTRALREF_H
#define PART_NEUTRALREF_H

#include <string>
#include <Mod/Part/PartGlobal.h>

namespace App
{
class Document;
}

namespace Part
{

class ShapeFeature;

/**
 * A kernel-neutral reference to a sub-shape of a feature -- the stored form a
 * durable reference takes, in our own schema rather than a kernel's dialect.
 *
 * Both regimes of the two-regime identity model live here. A sub-shape is
 * identified by up to three handles, tried in order of strength:
 *
 *  - role: for a primitive feature that knows its faces parametrically (a box has
 *    a +X, -X, ... face by construction), the symmetry-proof intent name. Empty
 *    when the owning feature is not a role-bearing primitive. (Leaf regime.)
 *  - prov: for a face produced by an operation (a cut, a fuse), its history-derived
 *    provenance name from the element map, made portable between files by rewriting
 *    each per-document object tag to that object's durable Uid. Empty for a leaf
 *    with no construction history. (Derived regime.)
 *  - signature: the geometric signature (Part::subShapeSignature), the identity for
 *    a history-less import leaf, and a cross-check otherwise. Always recorded.
 *
 * Resolution prefers the role, then the prov name, then the signature -- the earlier
 * handles survive a self-symmetry that the world-read signature aliases.
 */
struct PartExport NRef
{
    std::string featureUid;  ///< owning feature's durable Uid
    std::string kind;        ///< "face" (the only supported grain for now); empty = null ref
    std::string role;        ///< leaf role ("+X".."-Z") for a primitive; empty otherwise
    std::string prov;        ///< neutralized provenance name for a derived face; empty otherwise
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
PartExport NRef captureFaceRef(const ShapeFeature& feature, const std::string& subName);

/**
 * What became of a reference when it was resolved.
 *
 * A reference that fails to bind fails in one of two quite different ways, and a
 * caller cannot answer honestly without knowing which. @c Ambiguous means the
 * target is still there several times over and only the user can say which one was
 * meant; @c Lost means it is not there at all. Collapsing both into "no answer"
 * is what lets a caller quietly fall back to a stale position and bind to whatever
 * now sits at that index. These are the green / yellow / red outcomes of the
 * re-import protocol (ARCHITECTURE §7.8), named here in the reference layer's own
 * terms because they arise wherever a durable reference is resolved, not only on
 * an import.
 */
enum class RefMatch
{
    None,       ///< the reference asks nothing: a null ref, or not a face ref
    Matched,    ///< exactly one sub-shape carries the reference (green)
    Ambiguous,  ///< several sub-shapes are equally good candidates (yellow)
    Lost        ///< nothing carries the reference any more (red)
};

/**
 * The result of resolving an NRef: what was found, and where.
 */
struct PartExport NRefResolution
{
    RefMatch match {RefMatch::None};  ///< which of the three answers resolution reached
    std::string subName;              ///< the sub-name now carrying the ref; empty unless Matched
};

/**
 * Resolve an NRef against @p feature to the positional sub-name that now carries
 * it -- the inverse of captureFaceRef.
 *
 * Two-regime: a ref with a role resolves through the role (symmetry-proof); a ref
 * with a prov name resolves through the element map (the provenance name is first
 * rewritten from durable Uids back to this document's object tags); a bare leaf
 * resolves by a unique geometric-signature match. Either way a lost or ambiguous
 * target yields no sub-name, never a guessed binding -- and says which of the two
 * it was, so the caller can fail honestly or ask.
 *
 * @param ref     an NRef as produced by captureFaceRef
 * @param feature the feature to resolve against (recomputed or moved)
 * @return the outcome, carrying a sub-name only when exactly one face matched
 */
PartExport NRefResolution resolveFaceRef(const NRef& ref, const ShapeFeature& feature);

/**
 * Serialize an NRef to its neutral string form -- the shape it takes when written
 * to a file, so a reference can travel between saved versions of a design.
 *
 * The form is a versioned, pipe-delimited line: @c "NRef|2|<uid>|<kind>|<role>|
 * <prov>|<signature>". It speaks no kernel's dialect; the signature is the last
 * field so it is read as the remainder, robust to any delimiter a future signature
 * might contain. The version tag lets the schema grow.
 *
 * @param ref the reference to serialize
 * @return the neutral string form
 */
PartExport std::string toNeutralString(const NRef& ref);

/**
 * Parse a neutral string form back to an NRef -- the inverse of toNeutralString.
 *
 * @param text a string as produced by toNeutralString
 * @return the parsed NRef; a null ref (empty @c kind) if @p text is not a
 *         well-formed NRef string of a known version
 */
PartExport NRef fromNeutralString(const std::string& text);

/**
 * The live binding a stored NRef resolves to inside a target document: the feature
 * that now carries the referenced sub-shape, and its current positional sub-name.
 */
struct PartExport NRefBinding
{
    const ShapeFeature* feature {nullptr};  ///< the located owning feature; null if unbound
    std::string subName;                    ///< its current sub-name for the ref; empty if unbound
};

/**
 * Consume a stored NRef against a whole document -- the cross-file merge step.
 *
 * This is where @c featureUid earns its place. The caller holds a reference lifted
 * from another saved version of a design and has no live pointer into @p doc; the
 * consumer finds the feature whose durable Uid the reference names (the same
 * authored feature, whatever face ordinals its own rebuild happened to produce) and
 * resolves the current sub-name on it. A merge that brings a downstream feature from
 * one branch onto the same primitive on another rides through this call.
 *
 * The Uid, not a positional index, is the join key: the two branches share the
 * feature's identity even when their kernel face numbering diverges.
 *
 * @param ref an NRef, e.g. one just read back via fromNeutralString
 * @param doc the document to bind against
 * @return the live binding; a null binding (null @c feature, empty @c subName) if no
 *         feature in @p doc carries the ref's Uid, or its sub-shape is gone or not
 *         uniquely matched. A partial location (feature found, sub-shape lost) still
 *         reports unbound -- never a feature paired with an empty sub-name.
 */
PartExport NRefBinding bindInDocument(const NRef& ref, const App::Document& doc);

}  // namespace Part

#endif  // PART_NEUTRALREF_H
