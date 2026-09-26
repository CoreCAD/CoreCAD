// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
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

// Gestures that act on several Bodies at once, and overlap between Bodies.

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <gp_Vec.hxx>

#include <App/Document.h>
#include <App/GeoFeature.h>
#include <Base/Uuid.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/SpatialInterference.h>
#include <Mod/Part/App/TopoShape.h>

#include "Body.h"
#include "FeatureBoolean.h"
#include "FeaturePocket.h"

using namespace PartDesign;

namespace
{
// One sibling per target Body, all sharing the gesture's tag. addFeature owns the chain wiring
// (BaseFeature + Tip advance); membership stays derived from the chain.
template<typename MakeSibling>
std::vector<App::DocumentObject*> spawnSiblings(
    App::DocumentObject* shared,
    const std::vector<Body*>& targets,
    const std::string& gestureId,
    MakeSibling makeSibling
)
{
    std::vector<App::DocumentObject*> siblings;
    App::Document* doc = shared ? shared->getDocument() : nullptr;
    if (!doc || targets.empty()) {
        return siblings;
    }
    siblings.reserve(targets.size());
    for (Body* body : targets) {
        if (!body) {
            continue;
        }
        PartDesign::Feature* sibling = makeSibling(doc);
        sibling->GestureId.setValue(gestureId);
        body->addFeature(sibling);
        siblings.push_back(sibling);
    }
    return siblings;
}
}  // namespace

bool Body::toolReaches(const Part::TopoShape& tool, const Part::TopoShape& bodyShape)
{
    // The reach test is set intersection of the two solids: they are "reached" only if they share
    // positive volume, so cutting the tool would actually change the Body. Mere surface contact
    // reads as not reached. A boolean failure is treated as "not reached" rather than propagated:
    // the reach test is a pre-flight for the gesture, not the cut itself.
    return Part::sharesVolume(tool, bodyShape);
}

std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> Body::findInterferingPairs(
    App::Document* doc
)
{
    // Cruth §8.6: solids overlapping in space without a topological merge. A pure geometry sweep --
    // no state read, no recompute touched (§8.6: detection is a UI concern, not a model one). The
    // sweep asks every independent solid in the document, not only the Bodies: an imported part
    // occupies space the same way a Body does, and nothing else was looking for it.
    return Part::overlappingPairs(doc);
}

bool Body::isInterferenceDismissable(const App::DocumentObject* a, const App::DocumentObject* b)
{
    // The acknowledgement is recorded on the two Bodies themselves, so a pair with anything else in
    // it has nowhere to be recorded. Saying so plainly is what keeps the UI from offering the user
    // a button that would do nothing.
    return freecad_cast<const Body*>(a) != nullptr && freecad_cast<const Body*>(b) != nullptr;
}

bool Body::isInterferenceDismissed(const App::DocumentObject* first, const App::DocumentObject* second)
{
    // §8.6: the dismissal is symmetric and stored on both sides, but honour either — a one-sided
    // record (e.g. after the other side was edited) still counts. Match on the durable §8.2 Uid.
    const auto* a = freecad_cast<const Body*>(first);
    const auto* b = freecad_cast<const Body*>(second);
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

std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> Body::liveInterferingPairs(
    App::Document* doc
)
{
    std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> live;
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
    // Each sibling is an ordinary single-BaseShape Boolean of the gesture's kind (Clause 5.1),
    // referencing the one shared tool.
    return spawnSiblings(tool, targets, gestureId, [&](App::Document* doc) {
        auto* cut = static_cast<PartDesign::Boolean*>(doc->addObject("PartDesign::Boolean"));
        cut->Type.setValue(booleanType);
        cut->Tools.setValue(tool);
        return cut;
    });
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
    // Each sibling is an ordinary Pocket subtracting the one shared profile, which is referenced,
    // never owned.
    return spawnSiblings(profile, targets, gestureId, [&](App::Document* doc) {
        auto* pocket = static_cast<PartDesign::Pocket*>(doc->addObject("PartDesign::Pocket"));
        pocket->Profile.setValue(profile, std::vector<std::string> {""});
        pocket->Type.setValue(pocketType);
        pocket->Length.setValue(length);
        return pocket;
    });
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
