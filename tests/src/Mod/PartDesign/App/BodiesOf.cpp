// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>

class BodiesOfTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("BodiesOf_test", "testUser", {.documentType = "Part"});
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    PartDesign::Pad* step(App::DocumentObject* base, long copy)
    {
        auto* pad = _doc->addObject<PartDesign::Pad>();
        pad->BaseFeature.setValue(base);
        pad->BaseInstance.setValue(copy);
        return pad;
    }

    PartDesign::Body* bodyTippedAt(App::DocumentObject* tip)
    {
        auto* body = _doc->addObject<PartDesign::Body>();
        body->Tip.setValue(tip);
        return body;
    }

    App::Document* _doc = nullptr;
};

// Every copy of a pattern carries its own step, so no Body ends at the pattern itself. A feature
// before the pattern feeds every copy, so it belongs to every copy's Body.
TEST_F(BodiesOfTest, FeatureBeforeAPatternBelongsToEveryCopysBody)
{
    auto* root = step(nullptr, -1);
    auto* pattern = step(root, -1);
    auto* onFirst = bodyTippedAt(step(pattern, 0));
    auto* onSecond = bodyTippedAt(step(pattern, 1));

    auto bodies = PartDesign::Body::bodiesOf(root);
    std::ranges::sort(bodies);
    std::vector<PartDesign::Body*> expected {onFirst, onSecond};
    std::ranges::sort(expected);
    EXPECT_EQ(bodies, expected);
}

// One copy still ends at the pattern, the other carries a step: both copies' Bodies count.
TEST_F(BodiesOfTest, BodyAtThePatternDoesNotHideTheStepOnAnotherCopy)
{
    auto* root = step(nullptr, -1);
    auto* pattern = step(root, -1);
    auto* atPattern = bodyTippedAt(pattern);
    auto* onFirst = bodyTippedAt(step(pattern, 0));

    auto bodies = PartDesign::Body::bodiesOf(root);
    std::ranges::sort(bodies);
    std::vector<PartDesign::Body*> expected {atPattern, onFirst};
    std::ranges::sort(expected);
    EXPECT_EQ(bodies, expected);
    EXPECT_TRUE(PartDesign::Body::backsBody(pattern, onFirst));
}

// A Body built on another Body's Tip does not claim the features before that Tip.
TEST_F(BodiesOfTest, NextBodyAcrossASeamDoesNotClaimTheEarlierOne)
{
    auto* root = step(nullptr, -1);
    auto* first = bodyTippedAt(root);
    bodyTippedAt(step(root, -1));
    EXPECT_EQ(PartDesign::Body::bodiesOf(root), std::vector<PartDesign::Body*> {first});
}

TEST_F(BodiesOfTest, PlainChainBelongsToTheOneBody)
{
    auto* root = step(nullptr, -1);
    auto* body = bodyTippedAt(step(root, -1));
    EXPECT_EQ(PartDesign::Body::bodiesOf(root), std::vector<PartDesign::Body*> {body});
}
