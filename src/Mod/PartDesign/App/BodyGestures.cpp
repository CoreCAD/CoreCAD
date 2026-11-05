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
// One sibling per target Body, each on that Body's Tip, all carrying the gesture's tag.
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
    return Part::sharesVolume(tool, bodyShape);
}

std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> Body::findInterferingPairs(
    App::Document* doc
)
{
    return Part::overlappingPairs(doc);
}

bool Body::isInterferenceDismissable(const App::DocumentObject* a, const App::DocumentObject* b)
{
    return freecad_cast<const Body*>(a) != nullptr && freecad_cast<const Body*>(b) != nullptr;
}

bool Body::isInterferenceDismissed(const App::DocumentObject* first, const App::DocumentObject* second)
{
    const auto* a = freecad_cast<const Body*>(first);
    const auto* b = freecad_cast<const Body*>(second);
    const auto lists = [](const Body* body, const Body* partner) {
        const auto& acks = body->AcknowledgedOverlaps.getValues();
        return std::ranges::find(acks, partner->Uid.getValueStr()) != acks.end();
    };
    // Either side's record counts, in case only one was kept.
    return a && b && (lists(a, b) || lists(b, a));
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
    return spawnScopeSiblings(tool, targets, booleanType, Base::Uuid::createUuid());
}

std::vector<App::DocumentObject*> Body::spawnScopeSiblings(
    App::DocumentObject* tool,
    const std::vector<Body*>& targets,
    const char* booleanType,
    const std::string& gestureId
)
{
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
        auto* geo = freecad_cast<App::GeoFeature*>(profile);
        if (geo) {
            geo->getPlacement().getRotation().multVec(Base::Vector3d(0, 0, 1), normal);
        }
    }
    catch (const Standard_Failure&) {
        return false;
    }
    // Long enough to cross the whole body.
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
    for (auto* obj : doc->getObjects()) {
        auto* feat = freecad_cast<PartDesign::Feature*>(obj);
        if (feat && gestureId == feat->GestureId.getValue()) {
            out.push_back(feat);
        }
    }
    return out;
}
