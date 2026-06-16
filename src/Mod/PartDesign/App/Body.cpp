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

#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <App/VarSet.h>
#include <App/Origin.h>
#include <Base/Color.h>
#include <Base/Parameter.h>
#include <Base/Placement.h>

#include <Mod/Part/App/AttachExtension.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>

#include "Body.h"
#include "BodyPy.h"
#include "FeatureBase.h"
#include "FeatureSketchBased.h"
#include "FeatureSolid.h"
#include "FeatureTransformed.h"
#include "ShapeBinder.h"

using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Body, Part::BodyBase)

namespace
{
// CoreCAD §4.6 palette — 8 distinguishable Body identity colours.
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

// CoreCAD intra-body de-ownership (Day 3). When enabled, new features are wired
// into the Body's pipeline by reference (BaseFeature chain + Tip) instead of by
// exclusive Body.Group membership (ARCHITECTURE §3.2/§3.3). Off by default so the
// legacy ownership path remains the backstop during the migration.
bool deownedFeatureCreation()
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication()
                                             .GetUserParameter()
                                             .GetGroup("BaseApp")
                                             ->GetGroup("Preferences")
                                             ->GetGroup("Mod/PartDesign");
    return hGrp->GetBool("DeownedFeatureCreation", false);
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

/*
// Note: The following code will catch Python Document::removeObject() modifications. If the object
removed is
// a member of the Body::Group, then it will be automatically removed from the Group property which
triggers the
// following two methods
// But since we require the Python user to call both Document::addObject() and Body::addObject(), we
should
// also require calling both Document::removeObject and Body::removeFeature() in order to be
consistent void Body::onBeforeChange(const App::Property *prop)
{
    // Remember the feature before the current Tip. If the Tip is already at the first feature,
remember the next feature if (prop == &Group) { std::vector<App::DocumentObject*> features =
Group.getValues(); if (features.empty()) { rememberTip = NULL; } else {
            std::vector<App::DocumentObject*>::iterator it = std::find(features.begin(),
features.end(), Tip.getValue()); if (it == features.begin()) { it++; if (it == features.end())
rememberTip = NULL; else rememberTip = *it; } else { it--; rememberTip = *it;
            }
        }
    }

    return Part::Feature::onBeforeChange(prop);
}

void Body::onChanged(const App::Property *prop)
{
    if (prop == &Group) {
        std::vector<App::DocumentObject*> features = Group.getValues();
        if (features.empty()) {
            Tip.setValue(NULL);
        } else {
            std::vector<App::DocumentObject*>::iterator it = std::find(features.begin(),
features.end(), Tip.getValue()); if (it == features.end()) {
                // Tip feature was deleted
                Tip.setValue(rememberTip);
            }
        }
    }

    return Part::Feature::onChanged(prop);
}
*/

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

    if (!hasObject(start)) {
        return nullptr;
    }

    const std::vector<App::DocumentObject*>& features = Group.getValues();

    auto startIt = std::find(features.rbegin(), features.rend(), start);
    if (startIt == features.rend()) {  // object not found
        return nullptr;
    }

    auto rvIt = std::find_if(startIt + 1, features.rend(), isSolidFeature);
    if (rvIt != features.rend()) {  // the solid found in model list
        return *rvIt;
    }
    return nullptr;
}

App::DocumentObject* Body::getNextSolidFeature(App::DocumentObject* start)
{
    if (!start) {  // default to tip
        start = Tip.getValue();
    }

    if (!start || !hasObject(start)) {  // no or faulty tip
        return nullptr;
    }

    const std::vector<App::DocumentObject*>& features = Group.getValues();
    std::vector<App::DocumentObject*>::const_iterator startIt;

    startIt = std::find(features.begin(), features.end(), start);
    if (startIt == features.end()) {  // object not found
        return nullptr;
    }

    startIt++;
    if (startIt == features.end()) {  // features list has only one element
        return nullptr;
    }

    auto rvIt = std::find_if(startIt, features.end(), isSolidFeature);
    if (rvIt != features.end()) {  // the solid found in model list
        return *rvIt;
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

    if (deownedFeatureCreation()) {
        return addObjectDeowned(feature);
    }

    // TODO: features should not add all links

    // only one group per object. If it is in a body the single feature will be removed
    auto* group = App::GroupExtension::getGroupOfObject(feature);
    if (group && group != getExtendedObject()) {
        group->getExtensionByType<GroupExtension>()->removeObject(feature);
    }


    insertObject(feature, getNextSolidFeature(), /*after = */ false);
    // Move the Tip if we added a solid
    if (isSolidFeature(feature)) {
        Tip.setValue(feature);
    }

    if (feature->Visibility.getValue() && feature->isDerivedFrom<PartDesign::Feature>()) {
        for (auto obj : Group.getValues()) {
            if (obj->Visibility.getValue() && obj != feature
                && obj->isDerivedFrom<PartDesign::Feature>()) {
                obj->Visibility.setValue(false);
            }
        }
    }

    std::vector<App::DocumentObject*> result = {feature};
    return result;
}

std::vector<App::DocumentObject*> Body::addObjects(std::vector<App::DocumentObject*> objs)
{

    for (auto obj : objs) {
        addObject(obj);
    }

    return objs;
}

// Cruth intra-body de-ownership (Day 3; mid-chain splice added Day 6): wire a new
// feature into the Body's pipeline by reference — BaseFeature chain + Tip —
// WITHOUT adding it to Body.Group. The pipeline is derived from the chain back
// from the Tip (ARCHITECTURE §3.2/§3.3), so group membership is no longer the
// source of truth for feature ordering. Handles both the tip-append gesture (the
// new solid extends the body from the current Tip) and mid-chain insert (the Tip
// is an interior feature): in the latter case the displaced successor is rerouted
// onto the new feature so the chain stays linear instead of forking.
std::vector<App::DocumentObject*> Body::addObjectDeowned(App::DocumentObject* feature)
{
    // Detach from any prior owning group, mirroring the legacy path. A freshly
    // created feature is normally group-less, but a moved feature may not be.
    auto* group = App::GroupExtension::getGroupOfObject(feature);
    if (group) {
        group->getExtensionByType<GroupExtension>()->removeObject(feature);
    }

    // Keep origin/datum links resolving against this Body's Origin frame.
    relinkToOrigin(feature);

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

    std::vector<App::DocumentObject*> result = {feature};
    return result;
}


void Body::insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after)
{
    if (target && !hasObject(target)) {
        throw Base::ValueError(
            "Body: the feature we should insert relative to is not part of that body"
        );
    }

    // ensure that all origin links are ok
    relinkToOrigin(feature);

    std::vector<App::DocumentObject*> model = Group.getValues();
    std::vector<App::DocumentObject*>::iterator insertInto;

    // Find out the position there to insert the feature
    if (!target) {
        if (after) {
            insertInto = model.begin();
        }
        else {
            insertInto = model.end();
        }
    }
    else {
        std::vector<App::DocumentObject*>::iterator targetIt
            = std::find(model.begin(), model.end(), target);
        assert(targetIt != model.end());
        if (after) {
            insertInto = targetIt + 1;
        }
        else {
            insertInto = targetIt;
        }
    }

    // Insert the new feature after the given
    model.insert(insertInto, feature);

    Group.setValues(model);

    if (feature->isDerivedFrom<PartDesign::Feature>()) {
        static_cast<PartDesign::Feature*>(feature)->_Body.setValue(this);
    }

    // Set the BaseFeature property
    setBaseProperty(feature);
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
std::vector<App::DocumentObject*> Body::removeObjectDeowned(App::DocumentObject* feature)
{
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

std::vector<App::DocumentObject*> Body::removeObject(App::DocumentObject* feature)
{
    // This method must be called BEFORE the feature is removed from the Document!

    if (deownedFeatureCreation()) {
        return removeObjectDeowned(feature);
    }

    App::DocumentObject* nextSolidFeature = getNextSolidFeature(feature);
    App::DocumentObject* prevSolidFeature = getPrevSolidFeature(feature);

    // It's ok to remove the first solid feature, that just mean the next feature become the base one

    if (nextSolidFeature && nextSolidFeature->isDerivedFrom(PartDesign::Feature::getClassTypeId())) {
        auto* nextPD = static_cast<PartDesign::Feature*>(nextSolidFeature);
        // Check if the next feature is pointing to the one being deleted
        if (nextPD->BaseFeature.getValue() == feature) {
            nextPD->BaseFeature.setValue(prevSolidFeature);
        }
    }

    std::vector<App::DocumentObject*> model = Group.getValues();
    const auto it = std::ranges::find(model, feature);

    // Adjust Tip feature if it is pointing to the deleted object
    if (Tip.getValue() == feature) {
        if (prevSolidFeature) {
            Tip.setValue(prevSolidFeature);
        }
        else {
            Tip.setValue(nextSolidFeature);
        }
    }

    // Erase feature from Group
    if (it != model.end()) {
        model.erase(it);
        Group.setValues(model);
    }
    std::vector<App::DocumentObject*> result = {feature};
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

void Body::setupObject()
{
    Part::BodyBase::setupObject();

    // CoreCAD §4.6: assign a deterministic identity colour at spawn time.
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
