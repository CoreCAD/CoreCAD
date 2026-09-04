// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cctype>
# include <cstdlib>
# include <sstream>
# include <string>

# include <TopLoc_Location.hxx>
# include <TopoDS_Shape.hxx>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>

#include "NeutralRef.h"
#include "FeaturePartBox.h"
#include "PartFeature.h"
#include "PrimitiveFaceRoleRef.h"
#include "PrimitiveFeature.h"
#include "SubShapeSignature.h"
#include "TopoShape.h"

namespace Part
{

namespace
{
// The primitives whose faces carry a parametric role. An explicit allow-list, not
// "derives from Primitive": that base also covers leaves with no clean per-face
// role (a wedge, a helix, a regular polygon), which stay signature-only.
bool isRoleBearingPrimitive(const ShapeFeature& feature)
{
    return feature.isDerivedFrom<Box>() || feature.isDerivedFrom<Cylinder>()
        || feature.isDerivedFrom<Sphere>() || feature.isDerivedFrom<Cone>()
        || feature.isDerivedFrom<Torus>();
}

// An element-map provenance name embeds, at each "\;:H" postfix, the per-document
// integer tag (in hex) of the object that operation belonged to. That tag is not
// portable: the same authored object carries a different tag in another file. These
// two helpers rewrite between the on-file form (";:U<durable-uid>") and the live
// form (";:H<this-doc-tag-hex>"), leaving every other character of the name -- the
// fork-stable part -- untouched. A tag that names no object, or a Uid found on no
// object, is left in place: it will simply fail to resolve, never silently rebind.

bool isHexDigit(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

// ";:H<tag-hex>" -> ";:U<uid>": the capture direction (live name -> on-file name).
std::string neutralizeProv(const std::string& name, const App::Document& doc)
{
    const std::string mark = ";:H";
    std::string out;
    out.reserve(name.size());
    std::string::size_type i = 0;
    while (i < name.size()) {
        const std::string::size_type at = name.find(mark, i);
        if (at == std::string::npos) {
            out.append(name, i, std::string::npos);
            break;
        }
        out.append(name, i, at - i);  // text before the postfix

        std::string::size_type j = at + mark.size();
        if (j < name.size() && name[j] == '-') {
            ++j;  // a negative (external) tag; kept verbatim below
        }
        const std::string::size_type hexStart = at + mark.size();
        while (j < name.size() && isHexDigit(name[j])) {
            ++j;
        }
        const std::string token = name.substr(hexStart, j - hexStart);

        App::DocumentObject* obj = nullptr;
        if (!token.empty() && token[0] != '-') {
            obj = doc.getObjectByID(static_cast<long>(std::strtoll(token.c_str(), nullptr, 16)));
        }
        if (obj != nullptr) {
            out += ";:U";
            out += obj->Uid.getValueStr();
        }
        else {
            out += mark;
            out += token;  // unresolved -> verbatim
        }
        i = j;
    }
    return out;
}

// ";:U<uid>" -> ";:H<tag-hex>": the resolve direction (on-file name -> live name).
std::string denormalizeProv(const std::string& prov, const App::Document& doc)
{
    const std::string mark = ";:U";
    std::string out;
    out.reserve(prov.size());
    std::string::size_type i = 0;
    while (i < prov.size()) {
        const std::string::size_type at = prov.find(mark, i);
        if (at == std::string::npos) {
            out.append(prov, i, std::string::npos);
            break;
        }
        out.append(prov, i, at - i);

        std::string::size_type j = at + mark.size();
        while (j < prov.size() && (isHexDigit(prov[j]) || prov[j] == '-')) {
            ++j;
        }
        const std::string uid = prov.substr(at + mark.size(), j - (at + mark.size()));

        App::DocumentObject* found = nullptr;
        for (App::DocumentObject* o : doc.getObjects()) {
            if (o->Uid.getValueStr() == uid) {
                found = o;
                break;
            }
        }
        if (found != nullptr) {
            std::ostringstream hex;
            hex << std::hex << found->getID();
            out += ";:H";
            out += hex.str();
        }
        else {
            out += mark;
            out += uid;  // unresolved -> verbatim
        }
        i = j;
    }
    return out;
}
}  // namespace

NRef captureFaceRef(const ShapeFeature& feature, const std::string& subName)
{
    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull() || subName.empty()) {
        return {};
    }

    // The signature is read in the feature's own frame per Amendment 4 -- the shape's
    // location taken back off the face -- the same frame resolveFaceRef reads it back
    // in. Where the feature sits is not part of what its face is: read in place, a
    // reference would be lost the moment the user moved the thing it points at.
    const TopoDS_Shape sub = stored.getSubShape(subName.c_str(), /*silent*/ true);
    if (sub.IsNull()) {
        return {};
    }
    const TopoDS_Shape localSub = sub.Moved(stored.getShape().Location().Inverted());

    NRef ref;
    ref.kind = "face";
    ref.featureUid = feature.Uid.getValueStr();
    ref.signature = subShapeSignature(localSub);

    // A role is meaningful only for a feature that knows its faces parametrically.
    // For any other leaf (an import) the role stays empty and the signature is the
    // identity. primitiveFaceRole would happily label any planar face by its
    // centroid direction, so the regime is gated on the feature type -- an explicit
    // allow-list of the primitives whose faces have parametric roles -- not on
    // whether a role string comes back.
    // A role-bearing primitive is a frame anchor, so it is a placed Part::Feature;
    // the role regime reads its parametric faces through that placed type. The
    // guard above establishes the downcast.
    if (isRoleBearingPrimitive(feature)) {
        ref.role = capturePrimitiveFaceRole(static_cast<const Feature&>(feature), subName);
    }

    // Derived regime: a face produced by an operation carries a provenance name in
    // the element map. Neutralize its per-document object tags to durable Uids so the
    // name travels between files. A leaf with no history has no ";:H" op tag -- and a
    // bare primitive has no element map at all -- so prov stays empty there.
    if (const App::Document* doc = feature.getDocument()) {
        const Data::MappedName mapped = stored.getMappedName(Data::IndexedName(subName.c_str()));
        const std::string raw = mapped.toString();
        if (raw.find(";:H") != std::string::npos) {
            ref.prov = neutralizeProv(raw, *doc);
        }
    }
    return ref;
}

NRefResolution resolveFaceRef(const NRef& ref, const ShapeFeature& feature)
{
    // A ref that names no face asks nothing, which is not the same as asking and
    // getting no answer: a feature holding one has simply never captured a
    // reference yet, and must not be reported as having lost one.
    if (ref.kind != "face") {
        return {RefMatch::None, {}};
    }

    // Regime 1: a role-bearing primitive leaf -- symmetry-proof. Only a placed
    // Part::Feature can carry a role; a ref bearing one that is aimed at an
    // unplaced derived feature is unbound, never guessed at through another regime.
    if (!ref.role.empty()) {
        if (!feature.isDerivedFrom<Feature>()) {
            return {RefMatch::Lost, {}};
        }
        std::string sub = resolvePrimitiveFaceRole(static_cast<const Feature&>(feature), ref.role);
        if (sub.empty()) {
            return {RefMatch::Lost, {}};
        }
        return {RefMatch::Matched, std::move(sub)};
    }

    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull()) {
        return {RefMatch::Lost, {}};
    }

    // Regime 2: a derived face -- resolve its provenance name through the element
    // map, after rewriting the durable Uids back to this document's object tags.
    if (!ref.prov.empty()) {
        const App::Document* doc = feature.getDocument();
        if (doc == nullptr) {
            return {RefMatch::Lost, {}};
        }
        const Data::MappedName local(denormalizeProv(ref.prov, *doc));
        const Data::IndexedName idx = stored.getIndexedName(local);
        if (!idx) {
            // provenance name no longer maps to any face -> unbound, no guess
            return {RefMatch::Lost, {}};
        }
        return {RefMatch::Matched, std::string(idx.getType()) + std::to_string(idx.getIndex())};
    }

    // Regime 3: a history-less leaf -- unique geometric-signature match, read in
    // the same feature-local frame the signature was captured in.
    if (ref.signature.empty()) {
        return {RefMatch::Lost, {}};
    }

    const int faceCount = static_cast<int>(stored.countSubShapes(TopAbs_FACE));
    const TopLoc_Location toOwnFrame = stored.getShape().Location().Inverted();
    std::string match;
    for (int i = 1; i <= faceCount; ++i) {
        const TopoDS_Shape face = stored.getSubShape(TopAbs_FACE, i, /*silent*/ true);
        if (face.IsNull() || subShapeSignature(face.Moved(toOwnFrame)) != ref.signature) {
            continue;
        }
        if (!match.empty()) {
            // two faces share one signature -> the user's call, not ours to guess
            return {RefMatch::Ambiguous, {}};
        }
        match = "Face" + std::to_string(i);
    }
    if (match.empty()) {
        return {RefMatch::Lost, {}};
    }
    return {RefMatch::Matched, std::move(match)};
}

namespace
{
// The one delimiter between fields. Chosen because it appears in none of the
// fields: a Uid is hex + hyphens, a role is a signed axis, and a signature is
// letters/digits/':'/','/'-' (Part::subShapeSignature) -- never a pipe.
const std::string neutralPrefix = "NRef|2|";
constexpr char fieldSep = '|';
}  // namespace

std::string toNeutralString(const NRef& ref)
{
    return neutralPrefix + ref.featureUid + fieldSep + ref.kind + fieldSep + ref.role + fieldSep
        + ref.prov + fieldSep + ref.signature;
}

NRef fromNeutralString(const std::string& text)
{
    if (text.rfind(neutralPrefix, 0) != 0) {
        return {};  // wrong magic/version -> not an NRef we know how to read
    }

    // Split the body into uid | kind | role | prov | signature. The signature is the
    // remainder after the fourth separator, so it survives any delimiter it might
    // itself contain in a future schema.
    const std::string body = text.substr(neutralPrefix.size());
    const std::string::size_type p1 = body.find(fieldSep);
    const std::string::size_type p2 = p1 == std::string::npos ? p1 : body.find(fieldSep, p1 + 1);
    const std::string::size_type p3 = p2 == std::string::npos ? p2 : body.find(fieldSep, p2 + 1);
    const std::string::size_type p4 = p3 == std::string::npos ? p3 : body.find(fieldSep, p3 + 1);
    if (p4 == std::string::npos) {
        return {};  // too few fields -> malformed
    }

    NRef ref;
    ref.featureUid = body.substr(0, p1);
    ref.kind = body.substr(p1 + 1, p2 - p1 - 1);
    ref.role = body.substr(p2 + 1, p3 - p2 - 1);
    ref.prov = body.substr(p3 + 1, p4 - p3 - 1);
    ref.signature = body.substr(p4 + 1);
    return ref;
}

NRefBinding bindInDocument(const NRef& ref, const App::Document& doc)
{
    if (ref.kind.empty() || ref.featureUid.empty()) {
        return {};  // null ref names no feature to bind
    }

    // The Uid is the join key: find the feature that carries this identity, whatever
    // face ordinals its own rebuild produced, and resolve the sub-name on it.
    for (const ShapeFeature* feature : doc.getObjectsOfType<ShapeFeature>()) {
        if (feature->Uid.getValueStr() != ref.featureUid) {
            continue;
        }
        const NRefResolution resolved = resolveFaceRef(ref, *feature);
        if (resolved.match != RefMatch::Matched) {
            // right feature, but its sub-shape is gone or ambiguous -> unbound, not a guess
            return {};
        }
        return {feature, resolved.subName};
    }
    return {};  // no feature in this document carries the ref's identity
}

}  // namespace Part
