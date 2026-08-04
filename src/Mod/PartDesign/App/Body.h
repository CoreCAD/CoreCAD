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


#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <App/PropertyStandard.h>
#include <Mod/Part/App/BodyBase.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

namespace App
{
class Origin;
}

namespace Part
{
class Part2DObject;
class TopoShape;
}  // namespace Part

namespace PartDesign
{

class Feature;

// Cruth §3.3 / Amendment 17 (#79): a Body carries its shape as a *composed capability*, not as an
// inherited role. The shape-source capability (Part::ShapeExtension) is composed by the
// Part::ShapeFeature base — the same peel Amendment 4 performed for placement — so a Body carries
// it through BodyBase and routes the own-shape half of getSubObject through it (overriding
// getSubObject to delegate to the App base), sourcing its element-mapped sub-shapes via the
// capability exactly as a stored-backed feature (Part::Box) does. The backing geometry stays
// derived from the Tip: the extension reads it through the inherited getPropertyOfGeometry() hook,
// which returns the Transient/ReadOnly Shape mirror execute() refreshes from derivedTipShape() each
// pass. Retiring that mirror property outright (sourcing the capability and the element-map paths
// without a stored Shape) is the separate, TNP-critical step 3b.
class PartDesignExport Body: public Part::BodyBase
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Body);

public:
    /// Cruth §4.6 visual identity — auto-assigned from a deterministic palette at spawn.
    App::PropertyColor Color;

    /// Cruth §3.3 component identity. A Body's Tip is a (feature, component-id) pair: this
    /// names which connected component of the Tip feature's output shape the Body represents.
    /// Empty means the implicit, single-component case (the overwhelming majority of Bodies).
    /// Features that produce multiple disjoint components spawn one Body per component, each
    /// sharing the Tip feature but carrying a distinct, element-map-stable id here.
    App::PropertyString TipComponentId;

    /// Cruth §8.2 durable body identity — a UUID minted once at birth, never recomputed,
    /// persisted. This is the stable id assemblies and BOMs resolve a body through (§13.1);
    /// unlike the runtime which-solid predicate, it costs no robustness to topology changes.
    App::PropertyUUID Uid;

    /// Cruth §8.6 dismissed spatial interferences — the durable Uids of the other Bodies whose
    /// geometric overlap with this one the user has acknowledged as intentional. Stored on both
    /// Bodies of a dismissed pair; the interference notice is suppressed while either side lists
    /// the other. Keyed on the durable §8.2 Uid (not the object name), so a Body that is retired
    /// and reborn gets a fresh identity and correctly re-warns. Hidden document state, not a
    /// user-facing property.
    App::PropertyStringList AcknowledgedOverlaps;

    /// True if this body feature is active or was active when the document was last closed
    // App::PropertyBool IsActive;

    Body();

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;

    /// returns the type name of the view provider
    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderBody";
    }
    /// A Body is Part-scoped content (ARCHITECTURE §7.1, Amendment 8): only a Part document
    /// admits it. Narrows the Feature scope it would otherwise inherit as a Part::Feature.
    App::DocumentObject::ContentScope getContentScope() const override
    {
        return App::DocumentObject::ContentScope::Body;
    }
    //@}

    /**
     * Splice an already-created feature into this Body's pipeline at the current insert
     * point (the position before the next solid after the Tip), advancing the Tip.
     *
     * Cruth §11 step 5e: this is a pipeline edit, NOT a container add. The Body does not
     * create or own the feature — the Document creates it (see PartDesignGui::createFeature);
     * this method only rewires the BaseFeature chain + Tip that the Body marks and stamps the
     * derived _Body back-pointer. It replaced the retired GroupExtension addObject(); the
     * feature-flavoured name makes the pipeline (not container) semantics explicit. Handles
     * both the tip-append gesture and mid-chain insert. See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<App::DocumentObject*> addFeature(App::DocumentObject* feature);
    std::vector<DocumentObject*> addFeatures(std::vector<DocumentObject*> features);

    /**
     * Insert the feature into the body after the given feature.
     *
     * @param feature  The feature to insert into the body
     * @param target   The feature relative which one should be inserted the given.
     *                 If target is NULL than insert into the end if where is InsertBefore
     *                 and into the begin if where is InsertAfter.
     * @param after    if true insert the feature after the target. Default is false.
     *
     * @note the method doesn't modify the Tip unlike addFeature()
     */
    void insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after = false);

    void setBaseProperty(App::DocumentObject* feature);

    /**
     * Remove a feature from this Body's pipeline (Cruth intra-body de-ownership): rewire the
     * BaseFeature chain and retreat the Tip; retire the Body if its chain empties. A pipeline
     * edit, not a container remove — the feature is not destroyed. Must be called BEFORE the
     * feature is removed from the Document. See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<DocumentObject*> removeFeature(DocumentObject* feature);
    /// Convenience: removeFeature over a list (used when re-homing features between bodies).
    void removeFeatures(const std::vector<App::DocumentObject*>& features);

    /// Cruth: chain successor of a feature (the solid whose BaseFeature links to it).
    App::DocumentObject* getNextSolidFeatureByChain(App::DocumentObject* feature) const;

    /**
     * Cruth §4.8 multi-output spawn. Run after a document recompute: for each
     * recomputed PartDesign feature that a Body points at as its Tip, count the
     * connected solid components of the feature's output. When the count exceeds
     * the number of Bodies referencing that feature, spawn one Body per extra
     * component and stamp each Body's TipComponentId (§3.3) with an element-map-
     * stable id. The single-component case is a no-op. Idempotent: re-running on a
     * reconciled document changes nothing. Spawn direction only — shrink/retire
     * (§4.7) is handled separately. See ARCHITECTURE §3.3/§4.7/§4.8.
     */
    static void reconcileMultiOutput(
        App::Document* doc,
        const std::vector<App::DocumentObject*>& recomputed
    );

    /**
     * Cruth §4.6/§4.7 retire-on-Tip-delete. Run when a PartDesign feature is deleted
     * directly (raw `Document::removeObject`, e.g. from a script or the MCP bridge),
     * which bypasses Body::removeFeature. A Tip feature's deletion is the ultimate
     * "output ceases to produce a component" event: every Body tipped by it would
     * otherwise survive as a zombie (Tip nulled to None, no resolvable component).
     * Mirror removeFeature's tip-retreat — retreat each such Body's Tip onto the
     * deleted feature's base and, when more than one Body was tipped, touch that base
     * so reconcileMultiOutput unifies them on the next recompute; when there is no
     * base, the Body has nothing left and is retired (§4.6 auto-retirement). No-op
     * for a non-Tip feature (nothing is tipped by it) and for the GUI delete path
     * (removeFeature already retreated the Tips before removeObject fires).
     */
    static void retireOrRetreatTippedBodies(App::Document* doc, App::DocumentObject* feature);

    /// Wire reconcileMultiOutput onto every document's recompute signal. Call once
    /// at module init; idempotent. P8: fires for both UI and API recompute paths.
    static void initMultiOutputObserver();

    /// Cruth §3.3 component-id for the i-th (1-based) solid of a shape: the solid's
    /// lexicographically-smallest mapped face name, stable across recomputes that
    /// preserve topology and independent of OCCT's solid ordering (positional
    /// "Solid{i}" fallback when no face is mapped). This is THE component identity —
    /// the Tip's TipComponentId and the pattern break-out skip-list (§5.6) both match
    /// against it, so the computation must have a single source of truth.
    static std::string componentIdOfSolid(const Part::TopoShape& shape, int index);

    /// Cruth Amendment 3 §4.3 native-ancestry provenance of a solid: the set of stable
    /// `(source-object-tag, element-token)` roots its faces trace back to through the element-map
    /// history (P5). This is the *match key* by which a recomputed solid is re-linked to its prior
    /// Body across a recompute — never the identity itself (that is the per-body UUID §8.2), never
    /// a geometric resemblance. One history hop per face (proven for sketch-consuming features and
    /// the #33 sever); a recursive walk up the feature graph for solid-consuming steps (boolean,
    /// deep chains) is a bounded robustness follow-on. Empty for a null solid or one with no mapped
    /// faces.
    static std::set<std::string> provenanceOfSolid(const Part::TopoShape& solid);

    /// The stored component key for the @p index-th (1-based) solid of @p tipFeature's output —
    /// the value written to a Body's TipComponentId and matched on for solid extraction and
    /// picked-sub resolution. Built-geometry Tips use the native-ancestry provenance root-set
    /// (serialized, stable across recompute, Amendment 3 §4.3); pattern/mirror Tips (Transformed)
    /// keep the element-map-name instance-selector (§4.5 — copies share provenance). Falls back to
    /// componentIdOfSolid when provenance is unavailable (import/baked: no element history).
    static std::string componentKeyOfSolid(
        const App::DocumentObject* tipFeature,
        const Part::TopoShape& shape,
        int index
    );

    /// Cruth Amendment 5 §5.1 reach test: does @p tool's solid share positive volume with
    /// @p bodyShape? A Body is *reached* by a multi-body subtractive/intersective tool when the two
    /// solids intersect (`tool ∩ Body ≠ ∅`) — i.e. cutting the tool would actually change the Body.
    /// Bare surface contact (a shared face, zero shared volume) does not count as a reach: it changes
    /// nothing. Robust to disjoint inputs (returns false, never throws). This is a pure geometry
    /// predicate on two shapes; the caller gathers each Body's Tip shape and asks per Body.
    static bool toolReaches(const Part::TopoShape& tool, const Part::TopoShape& bodyShape);

    /// Cruth §8.6 Spatial Interference: every unordered pair of distinct Bodies in @p doc whose
    /// shapes share positive volume. Two Bodies may occupy overlapping space without being
    /// topologically merged (§4.8) — geometrically valid but usually unintended (e.g. keep-distinct
    /// pattern instances that coincide). This is the pure-geometry detector; per §8.6 it is a UI
    /// concern surfaced as a NON-blocking notice, never a recompute failure, and is meant to be run
    /// on demand rather than every recompute (the pairwise boolean is costly). Bounding boxes
    /// pre-reject far-apart Bodies; the volume test (via toolReaches) ignores bare surface contact.
    /// Bodies with a null or solid-less Shape are skipped. Dismissal of intentional overlaps is a
    /// caller/document-state concern layered above this predicate, not filtered here.
    static std::vector<std::pair<Body*, Body*>> findInterferingPairs(App::Document* doc);

    /// Cruth §8.6: the interfering pairs in @p doc the user has NOT dismissed —
    /// findInterferingPairs minus every pair whose overlap was acknowledged as intentional (either
    /// Body listing the other in AcknowledgedOverlaps). This is what the interference notice
    /// surfaces; an empty result means nothing needs the user's attention.
    static std::vector<std::pair<Body*, Body*>> liveInterferingPairs(App::Document* doc);

    /// Cruth §8.6: has the overlap between @p a and @p b been dismissed as intentional? True when
    /// either Body records the other's durable §8.2 Uid in AcknowledgedOverlaps.
    static bool isInterferenceDismissed(const Body* a, const Body* b);

    /// Cruth §8.6: acknowledge the overlap between @p a and @p b as intentional, silencing its
    /// notice. Records each Body's durable Uid on the other (idempotent). A no-op on null Bodies or
    /// a Body with no Uid.
    static void dismissInterference(Body* a, Body* b);

    /// Cruth Amendment 5 §5.1 multi-body scope fan-out: given one shared @p tool and the set of
    /// @p targets the user chose to affect, spawn one ordinary single-`BaseShape` Boolean feature
    /// of kind @p booleanType (`"Cut"` or `"Common"`) per target Body, each advancing that Body's
    /// own chain (spliced at its Tip via addFeature) and referencing the one @p tool by reference —
    /// the tool is shared, never owned or consumed. No feature ever extends two chains; the only
    /// edge written is `Body → Tip`, never `feature → Body` (ownership stays derived). Returns the
    /// spawned siblings in @p targets order. The reach decision (which Bodies are in @p targets) is
    /// the caller's — this fan-out does not re-run toolReaches. The inert shared gesture-id that
    /// ties the siblings together for a later Scope edit is applied separately (Clause 5.3).
    static std::vector<App::DocumentObject*> spawnScopeSiblings(
        App::DocumentObject* tool,
        const std::vector<Body*>& targets,
        const char* booleanType
    );

    /// Tagged overload (Clause 5.3 scope edit): fan out siblings carrying the given @p gestureId
    /// instead of minting a fresh one, so a Scope edit can *extend an existing gesture* onto newly
    /// included Bodies. The 3-arg form is exactly this with a freshly minted id.
    static std::vector<App::DocumentObject*> spawnScopeSiblings(
        App::DocumentObject* tool,
        const std::vector<Body*>& targets,
        const char* booleanType,
        const std::string& gestureId
    );

    /// Cruth Amendment 5 §5.1 sketch-tooled reach test: does @p profile — a sketch swept
    /// perpendicular to its own plane — pass through @p bodyShape? The profile-tool counterpart of
    /// toolReaches. The reach question for a sketch tool is "does the profile's column hit the
    /// body," independent of how deep the cut runs (Length vs ThroughAll changes only the spawned
    /// Pocket's extent, never whether it reaches). Direction-agnostic — the column is tested both
    /// ways along the plane normal, sized to the body's bounding box. Robust to a null/empty
    /// profile or a non-planar wire (returns false, never throws).
    static bool profileReaches(App::DocumentObject* profile, const Part::TopoShape& bodyShape);

    /// Cruth Amendment 5 §5.1 sketch-tooled fan-out: the profile-tool sibling of spawnScopeSiblings.
    /// Spawn one ordinary PartDesign::Pocket per target Body, each subtracting the one shared
    /// @p profile (referenced, never owned) from that Body's Tip via addFeature, of extent
    /// @p pocketType ("Length" | "ThroughAll"); @p length applies to the Length flavour. Same
    /// invariants as the boolean fan-out: the only edge written is Body -> Tip, membership stays
    /// derived, and all siblings of the gesture carry the shared inert GestureId (Clause 5.3).
    static std::vector<App::DocumentObject*> spawnScopeSiblingsFromProfile(
        App::DocumentObject* profile,
        const std::vector<Body*>& targets,
        const char* pocketType,
        double length
    );

    /// Tagged overload (Clause 5.3 scope edit): as above but carrying the given @p gestureId rather
    /// than a freshly minted one, so a Scope edit can extend an existing sketch-tooled gesture.
    static std::vector<App::DocumentObject*> spawnScopeSiblingsFromProfile(
        App::DocumentObject* profile,
        const std::vector<Body*>& targets,
        const char* pocketType,
        double length,
        const std::string& gestureId
    );

    /// Cruth Amendment 5 §5.3 scope re-collection: every PartDesign feature in @p doc carrying the
    /// given non-empty @p gestureId, in document order — the siblings born of one multi-body gesture.
    /// This is how a Scope edit rediscovers a gesture's members without any stored membership list:
    /// the shared inert tag is the only link, and ownership of each sibling still derives from its
    /// own Body chain. An empty @p gestureId returns nothing (a plain feature is not a gesture).
    static std::vector<App::DocumentObject*> gestureSiblings(
        App::Document* doc,
        const std::string& gestureId
    );

    /**
     * Checks if the given document object lays after the current insert point
     * (place before next solid after the Tip)
     */
    bool isAfterInsertPoint(App::DocumentObject* feature);

    /**
     * Return true if the given feature is a solid feature allowed in a Body. Currently this is only
     * valid for features derived from PartDesign::Feature Return false if the given feature is a
     * Sketch or a Part::Datum feature
     */
    static bool isSolidFeature(const App::DocumentObject* obj);

    /**
     * Return true if the given feature is allowed in a Body. Currently allowed are
     * all features derived from PartDesign::Feature and Part::Datum and sketches
     */
    static bool isAllowed(const App::DocumentObject* obj);
    bool allowObject(DocumentObject* obj)
    {
        return isAllowed(obj);
    }

    /**
     * Return the nearest downstream Body marker for @p feature, or NULL.
     *
     * CPART_DESIGN §9.1: this is a reverse lookup, not an ownership read. A Body points
     * only one way — at the Tip it marks — so "which Body is this feature under" is
     * answered by walking the BaseFeature chain forward to the first feature that is some
     * Body's Tip and returning that marker. The result is a derived view of the current
     * graph, never a stored attribute of the feature. Group membership is no longer
     * consulted for PartDesign features (it is empty under de-ownership).
     */
    static Body* findBodyOf(const App::DocumentObject* feature);

    /**
     * Return EVERY Body that @p feature backs — the honest, N-valued reverse lookup.
     *
     * Cruth ownership-query contract. findBodyOf is scalar (one feature → one Body), which
     * is correct only while chains are linear. Under de-ownership a single Tip feature can
     * back several Bodies at once — one per output component of a pattern or a severed solid
     * (§4.7), told apart by TipComponentId. This walks the BaseFeature chain forward to the
     * first feature that is some Body's Tip (the nearest downstream marker) and returns all
     * Bodies naming that Tip. Ownership stays derived: it reads the graph, never a stored
     * feature→Body link or a Group. Empty when the feature reaches no Body.
     */
    static std::vector<Body*> bodiesOf(const App::DocumentObject* feature);

    /**
     * Resolve @p feature plus the caller's picked @p subElement to the single Body meant.
     *
     * Cruth ownership-query contract, P7 fail-loud. One candidate → the sub-element is
     * irrelevant, return it. Several candidates (a multi-output Tip) → the picked
     * sub-element names the component: map it to its solid, take that solid's component-id
     * and return the Body carrying it. Asking for "the" Body of a multi-output feature with
     * NO sub-element is ambiguous and THROWS rather than silently guessing a Body. Returns
     * NULL only when the feature backs no Body at all.
     */
    static Body* bodyOf(const App::DocumentObject* feature, const char* subElement);

    /**
     * Component-id of the solid that owns @p subElement on @p feature's shape, or empty.
     *
     * The discriminator half of bodyOf: resolves a picked sub-element (e.g. "Face5") to its
     * owning solid and returns that solid's componentIdOfSolid. Empty when the sub-element
     * is missing, unresolvable, or owned by no solid. Shared with the §7 import
     * face-identity work (same fingerprint need).
     */
    static std::string componentIdOfSub(const App::DocumentObject* feature, const char* subElement);

    /**
     * True when @p feature is one of the makers of @p body — honest membership.
     *
     * Cruth ownership-query contract. The pre-sweep idiom `findBodyOf(x) == body` asks "does x
     * belong to body?" but findBodyOf returns only the FIRST marker, so it answers false for a
     * feature that legitimately backs @p body alongside others (a multi-output Tip, §4.7). This
     * tests whether @p body is among ALL the Bodies @p feature backs. Derived over bodiesOf —
     * reads the graph, stores nothing.
     */
    static bool backsBody(const App::DocumentObject* feature, const Body* body);

    /**
     * True when @p feature backs at least one Body — an honest membership predicate.
     *
     * Cruth ownership-query contract. Several call sites use `findBodyOf(x)` purely as a
     * yes/no ("is x already in a body?"), never touching the returned Body. Phrased that way
     * the scalar lookup answers a question it was not asked and hides that the real intent is
     * membership, not identity. This says only what those sites mean: does @p feature reach
     * any Body? Derived over bodiesOf — reads the graph, stores nothing.
     */
    static bool inAnyBody(const App::DocumentObject* feature);

    /**
     * True when @p a and @p b share at least one Body — an honest same-body test.
     *
     * Cruth ownership-query contract. Code that asks "are these two features in the same
     * body?" tended to materialize one feature's Body via a scalar findBodyOf and then test
     * the other against it. That middleman coin-flips when a feature straddles several Bodies
     * (§4.7): the arbitrary first marker may miss the Body they genuinely share. This compares
     * the two feature→Body sets directly and is true iff they overlap. Derived over bodiesOf —
     * reads the graph, stores nothing.
     */
    static bool sameBody(const App::DocumentObject* a, const App::DocumentObject* b);

    /**
     * Return the features that make up this Body, derived from the feature graph.
     *
     * CPART_DESIGN §9.1-inverse: a de-owned Body keeps no Group, so its member list is
     * computed, not stored — the mirror image of findBodyOf. Solid features are those
     * whose findBodyOf resolves to this Body (collected along the BaseFeature chain from
     * the Tip back, which stops naturally at a cross-body seam). Loose features (sketches,
     * datums, shapebinders) belong here when their §8.5 attachment anchor-walk terminates
     * on this Body. Returned solids-first in build order, then the loose features.
     */
    std::vector<App::DocumentObject*> getFullModel() override;

    /// Cruth de-ownership (§3.3): find a pipeline feature that resolves to this Body (via
    /// the derived Feature::_Body marker) whose name (or $-prefixed label) matches, for
    /// sub-object path resolution. Features are no longer Group members, so the Group-based
    /// resolver cannot see them. Returns nullptr if no matching feature resolves to us.
    PartDesign::Feature* findOwnedFeature(const std::string& name) const;

    /**
     * Cruth §8.5/§4.6: PURE reverse query — resolve the base Body for a new
     * sketch-based feature by walking the sketch's anchor chain (AttachmentSupport
     * through datums/reference geometry). No side effects; it never creates anything.
     *  - chain terminates on exactly one Body → return that Body (extend);
     *  - chain reaches more than one Body → return nullptr, set @p ambiguous
     *    (the caller surfaces the §8.3 ambiguity prompt);
     *  - chain ends at a global plane / independent geometry (no Body) → return
     *    nullptr with @p ambiguous false. This is the auto-spawn case: the caller
     *    decides to create a Body (§4.6) via the explicit spawnAutoBody() step, and
     *    must do so INSIDE its undo transaction so a cancelled feature does not leak
     *    a stray Body (#17).
     *
     * Lives in the App layer so the Gui command and the Python API share one code
     * path — the P8 (Programmatic Equivalence) guarantee. Both resolve purely here,
     * then spawn explicitly.
     */
    static Body* resolveBaseBody(Part::Part2DObject* sketch, bool& ambiguous);

    /**
     * Cruth §4.6: auto-spawn a new Body at document level (no active-Part
     * containment). Color is assigned in setupObject() from the per-document
     * palette index. Returns nullptr if @p doc is null.
     */
    static Body* spawnAutoBody(App::Document* doc);

    /**
     * Cruth §8.5 (Merge Result, #27): re-home @p feature onto @p target's pipeline —
     * the model half of the feature-creation "Merge result" control. Composes the
     * existing pipeline primitives so the spawn-vs-extend choice is reversible:
     *   - detach @p feature from its current Body (removeFeature: heals the
     *     BaseFeature chain, retreats that Body's Tip, and auto-retires the Body if
     *     its chain empties, §4.7);
     *   - if @p target is null, spawn a fresh Body (§4.6) to receive @p feature;
     *   - splice @p feature onto @p target's Tip (addFeature).
     *
     * A no-op returning @p target when @p feature already resolves to it. Returns
     * the Body the feature now belongs to (the freshly spawned one when @p target was
     * null), or nullptr on failure. Does not recompute — the caller does, inside its
     * undo transaction. Shared by the GUI control and the Python API (P8 equivalence).
     */
    static Body* moveFeatureToBody(App::DocumentObject* feature, Body* target);

    /**
     * Cruth §5.6: break a single pattern instance out into its own independent,
     * frozen Body. Captures the instance's solid (element map preserved, §7.8)
     * from the pattern's current output, re-homes it into a fresh Body whose
     * chain begins with a BakedShape feature, then records a skip on the pattern
     * so it emits one fewer instance and recomputes. The new Body is fully
     * severed — no link back to the pattern or its base — so subsequent pattern
     * edits never re-merge it.
     *
     * Order matters: the capture happens before the skip recompute, while the
     * instance solid still exists; the now-orphaned originating Body is retired
     * by the reconciler (§4.7).
     *
     * @param instanceBody a Body whose Tip is a multi-output pattern feature and
     *                     whose TipComponentId names one emitted instance.
     * @return the new frozen Body, or nullptr on failure (Tip is not a pattern,
     *         component not found, etc.).
     */
    static Body* breakOutInstance(Body* instanceBody);

    PyObject* getPyObject() override;

    std::vector<std::string> getSubObjects(int reason = 0) const override;
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* pmat,
        bool transform,
        int depth
    ) const override;

    /// The shape a Body propagates as its output, derived on demand from its Tip
    /// (ARCHITECTURE §3.3: "the Body propagates its Tip's shape; it does not compute
    /// geometry of its own"). For a multi-output Body it is the single connected
    /// component named by TipComponentId (§3.3), extracted and world-placed. A null
    /// shape means there is no valid Tip/component yet; callers decide what a miss means.
    Part::TopoShape derivedTipShape() const;

    void setShowTip(bool enable)
    {
        showTip = enable;
    }

    /**
     * Return the solid feature before the given feature, or before the Tip feature
     * That is, sketches and datum features are skipped
     */
    App::DocumentObject* getPrevSolidFeature(App::DocumentObject* start = nullptr);

    /**
     * Return the next solid feature after the given feature, or after the Tip feature
     * That is, sketches and datum features are skipped
     */
    App::DocumentObject* getNextSolidFeature(App::DocumentObject* start = nullptr);

    // a body is solid if it has features that are solid according to member isSolidFeature.
    bool isSolid();

    /// Pure LOOKUP of the single document-level Origin — no side effects, no throw. Returns the
    /// shared free-standing App::Origin (the coordinate root de-owned features anchor to), or
    /// nullptr if the document has none. A Body never mints the world frame: under the
    /// document-owned world-frame contract (ARCHITECTURE_AMENDMENTS Amendment 2) a
    /// CAD (Part) document creates it at document creation. Static so body-creation entry points
    /// can check a document up front, before any Body object exists.
    static App::Origin* findDocumentOrigin(App::Document* doc);

    /// Same lookup as findDocumentOrigin, but throws Base::RuntimeError with a clear message when
    /// the document has no world frame. Body-creation entry points call this BEFORE addObject so
    /// an invalid attempt (a Body in a non-CAD document) fails with no side effect.
    static App::Origin* requireDocumentOrigin(App::Document* doc);

    /// Resolves this Body's shared document-level Origin (the setupObject/display backstop),
    /// throwing via requireDocumentOrigin if absent. See findDocumentOrigin for the contract.
    App::Origin* getDocumentOrigin()
    {
        return requireDocumentOrigin(getDocument());
    }

    /// Cruth §11 step 5e: with the OriginGroup extension retired, a Body no longer stores an
    /// Origin link. getOrigin() returns the single shared document Origin by lookup (the same
    /// object every Body and de-owned feature anchors to). Preserves the GUI feature-creation
    /// call sites that reach the base planes/axes via body->getOrigin(). Throws if absent.
    App::Origin* getOrigin()
    {
        return getDocumentOrigin();
    }

protected:
    void onSettingDocument() override;

    /// Adjusts the first solid's feature's base on BaseFeature getting set
    void onChanged(const App::Property* prop) override;

    /// Creates the corresponding Origin object
    void setupObject() override;
    /// Removes all planes and axis if they are still linked to the document
    void unsetupObject() override;

    void onDocumentRestored() override;

private:
    /// Cruth de-ownership (§9 / §8.3): repopulate the transient Feature::_Body cache from
    /// the BaseFeature chain after a document restore. The feature->marker relationship is
    /// not serialised, so it is re-derived by walking back from the Tip and memoising this
    /// marker on each feature that resolves to us, up to the seam (where the chain bases on
    /// another Body's Tip via a FeatureBase). Group is empty under de-ownership and is no
    /// longer consulted.
    void rebuildBodyCacheFromChain();

    fastsignals::scoped_connection connection;
    bool showTip = false;
};

}  // namespace PartDesign
