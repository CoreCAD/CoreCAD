// SPDX-License-Identifier: LGPL-2.1-or-later

// Locks the ARCHITECTURE Amendment 3 §3.3 fail-safe body-identity rule enforced by
// Body::reconcileMultiOutput: a stored body UUID is re-acquired across a recompute ONLY when the
// match is decidable WITHOUT resemblance. The single case the floor recognises is the trivial
// 1:1 (one prior Body naming the Tip, one solid) — it keeps its UUID. Every other outcome is a
// §4.7 topology event: all prior bodies retire (UUIDs die) and a fresh Body is minted per solid.
// These tests exist to catch any reintroduction of resemblance-based matching.

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <TopAbs_ShapeEnum.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeaturePocket.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class ReconcileMultiOutputTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Reconcile_test", "testUser", {.documentType = "Part"});
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    // A closed rectangle profile (four line segments) added to a sketch.
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

    Sketcher::SketchObject* newSketch(PartDesign::Body* body, const char* name)
    {
        auto* sk = _doc->addObject<Sketcher::SketchObject>(name);
        body->addFeature(sk);
        sk->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        sk->MapMode.setValue("FlatFace");
        return sk;
    }

    // Drive the reconciler exactly as the recompute observer would: recompute, then hand every
    // object to reconcileMultiOutput (it filters to Tip features itself).
    void recomputeAndReconcile()
    {
        _doc->recompute();
        PartDesign::Body::reconcileMultiOutput(_doc, _doc->getObjects());
    }

    std::vector<PartDesign::Body*> bodies() const
    {
        std::vector<PartDesign::Body*> out;
        for (auto* obj : _doc->getObjectsOfType(PartDesign::Body::getClassTypeId())) {
            out.push_back(static_cast<PartDesign::Body*>(obj));
        }
        return out;
    }

    // The individual solids of a feature's output shape.
    static std::vector<Part::TopoShape> solidsOf(App::DocumentObject* feat)
    {
        std::vector<Part::TopoShape> out;
        auto* pf = dynamic_cast<Part::Feature*>(feat);
        if (!pf) {
            return out;
        }
        const Part::TopoShape shape = pf->Shape.getShape();
        const auto n = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
        for (int i = 1; i <= n; ++i) {
            out.push_back(shape.getSubTopoShape(TopAbs_SOLID, i, /*silent*/ true));
        }
        return out;
    }

    static std::vector<std::string> intersect(
        const std::set<std::string>& a,
        const std::set<std::string>& b
    )
    {
        std::vector<std::string> out;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
        return out;
    }

    App::Document* _doc = nullptr;
};

// Trivial 1:1: a single body making a single solid keeps its UUID across a parameter edit, however
// far the geometry moves (Clause 3.1). This is the everyday case and must never churn identity.
TEST_F(ReconcileMultiOutputTest, TrivialOneToOneKeepsUuid)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* sketch = newSketch(body, "Base");
    addRect(sketch, 0, 0, 20, 10);

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(10.0);
    recomputeAndReconcile();

    ASSERT_EQ(bodies().size(), 1U);
    const std::string uid = bodies().front()->Uid.getValueStr();
    const std::string name = bodies().front()->getNameInDocument();

    // A large length change reshapes the solid but does not change the topology.
    pad->Length.setValue(37.0);
    recomputeAndReconcile();

    ASSERT_EQ(bodies().size(), 1U);
    EXPECT_EQ(bodies().front()->Uid.getValueStr(), uid) << "trivial 1:1 must keep the body UUID";
    EXPECT_EQ(bodies().front()->getNameInDocument(), name);
}

// A single feature yielding two disjoint solids is a topology event: the original body retires and
// two fresh bodies are minted, neither carrying the original UUID.
TEST_F(ReconcileMultiOutputTest, TwoDisjointSolidsMintTwoFreshBodies)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* sketch = newSketch(body, "Base");
    addRect(sketch, 0, 0, 10, 10);
    addRect(sketch, 30, 0, 40, 10);  // second, disjoint profile
    const std::string origUid = body->Uid.getValueStr();

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(5.0);
    recomputeAndReconcile();

    auto bs = bodies();
    ASSERT_EQ(bs.size(), 2U);
    std::vector<std::string> uids {bs[0]->Uid.getValueStr(), bs[1]->Uid.getValueStr()};
    EXPECT_EQ(std::count(uids.begin(), uids.end(), origUid), 0)
        << "the original UUID must not survive the topology event";
    EXPECT_NE(uids[0], uids[1]) << "the two fresh bodies must have distinct UUIDs";
}

// #33: a cut that severs one bar into two halves is a split — the original body retires and BOTH
// halves are new bodies. Identity never transfers to a half; there is no "original half".
TEST_F(ReconcileMultiOutputTest, SplitRetiresOriginalAndMintsFreshHalves)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* base = newSketch(body, "Base");
    addRect(base, 0, 0, 40, 10);

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(base, {""});
    pad->Length.setValue(10.0);
    recomputeAndReconcile();

    ASSERT_EQ(bodies().size(), 1U);
    const std::string origUid = bodies().front()->Uid.getValueStr();
    const std::string origName = bodies().front()->getNameInDocument();

    // A slot across the full width, through-all, severs the bar into two halves.
    auto* cut = newSketch(body, "Cut");
    addRect(cut, 18, -5, 22, 15);

    auto* pocket = _doc->addObject<PartDesign::Pocket>("Pocket");
    body->addFeature(pocket);
    pocket->Profile.setValue(cut, {""});
    pocket->Type.setValue("ThroughAll");
    pocket->Midplane.setValue(true);
    recomputeAndReconcile();

    auto bs = bodies();
    ASSERT_EQ(bs.size(), 2U);
    std::vector<std::string> uids {bs[0]->Uid.getValueStr(), bs[1]->Uid.getValueStr()};
    std::vector<std::string> names {bs[0]->getNameInDocument(), bs[1]->getNameInDocument()};
    EXPECT_EQ(std::count(names.begin(), names.end(), origName), 0) << "original body must retire";
    EXPECT_EQ(std::count(uids.begin(), uids.end(), origUid), 0)
        << "a split must not transfer the original UUID to a half (#33)";
    EXPECT_NE(uids[0], uids[1]) << "the two halves must have distinct fresh UUIDs";
}

// Piece 3 (native-ancestry match, the churn fix): a genuinely two-lump part keeps BOTH body UUIDs
// across a plain recompute and across a topology-preserving parameter edit. Before piece 3 the
// floor re-minted them every cycle.
TEST_F(ReconcileMultiOutputTest, TwoLumpsKeepUuidsAcrossRecompute)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* sketch = newSketch(body, "Base");
    addRect(sketch, 0, 0, 10, 10);
    addRect(sketch, 30, 0, 40, 10);

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(5.0);
    recomputeAndReconcile();

    auto bs = bodies();
    ASSERT_EQ(bs.size(), 2U);
    std::set<std::string> before {bs[0]->Uid.getValueStr(), bs[1]->Uid.getValueStr()};
    ASSERT_EQ(before.size(), 2U);

    // Plain recompute — nothing changed.
    recomputeAndReconcile();
    auto bs2 = bodies();
    ASSERT_EQ(bs2.size(), 2U);
    std::set<std::string> after {bs2[0]->Uid.getValueStr(), bs2[1]->Uid.getValueStr()};
    EXPECT_EQ(after, before) << "a two-lump part must keep both UUIDs across recompute (no churn)";

    // A length change preserves the topology, so identity must survive it too.
    pad->Length.setValue(9.0);
    recomputeAndReconcile();
    auto bs3 = bodies();
    ASSERT_EQ(bs3.size(), 2U);
    std::set<std::string> after2 {bs3[0]->Uid.getValueStr(), bs3[1]->Uid.getValueStr()};
    EXPECT_EQ(after2, before) << "a length change must preserve both UUIDs";
}

// Piece 3: after a sever mints two fresh halves (#33), those halves are STABLE across further
// recompute — they re-acquire their own UUIDs even though they share base-bar ancestry (the subset
// match distinguishes them by each half's distinct roots).
TEST_F(ReconcileMultiOutputTest, SplitHalvesAreStableAcrossRecompute)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* base = newSketch(body, "Base");
    addRect(base, 0, 0, 40, 10);
    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(base, {""});
    pad->Length.setValue(10.0);
    recomputeAndReconcile();

    auto* cut = newSketch(body, "Cut");
    addRect(cut, 18, -5, 22, 15);
    auto* pocket = _doc->addObject<PartDesign::Pocket>("Pocket");
    body->addFeature(pocket);
    pocket->Profile.setValue(cut, {""});
    pocket->Type.setValue("ThroughAll");
    pocket->Midplane.setValue(true);
    recomputeAndReconcile();

    auto bs = bodies();
    ASSERT_EQ(bs.size(), 2U);
    std::set<std::string> halves {bs[0]->Uid.getValueStr(), bs[1]->Uid.getValueStr()};
    ASSERT_EQ(halves.size(), 2U);

    recomputeAndReconcile();
    auto bs2 = bodies();
    ASSERT_EQ(bs2.size(), 2U);
    std::set<std::string> after {bs2[0]->Uid.getValueStr(), bs2[1]->Uid.getValueStr()};
    EXPECT_EQ(after, halves) << "severed halves must keep their UUIDs across further recompute";
}

// Piece 1 (native-ancestry provenance): two separate profiles grow from different sketch edges, so
// their solids' provenance root-sets are DISJOINT — the reconciler will read them as unrelated
// bodies, not a split.
TEST_F(ReconcileMultiOutputTest, ProvenanceDisjointForSeparateProfiles)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* sketch = newSketch(body, "Base");
    addRect(sketch, 0, 0, 10, 10);
    addRect(sketch, 30, 0, 40, 10);

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(5.0);
    _doc->recompute();

    auto solids = solidsOf(pad);
    ASSERT_EQ(solids.size(), 2U);
    const std::set<std::string> pa = PartDesign::Body::provenanceOfSolid(solids[0]);
    const std::set<std::string> pb = PartDesign::Body::provenanceOfSolid(solids[1]);
    EXPECT_FALSE(pa.empty());
    EXPECT_FALSE(pb.empty());
    EXPECT_TRUE(intersect(pa, pb).empty()) << "separate profiles must have disjoint provenance";
}

// Two halves of a severed bar both grow from the SAME base-bar edges, so their provenance root-sets
// OVERLAP — that shared ancestry is exactly what marks the event as a split of one original body
// (not two unrelated bodies).
TEST_F(ReconcileMultiOutputTest, ProvenanceOverlapsForSeveredHalves)
{
    auto* body = _doc->addObject<PartDesign::Body>();
    auto* base = newSketch(body, "Base");
    addRect(base, 0, 0, 40, 10);

    auto* pad = _doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(base, {""});
    pad->Length.setValue(10.0);
    _doc->recompute();

    auto* cut = newSketch(body, "Cut");
    addRect(cut, 18, -5, 22, 15);
    auto* pocket = _doc->addObject<PartDesign::Pocket>("Pocket");
    body->addFeature(pocket);
    pocket->Profile.setValue(cut, {""});
    pocket->Type.setValue("ThroughAll");
    pocket->Midplane.setValue(true);
    _doc->recompute();

    auto solids = solidsOf(pocket);
    ASSERT_EQ(solids.size(), 2U);
    const std::set<std::string> pa = PartDesign::Body::provenanceOfSolid(solids[0]);
    const std::set<std::string> pb = PartDesign::Body::provenanceOfSolid(solids[1]);
    EXPECT_FALSE(pa.empty());
    EXPECT_FALSE(pb.empty());
    EXPECT_FALSE(intersect(pa, pb).empty())
        << "severed halves share base-bar ancestry, so their provenance must overlap";
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
