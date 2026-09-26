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

// Which solid is which Body, and the reconciler that keeps one Body per separate solid after
// every recompute.

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <TopAbs_ShapeEnum.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/IndexedName.h>
#include <App/Link.h>
#include <App/MappedName.h>
#include <Base/Console.h>
#include <Base/Tools.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>

#include "Body.h"
#include "BodyChain.h"
#include "Feature.h"
#include "FeatureTransformed.h"

using namespace PartDesign;

namespace
{
using Provenance = std::set<std::string>;

// Length-prefixed ("<n>#<root>"), so no root can break the join.
std::string serializeProvenance(const Provenance& roots)
{
    std::string out;
    for (const auto& root : roots) {
        out += std::to_string(root.size()) + '#' + root;
    }
    return out;
}

// Empty for anything not in serializeProvenance's format, such as a pattern's component id.
Provenance parseProvenance(const std::string& encoded)
{
    Provenance roots;
    std::size_t pos = 0;
    while (pos < encoded.size()) {
        const std::size_t hash = encoded.find('#', pos);
        if (hash == std::string::npos || hash == pos) {
            return {};
        }
        std::size_t len = 0;
        for (std::size_t k = pos; k < hash; ++k) {
            if (encoded[k] < '0' || encoded[k] > '9') {
                return {};
            }
            len = len * 10 + static_cast<std::size_t>(encoded[k] - '0');
        }
        if (hash + 1 + len > encoded.size()) {
            return {};
        }
        roots.insert(encoded.substr(hash + 1, len));
        pos = hash + 1 + len;
    }
    return roots;
}

bool sharesAnyRoot(const Provenance& a, const Provenance& b)
{
    return std::ranges::any_of(a, [&b](const std::string& root) { return b.contains(root); });
}

// A Body continues onto a solid only if the solid holds all of its roots: the two halves of a
// split share roots, so a partial match would claim both.
bool holdsAllRoots(const Provenance& solid, const Provenance& body)
{
    return !body.empty() && std::ranges::includes(solid, body);
}

// Stops the reconciler re-entering itself through the Bodies it spawns and recomputes.
bool g_reconciling = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Recomputed during signalRecomputed, which does not clear the touched flag by itself.
void bringUpToDate(Body* body)
{
    body->recomputeFeature();
    body->purgeTouched();
}

// Something else already answers for this geometry: a feature built on it, or a link placing it
// (how an imported assembly shows its parts).
bool answeredForElsewhere(const App::DocumentObject* obj)
{
    return std::ranges::any_of(obj->getInList(), [](const App::DocumentObject* user) {
        return user
            && (user->isDerivedFrom<Part::ShapeFeature>()
                || user->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId()));
    });
}

// The Body for each solid that continues: the one Body whose roots only that solid holds.
// Pattern copies share roots, so a pattern matches nothing.
std::vector<Body*> matchByProvenance(
    const std::vector<Body*>& bodies,
    const std::vector<Provenance>& solids,
    bool isPattern
)
{
    std::vector<Body*> owner(solids.size(), nullptr);
    if (isPattern) {
        return owner;
    }
    std::vector<int> solidsHoldingBody(bodies.size(), 0);
    std::vector<int> heldBy(bodies.size(), -1);
    std::vector<int> bodiesHeldBySolid(solids.size(), 0);
    for (std::size_t b = 0; b < bodies.size(); ++b) {
        const Provenance roots = parseProvenance(bodies[b]->TipComponentId.getStrValue());
        for (std::size_t s = 0; s < solids.size(); ++s) {
            if (holdsAllRoots(solids[s], roots)) {
                ++solidsHoldingBody[b];
                heldBy[b] = static_cast<int>(s);
                ++bodiesHeldBySolid[s];
            }
        }
    }
    for (std::size_t b = 0; b < bodies.size(); ++b) {
        if (solidsHoldingBody[b] != 1) {
            continue;  // split, or no match
        }
        const auto s = static_cast<std::size_t>(heldBy[b]);
        if (bodiesHeldBySolid[s] == 1 && !owner[s]) {  // more than one would be a union
            owner[s] = bodies[b];
            const std::string key = serializeProvenance(solids[s]);
            if (bodies[b]->TipComponentId.getStrValue() != key) {
                bodies[b]->TipComponentId.setValue(key);
            }
        }
    }
    return owner;
}

// A retiring Body's material, passed on to the new solids that grew from it. A Body that stood
// for a whole shape passes it on to every piece.
struct MaterialDonor
{
    Materials::Material material;
    Provenance roots;

    bool gaveTo(const Provenance& solid) const
    {
        return roots.empty() || sharesAnyRoot(roots, solid);
    }
};

// Set only when every donor to this solid agrees.
void inheritMaterial(Body* body, const std::vector<MaterialDonor>& donors, const Provenance& solid)
{
    const Materials::Material* chosen = nullptr;
    for (const MaterialDonor& donor : donors) {
        if (!donor.gaveTo(solid)) {
            continue;
        }
        if (chosen && donor.material.getUUID() != chosen->getUUID()) {
            return;
        }
        chosen = &donor.material;
    }
    if (chosen) {
        body->Material.setValue(*chosen);
    }
}

// Pattern copies that a later step builds on belong to that step's Body.
std::vector<bool> copiesCarriedDownstream(Part::ShapeFeature* feature, int solidCount)
{
    std::vector<bool> carried(static_cast<std::size_t>(solidCount), false);
    auto* pattern = freecad_cast<PartDesign::Transformed*>(feature);
    if (!pattern) {
        return carried;
    }
    for (auto* step : chain::nextSteps(pattern)) {
        const long copy = step->BaseInstance.getValue();
        const int index = copy >= 0 ? pattern->solidIndexOfInstance(copy) : 0;
        if (index > 0 && index <= solidCount) {
            carried[static_cast<std::size_t>(index - 1)] = true;
        }
    }
    return carried;
}

void reconcileTip(App::Document* doc, Part::ShapeFeature* feature, std::vector<Body*> bodies)
{
    const Part::TopoShape shape = feature->Shape.getShape();
    const auto solidCount = static_cast<int>(shape.isNull() ? 0 : shape.countSubShapes(TopAbs_SOLID));
    if (solidCount == 0) {
        return;  // a failed or empty compute is not a change of topology
    }

    // One Body, one solid: nothing to match, and it keeps its identity however far it moved.
    if (bodies.size() == 1 && solidCount == 1) {
        if (!bodies.front()->TipComponentId.getStrValue().empty()) {
            bodies.front()->TipComponentId.setValue("");
            bringUpToDate(bodies.front());
        }
        return;
    }

    std::vector<Provenance> solids;
    for (int i = 1; i <= solidCount; ++i) {
        solids.push_back(
            Body::provenanceOfSolid(shape.getSubTopoShape(TopAbs_SOLID, i, /*silent*/ true))
        );
    }
    const bool isPattern = freecad_cast<PartDesign::Transformed*>(feature) != nullptr;
    const std::vector<Body*> owner = matchByProvenance(bodies, solids, isPattern);

    // Every other Body retires; new ones are spawned below for the solids left unclaimed.
    std::erase_if(bodies, [&owner](Body* body) {
        return std::ranges::find(owner, body) != owner.end();
    });
    std::vector<MaterialDonor> donors;
    for (Body* body : bodies) {
        donors.push_back(
            {body->Material.getValue(), parseProvenance(body->TipComponentId.getStrValue())}
        );
    }
    if (std::ranges::any_of(bodies, [](Body* body) { return !body->getInList().empty(); })) {
        Base::Console().warning(
            "Feature '%s' changed body topology; identities that could not be matched by "
            "ancestry were reset. Re-pick any references to the retired bodies.\n",
            feature->getNameInDocument()
        );
    }
    for (Body* body : bodies) {
        doc->removeObject(body->getNameInDocument());
    }

    const std::vector<bool> carried = copiesCarriedDownstream(feature, solidCount);
    for (std::size_t s = 0; s < solids.size(); ++s) {
        if (owner[s] || carried[s]) {
            continue;
        }
        Body* body = Body::spawnAutoBody(doc);
        if (!body) {
            continue;
        }
        body->Tip.setValue(feature);
        body->TipComponentId.setValue(
            solidCount > 1 ? Body::componentKeyOfSolid(feature, shape, static_cast<int>(s) + 1) : ""
        );
        inheritMaterial(body, donors, solids[s]);
    }

    for (auto* body : Body::bodiesTippedAt(feature)) {
        bringUpToDate(body);
    }
}
}  // namespace

// Element maps name faces but not solids, so a solid is named by its smallest face name.
std::string Body::componentIdOfSolid(const Part::TopoShape& shape, int index)
{
    const Part::TopoShape solid = shape.getSubTopoShape(TopAbs_SOLID, index, /*silent*/ true);
    std::string best;
    const auto faceCount = static_cast<int>(solid.isNull() ? 0 : solid.countSubShapes(TopAbs_FACE));
    for (int f = 1; f <= faceCount; ++f) {
        const Data::MappedName mapped = solid.getMappedName(Data::IndexedName("Face", f));
        if (!mapped.empty() && (best.empty() || mapped.toString() < best)) {
            best = mapped.toString();
        }
    }
    return best.empty() ? "Solid" + std::to_string(index) : best;
}

std::set<std::string> Body::provenanceOfSolid(const Part::TopoShape& solid)
{
    Provenance roots;
    if (solid.isNull()) {
        return roots;
    }
    // One history hop per face: the source object's tag and the element it started as. The tag
    // is needed because two sketches both start at "g1".
    const auto faceCount = static_cast<int>(solid.countSubShapes(TopAbs_FACE));
    for (int f = 1; f <= faceCount; ++f) {
        const Data::MappedName mapped = solid.getMappedName(Data::IndexedName("Face", f));
        if (mapped.empty()) {
            continue;
        }
        Data::MappedName original;
        const long tag = solid.getElementHistory(mapped, &original);
        if (tag != 0 || !original.empty()) {
            roots.insert(std::to_string(tag) + ":" + original.toString());
        }
    }
    return roots;
}

std::string Body::componentKeyOfSolid(
    const App::DocumentObject* tipFeature,
    const Part::TopoShape& shape,
    int index
)
{
    // Pattern copies share provenance, and an import has none; both fall back to the face name.
    if (!freecad_cast<const PartDesign::Transformed*>(tipFeature)) {
        const Provenance roots = provenanceOfSolid(
            shape.getSubTopoShape(TopAbs_SOLID, index, /*silent*/ true)
        );
        if (!roots.empty()) {
            return serializeProvenance(roots);
        }
    }
    return componentIdOfSolid(shape, index);
}

void Body::spawnBodiesForUnclaimedOutput(
    App::Document* doc,
    const std::vector<App::DocumentObject*>& recomputed
)
{
    if (!doc || g_reconciling) {
        return;
    }
    Base::StateLocker guard(g_reconciling);

    for (auto* obj : recomputed) {
        const auto* feature = freecad_cast<const Part::ShapeFeature*>(obj);
        if (!feature || !feature->spawnsBodyForOutput() || answeredForElsewhere(obj)
            || feature->Shape.getShape().countSubShapes(TopAbs_SOLID) == 0 || !bodiesOf(obj).empty()) {
            continue;
        }
        // A document without a world frame (a drawing) keeps no Bodies; that is not an error.
        Body* body = nullptr;
        try {
            body = spawnAutoBody(doc);
        }
        catch (const Base::Exception&) {
            continue;
        }
        if (body) {
            body->addFeature(obj);
            bringUpToDate(body);  // born after the recompute that prompted it
        }
    }
}

void Body::reconcileMultiOutput(App::Document* doc, const std::vector<App::DocumentObject*>& recomputed)
{
    if (!doc || g_reconciling || doc->getObjectsOfType<Body>().empty()) {
        return;
    }
    Base::StateLocker guard(g_reconciling);

    for (auto* obj : recomputed) {
        auto* feature = freecad_cast<Part::ShapeFeature*>(obj);
        if (!feature || !isSolidFeature(obj)) {
            continue;
        }
        if (auto bodies = bodiesTippedAt(feature); !bodies.empty()) {
            reconcileTip(doc, feature, std::move(bodies));
        }
    }
}

void Body::retreatTippedBodies(App::DocumentObject* feature, App::DocumentObject* retreatTo)
{
    const std::vector<Body*> tipped = bodiesTippedAt(feature);
    for (auto* body : tipped) {
        body->Tip.setValue(retreatTo);
    }
    // A feature that split a solid tips every piece. The reconciler only sees what was
    // recomputed, and nothing else touches the base, so touch it to have the pieces merged.
    if (tipped.size() > 1 && retreatTo) {
        retreatTo->touch();
    }
}

void Body::retireOrRetreatTippedBodies(App::Document* doc, App::DocumentObject* feature)
{
    auto* pd = freecad_cast<PartDesign::Feature*>(feature);
    if (!doc || g_reconciling || !pd) {
        return;
    }
    if (App::DocumentObject* base = pd->BaseFeature.getValue()) {
        retreatTippedBodies(feature, base);
        return;
    }
    for (auto* body : bodiesTippedAt(feature)) {
        doc->removeObject(body->getNameInDocument());
    }
}

namespace
{
// Watches every document: after each recompute, and on each deletion that skipped
// removeFeature (a script).
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
                Body::spawnBodiesForUnclaimedOutput(docPtr, objs);
                Body::reconcileMultiOutput(docPtr, objs);
            }
        );
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
