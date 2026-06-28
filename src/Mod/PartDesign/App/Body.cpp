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


#include <array>

#include <map>

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
#include <Base/Color.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>
#include <Base/Tools.h>

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
    ADD_PROPERTY_TYPE(AllowCompound, (true), "Base", App::Prop_None, "Allow multiple solids in Body");
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

    _GroupTouched.setStatus(App::Property::Output, true);
}

Body* Body::spawnAutoBody(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }

    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter().GetGroup(
        "BaseApp/Preferences/Mod/PartDesign"
    );
    const bool allowCompound = hGrp->GetBool("AllowCompoundDefault", true);

    auto name = doc->getUniqueObjectName("Body");
    auto* body = freecad_cast<Body*>(doc->addObject("PartDesign::Body", name.c_str()));
    if (body) {
        // Color is assigned by setupObject() from the per-document palette index.
        body->AllowCompound.setValue(allowCompound);
    }
    return body;
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
    newBody->addObject(baked);  // also points the new Body's Tip at the BakedShape

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
        const auto componentCount = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));

        if (componentCount <= 1) {
            // Collapse to a single component (Cruth §4.7 retire-on-shrink): the Tip now
            // has one solid, so one Body survives carrying the whole shape and any extra
            // Bodies are retired. A marker owns nothing, so removing it breaks no refs;
            // downstream breakage, if any, surfaces honestly at the feature graph (P7).
            Body* survivor = bodies.front();
            const bool hadExtras = bodies.size() > 1;
            const bool staleCid = !survivor->TipComponentId.getStrValue().empty();
            for (std::size_t i = 1; i < bodies.size(); ++i) {
                doc->removeObject(bodies[i]->getNameInDocument());
            }
            if (hadExtras || staleCid) {
                survivor->TipComponentId.setValue("");
                survivor->recomputeFeature();
                survivor->purgeTouched();
            }
            continue;
        }

        // Multi-output: ensure one Body per component, each stamped with its id.
        std::vector<std::string> ids;
        ids.reserve(componentCount);
        for (int i = 1; i <= componentCount; ++i) {
            ids.push_back(componentIdOfSolid(shape, i));
        }

        // Keep Bodies already pointing at a still-present component; the rest are
        // free to be re-stamped (covers the first multi-output recompute, where the
        // originating Body still carries the empty single-component id).
        std::set<std::string> claimed;
        std::vector<Body*> freeBodies;
        for (auto* body : bodies) {
            const std::string cid = body->TipComponentId.getStrValue();
            if (!cid.empty() && claimed.count(cid) == 0
                && std::find(ids.begin(), ids.end(), cid) != ids.end()) {
                claimed.insert(cid);
            }
            else {
                freeBodies.push_back(body);
            }
        }

        App::DocumentObject* group = App::GeoFeatureGroupExtension::getGroupOfObject(bodies.front());
        for (const std::string& cid : ids) {
            if (claimed.count(cid) != 0) {
                continue;
            }
            Body* body = nullptr;
            if (!freeBodies.empty()) {
                body = freeBodies.back();
                freeBodies.pop_back();
            }
            else {
                body = Body::spawnAutoBody(doc);
                if (!body) {
                    continue;
                }
                body->Tip.setValue(feature);
                if (group) {
                    group->getExtensionByType<App::GeoFeatureGroupExtension>()->addObject(body);
                }
            }
            body->TipComponentId.setValue(cid);
            claimed.insert(cid);
        }

        // Retire orphaned markers: any freeBodies left after the assign loop carry a
        // component-id that has vanished from the Tip (e.g. a pattern instance was
        // skipped, N->N-1). A marker owns nothing, so removal is the exact inverse of
        // auto-spawn and always safe (Cruth §4.7).
        for (auto* orphan : freeBodies) {
            doc->removeObject(orphan->getNameInDocument());
        }
        freeBodies.clear();

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

Body* Body::resolveBaseBody(Part::Part2DObject* sketch, App::Document* doc, bool& ambiguous)
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

    if (bodies.empty()) {
        return spawnAutoBody(doc);
    }
    if (bodies.size() == 1) {
        return *bodies.begin();
    }

    ambiguous = true;
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


Body* Body::findBodyOf(const App::DocumentObject* feature)
{
    if (!feature) {
        return nullptr;
    }

    return static_cast<Body*>(BodyBase::findBodyOf(feature));
}


std::vector<App::DocumentObject*> Body::addObject(App::DocumentObject* feature)
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
        group->getExtensionByType<GroupExtension>()->removeObject(feature);
    }

    // Cruth substrate flip (Stage 3a): resolve origin/datum links against the single
    // document-level Origin, not this Body's own (now-dormant) per-body Origin. In the
    // de-ownership model the coordinate frame is shared at document level (Day-5 design;
    // ARCHITECTURE §3.3), so all bodies' features anchor to one Origin.
    relinkToOrigin(feature, ensureDocumentOrigin());

    // Associate the feature with this Body by reference (used by findBodyOf and
    // active-body tooling) without imprisoning it in the group.
    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        static_cast<PartDesign::Feature*>(feature)->_Body.setValue(this);
    }

    if (isSolidFeature(feature)) {
        // Splice the new solid into the chain at the Tip: its base is the old Tip,
        // and the Body now propagates the new feature.
        App::DocumentObject* prevTip = Tip.getValue();

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

std::vector<App::DocumentObject*> Body::addObjects(std::vector<App::DocumentObject*> objs)
{

    for (auto obj : objs) {
        addObject(obj);
    }

    return objs;
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
    relinkToOrigin(feature, ensureDocumentOrigin());

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
std::vector<App::DocumentObject*> Body::removeObject(App::DocumentObject* feature)
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

    // Drop any stale legacy Group membership (no-op on a born-de-owned body).
    std::vector<App::DocumentObject*> model = Group.getValues();
    const auto it = std::ranges::find(model, feature);
    if (it != model.end()) {
        model.erase(it);
        Group.setValues(model);
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
            auto first = Group.getValues().empty() ? nullptr : Group.getValues().front();

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
        else if (prop == &Group) {
            // if the FeatureBase was deleted we set the BaseFeature link to nullptr
            if (BaseFeature.getValue()
                && (Group.getValues().empty()
                    || !Group.getValues().front()->isDerivedFrom<FeatureBase>())) {
                BaseFeature.setValue(nullptr);
            }
        }
        else if (prop == &Placement) {
            // Cruth substrate flip, Stage 3a (interim guard). In the de-ownership
            // model the modeling Body carries no coordinate frame of its own — the
            // frame comes from each feature's attachment, not from Body containment
            // (Day-5 design; ARCHITECTURE §3.3). De-owned features do not inherit
            // Body.Placement, so a non-identity Body placement would silently diverge
            // from its features (the Day-1 Experiment-C case). Pin it back to identity
            // on any live edit until Stage 3b removes the property outright. Restore
            // and undo/redo are excluded by the guard above, so loaded data is never
            // mutated; this fires only on a live user change (e.g. dragging the Body).
            const Base::Placement identity;
            if (Placement.getValue() != identity) {
                Placement.setValue(identity);
            }
        }
        else if (prop == &AllowCompound) {
            // As disallowing compounds can break the model we need to recompute the whole tree.
            // This will inform user about first place where there is more than one solid.
            // On allowing compounds we must also recompute the entire feature tree
            for (auto feature : getFullModel()) {
                feature->enforceRecompute();
            }
        }
        else if (prop == &ShapeMaterial) {
            std::vector<App::DocumentObject*> features = Group.getValues();
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

App::Origin* Body::ensureDocumentOrigin()
{
    App::Document* doc = getDocument();
    if (!doc) {
        return nullptr;
    }

    // The single document-level Origin is shared by every PartDesign Body via the
    // shared-Origin contract (onExtendedSetupObject). It is identified as the App::Origin
    // already linked by a Body, or — before the first Body has linked it — a free-standing
    // App::Origin that no OriginGroup owns. A per-body/per-part private Origin (e.g. an
    // App::Part's own ruler) is owned by exactly that group and is never linked by a Body,
    // so it is correctly skipped.
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

    // None yet — create it. It carries the world-frame datum planes/axes and is linked
    // only by the features that reference it, never owned by a body.
    auto* shared = doc->addObject<App::Origin>("Origin");
    shared->Label.setValue("Origin");
    return shared;
}

void Body::onExtendedSetupObject()
{
    // Cruth shared-Origin contract (GitHub #4): a PartDesign Body does NOT own a private
    // coordinate frame. In the de-ownership model the world frame is shared at document
    // level (ARCHITECTURE §3.3) — every Body anchors its features to the single
    // free-standing App::Origin. Bind this Body's Origin link to that shared Origin instead
    // of letting OriginGroupExtension mint a private one. Because no per-body Origin is ever
    // created, retiring a Body can never bin an axis that another Body's feature references
    // (the leak that nulled a pattern's Direction on break-out), getOrigin() returns the
    // shared ruler so the ~20 GUI feature-creation sites auto-anchor to it, and there is only
    // one X/Y/Z axis so no create-order race can escape onto a dormant per-body axis.
    //
    // We deliberately bypass OriginGroupExtension::onExtendedSetupObject() (which would call
    // getLocalizedOrigin()); App::Part still uses the base behaviour since it does not derive
    // from Body.
    if (App::Origin* shared = ensureDocumentOrigin()) {
        Origin.setValue(shared);
    }
    App::GeoFeatureGroupExtension::onExtendedSetupObject();
}

void Body::onExtendedUnsetupObject()
{
    // Counterpart to the contract above: the shared document Origin is not owned by this
    // Body, so it must outlive the Body's retirement. Detach our link and skip
    // OriginGroupExtension::onExtendedUnsetupObject()'s destructive delete of the linked
    // Origin; defer the remaining teardown to the grandparent.
    Origin.setValue(nullptr);
    App::GeoFeatureGroupExtension::onExtendedUnsetupObject();
}

void Body::setupObject()
{
    Part::BodyBase::setupObject();

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

    // PartDesign::Feature now support grouping sibling features, and the user
    // is free to expand/collapse at any time. To not disrupt subname path
    // because of this, the body will peek the next two sub-objects reference,
    // and skip the first sub-object if possible.
    if (subname) {
        const char* firstDot = strchr(subname, '.');
        if (firstDot) {
            const char* secondDot = strchr(firstDot + 1, '.');
            if (secondDot) {
                auto firstObj = Group.find(std::string(subname, firstDot).c_str());
                if (!firstObj || firstObj->isDerivedFrom<PartDesign::Feature>()) {
                    auto secondObj = Group.find(std::string(firstDot + 1, secondDot).c_str());
                    if (secondObj) {
                        // we support only one level of sibling grouping, so no
                        // recursive call to our own getSubObject()
                        return Part::BodyBase::getSubObject(
                            firstDot + 1,
                            pyObj,
                            pmat,
                            transform,
                            depth + 1
                        );
                    }
                }
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

    // We return the shape only if there are feature visible inside
    for (auto obj : Group.getValues()) {
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

void Body::onDocumentRestored()
{
    for (auto obj : Group.getValues()) {
        if (obj->isDerivedFrom<PartDesign::Feature>()) {
            static_cast<PartDesign::Feature*>(obj)->_Body.setValue(this);
        }
    }
    _GroupTouched.setStatus(App::Property::Output, true);

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
