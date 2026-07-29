// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
 *   Copyright (c) 2026 Cruth contributors                                 *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include <algorithm>
#include <array>

#include <map>

#include <cmath>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <App/VarSet.h>
#include <App/Origin.h>
#include <App/OriginGroupExtension.h>
#include <App/PropertyLinks.h>
#include <Base/Color.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>
#include <Base/Tools.h>
#include <Base/Uuid.h>

#include <Mod/Part/App/AttachExtension.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/TopoShape.h>

#include <App/GeoFeature.h>

#include "Feature.h"
#include "FeaturePocket.h"

#include "Body.h"
#include "BodyPy.h"
#include "FeatureBakedShape.h"
#include "FeatureBase.h"
#include "FeatureBoolean.h"
#include "FeatureSketchBased.h"
#include "FeatureSolid.h"
#include "FeatureTransformed.h"
#include "ShapeBinder.h"

using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Body, Part::BodyBase)

namespace
{
// Cruth §4.6 palette — 8 distinguishable Body identity colours.
// Order: blue, orange, green, purple, teal, magenta, gold, slate.
constexpr std::array<std::array<float, 3>, 8> bodyPalette = {{
    {0.30F, 0.55F, 0.90F},
    {0.95F, 0.60F, 0.20F},
    {0.40F, 0.75F, 0.40F},
    {0.65F, 0.45F, 0.85F},
    {0.25F, 0.70F, 0.70F},
    {0.90F, 0.40F, 0.70F},
    {0.85F, 0.75F, 0.25F},
    {0.50F, 0.55F, 0.65F},
}};

Base::Color paletteColorFor(std::size_t index)
{
    const auto& rgb = bodyPalette[index % bodyPalette.size()];
    return Base::Color(rgb[0], rgb[1], rgb[2], 1.0F);
}

// Cruth §11 step 5e: retarget a feature's origin/datum links onto the given shared Origin.
// Ported from the retired OriginGroupExtension::relinkToOrigin — it walks the feature's link
// properties and replaces any link pointing at an origin datum element (matched by Role) with
// the equivalent element of `origin`; subnames are unchanged. Under the shared-Origin model
// this is normally a no-op (there is only one Origin), but it keeps a moved or legacy feature
// that referenced a different Origin correctly anchored.
void relinkFeatureToOrigin(App::DocumentObject* obj, App::Origin* origin)
{
    if (!origin) {
        return;
    }
    auto isOriginFeature = [](App::DocumentObject* o) -> bool {
        if (auto* datumElement = dynamic_cast<App::DatumElement*>(o)) {
            return datumElement->isOriginFeature();
        }
        return false;
    };

    std::vector<App::Property*> list;
    obj->getPropertyList(list);
    for (App::Property* prop : list) {
        if (prop->isDerivedFrom<App::PropertyLink>()) {
            auto p = static_cast<App::PropertyLink*>(prop);
            if (!p->getValue() || !isOriginFeature(p->getValue())) {
                continue;
            }
            p->setValue(origin->getDatumElement(
                static_cast<App::DatumElement*>(p->getValue())->Role.getValue()
            ));
        }
        else if (prop->isDerivedFrom<App::PropertyLinkList>()) {
            auto p = static_cast<App::PropertyLinkList*>(prop);
            auto vec = p->getValues();
            std::vector<App::DocumentObject*> result;
            bool changed = false;
            for (App::DocumentObject* o : vec) {
                if (!isOriginFeature(o)) {
                    result.push_back(o);
                }
                else {
                    result.push_back(
                        origin->getDatumElement(static_cast<App::DatumElement*>(o)->Role.getValue())
                    );
                    changed = true;
                }
            }
            if (changed) {
                p->setValues(result);
            }
        }
        else if (prop->isDerivedFrom<App::PropertyLinkSub>()) {
            auto p = static_cast<App::PropertyLinkSub*>(prop);
            if (!p->getValue() || !isOriginFeature(p->getValue())) {
                continue;
            }
            std::vector<std::string> subValues = p->getSubValues();
            p->setValue(
                origin->getDatumElement(
                    static_cast<App::DatumElement*>(p->getValue())->Role.getValue()
                ),
                subValues
            );
        }
        else if (prop->isDerivedFrom<App::PropertyLinkSubList>()) {
            auto p = static_cast<App::PropertyLinkSubList*>(prop);
            auto vec = p->getSubListValues();
            bool changed = false;
            for (auto& v : vec) {
                if (isOriginFeature(v.first)) {
                    v.first = origin->getDatumElement(
                        static_cast<App::DatumElement*>(v.first)->Role.getValue()
                    );
                    changed = true;
                }
            }
            if (changed) {
                p->setSubListValues(vec);
            }
        }
    }
}

// Cruth §8.5 anchor-walk recursion cap. Realistic chains are
// sketch → datum → datum → body face (3 hops); 4 gives safety margin against
// pathological user-created datum cycles.
constexpr int MaxAnchorWalkDepth = 4;

// Cruth §8.5 anchor walk. Recurses through a feature's attachment chain,
// collecting any Bodies the chain terminates on. Returns true if at least one
// branch ends at a global plane, free datum, or otherwise unanchored geometry —
// the signal to spawn a new Body when no Body is found.
//
// Order of checks matters: a Part::Datum has AttachExtension and is also a
// Part::Feature, so the AttachExtension branch must come first to avoid treating
// it as a solid feature.
bool walkAnchorChain(App::DocumentObject* obj, std::set<PartDesign::Body*>& bodies, int depth)
{
    if (!obj || depth > MaxAnchorWalkDepth) {
        return true;
    }

    auto* attach = obj->getExtensionByType<Part::AttachExtension>(true);
    if (attach) {
        const auto& support = attach->AttachmentSupport.getValues();
        if (support.empty()) {
            return true;
        }
        bool reachedGlobal = false;
        for (auto* link : support) {
            if (walkAnchorChain(link, bodies, depth + 1)) {
                reachedGlobal = true;
            }
        }
        return reachedGlobal;
    }

    if (obj->isDerivedFrom(App::DatumElement::getClassTypeId())) {
        return true;
    }

    if (obj->isDerivedFrom(Part::ShapeFeature::getClassTypeId())) {
        // A Body is itself a ShapeFeature (via BodyBase) and provides the displayed
        // solid, so a datum/sketch attached to a face of its own Body anchors onto the
        // Body object. It is its own answer: never findBodyOf a Body, which re-enters
        // getFullModel and recurses without bound (getFullModel → walkAnchorChain →
        // findBodyOf → BodyBase::findBodyOf → getFullModel), SIGSEGV on create and on
        // document load (#46).
        if (auto* body = freecad_cast<Body*>(obj)) {
            bodies.insert(body);
            return false;
        }
        if (auto* body = PartDesign::Body::findBodyOf(obj)) {
            bodies.insert(body);
            return false;
        }
        return true;
    }

    return true;
}

// Encode a provenance root-set as one deterministic, reversible string for the TipComponentId
// slot. Each root is length-prefixed ("<n>#<root>"), so a root containing any delimiter character
// cannot corrupt the join; the set is already sorted (std::set), so the encoding is order-
// independent and stable across recompute. Reversed by parseProvenance (the piece-3 matcher).
std::string serializeProvenance(const std::set<std::string>& roots)
{
    std::string out;
    for (const auto& root : roots) {
        out += std::to_string(root.size());
        out += '#';
        out += root;
    }
    return out;
}

// Reverse of serializeProvenance. Returns an empty set on any string that is not this format
// (e.g. a pattern's face-name instance-selector, or the empty whole-shape id) so a non-provenance
// component-id is simply treated as "no stored ancestry to match" rather than mis-parsed.
std::set<std::string> parseProvenance(const std::string& encoded)
{
    std::set<std::string> roots;
    std::size_t pos = 0;
    while (pos < encoded.size()) {
        const std::size_t hash = encoded.find('#', pos);
        if (hash == std::string::npos || hash == pos) {
            return {};  // not our length-prefixed format
        }
        std::size_t len = 0;
        for (std::size_t k = pos; k < hash; ++k) {
            if (encoded[k] < '0' || encoded[k] > '9') {
                return {};
            }
            len = len * 10 + static_cast<std::size_t>(encoded[k] - '0');
        }
        if (hash + 1 + len > encoded.size()) {
            return {};  // truncated / not our format
        }
        roots.insert(encoded.substr(hash + 1, len));
        pos = hash + 1 + len;
    }
    return roots;
}

// True iff two sorted root-sets share at least one root. Used for MATERIAL descent (did this solid
// grow from this donor at all), not for identity matching.
bool provenanceOverlaps(const std::set<std::string>& a, const std::set<std::string>& b)
{
    if (a.empty() || b.empty()) {
        return false;
    }
    auto ia = a.begin();
    auto ib = b.begin();
    while (ia != a.end() && ib != b.end()) {
        if (*ia < *ib) {
            ++ia;
        }
        else if (*ib < *ia) {
            ++ib;
        }
        else {
            return true;
        }
    }
    return false;
}

// True iff every root of @p sub is present in @p sup (sub ⊆ sup). This — not mere overlap — is the
// identity match: a Body CONTINUES onto the one solid that holds ALL its provenance (§4.3 "one
// solid holds all of it"). Severed halves share base-bar roots, so overlap alone would match a half
// to both solids; requiring the whole set distinguishes them by each half's own distinct roots.
bool isProvenanceSubset(const std::set<std::string>& sub, const std::set<std::string>& sup)
{
    if (sub.empty()) {
        return false;  // no ancestry to match — never continues (fail-safe)
    }
    return std::includes(sup.begin(), sup.end(), sub.begin(), sub.end());
}

// Return the solid sub-shape whose component key matches, or a null shape if none. The key is
// resolved via Body::componentKeyOfSolid, so it matches whatever the reconciler stamped —
// native-ancestry provenance for a built-geometry Tip, the instance-selector for a pattern.
Part::TopoShape extractSolidById(
    const App::DocumentObject* tipFeature,
    const Part::TopoShape& shape,
    const std::string& cid
)
{
    const auto count = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
    for (int i = 1; i <= count; ++i) {
        if (Body::componentKeyOfSolid(tipFeature, shape, i) == cid) {
            return shape.getSubTopoShape(TopAbs_SOLID, i, /*silent*/ true);
        }
    }
    return Part::TopoShape();
}

// Guards reconcileMultiOutput against re-entry while it spawns/recomputes Bodies.
bool g_reconciling = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace

Body::Body()
{
    ADD_PROPERTY_TYPE(
        Color,
        (paletteColorFor(0)),
        "Base",
        App::Prop_None,
        "Body identity colour, auto-assigned at spawn from a deterministic palette"
    );
    ADD_PROPERTY_TYPE(
        TipComponentId,
        (""),
        "Base",
        App::Prop_None,
        "Cruth §3.3 component-id half of the (feature, component-id) Tip identity; empty means "
        "the implicit single-component case"
    );
    // Cruth §8.2: mint a durable body UUID at birth. On file load the persisted value restores
    // over this freshly-minted one (same pattern as App::Document::Uid). Read-only — never
    // recomputed, so body identity is robust to topology changes by construction (§13.1).
    Base::Uuid bodyId;
    ADD_PROPERTY_TYPE(
        Uid,
        (bodyId),
        "Base",
        App::Prop_ReadOnly,
        "Cruth §8.2 durable body identity; minted once at birth, persisted, never recomputed"
    );

    // Cruth §8.6: dismissed spatial-interference partners (other Bodies' durable Uids). Hidden
    // document state — persisted, but never a user-facing property and never a recompute input, so
    // NoRecompute keeps a dismissal from touching geometry (§8.6: not a model concern).
    ADD_PROPERTY_TYPE(
        AcknowledgedOverlaps,
        (),
        "Base",
        static_cast<App::PropertyType>(App::Prop_Hidden | App::Prop_NoRecompute),
        "Cruth §8.6 durable Uids of Bodies whose overlap with this one the user acknowledged"
    );

    // (Cruth §11 step 5e) The Group property and its _GroupTouched companion are gone entirely
    // now the OriginGroup extension is retired — nothing left to mark Transient/Output. Members
    // are derived from the BaseFeature chain (§9.1-inverse).

    // Cruth substrate flip, Stage 3b step 4. A de-owned Body carries no coordinate frame of
    // its own: de-owned features are not Group members, so the Body's placement never enters
    // a feature's global frame (globalGroupPlacement walks group membership, which is empty)
    // — the Day-1 Experiment-C finding. The onChanged guard already pins any live edit back to
    // identity; stop persisting the property too, so a legacy non-identity Body.Placement can't
    // be read back off disk and silently diverge from its features. Property lives on the shared
    // GeoFeature base and is retired for Body in step 5; this only stops its content being saved.
    Placement.setStatus(App::Property::Transient, true);
}

Body* Body::spawnAutoBody(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }

    // Fail early, before creating anything: a Body requires the document's shared world frame,
    // and it must never mint one. Checking here means an attempt to spawn into a non-CAD
    // document throws with no side effect, instead of leaving a half-created Body behind when
    // the setupObject backstop trips after addObject has already registered the object.
    requireDocumentOrigin(doc);

    auto name = doc->getUniqueObjectName("Body");
    auto* body = freecad_cast<Body*>(doc->addObject("PartDesign::Body", name.c_str()));
    // Color is assigned by setupObject() from the per-document palette index.
    return body;
}

Body* Body::moveFeatureToBody(App::DocumentObject* feature, Body* target)
{
    if (!feature) {
        return nullptr;
    }

    Body* from = findBodyOf(feature);
    if (target && from == target) {
        return target;  // already there — nothing to do
    }

    App::Document* doc = feature->getDocument();
    if (!doc) {
        return nullptr;
    }

    // Detach first. removeFeature heals the source chain and, if this was the source
    // Body's last feature, retires it (§4.7) — never touch `from` after this call.
    if (from) {
        from->removeFeature(feature);
    }

    // Null target = the "Merge result off" case: the feature starts a fresh Body (§4.6).
    if (!target) {
        target = spawnAutoBody(doc);
        if (!target) {
            return nullptr;
        }
    }

    target->addFeature(feature);
    return target;
}

Body* Body::breakOutInstance(Body* instanceBody)
{
    if (!instanceBody) {
        return nullptr;
    }
    App::Document* doc = instanceBody->getDocument();
    if (!doc) {
        return nullptr;
    }

    // The instance's Tip must be a multi-output pattern feature, and the Body must
    // name one emitted instance via its component-id (§3.3).
    auto* pattern = freecad_cast<PartDesign::Transformed*>(instanceBody->Tip.getValue());
    if (!pattern) {
        return nullptr;
    }
    const std::string cid = instanceBody->TipComponentId.getStrValue();
    if (cid.empty()) {
        return nullptr;
    }

    // Capture the instance's solid straight from the instance Body's own derived shape:
    // it is already the pattern component for this cid with the instance offset baked
    // into the geometry (derivedTipShape does that bake) and the element map intact.
    // Capture before recording the skip — once skipped, neither the pattern nor this
    // Body emit it any more.
    Part::TopoShape captured = instanceBody->derivedTipShape();
    if (captured.countSubShapes(TopAbs_SOLID) >= 1) {
        captured = captured.getSubTopoShape(TopAbs_SOLID, 1, /*silent*/ true);
    }
    if (captured.isNull()) {
        return nullptr;
    }

    // Re-home the captured solid into a frozen BakedShape (§7.8) inside a fresh,
    // independent Body. The BakedShape has no input link, so the new Body is
    // severed from the pattern and its base.
    auto* baked = freecad_cast<PartDesign::BakedShape*>(
        doc->addObject("PartDesign::BakedShape", "BakedShape")
    );
    if (!baked) {
        return nullptr;
    }
    baked->StoredShape.setValue(captured);

    Body* newBody = spawnAutoBody(doc);
    if (!newBody) {
        doc->removeObject(baked->getNameInDocument());
        return nullptr;
    }
    // The new Body lives directly in the document (marker model, §7.1): nothing to
    // re-parent — a body is not nested in a container.
    newBody->addFeature(baked);  // also points the new Body's Tip at the BakedShape

    // Record the skip (§5.6) so the pattern drops this instance, then recompute. The pattern's
    // skip-list keys on each instance's ORIGINAL ordinal (ARCHITECTURE §5.6/§11.2), not its
    // component-id: the element-map id is only self-consistent against the pattern's own stored
    // shape, so translate the selected Body's id to its ordinal HERE, once, where the ids agree,
    // and store the stable ordinal. The originating Body becomes an orphan and is retired by the
    // reconciler (§4.7).
    const Part::TopoShape patShape = pattern->Shape.getShape();
    const auto solidCount = static_cast<int>(patShape.countSubShapes(TopAbs_SOLID));
    int keptPos = -1;  // the selected instance's position among the pattern's CURRENTLY-kept solids
    for (int i = 1; i <= solidCount; ++i) {
        if (Body::componentIdOfSolid(patShape, i) == cid) {
            keptPos = i - 1;
            break;
        }
    }
    if (keptPos < 0) {
        // The Body's component is not present in the pattern output (already broken out or
        // retired); nothing to skip. The freshly baked independent Body still stands.
        return newBody;
    }
    // Map the kept-position back to an original ordinal by stepping over already-skipped ones:
    // the ordinal space spans every generated instance, the kept space only the survivors.
    std::vector<long> skips = pattern->SkipInstances.getValues();
    const std::set<long> skipSet(skips.begin(), skips.end());
    long ordinal = 0;
    for (int seen = 0;; ++ordinal) {
        if (skipSet.count(ordinal)) {
            continue;
        }
        if (seen == keptPos) {
            break;
        }
        ++seen;
    }
    skips.push_back(ordinal);
    pattern->SkipInstances.setValues(skips);
    doc->recompute();

    return newBody;
}

// Cruth §3.3 component-id. OCCT element maps name faces/edges/vertices but not solids,
// so identity is anchored to the solid's lexicographically-smallest mapped face name.
std::string Body::componentIdOfSolid(const Part::TopoShape& shape, int index)
{
    const Part::TopoShape solid = shape.getSubTopoShape(TopAbs_SOLID, index, /*silent*/ true);
    if (!solid.isNull()) {
        std::string best;
        const auto faceCount = static_cast<int>(solid.countSubShapes(TopAbs_FACE));
        for (int f = 1; f <= faceCount; ++f) {
            const Data::MappedName mapped = solid.getMappedName(Data::IndexedName("Face", f));
            if (!mapped.empty()) {
                const std::string name = mapped.toString();
                if (best.empty() || name < best) {
                    best = name;
                }
            }
        }
        if (!best.empty()) {
            return best;
        }
    }
    return std::string("Solid") + std::to_string(index);
}

std::set<std::string> Body::provenanceOfSolid(const Part::TopoShape& solid)
{
    std::set<std::string> roots;
    if (solid.isNull()) {
        return roots;
    }
    // Each face carries an element-map name that records what it grew from. One history hop
    // (getElementHistory) yields the SOURCE object's tag and the ORIGINAL element token there —
    // for a sketch-consuming feature that is the sketch geometry itself (the stable root); for a
    // solid-consuming feature it is the intermediate feature's face (still a stable, recompute-
    // invariant key, just shallower — the recursive walk to the ultimate sketch root is the
    // documented follow-on). The (tag, token) pair is the root key; a bare token would collide
    // across sources (two sketches both start at "g1"), so the tag is required.
    const auto faceCount = static_cast<int>(solid.countSubShapes(TopAbs_FACE));
    for (int f = 1; f <= faceCount; ++f) {
        const Data::MappedName mapped = solid.getMappedName(Data::IndexedName("Face", f));
        if (mapped.empty()) {
            continue;
        }
        Data::MappedName original;
        const long tag = solid.getElementHistory(mapped, &original);
        if (tag == 0 && original.empty()) {
            continue;
        }
        roots.insert(std::to_string(tag) + ":" + original.toString());
    }
    return roots;
}

std::string Body::componentKeyOfSolid(
    const App::DocumentObject* tipFeature,
    const Part::TopoShape& shape,
    int index
)
{
    // §4.5: pattern/mirror copies all grow from one seed, so their provenance is identical —
    // lineage cannot tell them apart. They keep the element-map-name instance-selector. Built
    // geometry (Pad/Pocket/sever) uses the native-ancestry provenance (§4.3), which is stable
    // across recompute where a bare face name drifts. The face name is also the fallback when
    // provenance is unavailable (an import or baked shape has no element history), so extraction
    // still resolves.
    const bool isPattern = freecad_cast<PartDesign::Transformed*>(
                               const_cast<App::DocumentObject*>(tipFeature)
                           )
        != nullptr;
    if (!isPattern) {
        const Part::TopoShape solid = shape.getSubTopoShape(TopAbs_SOLID, index, /*silent*/ true);
        const std::set<std::string> prov = provenanceOfSolid(solid);
        if (!prov.empty()) {
            return serializeProvenance(prov);
        }
    }
    return componentIdOfSolid(shape, index);
}

bool Body::toolReaches(const Part::TopoShape& tool, const Part::TopoShape& bodyShape)
{
    if (tool.isNull() || bodyShape.isNull()) {
        return false;
    }
    // The reach test is set intersection of the two solids: they are "reached" only if they share
    // positive volume, so cutting the tool would actually change the Body. A boolean common yields
    // an empty compound (mass 0) for disjoint solids and a zero-volume face/edge for mere surface
    // contact — both correctly read as "not reached". A boolean failure is treated as "not reached"
    // rather than propagated: the reach test is a pre-flight for the gesture, not the cut itself.
    TopoDS_Shape inter;
    try {
        inter = tool.common(bodyShape.getShape());
    }
    catch (const Standard_Failure&) {
        return false;
    }
    if (inter.IsNull()) {
        return false;
    }
    GProp_GProps props;
    BRepGProp::VolumeProperties(inter, props);
    return props.Mass() > Precision::Confusion();
}

std::vector<std::pair<Body*, Body*>> Body::findInterferingPairs(App::Document* doc)
{
    // Cruth §8.6: distinct Bodies overlapping in space without a topological merge. A pure geometry
    // sweep — no state read, no recompute touched (§8.6: detection is a UI concern, not a model one).
    std::vector<std::pair<Body*, Body*>> pairs;
    if (!doc) {
        return pairs;
    }

    // Collect each candidate Body once with its world-frame shape and bounding box. Skip Bodies
    // with no solid (a marker mid-edit or a degenerate compute) — there is nothing to interfere.
    std::vector<Body*> bodies;
    std::vector<Part::TopoShape> shapes;
    std::vector<Bnd_Box> boxes;
    for (auto* obj : doc->getObjectsOfType(Body::getClassTypeId())) {
        auto* body = static_cast<Body*>(obj);
        Part::TopoShape shape = body->derivedTipShape();
        if (shape.isNull() || shape.countSubShapes(TopAbs_SOLID) == 0) {
            continue;
        }
        Bnd_Box box;
        try {
            BRepBndLib::Add(shape.getShape(), box);
        }
        catch (const Standard_Failure&) {
            continue;
        }
        if (box.IsVoid()) {
            continue;
        }
        bodies.push_back(body);
        shapes.push_back(shape);
        boxes.push_back(box);
    }

    // Every unordered pair; the bounding-box reject keeps the costly boolean off far-apart Bodies.
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            if (boxes[i].IsOut(boxes[j])) {
                continue;
            }
            if (toolReaches(shapes[i], shapes[j])) {
                pairs.emplace_back(bodies[i], bodies[j]);
            }
        }
    }
    return pairs;
}

bool Body::isInterferenceDismissed(const Body* a, const Body* b)
{
    // §8.6: the dismissal is symmetric and stored on both sides, but honour either — a one-sided
    // record (e.g. after the other side was edited) still counts. Match on the durable §8.2 Uid.
    if (!a || !b) {
        return false;
    }
    const std::string aid = a->Uid.getValueStr();
    const std::string bid = b->Uid.getValueStr();
    for (const std::string& other : a->AcknowledgedOverlaps.getValues()) {
        if (other == bid) {
            return true;
        }
    }
    for (const std::string& other : b->AcknowledgedOverlaps.getValues()) {
        if (other == aid) {
            return true;
        }
    }
    return false;
}

void Body::dismissInterference(Body* a, Body* b)
{
    if (!a || !b) {
        return;
    }
    const std::string aid = a->Uid.getValueStr();
    const std::string bid = b->Uid.getValueStr();
    if (aid.empty() || bid.empty()) {
        return;
    }
    // Record each on the other, de-duplicated, so the notice stays silent regardless of which side
    // a later query starts from.
    const auto add = [](Body* body, const std::string& partnerId) {
        std::vector<std::string> acks = body->AcknowledgedOverlaps.getValues();
        if (std::ranges::find(acks, partnerId) == acks.end()) {
            acks.push_back(partnerId);
            body->AcknowledgedOverlaps.setValues(acks);
        }
    };
    add(a, bid);
    add(b, aid);
}

std::vector<std::pair<Body*, Body*>> Body::liveInterferingPairs(App::Document* doc)
{
    std::vector<std::pair<Body*, Body*>> live;
    for (const auto& pair : findInterferingPairs(doc)) {
        if (!isInterferenceDismissed(pair.first, pair.second)) {
            live.push_back(pair);
        }
    }
    return live;
}

std::vector<App::DocumentObject*> Body::spawnScopeSiblings(
    App::DocumentObject* tool,
    const std::vector<Body*>& targets,
    const char* booleanType
)
{
    // One gesture, one freshly minted shared inert tag (Clause 5.3). The tagged overload does the
    // work; a Scope edit calls it directly to extend an existing gesture.
    return spawnScopeSiblings(tool, targets, booleanType, Base::Uuid::createUuid());
}

std::vector<App::DocumentObject*> Body::spawnScopeSiblings(
    App::DocumentObject* tool,
    const std::vector<Body*>& targets,
    const char* booleanType,
    const std::string& gestureId
)
{
    std::vector<App::DocumentObject*> siblings;
    if (!tool || targets.empty()) {
        return siblings;
    }
    App::Document* doc = tool->getDocument();
    if (!doc) {
        return siblings;
    }
    siblings.reserve(targets.size());
    for (Body* body : targets) {
        if (!body) {
            continue;
        }
        // Each sibling is an ordinary single-BaseShape Boolean of the gesture's kind (Clause 5.1):
        // its BaseFeature is set to this Body's current Tip by addFeature, and its Tools references
        // the one shared tool. addFeature owns the chain wiring (BaseFeature + Tip advance) — we
        // never write a feature -> Body edge; membership stays derived from the chain.
        auto* cut = static_cast<PartDesign::Boolean*>(doc->addObject("PartDesign::Boolean"));
        cut->Type.setValue(booleanType);
        cut->Tools.setValue(tool);
        cut->GestureId.setValue(gestureId);
        body->addFeature(cut);
        siblings.push_back(cut);
    }
    return siblings;
}

bool Body::profileReaches(App::DocumentObject* profile, const Part::TopoShape& bodyShape)
{
    if (!profile || bodyShape.isNull()) {
        return false;
    }
    // Build the profile face (in world coords) and its plane normal.
    Part::TopoShape face;
    Base::Vector3d normal(0, 0, 1);
    try {
        Part::TopoShape wires = Part::Feature::getTopoShape(
            profile,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (wires.isNull()) {
            return false;
        }
        face = wires.makeElementFace();
        if (face.isNull()) {
            return false;
        }
        // Normal = the sketch plane's Z axis, taken to world coords like the face above.
        auto* geo = freecad_cast<App::GeoFeature*>(profile);
        if (geo) {
            geo->getPlacement().getRotation().multVec(Base::Vector3d(0, 0, 1), normal);
        }
    }
    catch (const Standard_Failure&) {
        return false;
    }
    // Size the swept column to the body so it spans it either way along the normal.
    Bnd_Box box;
    BRepBndLib::Add(bodyShape.getShape(), box);
    if (box.IsVoid()) {
        return false;
    }
    const double span = std::sqrt(box.SquareExtent());
    if (span <= Precision::Confusion()) {
        return false;
    }
    const gp_Vec dir(normal.x, normal.y, normal.z);
    try {
        // Direction-agnostic: the profile reaches the body if its column hits it either way. The
        // cut depth (Length vs ThroughAll) is a property of the spawned Pocket, not of reaching.
        if (toolReaches(face.makeElementPrism(span * dir), bodyShape)) {
            return true;
        }
        return toolReaches(face.makeElementPrism(-span * dir), bodyShape);
    }
    catch (const Standard_Failure&) {
        return false;
    }
}

std::vector<App::DocumentObject*> Body::spawnScopeSiblingsFromProfile(
    App::DocumentObject* profile,
    const std::vector<Body*>& targets,
    const char* pocketType,
    double length
)
{
    // One gesture, one freshly minted shared inert tag (Clause 5.3); tagged overload does the work.
    return spawnScopeSiblingsFromProfile(profile, targets, pocketType, length, Base::Uuid::createUuid());
}

std::vector<App::DocumentObject*> Body::spawnScopeSiblingsFromProfile(
    App::DocumentObject* profile,
    const std::vector<Body*>& targets,
    const char* pocketType,
    double length,
    const std::string& gestureId
)
{
    std::vector<App::DocumentObject*> siblings;
    if (!profile || targets.empty()) {
        return siblings;
    }
    App::Document* doc = profile->getDocument();
    if (!doc) {
        return siblings;
    }
    siblings.reserve(targets.size());
    for (Body* body : targets) {
        if (!body) {
            continue;
        }
        // Each sibling is an ordinary Pocket subtracting the one shared profile from this Body's
        // Tip. addFeature owns the BaseFeature + Tip wiring; the only edge written is Body -> Tip,
        // and the profile is referenced (Profile link), never owned.
        auto* pocket = static_cast<PartDesign::Pocket*>(doc->addObject("PartDesign::Pocket"));
        pocket->Profile.setValue(profile, std::vector<std::string> {""});
        pocket->Type.setValue(pocketType);
        pocket->Length.setValue(length);
        pocket->GestureId.setValue(gestureId);
        body->addFeature(pocket);
        siblings.push_back(pocket);
    }
    return siblings;
}

std::vector<App::DocumentObject*> Body::gestureSiblings(App::Document* doc, const std::string& gestureId)
{
    std::vector<App::DocumentObject*> out;
    if (!doc || gestureId.empty()) {
        return out;
    }
    // The shared inert tag is the only link between a gesture's siblings — there is no membership
    // list to consult. Rediscover them by scanning for the tag; each sibling's ownership still
    // derives from its own Body chain, untouched here.
    for (auto* obj : doc->getObjects()) {
        auto* feat = freecad_cast<PartDesign::Feature*>(obj);
        if (feat && gestureId == feat->GestureId.getValue()) {
            out.push_back(feat);
        }
    }
    return out;
}

void Body::reconcileMultiOutput(App::Document* doc, const std::vector<App::DocumentObject*>& recomputed)
{
    if (!doc || g_reconciling) {
        return;
    }
    Base::StateLocker guard(g_reconciling);

    // All Bodies in the document, so we can find which ones point at a given Tip.
    std::vector<Body*> allBodies;
    for (auto* obj : doc->getObjectsOfType(Body::getClassTypeId())) {
        allBodies.push_back(static_cast<Body*>(obj));
    }
    if (allBodies.empty()) {
        return;
    }

    for (auto* obj : recomputed) {
        auto* feature = freecad_cast<PartDesign::Feature*>(obj);
        if (!feature) {
            continue;
        }

        // Bodies whose Tip is this feature. Only a Tip feature's components spawn
        // Bodies; a mid-chain feature is referenced by none and is skipped.
        std::vector<Body*> bodies;
        for (auto* body : allBodies) {
            if (body->Tip.getValue() == feature) {
                bodies.push_back(body);
            }
        }
        if (bodies.empty()) {
            continue;
        }

        const Part::TopoShape shape = feature->Shape.getShape();
        if (shape.isNull()) {
            continue;
        }
        const auto solidCount = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
        if (solidCount == 0) {
            // A non-null shape with no solid is a degenerate/empty compute, not a topology
            // decision; leave identity untouched rather than delete a Body on a transient state.
            continue;
        }

        // Cruth Amendment 3 §3.3 — a stored body UUID is re-acquired across a recompute FAIL-SAFE:
        // ONLY when the match is decidable WITHOUT geometric resemblance. The TRIVIAL 1:1 is the
        // cheapest such case — exactly one prior Body naming this Tip and exactly one solid, so the
        // mapping is unambiguous with nothing to match. That Body keeps its UUID (Clause 3.1:
        // identity survives recompute and parameter edits, however far the geometry moved). The
        // multi-output cases are matched by native ancestry just below (§4.3).
        if (bodies.size() == 1 && solidCount == 1) {
            Body* survivor = bodies.front();
            // One solid = the whole shape; its component-id handle is the implicit empty case.
            if (!survivor->TipComponentId.getStrValue().empty()) {
                survivor->TipComponentId.setValue("");
                survivor->recomputeFeature();
                survivor->purgeTouched();
            }
            continue;
        }

        // Cruth Amendment 3 §4.3 NATIVE-ANCESTRY MATCH (built geometry only). A recomputed solid
        // re-links to a prior Body when their stored PROVENANCE overlaps — the match key, never
        // geometry. Descendant-counting (§13 row 1) decides the event: a Body overlapping exactly
        // one solid, that solid overlapping exactly one Body, CONTINUES (keeps its UUID); a Body
        // whose provenance is spread across >=2 solids is a SPLIT; >=2 Bodies into one solid a
        // UNION; the remainder are new / retire. On continue nothing but the stored provenance is
        // refreshed. Everything else is a §4.7 topology event: the Body retires (UUID dies) and a
        // fresh Body is minted per unclaimed solid, its inbound refs surfacing at the feature graph
        // (P7), never silently re-bound. §4.5 pattern copies share provenance (lineage cannot tell
        // them apart), so a pattern Tip skips the match and every prior Body retires — the floor,
        // unchanged; their identity is the instance-selector, a separate concern (#34).
        const bool isPattern = freecad_cast<PartDesign::Transformed*>(feature) != nullptr;

        std::vector<std::set<std::string>> solidProv(static_cast<std::size_t>(solidCount));
        for (int i = 1; i <= solidCount; ++i) {
            solidProv[static_cast<std::size_t>(i - 1)] = provenanceOfSolid(
                shape.getSubTopoShape(TopAbs_SOLID, i, /*silent*/ true)
            );
        }

        std::vector<Body*> solidOwner(static_cast<std::size_t>(solidCount), nullptr);
        std::vector<bool> continues(bodies.size(), false);

        if (!isPattern) {
            // supCount[b] = how many solids hold ALL of Body b's provenance (subset match);
            // supIndex[b] = that solid when exactly one. subCount[s] = how many Bodies solid s
            // holds entirely — >=2 marks a UNION target. A clean CONTINUE is the mutual-unique
            // case: exactly one solid holds the Body, and that solid holds exactly one Body.
            std::vector<int> supCount(bodies.size(), 0);
            std::vector<int> supIndex(bodies.size(), -1);
            std::vector<int> subCount(static_cast<std::size_t>(solidCount), 0);
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                const std::set<std::string> prov = parseProvenance(
                    bodies[b]->TipComponentId.getStrValue()
                );
                for (std::size_t s = 0; s < solidProv.size(); ++s) {
                    if (isProvenanceSubset(prov, solidProv[s])) {
                        ++supCount[b];
                        supIndex[b] = static_cast<int>(s);
                        ++subCount[s];
                    }
                }
            }
            for (std::size_t b = 0; b < bodies.size(); ++b) {
                if (supCount[b] != 1) {
                    continue;  // 0 = split / no-match; >=2 = ambiguous — both fall through to retire
                }
                const auto s = static_cast<std::size_t>(supIndex[b]);
                if (subCount[s] == 1 && !solidOwner[s]) {  // subCount>=2 would be a union
                    continues[b] = true;
                    solidOwner[s] = bodies[b];
                    const std::string key = serializeProvenance(solidProv[s]);
                    if (bodies[b]->TipComponentId.getStrValue() != key) {
                        bodies[b]->TipComponentId.setValue(key);
                    }
                }
            }
        }

        // Snapshot each retiring Body's material + provenance BEFORE removal, so a fresh solid can
        // inherit material from the retired Body it descends from (§4.3: describe-the-part
        // inherits). A former single-solid Body has empty provenance and is a whole-shape donor
        // (e.g. the one bar a sever splits — both halves are its steel).
        struct Donor
        {
            Materials::Material mat;
            std::set<std::string> prov;
            bool wholeShape;
        };
        std::vector<Donor> donors;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            if (!continues[b]) {
                const std::set<std::string> prov = parseProvenance(
                    bodies[b]->TipComponentId.getStrValue()
                );
                donors.push_back({bodies[b]->ShapeMaterial.getValue(), prov, prov.empty()});
            }
        }

        // Warn only when a retired body was actually referenced by something. Retiring a body
        // whose identity nobody pointed at (the routine split/merge case) is silent bookkeeping,
        // not a user-actionable event — the message's whole purpose is "re-pick your references".
        bool anyReferencedRetire = false;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            if (!continues[b] && !bodies[b]->getInList().empty()) {
                anyReferencedRetire = true;
                break;
            }
        }
        if (anyReferencedRetire) {
            Base::Console().warning(
                "Cruth Amendment 3 §4.3: feature '%s' changed body topology; identities that could "
                "not be matched by ancestry were reset. Re-pick any references to the retired "
                "bodies.\n",
                feature->getNameInDocument()
            );
        }

        // Retire every prior Body that did not continue. A marker owns nothing, so removal breaks
        // no property refs (P7 surfaces any dangling inbound link at the feature graph).
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            if (!continues[b]) {
                doc->removeObject(bodies[b]->getNameInDocument());
            }
        }

        // Spawn a fresh Body per unclaimed solid — split children, union results, new components.
        // Identity resets (fresh name/UUID/colour via spawnAutoBody+setupObject, fresh component-id
        // here); material inherits only when the donors for this solid agree on one.
        const bool multiSolid = solidCount > 1;
        for (std::size_t s = 0; s < solidProv.size(); ++s) {
            if (solidOwner[s]) {
                continue;
            }
            Body* body = Body::spawnAutoBody(doc);
            if (!body) {
                continue;
            }
            body->Tip.setValue(feature);
            // Spawned body lives directly in the document — no container to nest into.
            body->TipComponentId.setValue(
                multiSolid ? componentKeyOfSolid(feature, shape, static_cast<int>(s) + 1) : ""
            );

            bool haveMat = false;
            bool consistent = true;
            Materials::Material mat;
            for (const Donor& d : donors) {
                if (d.wholeShape || provenanceOverlaps(d.prov, solidProv[s])) {
                    if (!haveMat) {
                        mat = d.mat;
                        haveMat = true;
                    }
                    else if (d.mat.getUUID() != mat.getUUID()) {
                        consistent = false;
                        break;
                    }
                }
            }
            if (haveMat && consistent) {
                body->ShapeMaterial.setValue(mat);
            }
        }

        // Re-extract each Body's component shape under its (possibly new) id. Re-query
        // so freshly spawned Bodies — not in the original `bodies` list — recompute too.
        // We run inside signalRecomputed (the document is still marked Recomputing), so
        // recomputeFeature takes the direct _recomputeFeature path, which computes the
        // shape but does not purge the touched flag — purge explicitly so the document
        // settles instead of looping on perpetually-touched Bodies.
        for (auto* obj : doc->getObjectsOfType(Body::getClassTypeId())) {
            auto* body = static_cast<Body*>(obj);
            if (body->Tip.getValue() == feature) {
                body->recomputeFeature();
                body->purgeTouched();
            }
        }
    }
}

void Body::retireOrRetreatTippedBodies(App::Document* doc, App::DocumentObject* feature)
{
    if (!doc || g_reconciling) {
        return;
    }
    // Only a solid PartDesign feature can be a Body's Tip; anything else tips nothing.
    if (!freecad_cast<PartDesign::Feature*>(feature)) {
        return;
    }

    std::vector<Body*> tipped;
    for (auto* obj : doc->getObjectsOfType(Body::getClassTypeId())) {
        auto* body = static_cast<Body*>(obj);
        if (body->Tip.getValue() == feature) {
            tipped.push_back(body);
        }
    }
    if (tipped.empty()) {
        // Non-Tip feature, or the GUI path already retreated the Tips via removeFeature.
        return;
    }

    // The base solid feature the deleted Tip extended, if any. A pattern/primitive Tip
    // with no BaseFeature has nothing to retreat onto.
    App::DocumentObject* retreatTo = static_cast<PartDesign::Feature*>(feature)->BaseFeature.getValue();

    if (retreatTo) {
        for (auto* body : tipped) {
            body->Tip.setValue(retreatTo);
        }
        if (tipped.size() > 1) {
            // Force the shared base into the next recompute's signalRecomputed set so
            // reconcileMultiOutput runs the §4.3 union that folds the split-children back
            // into one Body — nothing downstream touches the base (the deleted feature was
            // the Tip, so it had no successor to propagate a touch).
            retreatTo->touch();
        }
    }
    else {
        // No base to fall back on: the Body no longer accounts for any solid, so it
        // retires (§4.6). A marker owns nothing — removal breaks no refs (P7 surfaces
        // any dangling inbound link at the feature graph).
        for (auto* body : tipped) {
            doc->removeObject(body->getNameInDocument());
        }
    }
}

namespace
{
// Per-document observer that runs reconcileMultiOutput after every recompute.
// Lives for the process; connections are scoped and dropped when a document
// closes. P8: signalRecomputed fires for both UI and Python recompute paths.
class MultiOutputObserver
{
public:
    void init()
    {
        if (m_initialized) {
            return;
        }
        m_initialized = true;
        auto& app = App::GetApplication();
        m_newDocConn = app.signalNewDocument.connect([this](const App::Document& doc, bool) {
            watch(const_cast<App::Document&>(doc));
        });
        m_delDocConn = app.signalDeleteDocument.connect([this](const App::Document& doc) {
            m_recomputeConns.erase(&doc);
            m_deleteConns.erase(&doc);
        });
        for (auto* doc : app.getDocuments()) {
            watch(*doc);
        }
    }

private:
    void watch(App::Document& doc)
    {
        App::Document* docPtr = &doc;
        m_recomputeConns[docPtr] = doc.signalRecomputed.connect(
            [docPtr](const App::Document&, const std::vector<App::DocumentObject*>& objs) {
                Body::reconcileMultiOutput(docPtr, objs);
            }
        );
        // Raw Document::removeObject of a Tip feature (script/MCP path) bypasses
        // Body::removeFeature; retire or retreat the Bodies it tipped so none survives
        // as a zombie. The Tip still points at the feature when this fires (post-removal
        // from the object map, pre-teardown).
        m_deleteConns[docPtr] = doc.signalDeletedObject.connect(
            [docPtr](const App::DocumentObject& obj) {
                Body::retireOrRetreatTippedBodies(docPtr, const_cast<App::DocumentObject*>(&obj));
            }
        );
    }

    bool m_initialized = false;
    fastsignals::scoped_connection m_newDocConn;
    fastsignals::scoped_connection m_delDocConn;
    std::map<const App::Document*, fastsignals::scoped_connection> m_recomputeConns;
    std::map<const App::Document*, fastsignals::scoped_connection> m_deleteConns;
};

MultiOutputObserver g_multiOutputObserver;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace

void Body::initMultiOutputObserver()
{
    g_multiOutputObserver.init();
}

Body* Body::resolveBaseBody(Part::Part2DObject* sketch, bool& ambiguous)
{
    ambiguous = false;

    std::set<Body*> bodies;
    if (sketch) {
        auto* attach = sketch->getExtensionByType<Part::AttachExtension>(true);
        if (attach) {
            for (auto* link : attach->AttachmentSupport.getValues()) {
                walkAnchorChain(link, bodies, 1);
            }
        }
    }

    if (bodies.size() == 1) {
        return *bodies.begin();
    }
    if (bodies.size() > 1) {
        ambiguous = true;
    }

    // Empty chain (no Body reached) → nullptr with ambiguous == false. This is a
    // pure query: the auto-spawn (§4.6) is the caller's explicit spawnAutoBody()
    // step, run inside its undo transaction so a cancelled feature leaks no Body
    // (#17). Nothing is created here.
    return nullptr;
}

short Body::mustExecute() const
{
    if (Tip.isTouched()) {
        return 1;
    }
    return Part::BodyBase::mustExecute();
}

App::DocumentObject* Body::getPrevSolidFeature(App::DocumentObject* start)
{
    if (!start) {  // default to tip
        start = Tip.getValue();
    }

    if (!start) {  // No Tip
        return nullptr;
    }

    // Cruth de-ownership (Stage 3b-i): walk the BaseFeature chain backward. A de-owned
    // Body has no Group to order features by (the OriginGroup was retired, §11 step 5e),
    // so the chain is the only ordering there is. The chain links solid features
    // directly, so the previous solid is found by following BaseFeature back from
    // `start`, skipping any non-solid link and guarding against cycles.
    // ARCHITECTURE §3.2/§3.3.
    std::set<App::DocumentObject*> seen {start};
    for (auto* pd = freecad_cast<PartDesign::Feature*>(start); pd;) {
        App::DocumentObject* prev = pd->BaseFeature.getValue();
        if (!prev || !seen.insert(prev).second) {
            return nullptr;  // chain end or cycle
        }
        if (isSolidFeature(prev)) {
            return prev;
        }
        pd = freecad_cast<PartDesign::Feature*>(prev);  // skip non-solid, keep walking
    }
    return nullptr;
}

App::DocumentObject* Body::getNextSolidFeature(App::DocumentObject* start)
{
    if (!start) {  // default to tip
        start = Tip.getValue();
    }

    if (!start) {  // no tip
        return nullptr;
    }

    // Cruth de-ownership (Stage 3b-i): the chain successor is the feature whose
    // BaseFeature links back to `start`. Walk forward across any non-solid link,
    // returning the first solid successor; cycle-guarded. Mirrors
    // getNextSolidFeatureByChain but preserves the solid-only filter the callers
    // expect. ARCHITECTURE §3.2/§3.3.
    std::set<App::DocumentObject*> seen {start};
    for (App::DocumentObject* cursor = start; cursor;) {
        App::DocumentObject* next = getNextSolidFeatureByChain(cursor);
        if (!next || !seen.insert(next).second) {
            return nullptr;  // chain end or cycle
        }
        if (isSolidFeature(next)) {
            return next;
        }
        cursor = next;  // skip non-solid, keep walking
    }
    return nullptr;
}

bool Body::isAfterInsertPoint(App::DocumentObject* feature)
{
    App::DocumentObject* nextSolid = getNextSolidFeature();
    assert(feature);

    if (feature == nextSolid) {
        return true;
    }
    else if (!nextSolid) {  // the tip is last solid, we can't be placed after it
        return false;
    }
    else {
        return isAfter(feature, nextSolid);
    }
}

bool Body::isSolidFeature(const App::DocumentObject* obj)
{
    if (!obj) {
        return false;
    }

    if (obj->isDerivedFrom<PartDesign::Feature>()) {
        if (PartDesign::Feature::isDatum(obj)) {
            // Datum objects are not solid
            return false;
        }
        if (auto transFeature = freecad_cast<PartDesign::Transformed*>(obj)) {
            // Transformed Features inside a MultiTransform are not solid features
            return !transFeature->isMultiTransformChild();
        }
        return true;
    }
    return false;  // DeepSOIC: work-in-progress?
}

bool Body::isAllowed(const App::DocumentObject* obj)
{
    if (!obj) {
        return false;
    }

    // TODO: Should we introduce a PartDesign::FeaturePython class? This should then also return
    // true for isSolidFeature()
    return (
        obj->isDerivedFrom<PartDesign::Feature>() || obj->isDerivedFrom<Part::Datum>() ||
        // TODO Shouldn't we replace it with Sketcher::SketchObject? (2015-08-13, Fat-Zer)
        obj->isDerivedFrom<Part::Part2DObject>() || obj->isDerivedFrom<PartDesign::ShapeBinder>()
        || obj->isDerivedFrom<PartDesign::SubShapeBinder>() ||
        // TODO Why this lines was here? why should we allow anything of those? (2015-08-13,
        // Fat-Zer) obj->isDerivedFrom<Part::FeaturePython>() // trouble with this line on Windows!?
        // Linker fails to find getClassTypeId() of the Part::FeaturePython...
        // obj->isDerivedFrom<Part::ShapeFeature>()
        // allow VarSets for parameterization
        obj->isDerivedFrom<App::VarSet>() || obj->isDerivedFrom<App::DatumElement>()
        || obj->isDerivedFrom<App::LocalCoordinateSystem>()
    );
}


std::vector<Body*> Body::bodiesOf(const App::DocumentObject* feature)
{
    // Cruth ownership-query contract — the honest, N-valued reverse lookup. This is a
    // reverse lookup, not an ownership read: a Body only ever points one way, at the Tip it
    // marks. Asking "which Bodies is this feature under" means walking the BaseFeature chain
    // forward from the feature and stopping at the FIRST feature that is some Body's Tip —
    // the nearest downstream marker — then returning EVERY Body naming that Tip. Under
    // de-ownership a single Tip feature can back several Bodies at once, one per output
    // component of a pattern or a severed solid (§4.7), told apart by TipComponentId. The
    // result is a derived view of the current graph, never an attribute the feature carries:
    // nothing feature->Body is stored. De-owned features (ARCHITECTURE §3.2/§3.3) sit in no
    // Group, so this forward walk — not hasObject() — is the source of truth.
    //
    // Stopping at the FIRST downstream Tip (not the chain terminal) is what keeps a
    // cross-body seam correct: where Body B's chain bases on Body A's Tip (via a
    // FeatureBase), A's upstream features must resolve to A — they would otherwise be
    // dragged across the seam to B's Tip at the terminal of the merged chain.
    std::vector<Body*> result;
    if (!feature) {
        return result;
    }

    if (!feature->isDerivedFrom<PartDesign::Feature>()) {
        // Non-PartDesign objects never back multiple Bodies: defer to the base
        // BodyBase::findBodyOf (derived-membership lookup) and wrap its 0-or-1 answer as
        // a list.
        if (auto* body = static_cast<Body*>(BodyBase::findBodyOf(feature))) {
            result.push_back(body);
        }
        return result;
    }

    App::Document* doc = feature->getDocument();
    if (!doc) {
        return result;
    }
    const auto pdFeats = doc->getObjectsOfType(PartDesign::Feature::getClassTypeId());
    const auto bodies = doc->getObjectsOfType(Body::getClassTypeId());

    // Walk forward, collecting every Body whose Tip is the current feature — the nearest
    // downstream marker — before advancing. The seen-set guards against a malformed cyclic
    // chain.
    App::DocumentObject* cursor = const_cast<App::DocumentObject*>(feature);
    std::set<const App::DocumentObject*> seen;
    while (cursor && seen.insert(cursor).second) {
        for (auto* it : bodies) {
            auto* body = static_cast<Body*>(it);
            if (body->Tip.getValue() == cursor) {
                result.push_back(body);
            }
        }
        if (!result.empty()) {
            return result;  // nearest downstream Tip reached — do not walk past it
        }
        App::DocumentObject* next = nullptr;
        for (auto* obj : pdFeats) {
            if (static_cast<PartDesign::Feature*>(obj)->BaseFeature.getValue() == cursor) {
                next = obj;
                break;
            }
        }
        cursor = next;
    }
    return result;
}

Body* Body::findBodyOf(const App::DocumentObject* feature)
{
    // CPART_DESIGN §9.1 scalar convenience over the N-valued bodiesOf primitive. Returns the
    // nearest downstream marker; when several Bodies share that Tip (a multi-output feature)
    // it returns the FIRST — callers that must disambiguate use bodyOf(feature, subElement),
    // which fails loud rather than guessing (Cruth ownership-query contract, P7). Kept
    // best-effort (not fail-loud) here so the ~40 pre-sweep callers keep their current
    // behaviour; the #7 sweep moves the ones that mean "the one Body" onto bodyOf.
    //
    // The answer is memoised on the feature's transient _Body link (Prop_Output, so writing
    // it neither dirties the feature nor triggers recompute; Prop_Transient, so it is never
    // serialised — CPART_DESIGN §9 / §8.2).
    if (!feature) {
        return nullptr;
    }

    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        auto* pdFeat = const_cast<PartDesign::Feature*>(
            static_cast<const PartDesign::Feature*>(feature)
        );

        // Cache hit.
        if (auto* cached = freecad_cast<Body*>(pdFeat->_Body.getValue())) {
            return cached;
        }

        const std::vector<Body*> found = bodiesOf(feature);
        if (!found.empty()) {
            pdFeat->_Body.setValue(found.front());
            return found.front();
        }
    }

    // Fall-through for non-PartDesign objects: the base BodyBase::findBodyOf, a
    // derived-membership lookup (getFullModel), no longer a Group scan. The derived chain
    // walk above has already handled every PartDesign::Feature case.
    return static_cast<Body*>(BodyBase::findBodyOf(feature));
}

bool Body::backsBody(const App::DocumentObject* feature, const Body* body)
{
    // CPART_DESIGN §9 honest membership over the N-valued bodiesOf primitive. `body` counts as
    // backed when it is among EVERY Body the feature backs, not just the first marker that a
    // scalar findBodyOf would report. Ownership stays derived — this reads the graph and holds
    // no stored feature→Body link (Cruth ownership-query invariant).
    if (!feature || !body) {
        return false;
    }
    const std::vector<Body*> bodies = bodiesOf(feature);
    return std::find(bodies.begin(), bodies.end(), body) != bodies.end();
}

bool Body::inAnyBody(const App::DocumentObject* feature)
{
    // Honest membership: the yes/no that call sites really wanted when they tested the
    // truthiness of a scalar findBodyOf. Non-empty bodiesOf ⇔ the feature reaches some Body.
    // Derived over the graph, no stored feature→Body link (Cruth ownership-query invariant).
    return feature && !bodiesOf(feature).empty();
}

bool Body::sameBody(const App::DocumentObject* a, const App::DocumentObject* b)
{
    // Honest same-body test: true iff the two feature→Body sets overlap. Comparing the sets
    // directly avoids the straddle coin-flip of picking one feature's scalar Body and testing
    // the other against it. Derived over bodiesOf, no stored link (ownership-query invariant).
    if (!a || !b) {
        return false;
    }
    const std::vector<Body*> ba = bodiesOf(a);
    const std::vector<Body*> bb = bodiesOf(b);
    return std::any_of(ba.begin(), ba.end(), [&bb](Body* body) {
        return std::find(bb.begin(), bb.end(), body) != bb.end();
    });
}

std::string Body::componentIdOfSub(const App::DocumentObject* feature, const char* subElement)
{
    // Discriminator half of bodyOf: turn a picked sub-element (e.g. "Face5") into the
    // component-id of the solid that owns it. Empty on any miss — a missing/blank name, a
    // non-shape feature, an unresolvable element, or a sub-element owned by no solid — so
    // the caller falls through to its fail-loud path rather than matching the wrong Body.
    if (!subElement || subElement[0] == '\0') {
        return {};
    }
    auto* geo = freecad_cast<Part::ShapeFeature*>(const_cast<App::DocumentObject*>(feature));
    if (!geo) {
        return {};
    }
    const Part::TopoShape shape = geo->Shape.getShape();
    if (shape.isNull()) {
        return {};
    }

    TopoDS_Shape sub;
    try {
        sub = shape.getSubShape(subElement, /*silent*/ true);
    }
    catch (const Standard_Failure&) {
        return {};
    }
    if (sub.IsNull()) {
        return {};
    }

    // A picked face/edge/vertex resolves to its owning solid via the ancestor map. A picked
    // solid has no solid ancestor, so match it against the shape's own solids by identity.
    if (sub.ShapeType() == TopAbs_SOLID) {
        const auto count = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
        for (int i = 1; i <= count; ++i) {
            if (shape.getSubShape(TopAbs_SOLID, i, /*silent*/ true).IsSame(sub)) {
                return componentKeyOfSolid(feature, shape, i);
            }
        }
        return {};
    }

    const std::vector<int> solids = shape.findAncestors(sub, TopAbs_SOLID);
    if (solids.empty()) {
        return {};
    }
    return componentKeyOfSolid(feature, shape, solids.front());
}

Body* Body::bodyOf(const App::DocumentObject* feature, const char* subElement)
{
    // Cruth ownership-query contract, P7 fail-loud. One candidate → the sub-element is
    // irrelevant, return it. Several candidates (a multi-output Tip) → the picked
    // sub-element names the component; match the Body carrying that component-id. Asking for
    // "the" Body of a multi-output feature with NO usable sub-element is ambiguous and
    // THROWS rather than silently guessing a Body.
    const std::vector<Body*> bodies = bodiesOf(feature);
    if (bodies.empty()) {
        return nullptr;
    }
    if (bodies.size() == 1) {
        return bodies.front();
    }

    const std::string cid = componentIdOfSub(feature, subElement);
    if (cid.empty()) {
        throw Base::RuntimeError(
            "This feature backs several bodies; a picked sub-element is required to say "
            "which one is meant."
        );
    }
    for (auto* body : bodies) {
        if (body->TipComponentId.getStrValue() == cid) {
            return body;
        }
    }
    throw Base::RuntimeError(
        "The picked sub-element does not match any of the bodies this feature backs."
    );
}


std::vector<App::DocumentObject*> Body::getFullModel()
{
    // CPART_DESIGN §9.1-inverse. A de-owned Body keeps no Group (ARCHITECTURE §3.2/§3.3),
    // so "what features make up this Body" is derived from the graph — the mirror image of
    // findBodyOf — never read from a stored list. Two sources, matching the two ways a
    // feature joins a Body:
    //   1. Solid features: those whose findBodyOf resolves to this Body. Collected by
    //      walking the BaseFeature chain back from the Tip, which stops naturally at a
    //      cross-body seam (the upstream feature there belongs to the other Body).
    //   2. Loose features (sketches, datums, shapebinders): those whose §8.5 attachment
    //      anchor-walk terminates on this Body.
    // Returned solids-first in build order, then the loose features.
    std::vector<App::DocumentObject*> rv;
    App::Document* doc = getDocument();
    if (!doc) {
        return rv;
    }

    // Re-entrancy guard (#46). Part 2 below walks each loose feature's attachment chain and
    // calls findBodyOf on the anchor solid; when that anchor is not a PartDesign::Feature the
    // query routes through BodyBase::findBodyOf, which calls getFullModel on every Body —
    // re-entering this one. A datum/sketch attached to a face of its own Body closes that loop
    // (getFullModel → walkAnchorChain → findBodyOf → BodyBase::findBodyOf → getFullModel) and it
    // recurses without bound, SIGSEGV on sketch-create and on document load. The solid chain
    // (part 1) is recursion-free, so on re-entry we compute only the solids and skip the
    // anchor-walk: an anchor that is a member solid is still found there, which is all the
    // findBodyOf that re-entered us needs to resolve membership.
    static thread_local std::set<const Body*> inProgress;
    const bool reentrant = !inProgress.insert(this).second;
    struct ProgressGuard
    {
        std::set<const Body*>& set;
        const Body* body;
        bool owns;
        ~ProgressGuard()
        {
            if (owns) {
                set.erase(body);
            }
        }
    } progressGuard {inProgress, this, !reentrant};

    // 1) Solid chain, Tip → base, including only members (backsBody(cursor, this)); the
    //    membership test is what halts the walk at a seam. backsBody, not the scalar
    //    findBodyOf: a multi-output straddler backs several Bodies at once, and first-match
    //    could report a sibling Body and truncate this Body's model at the seam. Reversed
    //    afterwards to give build order.
    std::set<App::DocumentObject*> seen;
    for (App::DocumentObject* cursor = Tip.getValue(); cursor && seen.insert(cursor).second;) {
        auto* pd = freecad_cast<PartDesign::Feature*>(cursor);
        if (!pd || !backsBody(cursor, this)) {
            break;  // non-PartDesign terminal, or crossed the seam into another Body
        }
        rv.push_back(cursor);
        cursor = pd->BaseFeature.getValue();
    }
    std::reverse(rv.begin(), rv.end());
    const std::set<App::DocumentObject*> solidMembers(rv.begin(), rv.end());

    if (reentrant) {
        return rv;  // solids only — breaks the getFullModel↔findBodyOf cycle (#46)
    }

    // 2) Loose features (sketches, datums, shapebinders) that are not part of the solid
    //    chain. One belongs to this Body when either:
    //      (a) a member solid references it — e.g. a profile sketch consumed by a Pad, even
    //          when that sketch sits on a global plane; or
    //      (b) its §8.5 attachment anchor-walk terminates on this Body — e.g. a datum
    //          attached to one of this Body's faces.
    for (auto* obj : doc->getObjects()) {
        if (obj->isDerivedFrom<PartDesign::Feature>()) {
            continue;  // solids handled by the chain walk above
        }
        if (!obj->getExtensionByType<Part::AttachExtension>(true)) {
            continue;  // only attachable geometry can belong to a Body
        }

        bool member = false;
        for (auto* consumer : obj->getInList()) {  // (a) referenced by a member solid
            if (solidMembers.count(consumer)) {
                member = true;
                break;
            }
        }
        if (!member) {  // (b) anchored into this Body's geometry
            std::set<PartDesign::Body*> bodies;
            walkAnchorChain(obj, bodies, 0);
            member = bodies.count(this) > 0;
        }
        if (member) {
            rv.push_back(obj);
        }
    }

    return rv;
}


std::vector<App::DocumentObject*> Body::addFeature(App::DocumentObject* feature)
{
    if (!isAllowed(feature)) {
        throw Base::ValueError("Body: object is not allowed");
    }

    // De-ownership is the only feature-wiring path (ARCHITECTURE §3.2/§3.3): a new
    // feature joins the Body's pipeline by reference — BaseFeature chain + Tip —
    // WITHOUT being added to Body.Group. The pipeline is derived from the chain back
    // from the Tip, so group membership is no longer the source of truth for feature
    // ordering. Handles both the tip-append gesture (the new solid extends the body
    // from the current Tip) and mid-chain insert (the Tip is an interior feature): in
    // the latter case the displaced successor is rerouted onto the new feature so the
    // chain stays linear instead of forking.

    // Detach from any prior owning group. This is NOT the body-to-body move path
    // (that heals the source chain via Body::removeFeatures); the live case here is a
    // feature the user parked in a plain tree folder (App::DocumentObjectGroup) and
    // then homed into this body — without this it would stay double-filed (in the
    // folder AND referenced by the body). Whether that single-home rule is still right
    // under de-ownership (folder = organization vs body = derived reference, arguably
    // orthogonal) is an open design question tracked in #37 — keep as-is until settled.
    auto* group = App::GroupExtension::getGroupOfObject(feature);
    if (group) {
        group->getExtensionByType<App::GroupExtension>()->removeObject(feature);
    }

    // Cruth substrate flip (Stage 3a): resolve origin/datum links against the single
    // document-level Origin, not this Body's own (now-dormant) per-body Origin. In the
    // de-ownership model the coordinate frame is shared at document level (Day-5 design;
    // ARCHITECTURE §3.3), so all bodies' features anchor to one Origin.
    relinkFeatureToOrigin(feature, getDocumentOrigin());

    // Associate the feature with this Body by reference (used by findBodyOf and
    // active-body tooling) without imprisoning it in the group.
    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        static_cast<PartDesign::Feature*>(feature)->_Body.setValue(this);
    }

    if (isSolidFeature(feature)) {
        // Splice the new solid into the chain at the Tip: its base is the old Tip,
        // and the Body now propagates the new feature.
        App::DocumentObject* prevTip = Tip.getValue();

        // Clear any BaseFeature the caller pre-set on the incoming feature before we
        // scan for prevTip's successor (Cruth #18). addFeature owns the chain wiring;
        // if the feature already pointed at prevTip, the successor scan below would
        // return the new feature itself and the reroute would set
        // feature.BaseFeature = feature — a self-cycle that fails recompute with "The
        // graph must be a DAG". Clearing first also lets the scan find the *genuine*
        // mid-chain successor instead of the pre-wired feature.
        static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(nullptr);

        // Capture the insert point's existing chain successor BEFORE rewiring.
        // When the Tip is not the last feature (mid-chain insert), the new feature
        // must splice between prevTip and its successor rather than forking the
        // chain — otherwise the displaced tail is silently orphaned (its geometry
        // drops out of the Body with no error). The successor scan reads the
        // BaseFeature chain — a de-owned Body has no Group ordering to read.
        App::DocumentObject* successor = getNextSolidFeatureByChain(prevTip);

        static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(prevTip);

        // Mid-chain insert: reroute the displaced successor onto the new feature so
        // the chain stays linear (prevTip -> feature -> successor -> ...).
        if (successor && successor->isDerivedFrom<PartDesign::Feature>()) {
            static_cast<PartDesign::Feature*>(successor)->BaseFeature.setValue(feature);
        }

        Tip.setValue(feature);

        // Tip visibility bookkeeping: only the current Tip shows by default.
        if (prevTip && prevTip->isDerivedFrom<PartDesign::Feature>()
            && prevTip->Visibility.getValue()) {
            prevTip->Visibility.setValue(false);
        }
    }
    else if (feature->isDerivedFrom<PartDesign::Transformed>()) {
        // A freshly-created Transformed feature (LinearPattern/PolarPattern/Mirror/
        // MultiTransform) is misclassified by isSolidFeature() during the init phase:
        // isMultiTransformChild() heuristically reports true while TransformMode is at
        // its default and Originals is still empty (they are set after creation). That
        // skips the solid-splice above, leaving BaseFeature unset — so the pattern, and
        // hence its input feature, never join the BaseFeature chain (issue #1).
        //
        // A Transformed reaching addObject is always a standalone, body-level pattern:
        // MultiTransform *children* are held in the parent's Transformations list and
        // are never added to the Body. The execute-time self-wiring (FeatureTransformed
        // execute -> Body::setBaseProperty) cannot recover here because by execute time
        // the caller has set Tip to the pattern, so getPrevSolidFeature() (which walks
        // the BaseFeature chain) finds nothing. Wire BaseFeature now, while the previous
        // Tip is still known. Tip is intentionally NOT advanced: the caller advances it
        // once Originals are set (prepareTransformed), avoiding a transient recompute
        // with an unconfigured pattern as Tip.
        static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(Tip.getValue());
    }

    return {feature};
}

std::vector<App::DocumentObject*> Body::addFeatures(std::vector<App::DocumentObject*> features)
{
    for (auto* feature : features) {
        addFeature(feature);
    }

    return features;
}

void Body::insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after)
{
    // Cruth de-ownership (Stage 3b-i): splice `feature` into the BaseFeature chain at
    // the requested position. A de-owned Body has no Group to edit; the pipeline is
    // derived from the chain, so wiring BaseFeature links *is* the insert. Generalizes
    // the Tip-splice addObject performs to an arbitrary (target, after) anchor.
    // ARCHITECTURE §3.2/§3.3.

    // Validate target membership via the de-ownership back-pointer (there is no Group).
    if (target) {
        auto* tf = freecad_cast<PartDesign::Feature*>(target);
        if (!tf || tf->_Body.getValue() != this) {
            throw Base::ValueError(
                "Body: the feature we should insert relative to is not part of that body"
            );
        }
    }

    // Resolve origin/datum links against the shared document-level Origin (Stage 3a).
    relinkFeatureToOrigin(feature, getDocumentOrigin());

    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        static_cast<PartDesign::Feature*>(feature)->_Body.setValue(this);
    }

    // Non-solid members (sketches, datums) carry no pipeline position — nothing to splice.
    if (!isSolidFeature(feature)) {
        return;
    }

    // Resolve the predecessor/successor solids that will bracket `feature`.
    App::DocumentObject* pred = nullptr;
    App::DocumentObject* succ = nullptr;
    if (target) {
        if (after) {
            pred = target;
            succ = getNextSolidFeature(target);
        }
        else {
            pred = getPrevSolidFeature(target);
            succ = target;
        }
    }
    else if (after) {
        // Base end: feature becomes the new chain root; the old root rebases onto it.
        std::set<App::DocumentObject*> seen;
        App::DocumentObject* root = Tip.getValue();
        for (auto* pd = freecad_cast<PartDesign::Feature*>(root); pd;
             pd = freecad_cast<PartDesign::Feature*>(root)) {
            App::DocumentObject* base = pd->BaseFeature.getValue();
            if (!base || !seen.insert(root).second) {
                break;
            }
            root = base;
        }
        succ = root;
    }
    else {
        // Tip end: feature appends after the current Tip.
        pred = Tip.getValue();
    }

    static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(pred);
    if (succ && succ->isDerivedFrom<PartDesign::Feature>()) {
        static_cast<PartDesign::Feature*>(succ)->BaseFeature.setValue(feature);
    }
    if (!succ) {
        // No chain successor → feature is the new Tip.
        Tip.setValue(feature);
    }
}

void Body::setBaseProperty(App::DocumentObject* feature)
{
    if (Body::isSolidFeature(feature)) {
        // Set BaseFeature property to previous feature (this might be the Tip feature)
        App::DocumentObject* prevSolidFeature = getPrevSolidFeature(feature);
        // NULL is ok here, it just means we made the current one fiature the base solid
        static_cast<PartDesign::Feature*>(feature)->BaseFeature.setValue(prevSolidFeature);

        // Reroute the next solid feature's BaseFeature property to this feature
        App::DocumentObject* nextSolidFeature = getNextSolidFeature(feature);
        if (nextSolidFeature) {
            assert(nextSolidFeature->isDerivedFrom(PartDesign::Feature::getClassTypeId()));
            static_cast<PartDesign::Feature*>(nextSolidFeature)->BaseFeature.setValue(feature);
        }
    }
}

// Cruth intra-body de-ownership: find the chain successor of a feature — the
// solid feature whose BaseFeature links back to it — by scanning the document
// (a de-owned Body has no Group ordering to read). BaseFeature is an intra-body
// link, so the successor is unique. ARCHITECTURE §3.2/§3.3.
App::DocumentObject* Body::getNextSolidFeatureByChain(App::DocumentObject* feature) const
{
    if (!feature) {
        return nullptr;
    }
    App::Document* doc = feature->getDocument();
    if (!doc) {
        return nullptr;
    }
    for (auto* obj : doc->getObjectsOfType(PartDesign::Feature::getClassTypeId())) {
        if (static_cast<PartDesign::Feature*>(obj)->BaseFeature.getValue() == feature) {
            return obj;
        }
    }
    return nullptr;
}

// Cruth intra-body de-ownership (Day 4): delete a feature by rewiring the
// BaseFeature chain. The chain successor (the solid whose BaseFeature points at
// this feature) is relinked to this feature's own base, and the Tip retreats
// along the chain. A de-owned Body has no Group ordering, so the chain is the
// sole source of order. ARCHITECTURE §3.2/§3.3.
std::vector<App::DocumentObject*> Body::removeFeature(App::DocumentObject* feature)
{
    // This method must be called BEFORE the feature is removed from the Document!
    // De-ownership is the only path: heal the BaseFeature chain directly, retreat the
    // Tip, and retire the Body if its chain empties — there is no Group order to consult.
    App::DocumentObject* prevSolidFeature = nullptr;
    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        prevSolidFeature = static_cast<PartDesign::Feature*>(feature)->BaseFeature.getValue();
    }
    App::DocumentObject* nextSolidFeature = getNextSolidFeatureByChain(feature);

    // Reroute the chain successor's base past the feature being removed.
    if (nextSolidFeature && nextSolidFeature->isDerivedFrom<PartDesign::Feature>()) {
        auto* nextPD = static_cast<PartDesign::Feature*>(nextSolidFeature);
        if (nextPD->BaseFeature.getValue() == feature) {
            nextPD->BaseFeature.setValue(prevSolidFeature);
            // Re-map subelement links (fillet/chamfer faces, direct face profiles)
            // from the deleted base onto the matching geometry of the new base.
            nextPD->onBaseFeatureRerouted(feature, prevSolidFeature);
        }
    }

    // Retreat the Tip of EVERY Body tipped by the removed feature — not only this one.
    // A splitter (e.g. a Pocket that severs a solid) is the Tip of ALL the halves it
    // produced, so deleting it is a forward §4.7 topology event (a merge) that touches
    // every one of them. Retreat each onto the removed feature's base; when the feature
    // backed more than one Body, mark that base for recompute so reconcileMultiOutput runs
    // the §4.3 union — it retires the now-surplus split-children (identities reset; inbound
    // refs fail loud per P7) and mints one fresh Body for the merged solid. Undo, the reverse
    // edit, is what restores the originals; a forward delete never silently re-owns the merge.
    App::DocumentObject* const retreatTo = prevSolidFeature ? prevSolidFeature : nextSolidFeature;
    App::Document* doc = getDocument();
    std::size_t tippedByFeature = 0;
    if (doc) {
        for (auto* obj : doc->getObjectsOfType(Body::getClassTypeId())) {
            auto* sibling = static_cast<Body*>(obj);
            if (sibling->Tip.getValue() == feature) {
                ++tippedByFeature;
                sibling->Tip.setValue(retreatTo);
            }
        }
    }
    if (tippedByFeature > 1 && retreatTo) {
        // Force the merged base into the next recompute's signalRecomputed set — the
        // reconciler keys off that list, and nothing downstream touches the base (the
        // deleted feature was the Tip, so it had no successor to propagate a touch).
        retreatTo->touch();
    }

    std::vector<App::DocumentObject*> result = {feature};

    // Auto-retire (Cruth intra-body de-ownership, Day 4 — option 2). A Body is a
    // derived view over its Tip, never an owner of features (ARCHITECTURE §3.3). When
    // the last solid feature is deleted the Tip retreats to null, so the Body no
    // longer propagates any component and retires itself (§4.7). This handles only
    // the degenerate empty-chain case of retirement — split/merge topology events are
    // out of scope. Per §4.6 we retire even if something still references the Body;
    // the dangling reference is left to fail loudly (P7), not silently suppressed.
    //
    // Document::removeObject() destroys `this` when no undo transaction is active, so
    // this MUST be the last action: copy what we need into locals and touch no member
    // of `this` afterward.
    if (Tip.getValue() == nullptr) {
        const char* name = getNameInDocument();
        if (doc && name) {
            const std::string bodyName = name;
            doc->removeObject(bodyName.c_str());
        }
    }

    return result;
}

void Body::removeFeatures(const std::vector<App::DocumentObject*>& features)
{
    for (auto* feature : features) {
        removeFeature(feature);
    }
}

App::DocumentObjectExecReturn* Body::execute()
{
    Part::BodyBase::execute();

    App::DocumentObject* tip = Tip.getValue();

    Part::TopoShape tipShape;
    if (tip) {
        if (!tip->isDerivedFrom<PartDesign::Feature>()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Linked object is not a PartDesign feature")
            );
        }

        // get the shape of the tip
        tipShape = static_cast<Part::ShapeFeature*>(tip)->Shape.getShape();

        if (tipShape.getShape().IsNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip shape is empty")
            );
        }

        // Cruth §3.3: a multi-output Body represents one component of its Tip's
        // shape, named by TipComponentId. Empty id = the implicit single-component
        // case (propagate the whole shape). It is never a silent fall-back to the
        // whole shape — that would show wrong geometry.
        //
        // A set id that no longer resolves is NOT an execute-level failure. Per
        // ARCHITECTURE §4.6/§4.7 a Body whose Tip stops yielding its component is
        // simply *retired* — lifecycle owned by the reconciler (reconcileMultiOutput,
        // which runs on signalRecomputed after every recompute), not by execute. The
        // P7 fail-loud belongs to anything that still *references* the retired Body
        // (assembly/drawing/BOM), which surfaces at those consumers, not here. So a
        // component miss means either (a) this Body is about to be retired this same
        // pass, or (b) it is the §4.3 "Vanish deferred" case (a transient empty/failed
        // compute the reconciler keeps): in both, leave the last-good Shape untouched
        // and return normally rather than logging a spurious error every edit.
        const std::string cid = TipComponentId.getStrValue();
        if (!cid.empty()) {
            Part::TopoShape component = extractSolidById(tip, tipShape, cid);
            if (component.isNull()) {
                return App::DocumentObject::StdReturn;
            }
            // A pattern stores each instance's offset in the solid's placement, not
            // its geometry. Bake that placement into the geometry (an identity
            // transform with copy bakes the location and resets it) so this Body
            // keeps its own pattern position; otherwise every component would
            // collapse onto the Tip origin. (Cruth §3.3 multi-output.)
            component.transformShape(Base::Matrix4D(), true);
            tipShape = component;
        }

        // We should hide here the transformation of the baseFeature
        tipShape.transformShape(tipShape.getTransform(), true);
    }
    else {
        // Cruth §4.6/§4.8: a Body is the system's accounting of a connected solid — it
        // exists only because a feature's recompute produced one, and its identity IS
        // its Tip. A Body that reaches recompute with no Tip has no such component: it
        // is the illegal authored-empty-Body state (a raw addObject('PartDesign::Body')
        // with nothing spliced in, or a spawn whose feature never arrived). Fail loudly
        // rather than sit in the tree looking healthy with an empty shape. Legitimate
        // emptying — the last feature removed — retires the Body by deleting it in
        // removeFeature() within the same synchronous call, so execute() never observes
        // a *live* Tipless Body except this never-populated case.
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
            "Exception",
            "A Body must have at least one feature; empty bodies are not allowed"
        ));
    }

    // Cruth §3.3: a Body stores no geometry of its own. Its shape is derived from the Tip
    // on demand (derivedTipShape) — read by the render path and every consumer — so execute
    // no longer materialises it into the Shape property. The computation above is retained
    // as validation only: an empty Tip, a non-PartDesign Tip, or an empty Tip shape fails
    // loud (P7), while a missing named component silently defers to the reconciler (§4.7).
    // The Shape property (still inherited from Part::Feature here) is now unmaintained and
    // is removed with the base-class reparent in the next slice.
    return App::DocumentObject::StdReturn;
}

void Body::onSettingDocument()
{

    if (connection.connected()) {
        connection.disconnect();
    }

    Part::BodyBase::onSettingDocument();
}

void Body::onChanged(const App::Property* prop)
{
    // we neither load a project nor perform undo/redo
    if (!this->isRestoring() && this->getDocument()
        && !this->getDocument()->isPerformingTransaction()) {
        if (prop == &BaseFeature) {
            FeatureBase* bf = nullptr;

            // The chain root — the existing FeatureBase, if any — used to be Group.front().
            // A de-owned Body has no Group (§9.1), so find the root by walking the
            // BaseFeature chain back from the Tip until it leaves this Body (null base, or a
            // base belonging to another Body across a seam). There is no Group to read, so
            // without this walk we would mint a duplicate FeatureBase on every BaseFeature
            // re-set.
            App::DocumentObject* first = nullptr;
            std::set<App::DocumentObject*> seen;
            for (App::DocumentObject* cursor = Tip.getValue(); cursor && seen.insert(cursor).second;) {
                auto* pd = freecad_cast<PartDesign::Feature*>(cursor);
                if (!pd) {
                    break;
                }
                App::DocumentObject* base = pd->BaseFeature.getValue();
                if (!base || !backsBody(base, this)) {
                    first = cursor;  // earliest member of this Body's chain
                    break;
                }
                cursor = base;
            }

            if (BaseFeature.getValue()) {
                // setup the FeatureBase if needed
                if (!first || !first->isDerivedFrom<FeatureBase>()) {
                    bf = getDocument()->addObject<FeatureBase>("BaseFeature");
                    insertObject(bf, first, false);

                    if (!Tip.getValue()) {
                        Tip.setValue(bf);
                    }
                }
                else {
                    bf = static_cast<FeatureBase*>(first);
                }
            }

            if (bf && (bf->BaseFeature.getValue() != BaseFeature.getValue())) {
                bf->BaseFeature.setValue(BaseFeature.getValue());
            }
        }
        else if (prop == &Placement) {
            // Cruth substrate flip (de-ownership): the modeling Body carries no coordinate
            // frame of its own — the frame comes from each feature's attachment, not from
            // Body containment (ARCHITECTURE §3.3). De-owned features do not inherit
            // Body.Placement, so a non-identity Body placement would silently diverge from
            // its features (the Day-1 Experiment-C case). The property itself cannot be
            // removed: it lives on the shared App::GeoFeature base and every Part::Feature
            // inherits it. So this is a PERMANENT guard, not interim — it pins the frame to
            // identity on any live edit (e.g. dragging the Body) for as long as Body derives
            // from GeoFeature. Removing it requires reparenting Body off GeoFeature; tracked
            // as CoreCAD/CoreCAD#12. Restore and undo/redo are excluded by the guard above,
            // so loaded data is never mutated; this fires only on a live user change.
            const Base::Placement identity;
            if (Placement.getValue() != identity) {
                Placement.setValue(identity);
            }
        }
        else if (prop == &ShapeMaterial) {
            // Derived membership (§9.1-inverse): a de-owned Body has no Group container, so
            // push the Body material onto its features via the derived list.
            std::vector<App::DocumentObject*> features = getFullModel();
            if (!features.empty()) {
                for (auto it : features) {
                    auto feature = dynamic_cast<Part::ShapeFeature*>(it);
                    if (feature) {
                        if (feature->ShapeMaterial.getValue().getUUID()
                            != ShapeMaterial.getValue().getUUID()) {
                            feature->ShapeMaterial.setValue(ShapeMaterial.getValue());
                        }
                    }
                }
            }
        }
    }

    Part::BodyBase::onChanged(prop);
}

App::Origin* Body::findDocumentOrigin(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }

    // The single document-level Origin is shared by every PartDesign Body via the
    // shared-Origin contract (setupObject). It is identified as a free-standing App::Origin
    // that no OriginGroup owns (the older per-body Origin link is gone — Cruth §11 step 5e).
    // A per-body/per-part private Origin (e.g. an App::Part's own ruler) is owned by exactly
    // that group, so it is correctly skipped. The legacy "already linked by a Body" match is
    // kept as a belt-and-braces fallback for any Origin a Body still references.
    for (auto* obj : doc->getObjectsOfType<App::Origin>()) {
        bool usedByBody = false;
        bool ownedByGroup = false;
        for (auto* in : obj->getInList()) {
            if (in->isDerivedFrom<PartDesign::Body>()) {
                usedByBody = true;
                break;
            }
            if (in->hasExtension(App::OriginGroupExtension::getExtensionClassTypeId())) {
                ownedByGroup = true;
            }
        }
        if (usedByBody || !ownedByGroup) {
            return obj;
        }
    }

    return nullptr;
}

App::Origin* Body::requireDocumentOrigin(App::Document* doc)
{
    if (App::Origin* origin = findDocumentOrigin(doc)) {
        return origin;
    }

    // No shared world frame in this document — a Body only ever LOOKS the frame up; it must
    // never mint one. Under the document-owned world-frame contract (ARCHITECTURE_AMENDMENTS
    // Amendment 2) a CAD (Part) document mints its App::Origin at creation
    // (App.newDocument(type='Part')). Reaching here means a Body was created in a document
    // that has no world frame — a call-site error, not a recoverable state, so fail loudly
    // rather than lazily bootstrapping the coordinate system off the body.
    throw Base::RuntimeError(
        "PartDesign Body requires a document-level world frame (App::Origin), but the "
        "document has none. Create the Body in a Part document "
        "(App.newDocument(type='Part')); a Body must not create the coordinate frame."
    );
}

void Body::setupObject()
{
    Part::BodyBase::setupObject();

    // Cruth shared-Origin contract (GitHub #4) / §11 step 5e: a PartDesign Body does NOT own
    // a private coordinate frame, and it does NOT create one either. The world frame is shared
    // at document level (ARCHITECTURE §3.3) and minted by the CAD document at its creation
    // (ARCHITECTURE_AMENDMENTS Amendment 2) — every Body's features anchor to that
    // one free-standing App::Origin. Resolve it here so the invariant is checked at spawn: a
    // Body born into a document with no world frame fails loudly (getDocumentOrigin throws)
    // rather than lazily bootstrapping the coordinate system off the body.
    getDocumentOrigin();

    // Cruth §4.6: assign a deterministic identity colour at spawn time.
    // Per-document index — count Bodies already in the doc (excluding this one,
    // which is in the doc but not yet visible to countObjectsOfType).
    if (auto* doc = getDocument()) {
        const std::size_t index = doc->countObjectsOfType<PartDesign::Body>();
        Color.setValue(paletteColorFor(index ? index - 1 : 0));
    }
}

void Body::unsetupObject()
{
    Part::BodyBase::unsetupObject();
}

PyObject* Body::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new BodyPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

std::vector<std::string> Body::getSubObjects(int reason) const
{
    if (reason == GS_SELECT && !showTip) {
        return Part::BodyBase::getSubObjects(reason);
    }
    return {};
}

PartDesign::Feature* Body::findOwnedFeature(const std::string& name) const
{
    App::Document* doc = getDocument();
    if (!doc || name.empty()) {
        return nullptr;
    }
    const bool byLabel = name[0] == '$';
    const std::string key = byLabel ? name.substr(1) : name;
    for (auto* feat : doc->getObjectsOfType<PartDesign::Feature>()) {
        if (feat->_Body.getValue() != this) {
            continue;
        }
        const char* fname = feat->getNameInDocument();
        if (byLabel ? (key == feat->Label.getStrValue()) : (fname && key == fname)) {
            return feat;
        }
    }
    return nullptr;
}

// ARCHITECTURE §3.3/§4: a Body stores no geometry of its own — its shape is its Tip's
// shape, derived on demand and returned already world-placed. This is the single source
// used by both the render path (ViewProviderPartExt::getRenderedShape ->
// Part::Feature::getTopoShape -> getSubObject) and any generic consumer. It mirrors the
// geometry Body::execute() currently materialises into the (soon-to-be-retired) Shape
// property, so the two agree while both exist.
Part::TopoShape Body::derivedTipShape() const
{
    App::DocumentObject* tip = Tip.getValue();
    if (!tip || !tip->isDerivedFrom<PartDesign::Feature>()) {
        return {};
    }
    Part::TopoShape tipShape = static_cast<Part::ShapeFeature*>(tip)->Shape.getShape();
    if (tipShape.getShape().IsNull()) {
        return {};
    }
    const std::string cid = TipComponentId.getStrValue();
    if (!cid.empty()) {
        Part::TopoShape component = extractSolidById(tip, tipShape, cid);
        if (component.isNull()) {
            return {};
        }
        // A pattern stores each instance's offset in the solid's placement, not its
        // geometry; bake it in so this component keeps its own pattern position (§3.3).
        component.transformShape(Base::Matrix4D(), true);
        tipShape = component;
    }
    // Bake in the tip feature's own transform (matches Body::execute()).
    tipShape.transformShape(tipShape.getTransform(), true);
    return tipShape;
}

App::DocumentObject* Body::getSubObject(
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* pmat,
    bool transform,
    int depth
) const
{
    while (subname && *subname == '.') {
        ++subname;  // skip leading .
    }

    // (Cruth §11 step 5c) The legacy sibling-grouping peek that skipped a display-folder
    // path component was removed here: feature grouping placed those folders in the Body
    // Group, which no longer exists under de-ownership, so the peek was inert. Path
    // resolution now runs entirely through the derived findOwnedFeature delegation below.

    // Cruth de-ownership (§3.3): a Body's pipeline features reference it, they are not
    // held in its Group, so the base GeoFeatureGroup resolver (which looks children up in
    // Group) cannot resolve a "Feature.SubElement" path such as "Pad.Edge3" or
    // "BakedShape.Edge3" — the selection that drives fillet/dressup edge picking. Per the
    // architecture's reference model, a sub-element reference is anchored to the *feature*
    // that emits it (ARCHITECTURE §3 references table: "Anchored to the feature"), so a
    // click on an edge must resolve through the emitting feature, not the Body. When the
    // first path component names a feature that resolves to this Body, delegate the
    // remainder to that feature. (A plain "Edge3" with no feature component still resolves
    // against the Body's Tip shape via the fall-through below, unchanged.)
    if (subname && *subname && !Data::isMappedElement(subname)) {
        if (const char* dot = strchr(subname, '.')) {
            const std::string first(subname, dot);
            if (auto* feat = findOwnedFeature(first)) {
                if (pmat && transform) {
                    *pmat *= Placement.getValue().toMatrix();
                }
                return feat->getSubObject(dot + 1, pyObj, pmat, transform, depth + 1);
            }
            // The shared document Origin is claimed as a child of the Body in the tree/3D
            // (ViewProviderBody::claimChildren, so the base planes/axes are pickable for a new
            // sketch), but it is document-owned, not a Group member (Cruth §11 step 5e), so the
            // base resolver below cannot find it — a click on a base plane failed with
            // "Sub-object Body.Origin.YZ_Plane not found". Delegate an "<Origin>.…" path to the
            // document Origin. Its geometry is the world frame, so the Body's own Placement is
            // not applied (unlike an owned feature above).
            if (App::Origin* origin = findDocumentOrigin(getDocument())) {
                const char* oname = origin->getNameInDocument();
                if (oname && first == oname) {
                    return origin->getSubObject(dot + 1, pyObj, pmat, transform, depth + 1);
                }
            }
        }
    }
    // Cruth §3.3/§4: a Body stores no geometry of its own — answer a query for its own
    // shape by DERIVING it from the Tip on demand (derivedTipShape), never from a stored
    // Shape property. Mirrors ShapeFeature::getSubObject, but a Body holds no authored
    // position (§4: "nothing to guard"), so its own frame is identity and no placement is
    // composed here. (Path components containing '.' were already delegated to the owning
    // feature or the document Origin above; here subname is empty or a plain sub-element.)
    //
    // This supersedes the FreeCAD-era caution — returning the Body shape only when a child
    // was visible, to avoid double-draw when the Body sat inside another group — which was
    // long disabled; under de-ownership a Body always represents its Tip (§3.3).
    if (!pyObj) {
        return const_cast<Body*>(this);
    }
    try {
        Part::TopoShape ts = derivedTipShape();
        Base::Matrix4D _mat;
        auto& mat = pmat ? *pmat : _mat;
        bool doTransform = !ts.isNull() && mat != ts.getTransform();
        if (doTransform) {
            ts.setShape(ts.getShape().Located(TopLoc_Location()), false);
        }
        if (subname && *subname && !ts.isNull()) {
            ts = ts.getSubTopoShape(subname, /*silent*/ true);
        }
        if (doTransform && !ts.isNull()) {
            ts.transformShape(mat, false, true);
        }
        *pyObj = Py::new_reference_to(Part::shape2pyshape(ts));
    }
    catch (Standard_Failure&) {
        // Match ShapeFeature: swallow OCCT failures here rather than flood the log.
    }
    return const_cast<Body*>(this);
}

void Body::rebuildBodyCacheFromChain()
{
    App::Document* doc = getDocument();
    if (!doc) {
        return;
    }

    // The Tips that OTHER bodies mark are the seams where this body's chain ends: walking
    // back from our Tip, the first feature that is another body's Tip has that upstream
    // marker as its nearest downstream Tip (the cross-body FeatureBase reference), and so
    // does everything before it — none of it resolves to us.
    std::set<const App::DocumentObject*> otherTips;
    for (auto* it : doc->getObjectsOfType(Body::getClassTypeId())) {
        auto* body = static_cast<Body*>(it);
        if (body != this && body->Tip.getValue()) {
            otherTips.insert(body->Tip.getValue());
        }
    }

    // Walk back from the Tip along BaseFeature, memoising this marker on each feature that
    // resolves to us, up to the seam. The seen-set guards against a malformed cyclic chain.
    App::DocumentObject* cursor = Tip.getValue();
    std::set<const App::DocumentObject*> seen;
    while (cursor && seen.insert(cursor).second) {
        if (otherTips.count(cursor)) {
            break;
        }
        if (!cursor->isDerivedFrom<PartDesign::Feature>()) {
            break;
        }
        auto* pdFeat = static_cast<PartDesign::Feature*>(cursor);
        pdFeat->_Body.setValue(this);
        cursor = pdFeat->BaseFeature.getValue();
    }
}

void Body::onDocumentRestored()
{
    // CPART_DESIGN §9 / §8.3: the feature->marker relationship is not serialised; it is
    // re-derived by query at load. Repopulate the transient _Body cache from the
    // BaseFeature chain (a de-owned Body has no Group to read). findBodyOf self-heals on
    // demand, but direct _Body readers — findOwnedFeature, the de-owned sub-element path
    // — need the cache warm before the first selection click.
    rebuildBodyCacheFromChain();

    // trigger ViewProviderBody::copyColorsfromTip
    if (Tip.getValue()) {
        Tip.touch();
    }

    DocumentObject::onDocumentRestored();
}

// a body is solid if it has features that are solid
bool Body::isSolid()
{
    std::vector<App::DocumentObject*> features = getFullModel();
    for (auto feature : features) {
        if (isSolidFeature(feature)) {
            return true;
        }
    }
    return false;
}
