// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Locks the ARCHITECTURE §8.6 Spatial Interference detector implemented by
// Body::findInterferingPairs: every unordered pair of distinct Bodies whose shapes share positive
// volume. Two Bodies may occupy overlapping space without being topologically merged (§4.8) — valid
// but usually unintended (keep-distinct pattern instances that coincide are the motivating case,
// #34). The detector is pure geometry: it reports the pairs and touches no state (per §8.6
// detection is a non-blocking UI concern, never a recompute failure). These tests exist so it
// cannot start missing a real overlap, or counting mere surface contact / disjoint Bodies as
// interference.

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <utility>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class FindInterferingPairsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Interfere_test", "testUser", {.documentType = "Part"});
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    // A 10×10×10 box Body: a rectangle [x0..x0+10]×[0..10] on XY, padded 10 in +Z.
    PartDesign::Body* makeBoxBody(const char* name, double x0)
    {
        auto* body = _doc->addObject<PartDesign::Body>(name);

        auto* sk = _doc->addObject<Sketcher::SketchObject>();
        body->addFeature(sk);
        sk->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        sk->MapMode.setValue("FlatFace");
        const std::vector<Base::Vector3d> corners {
            Base::Vector3d(x0, 0, 0),
            Base::Vector3d(x0 + 10, 0, 0),
            Base::Vector3d(x0 + 10, 10, 0),
            Base::Vector3d(x0, 10, 0)
        };
        for (std::size_t i = 0; i < corners.size(); ++i) {
            Part::GeomLineSegment seg;
            seg.setPoints(corners[i], corners[(i + 1) % corners.size()]);
            sk->addGeometry(&seg, false);
        }

        auto* pad = _doc->addObject<PartDesign::Pad>();
        body->addFeature(pad);
        pad->Profile.setValue(sk, {""});
        pad->Length.setValue(10.0);

        _doc->recompute();
        return body;
    }

    App::Document* _doc = nullptr;
};

TEST(FindInterferingPairsNullDoc, NullDocumentYieldsNoPairs)
{
    EXPECT_TRUE(PartDesign::Body::findInterferingPairs(nullptr).empty());
}

TEST_F(FindInterferingPairsTest, DisjointBodiesDoNotInterfere)
{
    makeBoxBody("A", 0.0);
    makeBoxBody("B", 40.0);  // 30 units of clear air between them
    EXPECT_TRUE(PartDesign::Body::findInterferingPairs(_doc).empty());
}

TEST_F(FindInterferingPairsTest, FaceContactIsNotInterference)
{
    makeBoxBody("A", 0.0);
    makeBoxBody("B", 10.0);  // flush on the X = 10 plane: shared face, zero shared volume
    EXPECT_TRUE(PartDesign::Body::findInterferingPairs(_doc).empty());
}

TEST_F(FindInterferingPairsTest, OverlappingBodiesInterfere)
{
    auto* a = makeBoxBody("A", 0.0);
    auto* b = makeBoxBody("B", 5.0);  // share a 5×10×10 slab
    const auto pairs = PartDesign::Body::findInterferingPairs(_doc);
    ASSERT_EQ(pairs.size(), 1U);
    // Reported as the one unordered {A, B} pair, order unspecified.
    const bool matches = (pairs[0].first == a && pairs[0].second == b)
        || (pairs[0].first == b && pairs[0].second == a);
    EXPECT_TRUE(matches);
}

TEST_F(FindInterferingPairsTest, OnlyOverlappingPairIsReported)
{
    makeBoxBody("A", 0.0);
    makeBoxBody("B", 5.0);   // overlaps A
    makeBoxBody("C", 60.0);  // clear of both
    EXPECT_EQ(PartDesign::Body::findInterferingPairs(_doc).size(), 1U);
}

TEST_F(FindInterferingPairsTest, SingleBodyDoesNotInterfereWithItself)
{
    makeBoxBody("A", 0.0);
    EXPECT_TRUE(PartDesign::Body::findInterferingPairs(_doc).empty());
}

TEST_F(FindInterferingPairsTest, DismissedOverlapDropsFromLivePairsButNotRaw)
{
    auto* a = makeBoxBody("A", 0.0);
    auto* b = makeBoxBody("B", 5.0);

    // Before dismissal the overlap is both detected and live.
    ASSERT_EQ(PartDesign::Body::findInterferingPairs(_doc).size(), 1U);
    ASSERT_EQ(PartDesign::Body::liveInterferingPairs(_doc).size(), 1U);
    EXPECT_FALSE(PartDesign::Body::isInterferenceDismissed(a, b));

    PartDesign::Body::dismissInterference(a, b);

    // The raw detector still sees the geometry; only the live (user-facing) list drops it.
    EXPECT_TRUE(PartDesign::Body::isInterferenceDismissed(a, b));
    EXPECT_EQ(PartDesign::Body::findInterferingPairs(_doc).size(), 1U);
    EXPECT_TRUE(PartDesign::Body::liveInterferingPairs(_doc).empty());
}

TEST_F(FindInterferingPairsTest, DismissalIsSymmetricAndRecordedOnBothBodies)
{
    auto* a = makeBoxBody("A", 0.0);
    auto* b = makeBoxBody("B", 5.0);
    PartDesign::Body::dismissInterference(a, b);

    // Recorded on both sides, keyed by durable Uid, and queryable in either order.
    EXPECT_EQ(a->AcknowledgedOverlaps.getValues().size(), 1U);
    EXPECT_EQ(b->AcknowledgedOverlaps.getValues().size(), 1U);
    EXPECT_EQ(a->AcknowledgedOverlaps.getValues().front(), b->Uid.getValueStr());
    EXPECT_EQ(b->AcknowledgedOverlaps.getValues().front(), a->Uid.getValueStr());
    EXPECT_TRUE(PartDesign::Body::isInterferenceDismissed(b, a));
}

TEST_F(FindInterferingPairsTest, DismissIsIdempotent)
{
    auto* a = makeBoxBody("A", 0.0);
    auto* b = makeBoxBody("B", 5.0);
    PartDesign::Body::dismissInterference(a, b);
    PartDesign::Body::dismissInterference(a, b);
    EXPECT_EQ(a->AcknowledgedOverlaps.getValues().size(), 1U);
}

TEST_F(FindInterferingPairsTest, DismissingOneOverlapLeavesAnotherLive)
{
    auto* a = makeBoxBody("A", 0.0);
    auto* b = makeBoxBody("B", 5.0);  // overlaps A
    makeBoxBody("C", 8.0);            // overlaps A and B

    // Three mutually overlapping boxes => three pairs.
    ASSERT_EQ(PartDesign::Body::liveInterferingPairs(_doc).size(), 3U);
    PartDesign::Body::dismissInterference(a, b);
    // Only the A–B pair is silenced; A–C and B–C remain.
    EXPECT_EQ(PartDesign::Body::liveInterferingPairs(_doc).size(), 2U);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
