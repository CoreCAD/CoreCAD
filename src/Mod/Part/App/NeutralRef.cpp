// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <string>

# include <TopoDS_Shape.hxx>
#endif

#include "NeutralRef.h"
#include "BoxFaceRoleRef.h"
#include "FeaturePartBox.h"
#include "PartFeature.h"
#include "SubShapeSignature.h"
#include "TopoShape.h"

namespace Part
{

NRef captureFaceRef(const Feature& feature, const std::string& subName)
{
    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull() || subName.empty()) {
        return {};
    }

    // The signature is read in the feature-local (stored) frame per Amendment 4,
    // the same frame resolveFaceRef reads it back in.
    const TopoDS_Shape localSub = stored.getSubShape(subName.c_str(), /*silent*/ true);
    if (localSub.IsNull()) {
        return {};
    }

    NRef ref;
    ref.kind = "face";
    ref.featureUid = feature.Uid.getValueStr();
    ref.signature = subShapeSignature(localSub);

    // A role is meaningful only for a feature that knows its faces parametrically.
    // For any other leaf (an import) the role stays empty and the signature is the
    // identity. primitiveBoxFaceRole would happily label any planar face by its
    // centroid direction, so the regime is gated on the feature type, not on
    // whether a role string comes back.
    if (feature.isDerivedFrom<Box>()) {
        ref.role = captureBoxFaceRole(feature, subName);
    }
    return ref;
}

std::string resolveFaceRef(const NRef& ref, const Feature& feature)
{
    if (ref.kind != "face") {
        return {};
    }

    // Regime 1: a role-bearing primitive leaf -- symmetry-proof.
    if (!ref.role.empty()) {
        return resolveBoxFaceRole(feature, ref.role);
    }

    // Regime 2: a history-less leaf -- unique geometric-signature match, read in
    // the same feature-local frame the signature was captured in.
    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull() || ref.signature.empty()) {
        return {};
    }

    const int faceCount = static_cast<int>(stored.countSubShapes(TopAbs_FACE));
    std::string match;
    for (int i = 1; i <= faceCount; ++i) {
        const TopoDS_Shape face = stored.getSubShape(TopAbs_FACE, i, /*silent*/ true);
        if (face.IsNull() || subShapeSignature(face) != ref.signature) {
            continue;
        }
        if (!match.empty()) {
            return {};  // ambiguous: two faces share one signature -> stop, do not guess
        }
        match = "Face" + std::to_string(i);
    }
    return match;
}

namespace
{
// The one delimiter between fields. Chosen because it appears in none of the
// fields: a Uid is hex + hyphens, a role is a signed axis, and a signature is
// letters/digits/':'/','/'-' (Part::subShapeSignature) -- never a pipe.
const std::string neutralPrefix = "NRef|1|";
constexpr char fieldSep = '|';
}  // namespace

std::string toNeutralString(const NRef& ref)
{
    return neutralPrefix + ref.featureUid + fieldSep + ref.kind + fieldSep + ref.role + fieldSep
        + ref.signature;
}

NRef fromNeutralString(const std::string& text)
{
    if (text.rfind(neutralPrefix, 0) != 0) {
        return {};  // wrong magic/version -> not an NRef we know how to read
    }

    // Split the body into uid | kind | role | signature. The signature is the
    // remainder after the third separator, so it survives any delimiter it might
    // itself contain in a future schema.
    const std::string body = text.substr(neutralPrefix.size());
    const std::string::size_type p1 = body.find(fieldSep);
    const std::string::size_type p2 = p1 == std::string::npos ? p1 : body.find(fieldSep, p1 + 1);
    const std::string::size_type p3 = p2 == std::string::npos ? p2 : body.find(fieldSep, p2 + 1);
    if (p3 == std::string::npos) {
        return {};  // too few fields -> malformed
    }

    NRef ref;
    ref.featureUid = body.substr(0, p1);
    ref.kind = body.substr(p1 + 1, p2 - p1 - 1);
    ref.role = body.substr(p2 + 1, p3 - p2 - 1);
    ref.signature = body.substr(p3 + 1);
    return ref;
}

}  // namespace Part
