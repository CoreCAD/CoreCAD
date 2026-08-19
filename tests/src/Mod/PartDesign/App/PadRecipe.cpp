// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/ObjectRecipe.h>
#include <App/Recipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <set>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// Proves the generic App::emitObjectRecipe driver generalizes off the sketch to a real solid
// feature (a Pad), and that the node it produces flows through the existing App::RecipeMerge.
class PadRecipeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("PadRecipe_test", "testUser", {.documentType = "Part"});
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addFeature(_sketch);
        _sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        _sketch->MapMode.setValue("FlatFace");
        Part::GeomCircle circle;
        circle.setRadius(10.0);
        _sketch->addGeometry(&circle, false);
        _doc->recompute();

        _pad = _doc->addObject<PartDesign::Pad>("Pad");
        _body->addFeature(_pad);
        _pad->Profile.setValue(_sketch, {""});
        _pad->Length.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    PartDesign::Pad* _pad = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(PadRecipeTest, padEmitsIdentityAuthoredFieldsAndProfileRef)
{
    const App::RecipeNode node = App::emitObjectRecipe(*_pad);

    // Identity is the durable Uid; type is the real feature type.
    EXPECT_EQ(node.id, _pad->Uid.getValueStr());
    EXPECT_EQ(node.type, "PartDesign::Pad");

    // Authored parameters are emitted: the length as a unit-typed literal, the type as its enum
    // token (never the raw enum index).
    ASSERT_EQ(node.fields.count("Length"), 1u);
    EXPECT_EQ(node.fields.at("Length"), "10 mm");
    ASSERT_EQ(node.fields.count("Type"), 1u);
    EXPECT_FALSE(node.fields.at("Type").empty());

    // The profile is a reference to the sketch, addressed by the sketch's durable Uid.
    std::set<std::string> refTargets;
    for (const auto& ref : node.refs) {
        refTargets.insert(ref.target);
    }
    EXPECT_EQ(refTargets.count(_sketch->Uid.getValueStr()), 1u);
}

TEST_F(PadRecipeTest, loneLengthEditMergesCleanlyThroughRecipeMerge)
{
    const App::RecipeNode node = App::emitObjectRecipe(*_pad);

    App::RecipeSection base;
    base[node.id] = node;
    App::RecipeSection branchA = base;
    branchA[node.id].fields["Length"] = "25 mm";  // one branch lengthens the pad
    App::RecipeSection branchB = base;            // the other leaves it alone

    std::vector<App::MergeConflict> conflicts;
    const App::RecipeSection merged = App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);

    EXPECT_TRUE(conflicts.empty());
    EXPECT_EQ(merged.at(node.id).fields.at("Length"), "25 mm");
}

TEST_F(PadRecipeTest, divergentLengthEditsConflict)
{
    const App::RecipeNode node = App::emitObjectRecipe(*_pad);

    App::RecipeSection base;
    base[node.id] = node;
    App::RecipeSection branchA = base;
    branchA[node.id].fields["Length"] = "25 mm";
    App::RecipeSection branchB = base;
    branchB[node.id].fields["Length"] = "30 mm";  // both branches move the same object, differently

    std::vector<App::MergeConflict> conflicts;
    App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts.front().kind, App::MergeConflict::Kind::Value);
    EXPECT_EQ(conflicts.front().id, node.id);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
