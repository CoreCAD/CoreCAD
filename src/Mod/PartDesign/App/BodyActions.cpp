// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Creating a Body, moving a feature between Bodies, and breaking a pattern copy out.

#include <string>
#include <vector>

#include <TopAbs_ShapeEnum.hxx>

#include <App/Document.h>

#include <Mod/Part/App/TopoShape.h>

#include "Body.h"
#include "FeatureBakedShape.h"
#include "FeatureTransformed.h"

using namespace PartDesign;

Body* Body::spawnAutoBody(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }
    // Before addObject, so a document without a world frame is refused with nothing half-made.
    requireDocumentOrigin(doc);
    auto name = doc->getUniqueObjectName("Body");
    return freecad_cast<Body*>(doc->addObject("PartDesign::Body", name.c_str()));
}

Body* Body::moveFeatureToBody(App::DocumentObject* feature, Body* target)
{
    if (!feature) {
        return nullptr;
    }
    Body* from = findBodyOf(feature);
    if (target && from == target) {
        return target;
    }
    App::Document* doc = feature->getDocument();
    if (!doc) {
        return nullptr;
    }
    // May retire `from`; do not touch it afterwards.
    if (from) {
        from->removeFeature(feature);
    }
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
    auto* pattern = freecad_cast<PartDesign::Transformed*>(instanceBody->Tip.getValue());
    const std::string cid = instanceBody->TipComponentId.getStrValue();
    if (!doc || !pattern || cid.empty()) {
        return nullptr;
    }

    // Capture before the skip is recorded: once skipped, the copy is no longer made.
    Part::TopoShape captured = instanceBody->derivedTipShape();
    if (captured.countSubShapes(TopAbs_SOLID) >= 1) {
        captured = captured.getSubTopoShape(TopAbs_SOLID, 1, /*silent*/ true);
    }
    if (captured.isNull()) {
        return nullptr;
    }

    // A frozen shape with no input link, so the new Body is cut off from the pattern.
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
    newBody->addFeature(baked);

    // The skip list keys on the copy's ordinal, which stays stable; the component id does not.
    // The copy's old Body is retired by the reconciler.
    const long ordinal = pattern->ordinalOfComponent(cid);
    if (ordinal < 0) {
        return newBody;
    }
    std::vector<long> skips = pattern->SkipInstances.getValues();
    skips.push_back(ordinal);
    pattern->SkipInstances.setValues(skips);
    doc->recompute();
    return newBody;
}
