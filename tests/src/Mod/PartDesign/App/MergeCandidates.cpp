// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <algorithm>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// Cruth §8.5 (#26): the inferred merge target is a default, not a verdict. When the anchor
// chain infers nothing -- a sketch drawn on a global plane -- or infers the wrong Body, the
// user reaches the right answer through the "Extend a different body..." picker. This is the
// list that picker offers, and what it must leave out: the Body the feature already lives in
// (a move that would do nothing) and any Body that depends on the feature (a move that would
// make the feature its own ancestor).

class MergeCandidatesTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication()
                   .newDocument("MergeCandidates_test", "testUser", {.documentType = "Part"});

        _bodyA = _doc->addObject<PartDesign::Body>("BodyA");
        _padA = _doc->addObject<PartDesign::Pad>("PadA");
        _bodyA->addFeature(_padA);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    PartDesign::Pad* padInNewBody(const char* bodyName, const char* padName)
    {
        auto* body = _doc->addObject<PartDesign::Body>(bodyName);
        auto* pad = _doc->addObject<PartDesign::Pad>(padName);
        body->addFeature(pad);
        return pad;
    }

    static bool contains(const std::vector<PartDesign::Body*>& bodies, PartDesign::Body* body)
    {
        return std::ranges::find(bodies, body) != bodies.end();
    }

    App::Document* _doc = nullptr;
    PartDesign::Body* _bodyA = nullptr;
    PartDesign::Pad* _padA = nullptr;
};

// The case the picker exists for: a feature whose profile is anchored to nothing infers no
// target at all, yet another Body is sitting right there to be joined.
TEST_F(MergeCandidatesTest, FeatureWithNoInferredTargetIsStillOfferedTheOtherBody)
{
    auto* padB = padInNewBody("BodyB", "PadB");
    padB->Profile.setValue(
        _doc->addObject<Sketcher::SketchObject>("SketchB"),
        std::vector<std::string>()
    );

    ASSERT_EQ(PartDesign::Body::resolveMergeCandidate(padB), nullptr)
        << "the anchor chain must infer nothing for this case to mean anything";

    const auto candidates = PartDesign::Body::mergeCandidates(padB);
    EXPECT_TRUE(contains(candidates, _bodyA));
}

// The Body the feature already lives in is not a destination -- moving it there is a no-op.
TEST_F(MergeCandidatesTest, OwnBodyIsNotACandidate)
{
    auto* padB = padInNewBody("BodyB", "PadB");
    auto* bodyB = PartDesign::Body::findBodyOf(padB);

    const auto candidates = PartDesign::Body::mergeCandidates(padB);
    EXPECT_FALSE(contains(candidates, bodyB));
    EXPECT_TRUE(contains(candidates, _bodyA));
}

// A Body that depends on the feature cannot receive it: splicing the feature onto that
// Body's Tip would make the feature its own ancestor. This is the case the rule is FOR --
// Body C is not PadB's home, it simply reached PadB by having a sketch drawn on it, which
// is what a user does all the time.
TEST_F(MergeCandidatesTest, ABodyThatReachedTheFeatureThroughASketchIsNotACandidate)
{
    auto* padB = padInNewBody("BodyB", "PadB");

    // Body C is built on a face of PadB: its sketch is attached there, so Body C's chain
    // depends on PadB while PadB stays at home in Body B.
    auto* padC = padInNewBody("BodyC", "PadC");
    auto* bodyC = PartDesign::Body::findBodyOf(padC);
    auto* sketchC = _doc->addObject<Sketcher::SketchObject>("SketchC");
    sketchC->AttachmentSupport.setValues({padB}, {std::string()});
    padC->Profile.setValue(sketchC, std::vector<std::string>());

    ASSERT_NE(PartDesign::Body::findBodyOf(padB), bodyC) << "Body C must not be PadB's home";

    const auto candidates = PartDesign::Body::mergeCandidates(padB);
    EXPECT_FALSE(contains(candidates, bodyC))
        << "Body C reaches PadB, so receiving PadB would close a cycle";
    EXPECT_TRUE(contains(candidates, _bodyA)) << "an unrelated Body is still offered";
}

// A Body that has nothing to do with the feature is always a candidate, however many there
// are: the picker is what resolves a choice, so it never has to narrow one down.
TEST_F(MergeCandidatesTest, EveryUnrelatedBodyIsOffered)
{
    auto* padB = padInNewBody("BodyB", "PadB");
    auto* padC = padInNewBody("BodyC", "PadC");
    auto* bodyC = PartDesign::Body::findBodyOf(padC);

    const auto candidates = PartDesign::Body::mergeCandidates(padB);
    EXPECT_TRUE(contains(candidates, _bodyA));
    EXPECT_TRUE(contains(candidates, bodyC));
    EXPECT_EQ(candidates.size(), 2u);
}

// Nothing to ask about, nothing to offer.
TEST_F(MergeCandidatesTest, NullFeatureOffersNothing)
{
    EXPECT_TRUE(PartDesign::Body::mergeCandidates(nullptr).empty());
}
