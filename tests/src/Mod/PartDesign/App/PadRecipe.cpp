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

// Two branches edit DIFFERENT fields of the same Pad (A its length, B its type). Object-granular
// reports one whole-Pad conflict; the field-granular refinement dissolves it and the merged Pad
// carries both edits. This is the payoff of #1 on a real feature: independent parameter changes
// on one object no longer force a person to arbitrate.
TEST_F(PadRecipeTest, disjointFieldEditsOnOnePadAutoMerge)
{
    const App::RecipeNode node = App::emitObjectRecipe(*_pad);
    ASSERT_EQ(node.fields.count("Length"), 1u);
    ASSERT_EQ(node.fields.count("Type"), 1u);

    App::RecipeSection base;
    base[node.id] = node;
    App::RecipeSection branchA = base;
    branchA[node.id].fields["Length"] = "25 mm";  // A changes the length
    App::RecipeSection branchB = base;
    branchB[node.id].fields["Type"] = "TwoLengths";  // B changes the pad type

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection merged = App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);
    ASSERT_EQ(conflicts.size(), 1u);  // object-granular: one whole-Pad conflict

    const std::vector<App::MergeConflict> refined
        = App::RecipeMerge::refineConflicts(conflicts, base, branchA, branchB, merged);

    EXPECT_TRUE(refined.empty());                                   // dissolved — disjoint fields
    EXPECT_EQ(merged.at(node.id).fields.at("Length"), "25 mm");     // A's edit
    EXPECT_EQ(merged.at(node.id).fields.at("Type"), "TwoLengths");  // and B's, on one Pad
}

// Both branches change the SAME field of the Pad (its length) to different values. The refinement
// keeps it a conflict, now named at the field — this is the case a person must still arbitrate.
TEST_F(PadRecipeTest, sameFieldEditsOnOnePadStayAConflict)
{
    const App::RecipeNode node = App::emitObjectRecipe(*_pad);

    App::RecipeSection base;
    base[node.id] = node;
    App::RecipeSection branchA = base;
    branchA[node.id].fields["Length"] = "25 mm";
    App::RecipeSection branchB = base;
    branchB[node.id].fields["Length"] = "30 mm";

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection merged = App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);

    const std::vector<App::MergeConflict> refined
        = App::RecipeMerge::refineConflicts(conflicts, base, branchA, branchB, merged);

    ASSERT_EQ(refined.size(), 1u);
    EXPECT_EQ(refined.front().id, node.id);
    EXPECT_NE(refined.front().detail.find("Length"), std::string::npos);  // names the field
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
