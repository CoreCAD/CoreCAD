// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureLinearPattern.h>
#include <Mod/PartDesign/App/FeatureMultiTransform.h>
#include <Mod/PartDesign/App/FeaturePad.h>

// Cruth #18: Body::addFeature owns the pipeline wiring (BaseFeature chain + Tip). A caller
// that pre-sets BaseFeature on the incoming feature used to trigger a self-cycle: the
// successor scan (getNextSolidFeatureByChain) found the new feature itself as the current
// Tip's successor, so the mid-chain reroute set feature.BaseFeature = feature and recompute
// died with "The graph must be a DAG". addFeature now clears any pre-set BaseFeature before
// the scan. These tests lock in the corrected wiring; no geometry/recompute is required
// because the pipeline is derived from the chain, not from shape output.

class AddFeatureTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("AddFeature_test", "testUser", {.documentType = "Part"});
        _body = _doc->addObject<PartDesign::Body>();
        _pad1 = _doc->addObject<PartDesign::Pad>("Pad1");
        _body->addFeature(_pad1);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    PartDesign::Pad* _pad1 = nullptr;
};

// A feature whose BaseFeature is pre-set to the current Tip must not self-cycle: addFeature
// clears it, then rewires cleanly so the new feature extends the Tip.
TEST_F(AddFeatureTest, PreSetBaseFeatureDoesNotSelfCycle)
{
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    pad2->BaseFeature.setValue(_pad1);  // the misuse trap: pre-wire to the Tip

    _body->addFeature(pad2);

    // Rewired to extend Pad1, not itself.
    EXPECT_EQ(pad2->BaseFeature.getValue(), _pad1);
    EXPECT_NE(pad2->BaseFeature.getValue(), pad2);
    EXPECT_EQ(_body->Tip.getValue(), pad2);
}

// Pre-setting BaseFeature to an unrelated/stale feature is likewise cleared, not honored:
// addFeature is authoritative over the chain position.
TEST_F(AddFeatureTest, PreSetBaseFeatureIsOverriddenByChainPosition)
{
    auto* stray = _doc->addObject<PartDesign::Pad>("Stray");
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    pad2->BaseFeature.setValue(stray);  // bogus pre-wire

    _body->addFeature(pad2);

    // addFeature re-homes it onto the real Tip regardless of the stale pre-set value.
    EXPECT_EQ(pad2->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(_body->Tip.getValue(), pad2);
}

// Baseline sanity: normal append (no pre-set) still wires Pad2 -> Pad1 with Tip advanced.
TEST_F(AddFeatureTest, NormalAppendWiresChain)
{
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    _body->addFeature(pad2);

    EXPECT_EQ(pad2->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(_pad1->BaseFeature.getValue(), nullptr);
    EXPECT_EQ(_body->Tip.getValue(), pad2);
}

// A new pattern is a step like any other: it takes the Tip at once, before it is configured.
TEST_F(AddFeatureTest, UnconfiguredPatternTakesTheTip)
{
    auto* lp = _doc->addObject<PartDesign::LinearPattern>("LP");
    _body->addFeature(lp);

    EXPECT_EQ(lp->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(_body->Tip.getValue(), lp);
}

// A pattern inserted mid-chain (the Tip rolled back to an earlier feature) must splice, not
// fork: the feature that came after the insert point is rerouted onto the pattern.
TEST_F(AddFeatureTest, PatternInsertedMidChainKeepsTheTail)
{
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    _body->addFeature(pad2);
    _body->Tip.setValue(_pad1);

    auto* lp = _doc->addObject<PartDesign::LinearPattern>("LP");
    _body->addFeature(lp);

    EXPECT_EQ(lp->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(pad2->BaseFeature.getValue(), lp);
    EXPECT_EQ(_body->Tip.getValue(), lp);
}

// Cruth #142: the MultiTransform panel adds a child to the Body, then lists it. Listing it
// takes it off the chain, so the MultiTransform is the Tip again.
TEST_F(AddFeatureTest, ListingAPatternInAMultiTransformTakesItOffTheChain)
{
    auto* multi = _doc->addObject<PartDesign::MultiTransform>("MT");
    _body->addFeature(multi);
    auto* lp = _doc->addObject<PartDesign::LinearPattern>("LP");
    _body->addFeature(lp);
    ASSERT_EQ(_body->Tip.getValue(), lp);

    multi->Transformations.setValues({lp});

    EXPECT_TRUE(lp->isMultiTransformChild());
    EXPECT_FALSE(PartDesign::Body::isSolidFeature(lp));
    EXPECT_EQ(lp->BaseFeature.getValue(), nullptr);
    EXPECT_EQ(_body->Tip.getValue(), multi);
}

// Converting a pattern mid-chain: the MultiTransform goes in ahead of it, then lists it. What
// built on the pattern builds on the MultiTransform.
TEST_F(AddFeatureTest, ConvertingAPatternMidChainKeepsTheTail)
{
    auto* lp = _doc->addObject<PartDesign::LinearPattern>("LP");
    _body->addFeature(lp);
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    _body->addFeature(pad2);
    _body->Tip.setValue(_pad1);

    auto* multi = _doc->addObject<PartDesign::MultiTransform>("MT");
    _body->addFeature(multi);
    multi->Transformations.setValues({lp});

    EXPECT_EQ(multi->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(pad2->BaseFeature.getValue(), multi);
    EXPECT_EQ(lp->BaseFeature.getValue(), nullptr);
}

// Cruth #37: a folder is the user's filing and a body's membership is a reference, so
// joining a body leaves the feature in its folder. Both still hold it afterwards.
TEST_F(AddFeatureTest, JoiningABodyKeepsTheFeatureInItsFolder)
{
    auto* folder = _doc->addObject<App::DocumentObjectGroup>("Folder");
    auto* pad2 = _doc->addObject<PartDesign::Pad>("Pad2");
    folder->addObject(pad2);

    _body->addFeature(pad2);

    EXPECT_TRUE(folder->hasObject(pad2));
    EXPECT_EQ(pad2->BaseFeature.getValue(), _pad1);
    EXPECT_EQ(_body->Tip.getValue(), pad2);
}
