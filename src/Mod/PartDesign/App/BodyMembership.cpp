// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Which Body a feature belongs to, and which features make up a Body. Nothing is stored: a Body
// points only at its Tip, so every answer is read from the chain.

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Datums.h>
#include <App/Document.h>
#include <Base/Exception.h>

#include <Mod/Part/App/AttachExtension.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>

#include "Body.h"
#include "BodyChain.h"
#include "FeatureSketchBased.h"

using namespace PartDesign;

namespace
{
template<typename Range, typename T>
bool contains(const Range& range, const T& value)
{
    return std::ranges::find(range, value) != std::ranges::end(range);
}

// The Bodies a solid feeds: each branch forward from it, up to its nearest Tip. Past a Tip only
// steps on single copies continue, since each carries a copy no Body at that Tip stands for.
std::vector<Body*> bodiesFedBy(const App::DocumentObject* solid)
{
    std::set<Body*> found;
    std::set<const App::DocumentObject*> seen;
    std::vector<const App::DocumentObject*> frontier {solid};
    while (!frontier.empty()) {
        const App::DocumentObject* cursor = frontier.back();
        frontier.pop_back();
        if (!seen.insert(cursor).second) {
            continue;
        }
        const auto tipped = Body::bodiesTippedAt(cursor);
        found.insert(tipped.begin(), tipped.end());
        for (auto* step : chain::nextSteps(cursor)) {
            if (tipped.empty() || step->BaseInstance.getValue() >= 0) {
                frontier.push_back(step);
            }
        }
    }
    return {found.begin(), found.end()};
}

// Guards against a datum cycle; real chains are three hops (sketch, datum, datum, face).
constexpr int MaxAnchorDepth = 4;

// Collects the Bodies an attachment chain ends on. True if any branch ends off every Body (a
// global plane, a free datum, nothing at all).
bool walkAnchorChain(const App::DocumentObject* obj, std::set<Body*>& bodies, int depth)
{
    if (!obj || depth > MaxAnchorDepth) {
        return true;
    }
    // Before the shape test: a datum is attachable and may also carry a shape.
    if (auto* attach = obj->getExtensionByType<Part::AttachExtension>(true)) {
        bool reachedGlobal = attach->AttachmentSupport.getValues().empty();
        for (auto* link : attach->AttachmentSupport.getValues()) {
            reachedGlobal = walkAnchorChain(link, bodies, depth + 1) || reachedGlobal;
        }
        return reachedGlobal;
    }
    if (obj->isDerivedFrom<App::DatumElement>() || !Part::hasShape(obj)) {
        return true;
    }
    if (auto* body = freecad_cast<const Body*>(obj)) {
        bodies.insert(const_cast<Body*>(body));
        return false;
    }
    const auto fed = bodiesFedBy(obj);
    bodies.insert(fed.begin(), fed.end());
    return fed.empty();
}

bool isChainMember(const App::DocumentObject* obj)
{
    return obj->isDerivedFrom<PartDesign::Feature>() || Body::isSolidFeature(obj);
}
}  // namespace

std::vector<Body*> Body::bodiesOf(const App::DocumentObject* feature)
{
    if (!feature) {
        return {};
    }
    if (isChainMember(feature)) {
        return bodiesFedBy(feature);
    }
    if (auto* body = static_cast<Body*>(BodyBase::findBodyOf(feature))) {
        return {body};
    }
    return {};
}

std::vector<Body*> Body::bodiesTippedAt(const App::DocumentObject* feature)
{
    std::vector<Body*> result;
    if (!feature || !feature->getDocument()) {
        return result;
    }
    for (auto* body : feature->getDocument()->getObjectsOfType<Body>()) {
        if (body->Tip.getValue() == feature) {
            result.push_back(body);
        }
    }
    return result;
}

// First of several; callers that must choose use bodyOf, which refuses to guess.
Body* Body::findBodyOf(const App::DocumentObject* feature)
{
    const auto bodies = bodiesOf(feature);
    return bodies.empty() ? nullptr : bodies.front();
}

bool Body::backsBody(const App::DocumentObject* feature, const Body* body)
{
    return body && contains(bodiesOf(feature), body);
}

bool Body::inAnyBody(const App::DocumentObject* feature)
{
    return !bodiesOf(feature).empty();
}

bool Body::sameBody(const App::DocumentObject* a, const App::DocumentObject* b)
{
    if (!a || !b) {
        return false;
    }
    const auto inB = bodiesOf(b);
    return std::ranges::any_of(bodiesOf(a), [&inB](Body* body) { return contains(inB, body); });
}

std::string Body::componentIdOfSub(const App::DocumentObject* feature, const char* subElement)
{
    if (!subElement || subElement[0] == '\0') {
        return {};
    }
    auto* geo = freecad_cast<const Part::ShapeFeature*>(feature);
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

    // A solid has no solid ancestor, so match it against the shape's own solids.
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
    return solids.empty() ? std::string() : componentKeyOfSolid(feature, shape, solids.front());
}

Body* Body::bodyOf(const App::DocumentObject* feature, const char* subElement)
{
    const auto bodies = bodiesOf(feature);
    if (bodies.size() <= 1) {
        return bodies.empty() ? nullptr : bodies.front();
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

std::vector<App::DocumentObject*> Body::ownSolids() const
{
    std::vector<App::DocumentObject*> solids;
    App::DocumentObject* tip = Tip.getValue();
    // A Tip with no chain of its own (an import) is the whole solid model.
    if (tip && !tip->isDerivedFrom<PartDesign::Feature>()) {
        if (isSolidFeature(tip)) {
            solids.push_back(tip);
        }
        return solids;
    }
    std::set<App::DocumentObject*> seen;
    for (auto* step = freecad_cast<PartDesign::Feature*>(tip);
         step && seen.insert(step).second && backsBody(step, this);
         step = freecad_cast<PartDesign::Feature*>(step->BaseFeature.getValue())) {
        solids.push_back(step);
    }
    std::ranges::reverse(solids);
    return solids;
}

std::vector<App::DocumentObject*> Body::getFullModel()
{
    std::vector<App::DocumentObject*> model = ownSolids();
    App::Document* doc = getDocument();
    if (!doc) {
        return model;
    }
    const std::set<App::DocumentObject*> solids(model.begin(), model.end());

    // A sketch or datum belongs here if one of the solids uses it, or it is attached to them.
    for (auto* obj : doc->getObjects()) {
        if (obj->isDerivedFrom<PartDesign::Feature>()
            || !obj->getExtensionByType<Part::AttachExtension>(true)) {
            continue;
        }
        bool member = std::ranges::any_of(obj->getInList(), [&solids](auto* user) {
            return solids.contains(user);
        });
        if (!member) {
            std::set<Body*> anchors;
            walkAnchorChain(obj, anchors, 0);
            member = anchors.contains(this);
        }
        if (member) {
            model.push_back(obj);
        }
    }
    return model;
}

Body* Body::resolveBaseBody(Part::Part2DObject* sketch, bool& ambiguous)
{
    std::set<Body*> bodies;
    if (auto* attach = sketch ? sketch->getExtensionByType<Part::AttachExtension>(true) : nullptr) {
        for (auto* link : attach->AttachmentSupport.getValues()) {
            walkAnchorChain(link, bodies, 1);
        }
    }
    ambiguous = bodies.size() > 1;
    // Creates nothing: the caller spawns a Body inside its own undo transaction.
    return bodies.size() == 1 ? *bodies.begin() : nullptr;
}

Body* Body::resolveMergeCandidate(App::DocumentObject* feature)
{
    auto* pdFeature = freecad_cast<PartDesign::Feature*>(feature);
    if (!pdFeature) {
        return nullptr;
    }
    Body* home = findBodyOf(pdFeature);
    if (pdFeature->BaseFeature.getValue()) {
        return home;
    }
    // Standing alone: infer from the profile's anchor chain, as feature creation does.
    auto* profileBased = freecad_cast<PartDesign::ProfileBased*>(pdFeature);
    if (!profileBased) {
        return nullptr;
    }
    bool ambiguous = false;
    Body* candidate = resolveBaseBody(profileBased->getVerifiedSketch(true), ambiguous);
    if (ambiguous) {
        return nullptr;  // the picker decides, never a silent choice
    }
    return candidate == home ? nullptr : candidate;
}

std::vector<Body*> Body::mergeCandidates(App::DocumentObject* feature)
{
    auto* pdFeature = freecad_cast<PartDesign::Feature*>(feature);
    if (!pdFeature || !pdFeature->getDocument()) {
        return {};
    }
    // A Body that already depends on the feature would close a cycle. That includes its own
    // Body, so no separate test for it.
    const std::vector<App::DocumentObject*> dependents = pdFeature->getInListRecursive();
    std::vector<Body*> candidates;
    for (auto* body : pdFeature->getDocument()->getObjectsOfType<Body>()) {
        if (!contains(dependents, body)) {
            candidates.push_back(body);
        }
    }
    return candidates;
}
