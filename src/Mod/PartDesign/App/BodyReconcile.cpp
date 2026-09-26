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

// Which solid is which Body: component identity, and the reconciler that keeps one Body per
// separate solid after every recompute.

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
#include "Feature.h"
#include "FeatureTransformed.h"

using namespace PartDesign;

namespace
{
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

// Guards reconcileMultiOutput against re-entry while it spawns/recomputes Bodies.
bool g_reconciling = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

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
        if (!feature || !feature->spawnsBodyForOutput()) {
            continue;
        }
        // Only geometry that stands on its own gets a Body. Something else in the document
        // already answers for it in two cases: a feature built on it (the operands of a
        // boolean, or a body's own features), and an instance placed by a link, which is
        // how an imported assembly presents its leaf parts. Giving those a Body as well
        // would put the same geometry in the document twice over.
        //
        // Leaving an assembly's leaves loose is the honest state of things: §7.8 says an
        // assembly becomes one document per leaf body rather than one document holding a
        // link tree, and until that is built there is no Body for a leaf to be the tip of.
        bool answeredForElsewhere = false;
        for (const App::DocumentObject* user : obj->getInList()) {
            if (!user) {
                continue;
            }
            if (user->isDerivedFrom<Part::ShapeFeature>()
                || user->hasExtension(App::LinkBaseExtension::getExtensionClassTypeId())) {
                answeredForElsewhere = true;
                break;
            }
        }
        if (answeredForElsewhere) {
            continue;
        }
        if (feature->Shape.getShape().countSubShapes(TopAbs_SOLID) == 0) {
            continue;  // nothing solid to account for (yet)
        }
        if (!bodiesOf(obj).empty()) {
            continue;  // already accounted for
        }

        // A Body needs the document's shared world frame and must never mint one, so a
        // document that has no frame -- a drawing, a spreadsheet -- simply keeps none. That
        // is a fact about the document, not a failure of this recompute. Only the spawn is
        // guarded: a failure to then adopt the feature would be a real defect, and swallowing
        // it would leave a Body standing that marks nothing.
        Body* body = nullptr;
        try {
            body = spawnAutoBody(doc);
        }
        catch (const Base::Exception&) {
            continue;
        }
        if (body) {
            body->addFeature(obj);
            // The Body is born after the recompute that prompted it has finished, so nothing
            // will compute it until the next one: bring it up to date here, or the document
            // is left touched and the Body shows nothing until something else is edited.
            body->recomputeFeature();
            body->purgeTouched();
        }
    }
}

void Body::reconcileMultiOutput(App::Document* doc, const std::vector<App::DocumentObject*>& recomputed)
{
    if (!doc || g_reconciling) {
        return;
    }
    Base::StateLocker guard(g_reconciling);

    if (doc->getObjectsOfType(Body::getClassTypeId()).empty()) {
        return;
    }

    for (auto* obj : recomputed) {
        // Any feature a Body is tipped by, not only a PartDesign one: an import produces
        // several solids as readily as a pattern does, and §7.8 gives each of them a Body.
        auto* feature = freecad_cast<Part::ShapeFeature*>(obj);
        if (!feature || !isSolidFeature(obj)) {
            continue;
        }

        // Only a Tip feature's components spawn Bodies; a mid-chain feature is skipped.
        std::vector<Body*> bodies = bodiesTippedAt(feature);
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
                donors.push_back({bodies[b]->Material.getValue(), prov, prov.empty()});
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

        // #3: a copy that a later step builds on (its BaseInstance) is carried by that step's
        // Body, so the pattern spawns none of its own for it.
        std::vector<bool> carriedDownstream(static_cast<std::size_t>(solidCount), false);
        if (auto* pattern = freecad_cast<PartDesign::Transformed*>(feature)) {
            for (auto* user : pattern->getInList()) {
                auto* step = freecad_cast<PartDesign::Feature*>(user);
                if (!step || step->BaseFeature.getValue() != pattern
                    || step->BaseInstance.getValue() < 0) {
                    continue;
                }
                const int index = pattern->solidIndexOfInstance(step->BaseInstance.getValue());
                if (index > 0 && index <= solidCount) {
                    carriedDownstream[static_cast<std::size_t>(index - 1)] = true;
                }
            }
        }

        // Spawn a fresh Body per unclaimed solid — split children, union results, new components.
        // Identity resets (fresh name/UUID/colour via spawnAutoBody+setupObject, fresh component-id
        // here); material inherits only when the donors for this solid agree on one.
        const bool multiSolid = solidCount > 1;
        for (std::size_t s = 0; s < solidProv.size(); ++s) {
            if (solidOwner[s] || carriedDownstream[s]) {
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
                body->Material.setValue(mat);
            }
        }

        // Re-extract each Body's component shape under its (possibly new) id. Re-query
        // so freshly spawned Bodies — not in the original `bodies` list — recompute too.
        // We run inside signalRecomputed (the document is still marked Recomputing), so
        // recomputeFeature takes the direct _recomputeFeature path, which computes the
        // shape but does not purge the touched flag — purge explicitly so the document
        // settles instead of looping on perpetually-touched Bodies.
        for (auto* body : bodiesTippedAt(feature)) {
            body->recomputeFeature();
            body->purgeTouched();
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

    const std::vector<Body*> tipped = bodiesTippedAt(feature);
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
                Body::spawnBodiesForUnclaimedOutput(docPtr, objs);
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
