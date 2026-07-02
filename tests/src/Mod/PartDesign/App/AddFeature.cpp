// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/PartDesign/App/Body.h>
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
        _doc = App::GetApplication().newDocument("AddFeature_test", "testUser");
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
