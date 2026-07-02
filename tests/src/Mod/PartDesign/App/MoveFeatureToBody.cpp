// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>

// Cruth §8.5 (#27): Body::moveFeatureToBody is the model half of the feature-creation
// "Merge result" control — it makes the spawn-vs-extend choice reversible by re-homing a
// feature onto another Body (or onto a freshly spawned one when the target is null). These
// tests lock in the pipeline wiring (BaseFeature chain, Tip, auto-retire) that the GUI and
// Python API both depend on. No geometry/recompute is needed: the helper only rewires the
// pipeline, which is derived from the BaseFeature chain, not from shape output.

class MoveFeatureToBodyTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("MoveFeatureToBody_test", "testUser");
        _bodyA = _doc->addObject<PartDesign::Body>();
        _pad1 = _doc->addObject<PartDesign::Pad>("Pad1");
        _bodyA->addFeature(_pad1);
        _pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
        _bodyA->addFeature(_pad2);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc = nullptr;
    PartDesign::Body* _bodyA = nullptr;
    PartDesign::Pad* _pad1 = nullptr;
    PartDesign::Pad* _pad2 = nullptr;
};

// Baseline: Pad2 extends Pad1, both resolve to Body A, A's Tip is Pad2.
TEST_F(MoveFeatureToBodyTest, InitialChainIsWired)
{
    EXPECT_EQ(_bodyA->Tip.getValue(), _pad2);
    EXPECT_EQ(_pad2->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(_pad1->BaseFeature.getValue(), nullptr);
    EXPECT_EQ(PartDesign::Body::findBodyOf(_pad2), _bodyA);
    EXPECT_EQ(PartDesign::Body::findBodyOf(_pad1), _bodyA);
}

// "Merge result off": null target detaches the Tip feature into a fresh Body. The source
// Body survives with its Tip retreated to the previous feature.
TEST_F(MoveFeatureToBodyTest, ExtendToNewSpawnsBodyAndRetreatsSourceTip)
{
    PartDesign::Body* newBody = PartDesign::Body::moveFeatureToBody(_pad2, nullptr);

    ASSERT_NE(newBody, nullptr);
    EXPECT_NE(newBody, _bodyA);

    // Source Body A retreated to Pad1; still alive.
    EXPECT_EQ(_bodyA->Tip.getValue(), _pad1);
    EXPECT_EQ(PartDesign::Body::findBodyOf(_pad1), _bodyA);

    // Pad2 now heads its own Body, with no base (it starts a fresh chain).
    EXPECT_EQ(newBody->Tip.getValue(), _pad2);
    EXPECT_EQ(_pad2->BaseFeature.getValue(), nullptr);
    EXPECT_EQ(PartDesign::Body::findBodyOf(_pad2), newBody);
}

// "Merge result back on": re-homing the lone feature of a Body onto another Body splices it
// on and auto-retires the now-empty source Body (§4.7).
TEST_F(MoveFeatureToBodyTest, NewToExtendReHomesAndRetiresEmptiedBody)
{
    PartDesign::Body* newBody = PartDesign::Body::moveFeatureToBody(_pad2, nullptr);
    ASSERT_NE(newBody, nullptr);
    const std::string newBodyName = newBody->getNameInDocument();

    PartDesign::Body* result = PartDesign::Body::moveFeatureToBody(_pad2, _bodyA);

    EXPECT_EQ(result, _bodyA);
    EXPECT_EQ(_bodyA->Tip.getValue(), _pad2);
    EXPECT_EQ(_pad2->BaseFeature.getValue(), _pad1);

    // The Body spawned above emptied out and retired itself.
    EXPECT_EQ(_doc->getObject(newBodyName.c_str()), nullptr);
}

// Re-homing a feature onto the Body it already belongs to is a no-op.
TEST_F(MoveFeatureToBodyTest, AlreadyInTargetIsNoOp)
{
    PartDesign::Body* result = PartDesign::Body::moveFeatureToBody(_pad2, _bodyA);

    EXPECT_EQ(result, _bodyA);
    EXPECT_EQ(_bodyA->Tip.getValue(), _pad2);
    EXPECT_EQ(_pad2->BaseFeature.getValue(), _pad1);
}

// A null feature is rejected without side effects.
TEST_F(MoveFeatureToBodyTest, NullFeatureReturnsNull)
{
    EXPECT_EQ(PartDesign::Body::moveFeatureToBody(nullptr, _bodyA), nullptr);
}
