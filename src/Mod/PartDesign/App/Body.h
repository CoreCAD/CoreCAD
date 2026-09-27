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

// A Body marks the Tip of a chain of features and shows its shape. It owns nothing: membership
// is read from the chain, and the shape is derived from the Tip.
class PartDesignExport Body: public Part::BodyBase
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Body);

public:
    App::PropertyColor Color;
    /// Which solid of the Tip this Body stands for; empty for the whole Tip.
    App::PropertyString TipComponentId;
    /// Minted at birth and kept for life; what assemblies and drawings refer to.
    App::PropertyUUID Uid;
    /// Uids of Bodies whose overlap with this one the user acknowledged.
    App::PropertyStringList AcknowledgedOverlaps;
    /// The step the Body stops computing at; empty computes the whole chain. The steps past it
    /// keep their place and are skipped.
    App::PropertyLink RollbackMarker;

    Body();

    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderBody";
    }
    App::DocumentObject::ContentScope getContentScope() const override
    {
        return App::DocumentObject::ContentScope::Body;
    }

    // Editing the chain (Body.cpp). None of these create or destroy the feature.

    /// Builds @p feature on the shown step, ahead of what followed there, and shows it.
    std::vector<App::DocumentObject*> addFeature(App::DocumentObject* feature);
    std::vector<DocumentObject*> addFeatures(std::vector<DocumentObject*> features);
    /// Before @p target, or after it when @p after; at the start or the Tip when @p target is
    /// null. Moves the Tip only when the feature lands at the end.
    void insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after = false);
    /// Takes @p feature out of the chain and retires the Body if nothing is left. Call before the
    /// feature leaves the document.
    std::vector<DocumentObject*> removeFeature(DocumentObject* feature);
    void removeFeatures(const std::vector<App::DocumentObject*>& features);

    /// Every Body loses @p feature: what built on it builds on its base, and it keeps no base.
    static void takeOffChain(PartDesign::Feature* feature);

    static bool isSolidFeature(const App::DocumentObject* obj);
    static bool isAllowed(const App::DocumentObject* obj);
    /// The solid step before @p start (default: the Tip), skipping sketches and datums.
    App::DocumentObject* getPrevSolidFeature(App::DocumentObject* start = nullptr);
    bool isSolid();

    // One Body per separate solid, kept after every recompute (BodyReconcile.cpp).

    /// Spawns a Body for each extra solid a recomputed Tip now makes, and retires Bodies whose
    /// solid is gone.
    static void reconcileMultiOutput(
        App::Document* doc,
        const std::vector<App::DocumentObject*>& recomputed
    );
    /// For a Tip deleted without removeFeature (a script): retreat or retire its Bodies.
    static void retireOrRetreatTippedBodies(App::Document* doc, App::DocumentObject* feature);
    /// Gives a Body to each recomputed solid, such as an import, that no Body claims.
    static void spawnBodiesForUnclaimedOutput(
        App::Document* doc,
        const std::vector<App::DocumentObject*>& recomputed
    );
    /// Runs the three above on every document's signals. Call once at module init.
    static void initMultiOutputObserver();

    /// The @p index-th (1-based) solid's smallest mapped face name, or "Solid<index>".
    static std::string componentIdOfSolid(const Part::TopoShape& shape, int index);
    /// The element roots a solid's faces grew from: how a recomputed solid finds its old Body.
    static std::set<std::string> provenanceOfSolid(const Part::TopoShape& solid);
    /// What TipComponentId stores for a solid: its provenance, or for a pattern (whose copies
    /// share provenance) its component id.
    static std::string componentKeyOfSolid(
        const App::DocumentObject* tipFeature,
        const Part::TopoShape& shape,
        int index
    );

    // Gestures that act on several Bodies, and overlap between Bodies (BodyGestures.cpp).

    /// True when the two solids share volume; touching faces do not count.
    static bool toolReaches(const Part::TopoShape& tool, const Part::TopoShape& bodyShape);
    /// True when @p profile, swept either way along its normal, passes through @p bodyShape.
    static bool profileReaches(App::DocumentObject* profile, const Part::TopoShape& bodyShape);
    /// One Boolean per target Body, all using the one shared @p tool.
    static std::vector<App::DocumentObject*> spawnScopeSiblings(
        App::DocumentObject* tool,
        const std::vector<Body*>& targets,
        const char* booleanType
    );
    /// As above, joining the existing gesture @p gestureId.
    static std::vector<App::DocumentObject*> spawnScopeSiblings(
        App::DocumentObject* tool,
        const std::vector<Body*>& targets,
        const char* booleanType,
        const std::string& gestureId
    );
    /// One Pocket per target Body, all using the one shared @p profile.
    static std::vector<App::DocumentObject*> spawnScopeSiblingsFromProfile(
        App::DocumentObject* profile,
        const std::vector<Body*>& targets,
        const char* pocketType,
        double length
    );
    /// As above, joining the existing gesture @p gestureId.
    static std::vector<App::DocumentObject*> spawnScopeSiblingsFromProfile(
        App::DocumentObject* profile,
        const std::vector<Body*>& targets,
        const char* pocketType,
        double length,
        const std::string& gestureId
    );
    /// The features one gesture made, found by their shared tag.
    static std::vector<App::DocumentObject*> gestureSiblings(
        App::Document* doc,
        const std::string& gestureId
    );
    /// Every pair of independent solids that share volume. Costly; run on demand.
    static std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> findInterferingPairs(
        App::Document* doc
    );
    /// findInterferingPairs without the pairs the user acknowledged.
    static std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> liveInterferingPairs(
        App::Document* doc
    );
    static bool isInterferenceDismissed(const App::DocumentObject* a, const App::DocumentObject* b);
    /// Only a pair of Bodies has somewhere to record the acknowledgement.
    static bool isInterferenceDismissable(const App::DocumentObject* a, const App::DocumentObject* b);
    static void dismissInterference(Body* a, Body* b);

    // Which Body a feature belongs to (BodyMembership.cpp). Read from the chain, never stored.

    /// The first of bodiesOf, or null. Use bodyOf when the choice matters.
    static Body* findBodyOf(const App::DocumentObject* feature);
    /// Every Body the feature feeds, up to the nearest Tip on each branch.
    static std::vector<Body*> bodiesOf(const App::DocumentObject* feature);
    static std::vector<Body*> bodiesTippedAt(const App::DocumentObject* feature);
    /// The one Body meant by a pick on @p feature; throws when several fit and the pick
    /// cannot tell them apart.
    static Body* bodyOf(const App::DocumentObject* feature, const char* subElement);
    /// The component id of the solid that owns @p subElement, or empty.
    static std::string componentIdOfSub(const App::DocumentObject* feature, const char* subElement);
    static bool backsBody(const App::DocumentObject* feature, const Body* body);
    static bool inAnyBody(const App::DocumentObject* feature);
    static bool sameBody(const App::DocumentObject* a, const App::DocumentObject* b);
    /// This Body's solid steps in build order, then the sketches and datums they use or that
    /// are attached to them.
    std::vector<App::DocumentObject*> getFullModel() override;
    /// This Body's solid steps in build order.
    std::vector<App::DocumentObject*> ownSolids() const;
    /// Only a solid step builds the Body; its sketches and datums are inputs.
    bool isBuiltBy(const App::DocumentObject* f) override
    {
        return isSolidFeature(f) && Part::BodyBase::isBuiltBy(f);
    }
    /// The feature of this Body named @p name (or $-prefixed label), for sub-object paths.
    PartDesign::Feature* findOwnedFeature(const std::string& name) const;
    /// The Body a new feature on @p sketch extends: the one its attachment chain ends on.
    /// Null when it ends on none (the caller spawns one) or on several (@p ambiguous).
    static Body* resolveBaseBody(Part::Part2DObject* sketch, bool& ambiguous);
    /// The Body @p feature could merge into, asked independently of where it is now so the
    /// choice can be undone. Never throws; null when there is none or it is ambiguous.
    static Body* resolveMergeCandidate(App::DocumentObject* feature);
    /// Every Body @p feature could merge into: all that do not already depend on it.
    static std::vector<Body*> mergeCandidates(App::DocumentObject* feature);

    // Creating Bodies and moving features between them (BodyActions.cpp).

    /// A new, empty Body; it must receive a feature before the next recompute.
    static Body* spawnAutoBody(App::Document* doc);
    /// Moves @p feature onto @p target's Tip, or into a new Body when @p target is null.
    /// Returns the Body it ends up in. Does not recompute.
    static Body* moveFeatureToBody(App::DocumentObject* feature, Body* target);
    /// Freezes the pattern copy @p instanceBody stands for into a new Body of its own and
    /// drops that copy from the pattern.
    static Body* breakOutInstance(Body* instanceBody);

    // Shape and selection (Body.cpp).

    /// The step the Body shows and new steps build on: the marker, or the Tip.
    App::DocumentObject* shownStep() const;
    /// True when @p step lies past this Body's marker.
    bool stopsBefore(const App::DocumentObject* step) const;
    /// True when every Body @p step feeds stops before it, so computing it is wasted.
    static bool isRolledBackPast(const App::DocumentObject* step);
    /// True for a step rolled back past, and for anything whose every user is held back, such as
    /// the sketch of such a step: none of it is computed, so none of it may be shown.
    static bool isHeldBack(const App::DocumentObject* obj);
    /// The Tip's shape, or the one copy of it this Body stands for, placed in the world. Null
    /// when there is no such shape.
    Part::TopoShape derivedTipShape() const;
    /// As derivedTipShape, for the shown step.
    Part::TopoShape shownShape() const;
    /// The name on the shown step's shape of the element @p bodySub names on this Body's shape;
    /// empty when none. For a pattern copy the numbers differ, so the element is matched by
    /// itself.
    std::string shownSubElement(const char* bodySub) const;
    Base::Color getIdentityColor() const override
    {
        return Color.getValue();
    }
    void setShowTip(bool enable)
    {
        showTip = enable;
    }
    PyObject* getPyObject() override;
    std::vector<std::string> getSubObjects(int reason = 0) const override;
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* pmat,
        bool transform,
        int depth
    ) const override;

    // The document's world frame. A Body looks it up and never creates it.

    static App::Origin* findDocumentOrigin(App::Document* doc);
    /// Throws when the document has no world frame.
    static App::Origin* requireDocumentOrigin(App::Document* doc);
    App::Origin* getOrigin()
    {
        return requireDocumentOrigin(getDocument());
    }

protected:
    void onBeforeChange(const App::Property* prop) override;
    /// Keeps a FeatureBase at the start of the chain carrying this Body's BaseFeature.
    void onChanged(const App::Property* prop) override;
    void setupObject() override;
    void onDocumentRestored() override;

private:
    /// The pattern copy this Body stands for, or chain::WholeOutput.
    long tipCopy() const;
    void appendAtTip(PartDesign::Feature* feature, long copy);
    void insertAtMarker(PartDesign::Feature* feature);
    /// The step after the marker on the way to the Tip, or null.
    PartDesign::Feature* stepAfterMarker() const;
    /// The solid of @p step's @p shape this Body stands for; sets @p whole instead when it
    /// stands for all of it.
    Part::TopoShape standInSolid(
        const App::DocumentObject* step,
        const Part::TopoShape& shape,
        bool& whole
    ) const;
    Part::TopoShape placedShapeOf(const App::DocumentObject* step) const;
    /// Touches every step past @p marker, which were skipped while it stood there.
    void touchStepsPast(const App::DocumentObject* marker);
    /// Moves every Body tipped at @p feature back to @p retreatTo.
    static void retreatTippedBodies(App::DocumentObject* feature, App::DocumentObject* retreatTo);

    bool showTip = false;
    App::DocumentObject* markerBeforeChange = nullptr;
};

}  // namespace PartDesign
