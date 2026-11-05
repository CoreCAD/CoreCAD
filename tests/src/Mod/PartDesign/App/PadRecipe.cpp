// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/ObjectRecipe.h>
#include <App/Recipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureFillet.h>
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

// Each branch rounds one more edge of the pad. The order of a fillet's edges means nothing, so
// both new edges join the merged fillet rather than clashing as one changed list. Control: a link
// whose order may matter still conflicts when both sides change it (ObjectRecipe's re-chosen-face
// test).
TEST_F(PadRecipeTest, eachBranchsNewFilletEdgeJoinsTheMergedFillet)
{
    auto* fillet = _doc->addObject<PartDesign::Fillet>("Fillet");
    _body->addFeature(fillet);
    fillet->Radius.setValue(1.0);
    const auto filletOn = [&](const std::vector<std::string>& edges) {
        fillet->Base.setValue(_pad, edges);
        _doc->recompute();
        return App::emitObjectRecipe(*fillet);
    };

    App::RecipeSection base;
    App::RecipeSection branchA;
    App::RecipeSection branchB;
    const App::RecipeNode node = filletOn({"Edge1"});
    base[node.id] = node;
    branchA[node.id] = filletOn({"Edge1", "Edge3"});
    branchB[node.id] = filletOn({"Edge1", "Edge2"});
    ASSERT_EQ(node.unorderedLinks.count("Base"), 1u);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection merged = App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);
    const std::vector<App::MergeConflict> refined
        = App::RecipeMerge::refineConflicts(conflicts, base, branchA, branchB, merged);

    EXPECT_TRUE(refined.empty());
    std::set<std::string> edges;
    for (const App::RecipeRef& ref : merged.at(node.id).refs) {
        if (ref.link == "Base" && !ref.elementLabel.empty()) {
            edges.insert(ref.elementLabel);
        }
    }
    EXPECT_EQ(edges, (std::set<std::string> {"Edge1", "Edge2", "Edge3"}));
}

// A moves the fillet onto another solid; B rounds one more edge of the old one. A fillet rounds
// edges of one solid, so the merge cannot keep both: it asks, rather than leaving edges of two
// solids in a link that holds one.
TEST_F(PadRecipeTest, aFilletMovedOnOneBranchAndGivenAnEdgeOnTheOtherConflicts)
{
    auto* fillet = _doc->addObject<PartDesign::Fillet>("Fillet");
    _body->addFeature(fillet);
    auto* other = _doc->addObject("Part::Box", "Other");
    const auto filletOn = [&](App::DocumentObject* solid, const std::vector<std::string>& edges) {
        fillet->Base.setValue(solid, edges);
        return App::emitObjectRecipe(*fillet);
    };

    App::RecipeSection base;
    App::RecipeSection branchA;
    App::RecipeSection branchB;
    const App::RecipeNode node = filletOn(_pad, {"Edge1"});
    base[node.id] = node;
    branchA[node.id] = filletOn(other, {"Edge1"});
    branchB[node.id] = filletOn(_pad, {"Edge1", "Edge2"});

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection merged = App::RecipeMerge::threeWay(base, branchA, branchB, conflicts);
    const std::vector<App::MergeConflict> refined
        = App::RecipeMerge::refineConflicts(conflicts, base, branchA, branchB, merged);

    ASSERT_EQ(refined.size(), 1u);
    EXPECT_NE(refined.front().detail.find("Base"), std::string::npos) << refined.front().detail;
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
