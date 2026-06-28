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

class PartDesignExport Body: public Part::BodyBase
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Body);

public:
    App::PropertyBool AllowCompound;

    /// Cruth §4.6 visual identity — auto-assigned from a deterministic palette at spawn.
    App::PropertyColor Color;

    /// Cruth §3.3 component identity. A Body's Tip is a (feature, component-id) pair: this
    /// names which connected component of the Tip feature's output shape the Body represents.
    /// Empty means the implicit, single-component case (the overwhelming majority of Bodies).
    /// Features that produce multiple disjoint components spawn one Body per component, each
    /// sharing the Tip feature but carrying a distinct, element-map-stable id here.
    App::PropertyString TipComponentId;

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
    //@}

    /**
     * Add the feature into the body at the current insert point.
     * The insertion point is the before next solid after the Tip feature
     */
    /**
     * Cruth intra-body de-ownership: a new feature joins the pipeline by reference
     * (BaseFeature chain + Tip) without being added to Body.Group; handles both the
     * tip-append gesture and mid-chain insert. See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<App::DocumentObject*> addObject(App::DocumentObject*) override;
    std::vector<DocumentObject*> addObjects(std::vector<DocumentObject*> obj) override;

    /**
     * Insert the feature into the body after the given feature.
     *
     * @param feature  The feature to insert into the body
     * @param target   The feature relative which one should be inserted the given.
     *                 If target is NULL than insert into the end if where is InsertBefore
     *                 and into the begin if where is InsertAfter.
     * @param after    if true insert the feature after the target. Default is false.
     *
     * @note the method doesn't modify the Tip unlike addObject()
     */
    void insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after = false);

    void setBaseProperty(App::DocumentObject* feature);

    /**
     * Remove the feature from the body (Cruth intra-body de-ownership): rewire the
     * BaseFeature chain and retreat the Tip without consulting Group order; retire the
     * Body if its chain empties. Must be called BEFORE the feature is removed from the
     * Document. See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<DocumentObject*> removeObject(DocumentObject* obj) override;

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
    bool allowObject(DocumentObject* obj) override
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
     * Cruth §8.5/§4.6: resolve the base Body for a new sketch-based feature by
     * walking the sketch's anchor chain (AttachmentSupport through datums/reference
     * geometry).
     *  - chain ends at a global plane or independent geometry → auto-spawn a new
     *    Body at document level (§4.6) and return it;
     *  - chain terminates on exactly one Body → return that Body (extend);
     *  - chain reaches more than one Body → return nullptr and set @p ambiguous
     *    (the caller surfaces the §8.3 ambiguity prompt).
     *
     * Lives in the App layer so the Gui command and the Python API share one code
     * path — this is the P8 (Programmatic Equivalence) guarantee for auto-spawn.
     */
    static Body* resolveBaseBody(Part::Part2DObject* sketch, App::Document* doc, bool& ambiguous);

    /**
     * Cruth §4.6: auto-spawn a new Body at document level (no active-Part
     * containment). Color is assigned in setupObject() from the per-document
     * palette index. Returns nullptr if @p doc is null.
     */
    static Body* spawnAutoBody(App::Document* doc);

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

    /// Cruth substrate flip (Stage 3a): returns the single document-level Origin,
    /// lazily creating it if absent. This is the shared coordinate root that de-owned
    /// features resolve against, replacing the per-body Origin (which stays created and
    /// persisted but dormant until Stage 3b removes it). The document-level Origin is a
    /// free-standing App::Origin not owned by any OriginGroup.
    App::Origin* ensureDocumentOrigin();

protected:
    void onSettingDocument() override;

    /// Adjusts the first solid's feature's base on BaseFeature getting set
    void onChanged(const App::Property* prop) override;

    /// Creates the corresponding Origin object
    void setupObject() override;
    /// Removes all planes and axis if they are still linked to the document
    void unsetupObject() override;

    /// Cruth shared-Origin contract (GitHub #4): bind this Body's Origin link to the single
    /// shared document Origin instead of minting a private per-body one.
    void onExtendedSetupObject() override;
    /// Counterpart to the above: detach (do not delete) the shared Origin on retirement.
    void onExtendedUnsetupObject() override;

    void onDocumentRestored() override;

private:
    /// Cruth de-ownership (§3.3): find a pipeline feature that resolves to this Body (via
    /// the derived Feature::_Body marker) whose name (or $-prefixed label) matches, for
    /// sub-object path resolution. Features are no longer Group members, so the Group-based
    /// resolver cannot see them. Returns nullptr if no matching feature resolves to us.
    PartDesign::Feature* findOwnedFeature(const std::string& name) const;

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
