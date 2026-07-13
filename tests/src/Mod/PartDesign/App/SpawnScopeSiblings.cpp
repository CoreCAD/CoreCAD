// SPDX-License-Identifier: LGPL-2.1-or-later

// Locks the ARCHITECTURE Amendment 5 §5.1 multi-body fan-out implemented by
// Body::spawnScopeSiblings: one shared tool reaching several Bodies resolves to one ordinary Cut/
// Common feature PER Body, each advancing that Body's OWN chain and referencing the one tool by
// reference. The invariants under test are exactly the ones Clause 5.1 and
// ANALYSIS_ownership-query-multibody.md forbid regressing: no feature extends two chains (each
// sibling's BaseFeature is its own Body's prior Tip, each Body's Tip is its own sibling), the tool
// is shared not owned (every sibling's Tools is the same one object), and the cut actually removes
// the shared volume from each Body.

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <App/Application.h>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <App/Document.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureBoolean.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeaturePocket.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class SpawnScopeSiblingsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Scope_test", "testUser", {.documentType = "Part"});
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
        }
    }

    static void addRect(Sketcher::SketchObject* sk, double x0, double y0, double x1, double y1)
    {
        const std::vector<Base::Vector3d> corners {
            Base::Vector3d(x0, y0, 0),
            Base::Vector3d(x1, y0, 0),
            Base::Vector3d(x1, y1, 0),
            Base::Vector3d(x0, y1, 0)
        };
        for (std::size_t i = 0; i < corners.size(); ++i) {
            Part::GeomLineSegment seg;
            seg.setPoints(corners[i], corners[(i + 1) % corners.size()]);
            sk->addGeometry(&seg, false);
        }
    }

    // A Body whose single feature is a 10-tall Pad of the given rectangle.
    PartDesign::Body* padBody(double x0, double y0, double x1, double y1)
    {
        auto* body = _doc->addObject<PartDesign::Body>();
        auto* sk = _doc->addObject<Sketcher::SketchObject>();
        body->addFeature(sk);
        sk->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        sk->MapMode.setValue("FlatFace");
        addRect(sk, x0, y0, x1, y1);
        auto* pad = _doc->addObject<PartDesign::Pad>();
        body->addFeature(pad);
        pad->Profile.setValue(sk, {""});
        pad->Length.setValue(10.0);
        return body;
    }

    static double volumeOf(App::DocumentObject* feat)
    {
        auto* pf = dynamic_cast<Part::ShapeFeature*>(feat);
        if (!pf) {
            return 0.0;
        }
        GProp_GProps props;
        BRepGProp::VolumeProperties(pf->Shape.getShape().getShape(), props);
        return props.Mass();
    }

    static unsigned solidCount(App::DocumentObject* feat)
    {
        auto* pf = dynamic_cast<Part::ShapeFeature*>(feat);
        if (!pf) {
            return 0;
        }
        return pf->Shape.getShape().countSubShapes(TopAbs_SOLID);
    }

    static Part::TopoShape shapeOf(App::DocumentObject* feat)
    {
        auto* pf = dynamic_cast<Part::ShapeFeature*>(feat);
        return pf ? pf->Shape.getShape() : Part::TopoShape();
    }

    // A standalone sketch profile (a rectangle) placed at height z with +Z normal, usable as a
    // shared Pocket tool. Placed mid-body so a Pocket cuts material whichever way it runs.
    Sketcher::SketchObject* profileSketch(double x0, double y0, double x1, double y1, double z)
    {
        auto* sk = _doc->addObject<Sketcher::SketchObject>();
        sk->Placement.setValue(Base::Placement(Base::Vector3d(0, 0, z), Base::Rotation()));
        addRect(sk, x0, y0, x1, y1);
        return sk;
    }

    App::Document* _doc = nullptr;
};

// The core fan-out: a tool overlapping two Bodies, cut from both, yields one Cut sibling per Body,
// each rooted on its own Body's chain and sharing the one tool.
TEST_F(SpawnScopeSiblingsTest, CutFansOutOneSiblingPerBody)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);   // X 0..20
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);  // X 30..50
    PartDesign::Body* tool = padBody(10, 5, 40, 15);   // X 10..40 — overlaps both
    _doc->recompute();

    App::DocumentObject* tipA = bodyA->Tip.getValue();
    App::DocumentObject* tipB = bodyB->Tip.getValue();
    const double volA = volumeOf(tipA);
    const double volB = volumeOf(tipB);

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 2U);
    auto* cutA = static_cast<PartDesign::Boolean*>(siblings[0]);
    auto* cutB = static_cast<PartDesign::Boolean*>(siblings[1]);

    // Each sibling advances its OWN Body's chain: BaseFeature = that Body's prior Tip, and the
    // Body's Tip now names the sibling. No feature extends two chains.
    EXPECT_EQ(cutA->BaseFeature.getValue(), tipA);
    EXPECT_EQ(cutB->BaseFeature.getValue(), tipB);
    EXPECT_EQ(bodyA->Tip.getValue(), cutA);
    EXPECT_EQ(bodyB->Tip.getValue(), cutB);

    // The tool is shared by reference, not owned or duplicated: both siblings' Tools is the one tool.
    ASSERT_EQ(cutA->Tools.getValues().size(), 1U);
    ASSERT_EQ(cutB->Tools.getValues().size(), 1U);
    EXPECT_EQ(cutA->Tools.getValues().front(), tool);
    EXPECT_EQ(cutB->Tools.getValues().front(), tool);
    EXPECT_EQ(cutA->Type.getValueAsString(), std::string("Cut"));

    // The cut actually removed the shared volume from each Body, and left the tool untouched.
    EXPECT_LT(volumeOf(cutA), volA) << "Body A must lose the overlapped volume";
    EXPECT_LT(volumeOf(cutB), volB) << "Body B must lose the overlapped volume";
    EXPECT_GT(volumeOf(tool->Tip.getValue()), 0.0) << "the shared tool is not consumed";
}

// The spawn honours the caller's chosen set: cutting only one of the two reached Bodies leaves the
// other's chain untouched.
TEST_F(SpawnScopeSiblingsTest, RespectsChosenSubset)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    App::DocumentObject* tipB = bodyB->Tip.getValue();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_EQ(bodyA->Tip.getValue(), siblings[0]);
    EXPECT_EQ(bodyB->Tip.getValue(), tipB) << "an unchosen Body's chain must be untouched";
}

TEST_F(SpawnScopeSiblingsTest, EmptyTargetsIsNoOp)
{
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();
    EXPECT_TRUE(PartDesign::Body::spawnScopeSiblings(tool, {}, "Cut").empty());
    EXPECT_TRUE(PartDesign::Body::spawnScopeSiblings(nullptr, {}, "Cut").empty());
}

// Clause 5.3 gesture tag: every sibling of one fan-out shares one non-empty GestureId, a distinct
// gesture gets a distinct tag, and an ordinary single-body feature carries none.
TEST_F(SpawnScopeSiblingsTest, SiblingsShareOneGestureId)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    // An ordinary single-body feature (the tool's own Pad) has no gesture tag.
    auto* pad = dynamic_cast<PartDesign::Feature*>(tool->Tip.getValue());
    ASSERT_NE(pad, nullptr);
    EXPECT_TRUE(std::string(pad->GestureId.getValue()).empty())
        << "a plain feature carries no gesture tag";

    auto first = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    ASSERT_EQ(first.size(), 2U);
    const std::string id = static_cast<PartDesign::Boolean*>(first[0])->GestureId.getValue();
    EXPECT_FALSE(id.empty()) << "a spawned sibling must carry a gesture tag";
    EXPECT_EQ(static_cast<PartDesign::Boolean*>(first[1])->GestureId.getValue(), id)
        << "siblings of one gesture share one tag";

    // A second, independent gesture gets its own distinct tag.
    auto second = PartDesign::Body::spawnScopeSiblings(tool, {bodyA}, "Common");
    ASSERT_EQ(second.size(), 1U);
    EXPECT_NE(static_cast<PartDesign::Boolean*>(second[0])->GestureId.getValue(), id)
        << "a distinct gesture gets a distinct tag";
}

// The gesture tag is inert (Prop_NoRecompute): hand-editing it schedules no recompute of the
// feature, because nothing derives geometry or membership from it.
TEST_F(SpawnScopeSiblingsTest, GestureIdIsInert)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA}, "Cut");
    ASSERT_EQ(siblings.size(), 1U);
    auto* cut = static_cast<PartDesign::Boolean*>(siblings[0]);
    _doc->recompute();
    ASSERT_FALSE(cut->mustRecompute()) << "baseline: a freshly recomputed feature is settled";

    cut->GestureId.setValue("hand-edited");
    EXPECT_EQ(cut->GestureId.getValue(), std::string("hand-edited"));
    EXPECT_FALSE(cut->mustRecompute()) << "editing the inert tag must not schedule a recompute";
}

// The gesture tag persists across save and reopen.
TEST_F(SpawnScopeSiblingsTest, GestureIdRoundTripsSaveReopen)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();
    ASSERT_EQ(siblings.size(), 2U);
    const std::string cutName = siblings[0]->getNameInDocument();
    const std::string id = static_cast<PartDesign::Boolean*>(siblings[0])->GestureId.getValue();
    ASSERT_FALSE(id.empty());

    const std::string path = "scope_gestureid_roundtrip.FCStd";
    _doc->saveAs(path.c_str());
    const std::string docName = _doc->getName();
    App::GetApplication().closeDocument(docName.c_str());
    _doc = nullptr;

    App::Document* reopened = App::GetApplication().openDocument(path.c_str());
    _doc = reopened;  // hand ownership to TearDown
    ASSERT_NE(reopened, nullptr);
    auto* cut = dynamic_cast<PartDesign::Boolean*>(reopened->getObject(cutName.c_str()));
    ASSERT_NE(cut, nullptr);
    EXPECT_EQ(cut->GestureId.getValue(), id) << "the gesture tag must survive save/reopen";

    std::remove(path.c_str());
}

// Step D — Common (intersective) fan-out: a tool overlapping two Bodies, intersected with both,
// yields one Common sibling per Body, each reduced to its overlap with the tool.
TEST_F(SpawnScopeSiblingsTest, CommonFansOutOneSiblingPerBody)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);   // X 0..20
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);  // X 30..50
    PartDesign::Body* tool = padBody(10, 5, 40, 15);   // X 10..40 — overlaps both
    _doc->recompute();

    const double volA = volumeOf(bodyA->Tip.getValue());

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Common");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 2U);
    auto* comA = static_cast<PartDesign::Boolean*>(siblings[0]);
    auto* comB = static_cast<PartDesign::Boolean*>(siblings[1]);
    EXPECT_TRUE(comA->isValid());
    EXPECT_TRUE(comB->isValid());
    EXPECT_EQ(comA->Type.getValueAsString(), std::string("Common"));

    // Each Body is reduced to its intersection with the tool: positive, less than the original, and
    // exactly the overlap box (X[10,20]xY[5,15]xZ[0,10] = 1000, likewise X[30,40] for B).
    EXPECT_GT(volumeOf(comA), 0.0);
    EXPECT_LT(volumeOf(comA), volA);
    EXPECT_NEAR(volumeOf(comA), 1000.0, 1e-6);
    EXPECT_NEAR(volumeOf(comB), 1000.0, 1e-6);
}

// Step D degenerate (intersective): a Common scoped onto a Body the tool MISSES empties that Body.
// Per P7 this fails loud — the sibling errors — rather than silently leaving a zero-volume Tip.
// (Retirement of the emptied Body is the separate §4.7/#40 contract, deliberately not done here.)
TEST_F(SpawnScopeSiblingsTest, CommonMissEmptiesBodyAndFailsLoud)
{
    PartDesign::Body* bodyFar = padBody(100, 0, 120, 20);  // disjoint from the tool
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyFar}, "Common");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_TRUE(siblings[0]->isError())
        << "a Common that misses its Body must fail loud, not emit an empty body";
}

// Step D degenerate (subtractive): a Cut whose tool fully CONTAINS a Body empties it — same
// fail-loud contract, distinct corner from the intersective miss above.
TEST_F(SpawnScopeSiblingsTest, CutContainingBodyFailsLoud)
{
    PartDesign::Body* small = padBody(12, 8, 15, 12);  // wholly inside the tool's XY footprint
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {small}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_TRUE(siblings[0]->isError())
        << "a Cut that consumes the whole Body must fail loud, not emit an empty body";
}

// Step D composition (§4.7): a Cut whose tool SEVERS a Body into two disjoint lumps is NOT empty —
// it is a legitimate multi-solid body. The fail-loud guard must let it through (>= 1 solid). This
// also documents the still-open split-body gap: one Body carrying a two-solid compound.
TEST_F(SpawnScopeSiblingsTest, SeveringCutYieldsMultiSolidBody)
{
    PartDesign::Body* bar = padBody(0, 0, 50, 20);      // X 0..50
    PartDesign::Body* knife = padBody(20, -5, 30, 25);  // full-Y slab across X 20..30
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(knife, {bar}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_TRUE(siblings[0]->isValid()) << "a severing Cut is valid, not empty";
    EXPECT_EQ(solidCount(siblings[0]), 2U) << "the severed Body is a two-solid compound (§4.7 gap)";
}

// Step E — sketch-tool reach: the profile's perpendicular column reaches exactly the Bodies whose
// footprint it overlaps, independent of extent.
TEST_F(SpawnScopeSiblingsTest, ProfileReachesOnlyOverlappingBodies)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);   // X 0..20
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);  // X 30..50
    _doc->recompute();

    Sketcher::SketchObject* spanning = profileSketch(5, 5, 45, 15, 5);  // over both footprints
    Sketcher::SketchObject* onlyA = profileSketch(5, 5, 15, 15, 5);     // over A only
    _doc->recompute();

    EXPECT_TRUE(PartDesign::Body::profileReaches(spanning, shapeOf(bodyA->Tip.getValue())));
    EXPECT_TRUE(PartDesign::Body::profileReaches(spanning, shapeOf(bodyB->Tip.getValue())));
    EXPECT_TRUE(PartDesign::Body::profileReaches(onlyA, shapeOf(bodyA->Tip.getValue())));
    EXPECT_FALSE(PartDesign::Body::profileReaches(onlyA, shapeOf(bodyB->Tip.getValue())))
        << "a profile that misses a Body's footprint does not reach it";
}

// Step E — sketch-tool fan-out (ThroughAll): one shared profile cutting two Bodies yields one
// Pocket per Body, each on its own chain, sharing the one profile and the gesture tag.
TEST_F(SpawnScopeSiblingsTest, ProfileThroughAllFansOutPocketPerBody)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    _doc->recompute();
    Sketcher::SketchObject* profile = profileSketch(5, 5, 45, 15, 5);  // spans both
    _doc->recompute();

    const double volA = volumeOf(bodyA->Tip.getValue());
    const double volB = volumeOf(bodyB->Tip.getValue());

    auto siblings
        = PartDesign::Body::spawnScopeSiblingsFromProfile(profile, {bodyA, bodyB}, "ThroughAll", 0.0);
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 2U);
    auto* pocA = static_cast<PartDesign::Pocket*>(siblings[0]);
    auto* pocB = static_cast<PartDesign::Pocket*>(siblings[1]);
    EXPECT_TRUE(pocA->isValid());
    EXPECT_TRUE(pocB->isValid());
    EXPECT_EQ(pocA->Type.getValueAsString(), std::string("ThroughAll"));

    // Each Pocket advances its own Body's chain and the tool is the one shared profile by reference.
    EXPECT_EQ(bodyA->Tip.getValue(), pocA);
    EXPECT_EQ(bodyB->Tip.getValue(), pocB);
    EXPECT_EQ(pocA->Profile.getValue(), profile);
    EXPECT_EQ(pocB->Profile.getValue(), profile);

    // Each Body loses the cut volume, and the gesture tag is shared (Clause 5.3).
    EXPECT_LT(volumeOf(pocA), volA);
    EXPECT_LT(volumeOf(pocB), volB);
    const std::string id = pocA->GestureId.getValue();
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(std::string(pocB->GestureId.getValue()), id);
}

// Step E — sketch-tool fan-out (Length): the finite flavour removes a bounded pocket, not the whole
// Body, confirming the extent is honoured (Clause 5.1 "both tool kinds", both extents).
TEST_F(SpawnScopeSiblingsTest, ProfileLengthFansOutFiniteCut)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    _doc->recompute();
    Sketcher::SketchObject* profile = profileSketch(5, 5, 15, 15, 5);  // over A, mid-height
    _doc->recompute();

    const double volA = volumeOf(bodyA->Tip.getValue());

    auto siblings = PartDesign::Body::spawnScopeSiblingsFromProfile(profile, {bodyA}, "Length", 4.0);
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    auto* pocA = static_cast<PartDesign::Pocket*>(siblings[0]);
    EXPECT_TRUE(pocA->isValid());
    EXPECT_EQ(pocA->Type.getValueAsString(), std::string("Length"));
    EXPECT_LT(volumeOf(pocA), volA) << "a Length pocket removes some material";
    EXPECT_GT(volumeOf(pocA), 0.0) << "but not the whole Body";
}

// Step E follow-up: the full-consume Pocket. A ThroughAll profile covering a whole Body empties it;
// with the FeatureExtrude guard now matching Boolean::execute, that fails loud (P7) rather than
// leaving a phantom zero-solid Tip. (Retirement of the emptied Body remains the §4.7/#40 contract.)
TEST_F(SpawnScopeSiblingsTest, FullConsumePocketFailsLoud)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    _doc->recompute();
    // At the top face (Z=10); the default downward ThroughAll then runs through the whole Body.
    Sketcher::SketchObject* cover = profileSketch(-5, -5, 25, 25, 10);  // covers the whole footprint
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblingsFromProfile(cover, {bodyA}, "ThroughAll", 0.0);
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_TRUE(siblings[0]->isError())
        << "a Pocket that consumes the whole Body must fail loud, not emit an empty body";
}

// Step F — the shared tag re-collects a gesture's siblings, with no membership list anywhere.
TEST_F(SpawnScopeSiblingsTest, GestureSiblingsRecollectsByTag)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();
    ASSERT_EQ(siblings.size(), 2U);
    const std::string id = static_cast<PartDesign::Boolean*>(siblings[0])->GestureId.getValue();

    auto found = PartDesign::Body::gestureSiblings(_doc, id);
    EXPECT_EQ(found.size(), 2U);
    for (auto* s : siblings) {
        EXPECT_NE(std::find(found.begin(), found.end(), s), found.end());
    }
    EXPECT_TRUE(PartDesign::Body::gestureSiblings(_doc, "").empty())
        << "empty tag is not a gesture";
    EXPECT_TRUE(PartDesign::Body::gestureSiblings(_doc, "no-such-id").empty());
}

// Step F — extending a gesture's scope onto a newly included Body spawns exactly one more sibling,
// carrying the same tag, on that Body's own chain.
TEST_F(SpawnScopeSiblingsTest, AddingABodyToScopeSpawnsOneSibling)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* bodyC = padBody(60, 0, 80, 20);
    PartDesign::Body* tool = padBody(10, 5, 70, 15);  // reaches all three
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();
    const std::string id = static_cast<PartDesign::Boolean*>(siblings[0])->GestureId.getValue();

    // The Scope edit: include bodyC, reusing the gesture's own id (tagged overload).
    auto added = PartDesign::Body::spawnScopeSiblings(tool, {bodyC}, "Cut", id);
    _doc->recompute();

    ASSERT_EQ(added.size(), 1U);
    EXPECT_EQ(PartDesign::Body::gestureSiblings(_doc, id).size(), 3U);
    EXPECT_EQ(bodyC->Tip.getValue(), added[0]);
    EXPECT_EQ(std::string(static_cast<PartDesign::Boolean*>(added[0])->GestureId.getValue()), id);
}

// Step F — dropping a Body from scope prunes its sibling and retreats that Body's chain; the rest
// of the gesture is untouched.
TEST_F(SpawnScopeSiblingsTest, RemovingABodyFromScopePrunesItsSibling)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    App::DocumentObject* padA = bodyA->Tip.getValue();  // bodyA's chain before the cut
    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();
    const std::string id = static_cast<PartDesign::Boolean*>(siblings[0])->GestureId.getValue();
    App::DocumentObject* sibA = siblings[0];  // on bodyA

    // The Scope edit: exclude bodyA. Rewire its chain, then delete the sibling.
    bodyA->removeFeature(sibA);
    _doc->removeObject(sibA->getNameInDocument());
    _doc->recompute();

    EXPECT_EQ(PartDesign::Body::gestureSiblings(_doc, id).size(), 1U)
        << "only bodyB's sibling remains";
    EXPECT_EQ(bodyA->Tip.getValue(), padA) << "bodyA's chain retreated to its pad";
    EXPECT_EQ(bodyB->Tip.getValue(), siblings[1]) << "bodyB's sibling is untouched";
}

// Step F — the Clause 5.3 invariant: the reach set is fixed at resolve time. Editing the TOOL
// geometry alone recomputes the cuts but neither spawns nor prunes a sibling.
TEST_F(SpawnScopeSiblingsTest, ToolGeometryEditDoesNotChangeScope)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();
    const std::string id = static_cast<PartDesign::Boolean*>(siblings[0])->GestureId.getValue();
    auto before = PartDesign::Body::gestureSiblings(_doc, id);

    // Edit the tool's own geometry (taller pad) and recompute.
    auto* toolPad = dynamic_cast<PartDesign::Pad*>(tool->Tip.getValue());
    ASSERT_NE(toolPad, nullptr);
    toolPad->Length.setValue(20.0);
    _doc->recompute();

    auto after = PartDesign::Body::gestureSiblings(_doc, id);
    EXPECT_EQ(after.size(), before.size()) << "editing the tool must not change the sibling count";
    EXPECT_EQ(after, before) << "the same siblings, in the same order — scope is explicit-only";
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
