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
#include <App/Link.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <App/VarSet.h>
#include <App/Origin.h>
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
#include <Mod/Part/App/SpatialInterference.h>
#include <Mod/Part/App/TopoShape.h>

#include <App/GeoFeature.h>

#include "Feature.h"
#include "FeaturePocket.h"

#include "Body.h"
#include "BodyChain.h"
#include "BodyPy.h"
#include "FeatureBakedShape.h"
#include "FeatureBase.h"
#include "FeatureBoolean.h"
#include "FeatureSketchBased.h"
#include "FeatureSolid.h"
#include "FeatureTransformed.h"

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

// The palette colour the fewest other bodies in the document wear, earliest on a tie. A count of
// bodies is not enough: bodies retire and respawn (pattern and split results), so the count
// repeats and two live bodies would share a colour.
Base::Color leastUsedPaletteColor(const App::Document* doc, const PartDesign::Body* self)
{
    std::array<int, bodyPalette.size()> uses {};
    for (auto* other : doc->getObjectsOfType<PartDesign::Body>()) {
        if (other == self) {
            continue;
        }
        const Base::Color color = other->Color.getValue();
        for (std::size_t i = 0; i < bodyPalette.size(); ++i) {
            if (color == paletteColorFor(i)) {
                ++uses[i];
                break;
            }
        }
    }
    const auto least = std::ranges::min_element(uses);
    return paletteColorFor(static_cast<std::size_t>(least - uses.begin()));
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

// Build `feature` on copy `copy` of `base`; the steps that were there now build on it.
// Returns how many moved.
std::size_t spliceAfter(PartDesign::Feature* feature, App::DocumentObject* base, long copy)
{
    auto steps = chain::nextSteps(base, copy);
    std::erase(steps, feature);
    feature->BaseFeature.setValue(base);
    feature->BaseInstance.setValue(copy);
    for (auto* step : steps) {
        step->BaseFeature.setValue(feature);
        step->BaseInstance.setValue(-1);
    }
    return steps.size();
}

// Take `feature` out of the chain: the steps on its whole output move to its base. A step on
// one copy stays; the copy goes with the feature, so the step fails and says so.
void unsplice(App::DocumentObject* feature)
{
    auto* pd = freecad_cast<PartDesign::Feature*>(feature);
    App::DocumentObject* base = pd ? pd->BaseFeature.getValue() : nullptr;
    const long copy = pd ? pd->BaseInstance.getValue() : -1;
    for (auto* step : chain::nextSteps(feature, chain::WholeOutput)) {
        step->BaseInstance.setValue(copy);
        step->BaseFeature.setValue(base);
        step->onBaseFeatureRerouted(feature, base);  // re-find its edges on the new base
    }
}
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

    // (Cruth §3.3/§4, issue #12) A Body carries no coordinate frame of its own and no longer
    // has a Placement slot to pin: BodyBase now derives from the unplaced Part::ShapeFeature,
    // not Part::Feature, so App::PlacementExtension (and its Placement property) is gone. There
    // is nothing here to guard back to identity on a live edit.

    // (Cruth §3.3, issue #79-interim) The inherited Shape property is a DERIVED mirror of the
    // Tip, not authored state. Publish it Transient (never serialized — the Tip chain is the
    // source of truth on reload, so a persisted copy would only risk going stale) and ReadOnly
    // (a user never authors a Body's shape). execute() refreshes it from the Tip each pass; this
    // keeps getPropertyOfGeometry() and every direct .Shape reader (FEM meshing, Inspection, the
    // Transform bounding box) honest, since those bypass the getSubObject derivation. The full
    // fix — removing the property in favour of a Part::ShapeExtension — is the deferred #79.
    Shape.setStatus(App::Property::Transient, true);
    Shape.setStatus(App::Property::ReadOnly, true);

    // (Cruth §3.3 / Amendment 17, #79) The shape-source capability is composed by the
    // Part::ShapeFeature base (carried here via BodyBase), so a Body already has it —
    // Part::hasShape is true. The extension hosts the own-shape half of getSubObject and sources
    // its backing geometry through the inherited getPropertyOfGeometry() hook (the derived Shape
    // mirror above), so the Body::getSubObject override (which delegates to the App base) answers a
    // sub-element query via the capability, exactly like a stored-backed feature.
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

    // The first step on `start`'s whole output, skipping non-solids; cycle-guarded.
    std::set<App::DocumentObject*> seen {start};
    for (App::DocumentObject* cursor = start; cursor;) {
        auto steps = chain::nextSteps(cursor, chain::WholeOutput);
        App::DocumentObject* next = steps.empty() ? nullptr : steps.front();
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
    // A feature that says its output is a part of its own (§4.6) advances a Body's chain in
    // exactly the same way, whether or not it was authored in PartDesign. An import is the
    // first of these: §7.8 gives its geometry a Body rather than leaving it loose in the
    // document. It carries no BaseFeature, so it can only ever be the start of a chain --
    // which is what an anchor is.
    if (const auto* shapeFeature = freecad_cast<const Part::ShapeFeature*>(obj)) {
        return shapeFeature->spawnsBodyForOutput();
    }
    return false;  // DeepSOIC: work-in-progress?
}

bool Body::isAllowed(const App::DocumentObject* obj)
{
    if (!obj) {
        return false;
    }

    // An import whose output stands as a part of its own (§4.6) is a member like a solid feature.
    if (const auto* shapeFeature = freecad_cast<const Part::ShapeFeature*>(obj)) {
        if (shapeFeature->spawnsBodyForOutput()) {
            return true;
        }
    }

    // Solid features, sketches, lean datums and the local coordinate system, and VarSets for
    // parameters.
    return obj->isDerivedFrom<PartDesign::Feature>() || obj->isDerivedFrom<Part::Part2DObject>()
        || obj->isDerivedFrom<App::DatumElement>()
        || obj->isDerivedFrom<App::LocalCoordinateSystem>() || obj->isDerivedFrom<App::VarSet>();
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

    // A tree folder the feature is filed in is left alone. Folder membership is the user's
    // filing and the body's is a reference (P3); neither owns the feature, so joining a
    // body does not pull it out of a folder.

    // Cruth substrate flip (Stage 3a): resolve origin/datum links against the single
    // document-level Origin, not this Body's own (now-dormant) per-body Origin. In the
    // de-ownership model the coordinate frame is shared at document level (Day-5 design;
    // ARCHITECTURE §3.3), so all bodies' features anchor to one Origin.
    relinkFeatureToOrigin(feature, getDocumentOrigin());

    const long copy = tipCopy();

    if (isSolidFeature(feature) && !feature->isDerivedFrom<PartDesign::Feature>()) {
        // A solid feature with no BaseFeature of its own (an import, §7.8) cannot be spliced
        // into a chain: there is nothing on it to point at the previous Tip. It can only
        // start one, so it becomes the Tip and nothing else is rewired. Splicing one into an
        // existing chain would silently drop whatever came before it, so refuse instead.
        if (Tip.getValue()) {
            throw Base::ValueError(
                "Body: this feature has no base of its own, so it can only start a body's chain"
            );
        }
        Tip.setValue(feature);
    }
    else if (isSolidFeature(feature)) {
        appendAtTip(static_cast<PartDesign::Feature*>(feature), copy);
    }
    else if (feature->isDerivedFrom<PartDesign::Transformed>()) {
        // A new pattern reads as non-solid until configured (#1). Wire its base now, while
        // the Tip is known; it takes the Tip once configured (adoptConfiguredPattern, #125).
        auto* pattern = static_cast<PartDesign::Transformed*>(feature);
        pattern->BaseFeature.setValue(Tip.getValue());
        pattern->BaseInstance.setValue(copy);
        pattern->markAwaitingTip(this);
    }

    return {feature};
}

void Body::adoptConfiguredPattern(App::DocumentObject* pattern)
{
    auto* feature = freecad_cast<PartDesign::Feature*>(pattern);
    if (!feature) {
        return;
    }
    App::DocumentObject* prevTip = Tip.getValue();
    if (prevTip == pattern || prevTip != feature->BaseFeature.getValue()) {
        return;
    }

    appendAtTip(feature, feature->BaseInstance.getValue());
}

long Body::tipCopy() const
{
    auto* pattern = freecad_cast<PartDesign::Transformed*>(Tip.getValue());
    const std::string cid = TipComponentId.getStrValue();
    return pattern && !cid.empty() ? pattern->ordinalOfComponent(cid) : chain::WholeOutput;
}

void Body::appendAtTip(PartDesign::Feature* feature, long copy)
{
    App::DocumentObject* prevTip = Tip.getValue();
    spliceAfter(feature, prevTip, copy);
    if (copy >= 0) {
        TipComponentId.setValue("");  // the new Tip's whole output is that one copy
    }
    Tip.setValue(feature);
    if (prevTip && prevTip->isDerivedFrom<PartDesign::Feature>() && prevTip->Visibility.getValue()) {
        prevTip->Visibility.setValue(false);
    }
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
    // The chain is the Body's order, so wiring BaseFeature links is the insert.

    if (target) {
        if (!target->isDerivedFrom<PartDesign::Feature>() || !backsBody(target, this)) {
            throw Base::ValueError(
                "Body: the feature we should insert relative to is not part of that body"
            );
        }
    }

    // Resolve origin/datum links against the shared document-level Origin (Stage 3a).
    relinkFeatureToOrigin(feature, getDocumentOrigin());

    // Non-solid members (sketches, datums) carry no pipeline position — nothing to splice.
    if (!isSolidFeature(feature)) {
        return;
    }

    auto* pd = static_cast<PartDesign::Feature*>(feature);
    auto* targetPd = freecad_cast<PartDesign::Feature*>(target);
    if (target && after) {
        if (spliceAfter(pd, target, -1) == 0) {
            Tip.setValue(feature);  // nothing followed the target
        }
    }
    else if (targetPd && targetPd->BaseFeature.getValue()) {
        // Before the target: on the target's own base and copy, so the target builds on it.
        spliceAfter(pd, targetPd->BaseFeature.getValue(), targetPd->BaseInstance.getValue());
    }
    else if (target || after) {
        // Base end (or before the root): the feature becomes the root the old root builds on.
        App::DocumentObject* root = target ? target : Tip.getValue();
        std::set<App::DocumentObject*> seen;
        for (auto* cur = freecad_cast<PartDesign::Feature*>(root);
             cur && cur->BaseFeature.getValue() && seen.insert(cur).second;
             cur = freecad_cast<PartDesign::Feature*>(root)) {
            root = cur->BaseFeature.getValue();
        }
        pd->BaseFeature.setValue(nullptr);
        pd->BaseInstance.setValue(-1);
        if (auto* rootPd = freecad_cast<PartDesign::Feature*>(root)) {
            rootPd->BaseFeature.setValue(feature);
        }
        else {
            Tip.setValue(feature);  // empty body
        }
    }
    else {
        appendAtTip(pd, tipCopy());
    }
}

// Steps on the feature move to its base (not steps on one copy), the Tip retreats, and an
// emptied Body retires.
std::vector<App::DocumentObject*> Body::removeFeature(App::DocumentObject* feature)
{
    // Call BEFORE the feature is removed from the Document.
    App::DocumentObject* prevSolidFeature = nullptr;
    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        prevSolidFeature = static_cast<PartDesign::Feature*>(feature)->BaseFeature.getValue();
    }
    const auto steps = chain::nextSteps(feature, chain::WholeOutput);
    App::DocumentObject* nextSolidFeature = steps.empty() ? nullptr : steps.front();
    unsplice(feature);

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
    const std::vector<Body*> tippedByFeature = bodiesTippedAt(feature);
    for (auto* sibling : tippedByFeature) {
        sibling->Tip.setValue(retreatTo);
    }
    if (tippedByFeature.size() > 1 && retreatTo) {
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
    if (!tip) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
            "Exception",
            "A Body must have at least one feature; empty bodies are not allowed"
        ));
    }
    if (!isSolidFeature(tip)) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Linked object is not a solid feature")
        );
    }
    if (static_cast<Part::ShapeFeature*>(tip)->Shape.getShape().isNull()) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Tip shape is empty"));
    }
    // Empty here means this Body's copy is gone: the reconciler retires it after this
    // recompute, so keep the last good shape rather than report an error.
    Part::TopoShape shape = derivedTipShape();
    if (!shape.isNull()) {
        Shape.setValue(shape);
    }
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

            const auto solids = ownSolids();
            App::DocumentObject* first = solids.empty()
                    || !solids.front()->isDerivedFrom<PartDesign::Feature>()
                ? nullptr
                : solids.front();

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
        // (issue #12) The identity-pinning Placement guard is gone: BodyBase now derives from
        // the unplaced Part::ShapeFeature, so a Body has no Placement property to drift or pin.
    }

    Part::BodyBase::onChanged(prop);
}

App::Origin* Body::findDocumentOrigin(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }

    // The single document-level Origin is shared by every PartDesign Body via the
    // shared-Origin contract (setupObject). No object owns a private Origin any more, so the
    // document's Origin is the only one.
    const auto origins = doc->getObjectsOfType<App::Origin>();
    if (!origins.empty()) {
        return origins.front();
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

    // Cruth §4.6: assign a deterministic identity colour at spawn time, one no live body is
    // wearing while the palette lasts.
    if (auto* doc = getDocument()) {
        Color.setValue(leastUsedPaletteColor(doc, this));
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
        const char* fname = feat->getNameInDocument();
        if ((byLabel ? (key == feat->Label.getStrValue()) : (fname && key == fname))
            && backsBody(feat, this)) {
            return feat;
        }
    }
    return nullptr;
}

// The Tip's shape, or the one copy of it this Body stands for, placed in the world.
Part::TopoShape Body::derivedTipShape() const
{
    App::DocumentObject* tip = Tip.getValue();
    if (!tip || !isSolidFeature(tip)) {
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
        // A pattern keeps each copy's offset in its placement; bake it into the geometry.
        component.transformShape(Base::Matrix4D(), true);
        tipShape = component;
    }
    tipShape.transformShape(tipShape.getTransform(), true);
    return tipShape;
}

std::string Body::tipSubElement(const char* bodySub) const
{
    App::DocumentObject* tip = Tip.getValue();
    if (!tip || !isSolidFeature(tip) || !bodySub || !*bodySub) {
        return {};
    }
    const Part::TopoShape tipShape = static_cast<Part::ShapeFeature*>(tip)->Shape.getShape();
    if (tipShape.isNull()) {
        return {};
    }
    const std::string cid = TipComponentId.getStrValue();
    if (cid.empty()) {
        // The Body shows the Tip's whole shape, numbered as the Tip numbers it.
        return tipShape.findShape(bodySub).IsNull() ? std::string() : std::string(bodySub);
    }
    // One copy: the Body shows that solid alone, placed but not renumbered, so the element
    // the pick names is the same one on the solid inside the Tip's shape. Find that element
    // in the whole shape by what it is, and read back its number there.
    const Part::TopoShape solid = extractSolidById(tip, tipShape, cid);
    if (solid.isNull()) {
        return {};
    }
    const TopoDS_Shape element = solid.findShape(bodySub);
    if (element.IsNull()) {
        return {};
    }
    const int index = tipShape.findShape(element);
    if (index <= 0) {
        return {};
    }
    return Part::TopoShape::shapeName(element.ShapeType()) + std::to_string(index);
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
                // A Body has no frame of its own (§4: unplaced marker, no Placement), so it
                // composes identity here — the owned feature applies its own frame. Delegate
                // straight through.
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
    // Cruth §3.3/§4 / Amendment 17 (#79 step 3a): a Body stores no geometry of its own — answer a
    // query for its own shape through the composed Part::ShapeExtension, exactly as a stored-backed
    // feature (Part::Box) does. Delegating to the App base runs the extension loop, and the
    // extension resolves the sub-element against the backing geometry it obtains from this Body's
    // getPropertyOfGeometry() — the Transient/ReadOnly Shape mirror execute()/onDocumentRestored()
    // keep in step with derivedTipShape(). A Body holds no authored position (§4: "nothing to
    // guard"), so its own frame is identity and no placement is composed (the extension's
    // getPropertyByName("Placement") lookup finds none). This replaces the former in-line
    // derivedTipShape() resolution, which this path now reaches via the capability rather than
    // by hand. (Path components containing '.' were already delegated to the owning feature or the
    // document Origin above; here subname is empty or a plain sub-element.)
    //
    // This supersedes the FreeCAD-era caution — returning the Body shape only when a child
    // was visible, to avoid double-draw when the Body sat inside another group — which was
    // long disabled; under de-ownership a Body always represents its Tip (§3.3).
    return App::DocumentObject::getSubObject(subname, pyObj, pmat, transform, depth);
}

void Body::onDocumentRestored()
{
    // (Cruth §3.3, issue #79-interim) The derived Shape mirror is Transient — not serialized —
    // so after a reload it is empty until the next recompute. Repopulate it from the Tip now:
    // the Tip's own Shape IS serialized and has already been restored at this point, so
    // derivedTipShape() yields valid geometry without forcing a document recompute. This keeps a
    // direct .Shape / getPropertyOfGeometry() reader honest immediately on open. Skip silently if
    // the Tip does not yet resolve its component (a Body about to be reconciled/retired, §4.7).
    Part::TopoShape restoredShape = derivedTipShape();
    if (!restoredShape.isNull()) {
        Shape.setValue(restoredShape);
    }

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
