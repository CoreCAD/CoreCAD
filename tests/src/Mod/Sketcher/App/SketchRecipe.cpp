// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Cruth contributors

/* End-to-end proof of the recipe-merge component's first driver: the sketch authored-emit
 * (Sketcher::emitSketchRecipe) feeding the general engine (App::RecipeMerge). Each test
 * builds an ancestor sketch, reloads it into two independent documents that SHARE the
 * ancestor's durable identity (reload preserves tags; a copy would mint fresh ones), edits
 * each into a branch, emits all three recipes, and merges them with nothing running.
 *
 * The three cases the design names (DESIGN_recipe-merge.md §6): a position-independent clean
 * merge, a value conflict, and a dangling reference ("merges clean, regenerates broken").
 * Each carries a negative control so a check cannot pass by always reporting the same thing.
 */

#include <gtest/gtest.h>

#include <FCConfig.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Recipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/Sketcher/App/SketchRecipe.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace
{

using Sketcher::SketchRecipe;

class SketchRecipeMergeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (App::Application::GetARGC() == 0) {
            constexpr int argc = 1;
            std::array<char*, argc> argv {const_cast<char*>("FreeCAD")};
            App::Application::Config()["ExeName"] = "FreeCAD";
            App::Application::init(argc, argv.data());
        }
    }

    void TearDown() override
    {
        App::GetApplication().closeAllDocuments();
        for (const auto& path : _tempFiles) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        _tempFiles.clear();
    }

    std::string tempPath(const char* leaf)
    {
        auto p = (std::filesystem::temp_directory_path() / leaf).string();
        _tempFiles.push_back(p);
        return p;
    }

    static Sketcher::SketchObject* sketchOf(App::Document* doc)
    {
        return static_cast<Sketcher::SketchObject*>(doc->getObject("S"));
    }

    /// Two branch sketches that share their ancestor's durable identity, plus the ancestor's
    /// emitted recipe.
    struct Branches
    {
        Sketcher::SketchObject* a {nullptr};
        Sketcher::SketchObject* b {nullptr};
        SketchRecipe base;
    };

    /// Build an ancestor via `buildFn`, capture its recipe, then reload it into two
    /// independent documents (a byte copy of the same file) so both branches descend from a
    /// common ancestor with identical durable tags.
    Branches makeBranches(const std::function<void(Sketcher::SketchObject*)>& buildFn)
    {
        auto* doc = App::GetApplication().newDocument("recipeAncestor");
        auto* ancestor = static_cast<Sketcher::SketchObject*>(
            doc->addObject("Sketcher::SketchObject", "S")
        );
        buildFn(ancestor);
        doc->recompute();

        Branches out;
        out.base = Sketcher::emitSketchRecipe(*ancestor);

        const std::string baseP = tempPath("recipe_base.FCStd");
        const std::string aP = tempPath("recipe_a.FCStd");
        const std::string bP = tempPath("recipe_b.FCStd");
        doc->saveAs(baseP.c_str());
        std::error_code ec;
        std::filesystem::copy_file(baseP, aP, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::copy_file(baseP, bP, std::filesystem::copy_options::overwrite_existing, ec);

        out.a = sketchOf(App::GetApplication().openDocument(aP.c_str()));
        out.b = sketchOf(App::GetApplication().openDocument(bP.c_str()));
        return out;
    }

    std::vector<std::string> _tempFiles;
};

// --- helpers to author sketch content --------------------------------------------------

int addLine(Sketcher::SketchObject* sketch, double x1, double y1, double x2, double y2)
{
    Part::GeomLineSegment g;
    g.setPoints(Base::Vector3d(x1, y1, 0), Base::Vector3d(x2, y2, 0));
    return sketch->addGeometry(&g);
}

int addDistance(Sketcher::SketchObject* sketch, int geoId, double value)
{
    Sketcher::Constraint c;
    c.Type = Sketcher::Distance;
    c.setElement(0, Sketcher::GeoElementId(geoId, Sketcher::PointPos::none));
    c.setValue(value);
    const int idx = sketch->addConstraint(&c);
    sketch->getDocument()->recompute();
    return idx;
}

void addHorizontal(Sketcher::SketchObject* sketch, int geoId)
{
    Sketcher::Constraint c;
    c.Type = Sketcher::Horizontal;
    c.setElement(0, Sketcher::GeoElementId(geoId, Sketcher::PointPos::none));
    sketch->addConstraint(&c);
    sketch->getDocument()->recompute();
}

// --- the three cases -------------------------------------------------------------------

// Clean, position-independent: A deletes an EARLY entity (renumbering the rest), B adds a new
// one. Keyed by durable tag the two edits never collide — even though the same surviving
// entity sits at a different list position in each branch. A position-keyed merge could not
// tell B's untouched entities from A's deletion; this one merges clean.
TEST_F(SketchRecipeMergeTest, cleanMergeIsPositionIndependent)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);
        addLine(s, 10, 0, 10, 10);
        addLine(s, 10, 10, 0, 10);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);

    // Tags of the three ancestor lines, in GeoId order.
    const std::string tag0 = boost::uuids::to_string(br.a->getInternalGeometry()[0]->getTag());
    const std::string tag1 = boost::uuids::to_string(br.a->getInternalGeometry()[1]->getTag());
    const std::string tag2 = boost::uuids::to_string(br.a->getInternalGeometry()[2]->getTag());

    // Branch A: delete the first line -> the surviving lines renumber down one position.
    br.a->delGeometry(0);
    br.a->getDocument()->recompute();
    // Metric-distinguishing precondition: the SAME durable entity now sits at a different
    // GeoId in A than in B, so a position-keyed merge would misalign the two branches.
    EXPECT_EQ(boost::uuids::to_string(br.a->getInternalGeometry()[0]->getTag()), tag1);
    EXPECT_EQ(boost::uuids::to_string(br.b->getInternalGeometry()[1]->getTag()), tag1);

    // Branch B: add a fourth line (its own fresh tag), everything else untouched.
    addLine(br.b, 0, 10, 0, 0);
    br.b->getDocument()->recompute();
    const std::string tag3 = boost::uuids::to_string(br.b->getInternalGeometry()[3]->getTag());

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection merged
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);

    EXPECT_TRUE(conflicts.empty()) << "clean, non-overlapping edits must not conflict";
    // Result = ancestor minus A's deletion plus B's addition: {L1, L2, L3}, and NOT L0.
    EXPECT_EQ(merged.size(), 3U);
    EXPECT_EQ(merged.count(tag0), 0U) << "A's deletion did not carry through";
    EXPECT_EQ(merged.count(tag1), 1U);
    EXPECT_EQ(merged.count(tag2), 1U);
    EXPECT_EQ(merged.count(tag3), 1U) << "B's addition did not carry through";
}

// A dimensional constraint edited to different values on the two branches is a Value conflict;
// the same engine, given only one edit, takes it without conflict (the negative control).
TEST_F(SketchRecipeMergeTest, divergingValueIsAConflict)
{
    int constrId = -1;
    Branches br = makeBranches([&constrId](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 40, 0);
        constrId = addDistance(s, 0, 40.0);  // constrain the line's length
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_GE(constrId, 0);
    ASSERT_EQ(br.base.constraints.size(), 1U);
    const std::string ctag = br.base.constraints.begin()->first;

    br.a->setDatum(constrId, 50.0);
    br.b->setDatum(constrId, 60.0);
    br.a->getDocument()->recompute();
    br.b->getDocument()->recompute();

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    // Both branches moved the same authored value off the ancestor, differently -> conflict.
    std::vector<App::MergeConflict> conflicts;
    App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    ASSERT_EQ(conflicts.size(), 1U);
    EXPECT_EQ(conflicts.front().kind, App::MergeConflict::Kind::Value);
    EXPECT_EQ(conflicts.front().id, ctag);

    // Control: only A moved (B == ancestor) -> the engine takes A's value, no conflict.
    std::vector<App::MergeConflict> controlConflicts;
    App::RecipeSection merged = App::RecipeMerge::threeWay(
        br.base.constraints,
        a.constraints,
        br.base.constraints,
        controlConflicts
    );
    EXPECT_TRUE(controlConflicts.empty()) << "a single-sided edit is not a conflict";
    ASSERT_EQ(merged.count(ctag), 1U);
    EXPECT_EQ(merged.at(ctag).fields.at("value"), a.constraints.at(ctag).fields.at("value"));
}

// The CAD-specific conflict with no text analogy: A deletes geometry that B constrains. The
// sections each merge clean, but the surviving constraint dangles onto the deleted line ->
// a Referential conflict. Control: the same check against a live target does not fire.
TEST_F(SketchRecipeMergeTest, danglingReferenceIsReferentialConflict)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);
        addLine(s, 0, 5, 10, 5);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    const std::string tag1 = boost::uuids::to_string(br.a->getInternalGeometry()[1]->getTag());

    br.a->delGeometry(1);  // A retires the second line
    br.a->getDocument()->recompute();
    addHorizontal(br.b, 1);  // B constrains it (reference bound to L1's durable tag)

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection mergedGeom
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);
    App::RecipeSection mergedCons
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    EXPECT_TRUE(conflicts.empty()) << "each section merges clean on its own";
    EXPECT_EQ(mergedGeom.count(tag1), 0U) << "A's deletion of L1 carried through";
    EXPECT_EQ(mergedCons.size(), 1U) << "B's new constraint carried through";

    // The dangling reference only shows once the sections are checked against each other.
    std::vector<App::MergeConflict> refConflicts;
    App::RecipeMerge::checkReferences(mergedCons, mergedGeom, refConflicts);
    ASSERT_EQ(refConflicts.size(), 1U);
    EXPECT_EQ(refConflicts.front().kind, App::MergeConflict::Kind::Referential);

    // Control: the identical constraint against geometry where L1 still lives does not dangle.
    std::vector<App::MergeConflict> liveConflicts;
    App::RecipeMerge::checkReferences(mergedCons, br.base.geometry, liveConflicts);
    EXPECT_TRUE(liveConflicts.empty()) << "a reference to a live target is not a conflict";
}

}  // namespace
