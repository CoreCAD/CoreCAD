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

#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <App/GeoFeatureGroupExtension.h>
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
#include <Mod/Part/App/TopoShape.h>

#include "Feature.h"

#include "Body.h"
#include "BodyPy.h"
#include "FeatureBakedShape.h"
#include "FeatureBase.h"
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

    if (obj->isDerivedFrom(Part::Feature::getClassTypeId())) {
        if (auto* body = PartDesign::Body::findBodyOf(obj)) {
            bodies.insert(body);
            return false;
        }
        return true;
    }

    return true;
}

// Return the solid sub-shape whose component-id matches, or a null shape if none.
Part::TopoShape extractSolidById(const Part::TopoShape& shape, const std::string& cid)
{
    const auto count = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
    for (int i = 1; i <= count; ++i) {
        if (Body::componentIdOfSolid(shape, i) == cid) {
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

    // Capture the instance's solid straight from the instance Body's own Shape: it
    // is already the pattern component for this cid with the instance offset baked
    // into the geometry (the §3.3 multi-output display path does that bake) and the
    // element map intact. Capture before recording the skip — once skipped, neither
    // the pattern nor this Body emit it any more.
    Part::TopoShape captured = instanceBody->Shape.getShape();
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
    // Keep the new Body in the same container as the originating instance.
    if (auto* group = App::GeoFeatureGroupExtension::getGroupOfObject(instanceBody)) {
        group->getExtensionByType<App::GeoFeatureGroupExtension>()->addObject(newBody);
    }
    newBody->addFeature(baked);  // also points the new Body's Tip at the BakedShape

    // Record the skip (§5.6) so the pattern drops this instance, then recompute.
    // The originating Body becomes an orphan and is retired by the reconciler (§4.7).
    std::vector<std::string> skips = pattern->SkipComponentIds.getValues();
    if (std::find(skips.begin(), skips.end(), cid) == skips.end()) {
        skips.push_back(cid);
        pattern->SkipComponentIds.setValues(skips);
    }
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
        // ONLY when the match is decidable WITHOUT resemblance. The floor recognises the one such
        // case, the TRIVIAL 1:1 — exactly one prior Body naming this Tip and exactly one solid, so
        // the mapping is unambiguous with nothing to compare. That Body keeps its UUID (Clause
        // 3.1: identity survives recompute and parameter edits, however far the geometry moved).
        // Recognising a *continuing* multi-solid arrangement without resemblance needs the
        // recorded-association predicate Clause 3.3 permits as a later sharpening — deliberately
        // NOT built here, so a multi-solid arrangement re-mints each cycle (a "false retirement"
        // the amendment anticipates and defers).
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

        // Cruth Amendment 3 §4.7 TOPOLOGY EVENT — anything that is not the trivial 1:1 above: a
        // split (1 Body -> N solids), a union (N Bodies -> 1 solid), or any re-arrangement.
        // Identity NEVER transfers across it and is NEVER re-attached by resemblance (§3.3):
        // every prior Body retires, its UUID dies, and a fresh Body is born for each solid with a
        // fresh UUID/name/colour/component-id. Inbound references to a retired Body do not
        // silently re-bind — they surface honestly at the feature graph (P7). Only MATERIAL
        // (describe-the-part, not identify-the-body) carries over, and only when unambiguous: if
        // every prior Body shares one material the new solids inherit it; if they differ,
        // attributing a material to a specific new solid would itself be a resemblance judgement,
        // so it is left at the default.
        bool inheritMat = false;
        Materials::Material sharedMat;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            const Materials::Material mat = bodies[b]->ShapeMaterial.getValue();
            if (b == 0) {
                sharedMat = mat;
                inheritMat = true;
            }
            else if (mat.getUUID() != sharedMat.getUUID()) {
                inheritMat = false;
                break;
            }
        }

        Base::Console().warning(
            "Cruth Amendment 3 §4.7: feature '%s' changed topology (%zu body/bodies -> %d "
            "solid(s)); body identity was reset — every piece is a new body. Re-pick any "
            "references that pointed at the retired bodies.\n",
            feature->getNameInDocument(),
            bodies.size(),
            solidCount
        );

        App::DocumentObject* group = App::GeoFeatureGroupExtension::getGroupOfObject(bodies.front());

        // Retire every prior Body. A marker owns nothing, so removal breaks no property refs.
        for (auto* body : bodies) {
            doc->removeObject(body->getNameInDocument());
        }

        // Spawn a fresh Body for every solid: split children, union results and new components
        // alike. Identity resets throughout (fresh name/UUID/colour via spawnAutoBody+setupObject,
        // fresh component-id here); only material inherits, and only when it was unambiguous.
        const bool multiSolid = solidCount > 1;
        for (int i = 1; i <= solidCount; ++i) {
            Body* body = Body::spawnAutoBody(doc);
            if (!body) {
                continue;
            }
            body->Tip.setValue(feature);
            if (group) {
                group->getExtensionByType<App::GeoFeatureGroupExtension>()->addObject(body);
            }
            body->TipComponentId.setValue(multiSolid ? componentIdOfSolid(shape, i) : "");
            if (inheritMat) {
                body->ShapeMaterial.setValue(sharedMat);
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
    }

    bool m_initialized = false;
    fastsignals::scoped_connection m_newDocConn;
    fastsignals::scoped_connection m_delDocConn;
    std::map<const App::Document*, fastsignals::scoped_connection> m_recomputeConns;
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

    // Cruth de-ownership (Stage 3b-i): walk the BaseFeature chain backward, not
    // Group order. Group is dormant — reading it returns nothing on a de-owned
    // body, silently degrading every caller. The chain links solid features
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
        // obj->isDerivedFrom<Part::Feature>()
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
        // Non-PartDesign objects (and any feature still sitting in a legacy Group) never
        // back multiple Bodies: defer to the old Group-scan lookup and wrap its 0-or-1
        // answer as a list.
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

    // Fall-through for non-PartDesign objects (and any feature still sitting in a legacy
    // Group): the old Group-scan lookup. The derived chain walk above has already handled
    // every PartDesign::Feature case.
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
    auto* geo = freecad_cast<Part::Feature*>(const_cast<App::DocumentObject*>(feature));
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
                return componentIdOfSolid(shape, i);
            }
        }
        return {};
    }

    const std::vector<int> solids = shape.findAncestors(sub, TopAbs_SOLID);
    if (solids.empty()) {
        return {};
    }
    return componentIdOfSolid(shape, solids.front());
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

    // Detach from any prior owning group, mirroring the legacy path. A freshly
    // created feature is normally group-less, but a moved feature may not be.
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
        // BaseFeature chain, not Group order, so it works on a de-owned body.
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
    // the requested position rather than editing Group order — Group is dormant and
    // the pipeline is derived from the chain, so wiring BaseFeature links *is* the
    // insert. Generalizes the Tip-splice addObject performs to an arbitrary (target,
    // after) anchor. ARCHITECTURE §3.2/§3.3.

    // Validate target membership via the de-ownership back-pointer, not dormant Group.
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
// rather than reading Group order. BaseFeature is an intra-body link, so the
// successor is unique. ARCHITECTURE §3.2/§3.3.
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
// BaseFeature chain rather than by editing Group order. The chain successor
// (the solid whose BaseFeature points at this feature) is relinked to this
// feature's own base, and the Tip retreats along the chain. Group is never
// consulted for ordering, so this works on a fully de-owned body (empty Group);
// any stale legacy Group entry is still dropped. ARCHITECTURE §3.2/§3.3.
std::vector<App::DocumentObject*> Body::removeFeature(App::DocumentObject* feature)
{
    // This method must be called BEFORE the feature is removed from the Document!
    // De-ownership is the only path: heal the BaseFeature chain directly, retreat the
    // Tip, and retire the Body if its chain empties — Group order is never consulted.
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

    // Retreat the Tip if it pointed at the removed feature.
    if (Tip.getValue() == feature) {
        Tip.setValue(prevSolidFeature ? prevSolidFeature : nextSolidFeature);
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
        App::Document* doc = getDocument();
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
    /*
    Base::Console().error("Body '%s':\n", getNameInDocument());
    App::DocumentObject* tip = Tip.getValue();
    Base::Console().error("   Tip: %s\n", (tip == NULL) ? "None" : tip->getNameInDocument());
    std::vector<App::DocumentObject*> model = Group.getValues();
    Base::Console().error("   Group:\n");
    for (std::vector<App::DocumentObject*>::const_iterator m = model.begin(); m != model.end(); m++)
    { if (*m == NULL) continue; Base::Console().error("      %s", (*m)->getNameInDocument()); if
    (Body::isSolidFeature(*m)) { App::DocumentObject* baseFeature =
    static_cast<PartDesign::Feature*>(*m)->BaseFeature.getValue(); Base::Console().error(", Base:
    %s\n", baseFeature == NULL ? "None" : baseFeature->getNameInDocument()); } else {
            Base::Console().error("\n");
        }
    }
    */

    App::DocumentObject* tip = Tip.getValue();

    Part::TopoShape tipShape;
    if (tip) {
        if (!tip->isDerivedFrom<PartDesign::Feature>()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Linked object is not a PartDesign feature")
            );
        }

        // get the shape of the tip
        tipShape = static_cast<Part::Feature*>(tip)->Shape.getShape();

        if (tipShape.getShape().IsNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Tip shape is empty")
            );
        }

        // Cruth §3.3: a multi-output Body represents one component of its Tip's
        // shape, named by TipComponentId. Empty id = the implicit single-component
        // case (propagate the whole shape). A set id that no longer resolves is an
        // honest failure (P7), not a silent fall-back to the whole shape.
        const std::string cid = TipComponentId.getStrValue();
        if (!cid.empty()) {
            Part::TopoShape component = extractSolidById(tipShape, cid);
            if (component.isNull()) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Tip component for this Body no longer exists")
                );
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
        tipShape = Part::TopoShape();
    }

    Shape.setValue(tipShape);
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
            // Under de-ownership Group is empty (§9.1), so find the root by walking the
            // BaseFeature chain back from the Tip until it leaves this Body (null base, or a
            // base belonging to another Body across a seam). Reading Group here would always
            // see "empty" and mint a duplicate FeatureBase on every BaseFeature re-set.
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
            // Derived membership (§9.1-inverse): Group is empty under de-ownership, so push
            // the Body material onto its features via the derived list, not the container.
            std::vector<App::DocumentObject*> features = getFullModel();
            if (!features.empty()) {
                for (auto it : features) {
                    auto feature = dynamic_cast<Part::Feature*>(it);
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
    // Amendment 2, GitHub #19) a CAD (Part) document mints its App::Origin at creation
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
    // (ARCHITECTURE_AMENDMENTS Amendment 2, GitHub #19) — every Body's features anchor to that
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
    // path component was removed here: feature grouping placed those folders in Body.Group,
    // which is empty under de-ownership, so the peek was inert. Path resolution now runs
    // entirely through the derived findOwnedFeature delegation below.

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
        }
    }
#if 1
    return Part::BodyBase::getSubObject(subname, pyObj, pmat, transform, depth);
#else
    // The following code returns Body shape only if there is at least one
    // child visible in the body (when show through, not show tip). The
    // original intention is to sync visual to shape returned by
    // Part.getShape() when the body is included in some other group. But this
    // interfere with direct modeling using body shape. Therefore it is
    // disabled here.

    if (!pyObj || showTip
        || (subname && !Data::ComplexGeoData::isMappedElement(subname) && strchr(subname, '.'))) {
        return Part::BodyBase::getSubObject(subname, pyObj, pmat, transform, depth);
    }

    // We return the shape only if there are feature visible inside. Derived membership
    // (§9.1-inverse): Group is empty under de-ownership, so scan the derived list.
    for (auto obj : getFullModel()) {
        if (obj->Visibility.getValue() && obj->isDerivedFrom<PartDesign::Feature>()) {
            return Part::BodyBase::getSubObject(subname, pyObj, pmat, transform, depth);
        }
    }
    if (pmat && transform) {
        *pmat *= Placement.getValue().toMatrix();
    }
    return const_cast<Body*>(this);
#endif
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
    // BaseFeature chain (Group is empty under de-ownership). findBodyOf self-heals on
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
