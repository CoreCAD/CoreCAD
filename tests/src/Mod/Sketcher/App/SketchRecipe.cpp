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

void addCoincident(
    Sketcher::SketchObject* sketch,
    int geoId1,
    Sketcher::PointPos pos1,
    int geoId2,
    Sketcher::PointPos pos2
)
{
    Sketcher::Constraint c;
    c.Type = Sketcher::Coincident;
    c.setElement(0, Sketcher::GeoElementId(geoId1, pos1));
    c.setElement(1, Sketcher::GeoElementId(geoId2, pos2));
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

// Slice 4, end-to-end on real sketches: the dangling reference is not merely flagged but
// RESOLVED through §4.7's honest-retirement outcomes.

// Drop-with-disclosure: A retires L1; B adds a Horizontal whose ONLY subject is L1. After the
// merge nothing the constraint depends on survives, so it resolves to Drop and is erased from
// the merged recipe — the whole-subject case. Control: against geometry where L1 still lives,
// the same constraint carries and nothing is reported.
TEST_F(SketchRecipeMergeTest, danglingWholeSubjectResolvesToDrop)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);
        addLine(s, 0, 5, 10, 5);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);

    br.a->delGeometry(1);  // A retires L1
    br.a->getDocument()->recompute();
    addHorizontal(br.b, 1);  // B constrains L1 — its only reference

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection mergedGeom
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);
    App::RecipeSection mergedCons
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    ASSERT_EQ(mergedCons.size(), 1U);
    const std::string ctag = mergedCons.begin()->first;

    std::vector<App::RefResolution> res = App::RecipeMerge::resolveReferences(mergedCons, mergedGeom);
    ASSERT_EQ(res.size(), 1U);
    EXPECT_EQ(res.front().outcome, App::RefResolution::Outcome::Drop);
    EXPECT_EQ(res.front().id, ctag);
    EXPECT_EQ(mergedCons.count(ctag), 0U) << "the whole-subject constraint was dropped";

    // Control: re-merge (the first pass mutated mergedCons) and resolve against LIVE geometry.
    App::RecipeSection consAgain
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    std::vector<App::RefResolution> live
        = App::RecipeMerge::resolveReferences(consAgain, br.base.geometry);
    EXPECT_TRUE(live.empty()) << "with its target alive the constraint carries, nothing to resolve";
    EXPECT_EQ(consAgain.count(ctag), 1U);
}

// Stop-and-ask: A retires L1 but L0 survives; B adds a Coincident tying L0's end to L1's start,
// so the merged constraint references a live participant AND the retired one. A satisfiable
// target survives but re-targeting is the user's choice, so it resolves to StopAsk and is KEPT.
TEST_F(SketchRecipeMergeTest, danglingWithSurvivingParticipantResolvesToStopAsk)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);    // L0
        addLine(s, 10, 0, 10, 10);  // L1 — its start meets L0's end at (10,0)
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    const std::string tag0 = boost::uuids::to_string(br.a->getInternalGeometry()[0]->getTag());
    const std::string tag1 = boost::uuids::to_string(br.a->getInternalGeometry()[1]->getTag());

    br.a->delGeometry(1);  // A retires L1; L0 survives
    br.a->getDocument()->recompute();
    // B ties the surviving line to the retired one.
    addCoincident(br.b, 0, Sketcher::PointPos::end, 1, Sketcher::PointPos::start);

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection mergedGeom
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);
    App::RecipeSection mergedCons
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    ASSERT_EQ(mergedCons.size(), 1U);
    const std::string ctag = mergedCons.begin()->first;

    // Precondition that makes StopAsk the genuine outcome (not a fluke): the emitted coincidence
    // references BOTH the surviving line and the retired one.
    bool refsLive = false;
    bool refsDead = false;
    for (const App::RecipeRef& r : mergedCons.at(ctag).refs) {
        refsLive = refsLive || r.target == tag0;
        refsDead = refsDead || r.target == tag1;
    }
    ASSERT_TRUE(refsLive) << "constraint must reference the surviving line L0";
    ASSERT_TRUE(refsDead) << "constraint must reference the retired line L1";
    EXPECT_EQ(mergedGeom.count(tag1), 0U) << "L1 really was retired by the merge";

    std::vector<App::RefResolution> res = App::RecipeMerge::resolveReferences(mergedCons, mergedGeom);
    ASSERT_EQ(res.size(), 1U);
    EXPECT_EQ(res.front().outcome, App::RefResolution::Outcome::StopAsk);
    EXPECT_EQ(res.front().id, ctag);
    EXPECT_EQ(mergedCons.count(ctag), 1U) << "a live participant survives -> kept for the user";
}

// Slice 5, end-to-end on real sketches: regenerate. The recipe layer merges with nothing running;
// running the existing solver on the merged recipe is what reveals whether it "compiles". These
// two cases merge EQUALLY clean at the recipe level (no value conflict, no dangling reference) and
// are told apart only by the solver on regenerate — the payoff the design names.

// A fresh sketch to regenerate onto, plus the branch sketches as coordinate-seed sources.
Sketcher::SketchObject* freshTarget(App::Document* doc)
{
    return static_cast<Sketcher::SketchObject*>(doc->addObject("Sketcher::SketchObject", "T"));
}

// Merged-but-does-not-compile: A constrains L0's length to 40, B (independently, its own durable
// constraint) constrains the SAME line to 30. Different tags -> the three-way merge sees two
// non-overlapping additions and merges clean; both refs point at the surviving L0, so no dangling
// reference either. Only on regenerate does the solver find two lengths on one line irreconcilable.
TEST_F(SketchRecipeMergeTest, cleanMergeRegeneratesToConflict)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 40, 0);  // L0, unconstrained in the ancestor
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);

    addDistance(br.a, 0, 40.0);  // A: length 40
    addDistance(br.b, 0, 30.0);  // B: length 30 — a distinct constraint, not an edit of A's

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection mergedGeom
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);
    App::RecipeSection mergedCons
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    App::RecipeMerge::checkReferences(mergedCons, mergedGeom, conflicts);
    ASSERT_TRUE(conflicts.empty())
        << "the merge is textually clean: no value or referential conflict";
    ASSERT_EQ(mergedCons.size(), 2U) << "both branches' constraints carried through";

    SketchRecipe merged {mergedGeom, mergedCons};
    auto* doc = App::GetApplication().newDocument("regenConflict");
    Sketcher::RegenResult r = Sketcher::regenerateSketch(*freshTarget(doc), merged, {br.a, br.b});

    EXPECT_TRUE(r.fullyRealized) << "every geometry and constraint was materialized (not skipped)";
    EXPECT_TRUE(r.hasConflicts) << "two lengths on one line: the merge does not compile";
    EXPECT_LT(r.solverStatus, 0) << "the solver reports failure";
}

// Positive control — merges equally clean, and DOES compile: A constrains L0's length to 40, B
// makes the same line Horizontal. Both additions merge clean by tag, both ref the surviving L0,
// and on regenerate the two constraints are mutually satisfiable, so the solver succeeds. A blind
// regenerate that always reported a conflict would fail here; one that always succeeded would fail
// the case above.
TEST_F(SketchRecipeMergeTest, cleanMergeRegeneratesValid)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 40, 0);  // L0
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);

    addDistance(br.a, 0, 40.0);  // A: length 40
    addHorizontal(br.b, 0);      // B: horizontal — compatible with A's length

    SketchRecipe a = Sketcher::emitSketchRecipe(*br.a);
    SketchRecipe b = Sketcher::emitSketchRecipe(*br.b);

    std::vector<App::MergeConflict> conflicts;
    App::RecipeSection mergedGeom
        = App::RecipeMerge::threeWay(br.base.geometry, a.geometry, b.geometry, conflicts);
    App::RecipeSection mergedCons
        = App::RecipeMerge::threeWay(br.base.constraints, a.constraints, b.constraints, conflicts);
    App::RecipeMerge::checkReferences(mergedCons, mergedGeom, conflicts);
    ASSERT_TRUE(conflicts.empty())
        << "the merge is textually clean, exactly as in the conflict case";
    ASSERT_EQ(mergedCons.size(), 2U) << "both branches' constraints carried through";

    SketchRecipe merged {mergedGeom, mergedCons};
    auto* doc = App::GetApplication().newDocument("regenValid");
    Sketcher::RegenResult r = Sketcher::regenerateSketch(*freshTarget(doc), merged, {br.a, br.b});

    EXPECT_TRUE(r.fullyRealized) << "every geometry and constraint was materialized";
    EXPECT_FALSE(r.hasConflicts) << "length + horizontal are mutually satisfiable";
    EXPECT_EQ(r.solverStatus, 0) << "the merge compiles";
    // Both constraints really took effect: a free line has 4 DoF; length removes one, horizontal
    // another -> 2 remain. (If a constraint had been silently skipped, DoF would be higher.)
    EXPECT_EQ(r.dof, 2) << "distance and horizontal both applied on regenerate";
}

}  // namespace
