// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// Cruth §8.5 (#32): which Body a feature could merge into is an INDEPENDENT question,
// answered by walking the feature's anchor chain. It was previously derived from the body
// the feature already extended, which made the "Merge with existing body" control one-way:
// once a feature stood alone there was no remembered target, so the checkbox greyed out and
// the user could no longer change their mind.
//
// These tests pin the rule across the three arrangements the ticket asked for -- no body
// reachable, exactly one, and several -- plus the two cases that decide whether the control
// is offered at all.

class ResolveMergeCandidateTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication()
                   .newDocument("ResolveMergeCandidate_test", "testUser", {.documentType = "Part"});

        // Body A: a two-feature chain, so Pad2 extends Pad1.
        _bodyA = _doc->addObject<PartDesign::Body>("BodyA");
        _padA1 = _doc->addObject<PartDesign::Pad>("PadA1");
        _bodyA->addFeature(_padA1);
        _padA2 = _doc->addObject<PartDesign::Pad>("PadA2");
        _bodyA->addFeature(_padA2);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    // A Pad standing alone as the first (and only) solid feature of its own Body -- the
    // arrangement whose merge candidate used to be unanswerable.
    PartDesign::Pad* standaloneePadIn(PartDesign::Body* body, const char* name)
    {
        auto* pad = _doc->addObject<PartDesign::Pad>(name);
        body->addFeature(pad);
        return pad;
    }

    // Attach `sketch` to `anchor` so the anchor walk reaches whatever Body owns it, and
    // hand the sketch to `pad` as its profile.
    void anchorProfile(
        PartDesign::Pad* pad,
        Sketcher::SketchObject* sketch,
        const std::vector<App::DocumentObject*>& anchors
    )
    {
        std::vector<std::string> subs(anchors.size(), std::string());
        sketch->AttachmentSupport.setValues(anchors, subs);
        pad->Profile.setValue(sketch, std::vector<std::string>());
    }

    Sketcher::SketchObject* newSketch(const char* name)
    {
        return _doc->addObject<Sketcher::SketchObject>(name);
    }

    App::Document* _doc = nullptr;
    PartDesign::Body* _bodyA = nullptr;
    PartDesign::Pad* _padA1 = nullptr;
    PartDesign::Pad* _padA2 = nullptr;
};

// A feature that extends a chain reports the Body it extends: that is the target to come
// back to when the box is unticked and ticked again.
TEST_F(ResolveMergeCandidateTest, ExtendingFeatureReportsTheBodyItExtends)
{
    EXPECT_EQ(_padA2->BaseFeature.getValue(), _padA1);
    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(_padA2), _bodyA);
}

// ONE body reachable -- the defect. A Pad standing alone in its own Body, whose profile is
// anchored to a feature of Body A, can merge into Body A. The old rule answered nullptr
// here (no BaseFeature ⇒ nothing remembered) and the checkbox locked.
TEST_F(ResolveMergeCandidateTest, StandaloneFeatureAnchoredToAnotherBodyOffersThatBody)
{
    auto* bodyB = _doc->addObject<PartDesign::Body>("BodyB");
    auto* padB = standaloneePadIn(bodyB, "PadB");
    anchorProfile(padB, newSketch("SketchB"), {_padA1});

    ASSERT_EQ(padB->BaseFeature.getValue(), nullptr) << "PadB must stand alone for this case";
    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(padB), _bodyA);
}

// NO body reachable: a profile anchored to nothing leaves the default unset (§8.5 -- the
// feature spawns its own Body and no merge is offered).
TEST_F(ResolveMergeCandidateTest, StandaloneFeatureWithUnanchoredProfileOffersNothing)
{
    auto* bodyB = _doc->addObject<PartDesign::Body>("BodyB");
    auto* padB = standaloneePadIn(bodyB, "PadB");
    anchorProfile(padB, newSketch("SketchB"), {});

    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(padB), nullptr);
}

// SEVERAL bodies reachable: §8.3 ambiguity. Answering with one of them would be a silent
// pick, which is exactly what the law forbids -- the picker resolves it instead.
TEST_F(ResolveMergeCandidateTest, StandaloneFeatureAnchoredToSeveralBodiesOffersNothing)
{
    auto* bodyB = _doc->addObject<PartDesign::Body>("BodyB");
    auto* padB = standaloneePadIn(bodyB, "PadB");

    auto* bodyC = _doc->addObject<PartDesign::Body>("BodyC");
    auto* padC = standaloneePadIn(bodyC, "PadC");

    auto* bodyD = _doc->addObject<PartDesign::Body>("BodyD");
    auto* padD = standaloneePadIn(bodyD, "PadD");
    anchorProfile(padD, newSketch("SketchD"), {_padA1, padB, padC});

    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(padD), nullptr);
}

// A candidate the feature already lives in is not something to merge into -- offering it
// would present a toggle that does nothing.
TEST_F(ResolveMergeCandidateTest, CandidateThatIsTheFeaturesOwnBodyOffersNothing)
{
    auto* bodyB = _doc->addObject<PartDesign::Body>("BodyB");
    auto* padB1 = standaloneePadIn(bodyB, "PadB1");

    // PadB2 stands alone at the head of no chain but lives in Body B, anchored to Body B.
    auto* padB2 = _doc->addObject<PartDesign::Pad>("PadB2");
    bodyB->addFeature(padB2);
    padB2->BaseFeature.setValue(nullptr);
    anchorProfile(padB2, newSketch("SketchB2"), {padB1});

    ASSERT_EQ(PartDesign::Body::findBodyOf(padB2), bodyB);
    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(padB2), nullptr);
}

// Nothing to walk, nothing to answer.
TEST_F(ResolveMergeCandidateTest, NullFeatureOffersNothing)
{
    EXPECT_EQ(PartDesign::Body::resolveMergeCandidate(nullptr), nullptr);
}
