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
#include <App/ObjectRecipe.h>
#include <App/Recipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/Sketcher/App/SketchRecipe.h>

#include <algorithm>
#include <set>
#include <filesystem>
#include <functional>

#ifdef FC_OS_WIN32
# include <process.h>
#else
# include <unistd.h>
#endif
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

        std::error_code ec;
        std::filesystem::remove_all(_scratch, ec);
        _scratch.clear();
    }

    /// This test process's own id, so two concurrently scheduled cases cannot pick the same
    /// scratch directory.
    static long long currentProcessId()
    {
#ifdef FC_OS_WIN32
        return static_cast<long long>(_getpid());
#else
        return static_cast<long long>(getpid());
#endif
    }

    /// A path under a directory belonging to this test alone. ctest runs each case as its own
    /// process and may run many at once, so a shared leaf name ("recipe_base.FCStd") had every
    /// concurrent case saving over its neighbours -- the suite then failed on a different test
    /// each run. The directory is named for the running case and the process, so no two can
    /// collide however they are scheduled, and a save's companion files land inside it too.
    std::string tempPath(const char* leaf)
    {
        if (_scratch.empty()) {
            const ::testing::TestInfo* running
                = ::testing::UnitTest::GetInstance()->current_test_info();
            std::string leafDir = running != nullptr ? std::string(running->name()) : "case";
            leafDir += '.' + std::to_string(currentProcessId());
            _scratch = std::filesystem::temp_directory_path() / ("SketchRecipeMerge." + leafDir);
            std::error_code ec;
            std::filesystem::remove_all(_scratch, ec);
            std::filesystem::create_directories(_scratch, ec);
        }
        auto p = (_scratch / leaf).string();
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

        App::DocumentInitFlags branch;
        branch.duplicate = App::DuplicateAnswer::SamePart;
        out.a = sketchOf(App::GetApplication().openDocument(aP.c_str(), branch));
        out.b = sketchOf(App::GetApplication().openDocument(bP.c_str(), branch));
        return out;
    }

    std::vector<std::string> _tempFiles;
    std::filesystem::path _scratch;
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

// The document merge saw a sketch's lines and dimensions not at all, so one dimension set two ways
// read as "merged cleanly". It now hands a sketch both branches changed to the sketch merge.
TEST_F(SketchRecipeMergeTest, aDocumentMergeSeesADimensionSetTwoWays)
{
    int constrId = -1;
    Branches br = makeBranches([&constrId](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 40, 0);
        constrId = addDistance(s, 0, 40.0);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    const std::string ctag = br.base.constraints.begin()->first;
    App::Document* ancestor = App::GetApplication().getDocument("recipeAncestor");
    ASSERT_NE(ancestor, nullptr);

    br.a->setDatum(constrId, 50.0);
    br.b->setDatum(constrId, 60.0);
    br.a->getDocument()->recompute();
    br.b->getDocument()->recompute();

    const App::DocumentMergeReport report
        = App::mergeDocuments(*ancestor, *br.a->getDocument(), *br.b->getDocument());

    const bool dimensionConflicts
        = std::any_of(report.conflicts.begin(), report.conflicts.end(), [&](const auto& c) {
              return c.id == ctag;
          });
    EXPECT_TRUE(dimensionConflicts);
    EXPECT_EQ(App::formatDocumentReport(report).find("Merged cleanly"), std::string::npos);
}

// Two branches drawing different lines into one sketch is two changes, not one clash: the sketch
// merge takes both, so the document merge reports no conflict.
TEST_F(SketchRecipeMergeTest, aDocumentMergeTakesALineDrawnOnEachBranch)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) { addLine(s, 0, 0, 40, 0); });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    App::Document* ancestor = App::GetApplication().getDocument("recipeAncestor");
    ASSERT_NE(ancestor, nullptr);

    addLine(br.a, 0, 10, 40, 10);
    addLine(br.b, 0, 20, 40, 20);
    br.a->getDocument()->recompute();
    br.b->getDocument()->recompute();

    const App::DocumentMergeReport report
        = App::mergeDocuments(*ancestor, *br.a->getDocument(), *br.b->getDocument());

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.resolutions.empty());
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

// --- only a person's choices are merged ------------------------------------------------

int addPoint(Sketcher::SketchObject* sketch, double x, double y)
{
    Part::GeomPoint g(Base::Vector3d(x, y, 0));
    return sketch->addGeometry(&g);
}

void addConstraintOn(
    Sketcher::SketchObject* sketch,
    Sketcher::ConstraintType type,
    const std::vector<std::pair<int, Sketcher::PointPos>>& elements,
    double value = 0.0
)
{
    Sketcher::Constraint c;
    c.Type = type;
    for (size_t i = 0; i < elements.size(); ++i) {
        c.setElement(static_cast<int>(i), Sketcher::GeoElementId(elements[i].first, elements[i].second));
    }
    c.setValue(value);
    sketch->addConstraint(&c);
    sketch->getDocument()->recompute();
}

/// The ancestor as saved, reopened beside its two branches as the same part.
Sketcher::SketchObject* reopenAncestor(const std::string& path)
{
    App::DocumentInitFlags branch;
    branch.duplicate = App::DuplicateAnswer::SamePart;
    return static_cast<Sketcher::SketchObject*>(
        App::GetApplication().openDocument(path.c_str(), branch)->getObject("S")
    );
}

std::string tagOf(const Sketcher::SketchObject* sketch, int geoId)
{
    return boost::uuids::to_string(sketch->getInternalGeometry()[geoId]->getTag());
}

// A post pinned by its own dimensions, and an arm from its top whose far end is free. Constraint 3
// is the post's length.
void postAndArm(Sketcher::SketchObject* s)
{
    const int post = addLine(s, 0, 0, 0, 5);
    const int arm = addLine(s, 0, 5, 10, 5);
    addConstraintOn(s, Sketcher::DistanceX, {{post, Sketcher::PointPos::start}}, 0.0);
    addConstraintOn(s, Sketcher::Vertical, {{post, Sketcher::PointPos::none}});
    addConstraintOn(s, Sketcher::DistanceY, {{post, Sketcher::PointPos::start}}, 0.0);
    addDistance(s, post, 5.0);
    addConstraintOn(
        s,
        Sketcher::Coincident,
        {{arm, Sketcher::PointPos::start}, {post, Sketcher::PointPos::end}}
    );
}

// A lengthens the post, so its solver carries the arm's near end up; B drags the arm's far end.
// Only B chose a position on the arm, so B's arm is merged and the re-solve keeps B's far end.
// Seeding the arm from A instead would put the far end back where A's copy left it.
TEST_F(SketchRecipeMergeTest, aPositionTheSolverMovedIsNotAChange)
{
    Branches br = makeBranches(postAndArm);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->setDatum(3, 8.0), 0);
    br.a->getDocument()->recompute();
    br.b->moveGeometry(1, Sketcher::PointPos::end, Base::Vector3d(12, 3, 0));
    br.b->getDocument()->recompute();

    const std::string arm = tagOf(br.b, 1);
    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty()) << "the solver's move on A is not A's change";
    EXPECT_TRUE(report.overridden.empty()) << "the re-solve kept B's far end";
    EXPECT_EQ(report.regen.solverStatus, 0);
    EXPECT_NE(report.mergedGeometry.at(arm).fields.at("to").find("12"), std::string::npos)
        << "B's dragged far end is the merged arm's";
}

// A drives the post's length by a formula. The rebuilt sketch is judged at the length the formula
// gives, never at a zero standing in for a number nobody wrote.
TEST_F(SketchRecipeMergeTest, aDimensionSetByAFormulaRebuildsAtWhatTheFormulaGives)
{
    Branches br = makeBranches(postAndArm);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    std::string cmd = "import FreeCAD as App\n";
    cmd += "App.getDocument('" + std::string(br.a->getDocument()->getName()) + "').getObject('"
        + br.a->getNameInDocument() + "').setExpression('Constraints[3]', '4 mm * 2')\n";
    Base::Interpreter().runString(cmd.c_str());
    br.a->getDocument()->recompute();
    ASSERT_DOUBLE_EQ(br.a->Constraints.getValues()[3]->getValue(), 8.0);

    const std::string post = tagOf(br.a, 0);
    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.regen.fullyRealized);
    EXPECT_EQ(report.regen.solverStatus, 0);
    const std::string top = report.solvedPositions.at(post).fields.at("to");
    EXPECT_NE(top.find("8"), std::string::npos) << top;
}

// A draws a line and B lengthens the post, both in one sketch. The merged part's sketch holds the
// line and the new length, each element under the id it carried, and solves.
TEST_F(SketchRecipeMergeTest, aMergedPartHoldsTheSketchBothVersionsDrew)
{
    Branches br = makeBranches(postAndArm);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    const int drawn = addLine(br.a, 20, 0, 30, 0);
    br.a->getDocument()->recompute();
    ASSERT_EQ(br.b->setDatum(3, 8.0), 0);
    br.b->getDocument()->recompute();
    const std::string line = tagOf(br.a, drawn);
    const std::string length = boost::uuids::to_string(br.b->Constraints.getValues()[3]->getTag());

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const App::MergedDocument made = App::buildMergedDocument(
        *ancestor->getDocument(),
        *br.a->getDocument(),
        *br.b->getDocument()
    );

    ASSERT_NE(made.document, nullptr) << made.refusal;
    Sketcher::SketchObject* sketch = nullptr;
    for (App::DocumentObject* obj : made.document->getObjects()) {
        if (obj->Uid.getValueStr() == br.a->Uid.getValueStr()) {
            sketch = dynamic_cast<Sketcher::SketchObject*>(obj);
        }
    }
    ASSERT_NE(sketch, nullptr);
    std::set<std::string> tags;
    for (const Part::Geometry* geo : sketch->getInternalGeometry()) {
        tags.insert(boost::uuids::to_string(geo->getTag()));
    }
    EXPECT_EQ(tags.count(line), 1u) << "A's line, under its own id";
    bool lengthFound = false;
    for (const Sketcher::Constraint* constraint : sketch->Constraints.getValues()) {
        if (boost::uuids::to_string(constraint->getTag()) == length) {
            lengthFound = true;
            EXPECT_DOUBLE_EQ(constraint->getValue(), 8.0);
        }
    }
    EXPECT_TRUE(lengthFound) << "B's length, under its own id";
    EXPECT_EQ(sketch->solve(), 0);
    EXPECT_TRUE(made.newlyFailing.empty());

    App::GetApplication().closeDocument(made.document->getName());
}

// A changes one dimension and B adds another, and nobody drags anything. Each solver moves the arm,
// and neither move is a choice, so the merge has nothing to ask.
TEST_F(SketchRecipeMergeTest, aDifferentDimensionOnEachSideMovesNothingByHand)
{
    Branches br = makeBranches(postAndArm);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->setDatum(3, 8.0), 0);
    br.a->getDocument()->recompute();
    addDistance(br.b, 1, 12.0);

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.overridden.empty());
    EXPECT_EQ(report.regen.solverStatus, 0);
}

// One side changes a dimension and drags the arm's far end in the same edit; the other leaves the
// arm alone. The merge cannot tell that drag from the solver's work, so the arm starts the merged
// solve where that side left it, and the drag survives whichever side made it.
TEST_F(SketchRecipeMergeTest, aDragBesideAChangedDimensionSurvivesOnEitherSide)
{
    for (const bool aEdits : {true, false}) {
        Branches br = makeBranches(postAndArm);
        ASSERT_NE(br.a, nullptr);
        ASSERT_NE(br.b, nullptr);
        Sketcher::SketchObject* edits = aEdits ? br.a : br.b;
        Sketcher::SketchObject* other = aEdits ? br.b : br.a;
        ASSERT_EQ(edits->setDatum(3, 8.0), 0);
        edits->moveGeometry(1, Sketcher::PointPos::end, Base::Vector3d(15, 1, 0));
        edits->getDocument()->recompute();
        addPoint(other, 20, 20);
        other->getDocument()->recompute();

        auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
        ASSERT_NE(ancestor, nullptr);
        const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

        EXPECT_TRUE(report.conflicts.empty()) << (aEdits ? "A edits" : "B edits");
        EXPECT_TRUE(report.overridden.empty());
        const std::string& armEnd = report.solvedPositions.at(tagOf(edits, 1)).fields.at("to");
        EXPECT_EQ(armEnd.rfind("15", 0), 0u) << armEnd << " is not where the drag left it";
        App::GetApplication().closeAllDocuments();
    }
}

// One side moves a free line's start, the other its end: two changes to two values, so both land,
// in the merged record and in the rebuilt sketch.
TEST_F(SketchRecipeMergeTest, aLinesStartAndEndMovedOnDifferentSidesBothLand)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) { addLine(s, 0, 0, 10, 0); });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    br.a->moveGeometry(0, Sketcher::PointPos::start, Base::Vector3d(-3, 2, 0));
    br.a->getDocument()->recompute();
    br.b->moveGeometry(0, Sketcher::PointPos::end, Base::Vector3d(14, 6, 0));
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.overridden.empty());
    const App::RecipeNode& line = report.solvedPositions.at(tagOf(br.a, 0));
    EXPECT_EQ(line.fields.at("from"), "-3 2");
    EXPECT_EQ(line.fields.at("to"), "14 6");
}

// One side makes a line construction geometry, the other drags its end: both land.
TEST_F(SketchRecipeMergeTest, aConstructionToggleAndADragOnDifferentSidesBothLand)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) { addLine(s, 0, 0, 10, 0); });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->toggleConstruction(0), 0);
    br.a->getDocument()->recompute();
    br.b->moveGeometry(0, Sketcher::PointPos::end, Base::Vector3d(14, 6, 0));
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty());
    const std::string line = tagOf(br.a, 0);
    EXPECT_EQ(report.mergedGeometry.at(line).fields.at("construction"), "true");
    EXPECT_EQ(report.solvedPositions.at(line).fields.at("to"), "14 6");
}

// A side that changes a dimension and also drags a free end the dimension's solve reaches: the
// drag is still that side's choice. Both sides dragging the end differently is a conflict; had the
// drag been taken for the solver's work, B's end would have won and A's drag been lost unreported.
TEST_F(SketchRecipeMergeTest, aDragBesideAChangedDimensionIsStillAChoice)
{
    Branches br = makeBranches(postAndArm);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->setDatum(3, 8.0), 0);
    br.a->moveGeometry(1, Sketcher::PointPos::end, Base::Vector3d(15, 1, 0));
    br.a->getDocument()->recompute();
    br.b->moveGeometry(1, Sketcher::PointPos::end, Base::Vector3d(12, 3, 0));
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    ASSERT_EQ(report.conflicts.size(), 1U) << "both sides chose the arm's far end";
    EXPECT_EQ(report.conflicts.front().id, tagOf(br.a, 1));
}

// A free point: one side drags it, the other pins it with two dimensions. The drag is merged, then
// the re-solve puts the point where the dimensions say, so the drag is lost: a conflict, either
// way round. aPositionTheSolverMovedIsNotAChange is the control: there the re-solve keeps every
// chosen position.
TEST_F(SketchRecipeMergeTest, aChosenPositionTheMergeOverridesIsReported)
{
    for (const bool aDrags : {true, false}) {
        Branches br = makeBranches([](Sketcher::SketchObject* s) { addPoint(s, 3, 3); });
        ASSERT_NE(br.a, nullptr);
        ASSERT_NE(br.b, nullptr);
        Sketcher::SketchObject* drags = aDrags ? br.a : br.b;
        Sketcher::SketchObject* pins = aDrags ? br.b : br.a;

        drags->moveGeometry(0, Sketcher::PointPos::start, Base::Vector3d(5, 5, 0));
        drags->getDocument()->recompute();
        addConstraintOn(pins, Sketcher::DistanceX, {{0, Sketcher::PointPos::start}}, 1.0);
        addConstraintOn(pins, Sketcher::DistanceY, {{0, Sketcher::PointPos::start}}, 1.0);

        auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
        ASSERT_NE(ancestor, nullptr);
        const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

        EXPECT_TRUE(report.conflicts.empty()) << "only one side chose the point's position";
        ASSERT_EQ(report.overridden.size(), 1U) << (aDrags ? "A drags" : "B drags");
        EXPECT_EQ(report.overridden.front().id, tagOf(drags, 0));
        App::GetApplication().closeAllDocuments();
    }
}

// A line from the origin to a point on the horizontal axis, its length dimensioned: fully
// constrained only while both ties to the sketch's own frame survive the merge. The origin sits in
// the constraint's first place, so a tie that was left out would also shift the line into the
// wrong place. Each side makes an unrelated edit, so the merge itself is clean.
TEST_F(SketchRecipeMergeTest, tiesToTheAxesSurviveTheMerge)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        const int line = addLine(s, 0, 0, 5, 0);
        addConstraintOn(
            s,
            Sketcher::Coincident,
            {{Sketcher::GeoEnum::RtPnt, Sketcher::PointPos::start}, {line, Sketcher::PointPos::start}}
        );
        addConstraintOn(
            s,
            Sketcher::PointOnObject,
            {{line, Sketcher::PointPos::end}, {Sketcher::GeoEnum::HAxis, Sketcher::PointPos::none}}
        );
        addDistance(s, line, 5.0);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->setDatum(2, 7.0), 0);
    br.a->getDocument()->recompute();
    addPoint(br.b, 3, 3);
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    Sketcher::MergeReport report;
    ASSERT_NO_THROW(report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b));

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.resolutions.empty()) << "a tie to an axis never dangles";
    EXPECT_TRUE(report.regen.fullyRealized);
    EXPECT_EQ(report.regen.solverStatus, 0);
    EXPECT_EQ(report.regen.dof, 2) << "only B's free point can move";
}

// A deletes a line; B adds a constraint on it. A never saw B's constraint, so the merge does not
// drop it: it is a conflict, kept for the person settling it. Control: the ancestor's length on the
// same line, which B left as it was, is A's to retire and raises nothing.
TEST_F(SketchRecipeMergeTest, aConstraintAddedOnGeometryTheOtherSideDeletedIsAConflict)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);
        addLine(s, 0, 5, 10, 5);
        addDistance(s, 1, 10.0);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    br.a->delGeometry(1);
    br.a->getDocument()->recompute();
    addHorizontal(br.b, 1);

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    ASSERT_EQ(report.conflicts.size(), 1U);
    EXPECT_EQ(report.conflicts.front().kind, App::MergeConflict::Kind::Referential);
    EXPECT_EQ(report.conflicts.front().type, "Horizontal");
    EXPECT_EQ(report.mergedConstraints.count(report.conflicts.front().id), 1U)
        << "the conflicted constraint is kept, not dropped";
    EXPECT_TRUE(report.resolutions.empty());
    for (const auto& [id, node] : report.mergedConstraints) {
        EXPECT_NE(node.type, "Distance") << "A's delete retired the ancestor's length";
    }
}

// A deletes a line and the length on it; B changes that length. The change against the delete is
// an open conflict, so the length is held for the person settling it, not also dropped for
// pointing at the deleted line.
TEST_F(SketchRecipeMergeTest, aDimensionChangedOnOneSideAndDeletedOnTheOtherIsHeld)
{
    Branches br = makeBranches([](Sketcher::SketchObject* s) {
        addLine(s, 0, 0, 10, 0);
        addLine(s, 0, 5, 10, 5);
        addDistance(s, 1, 10.0);
    });
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    br.a->delGeometry(1);
    br.a->getDocument()->recompute();
    ASSERT_EQ(br.b->setDatum(0, 12.0), 0);
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    ASSERT_EQ(report.conflicts.size(), 1U);
    EXPECT_EQ(report.conflicts.front().type, "Distance");
    EXPECT_EQ(report.mergedConstraints.count(report.conflicts.front().id), 1U);
    EXPECT_TRUE(report.resolutions.empty());
}

// A sketch line beside a point it borrows from a box's corner.
void lineBesideABorrowedPoint(Sketcher::SketchObject* s)
{
    App::Document* doc = s->getDocument();
    doc->addObject("Part::Box", "Box");
    doc->recompute();
    s->addExternal(doc->getObject("Box"), "Vertex1");
    addLine(s, 2, 2, 8, 2);
}

constexpr int firstBorrowed = -3;

// A ties the line to the borrowed corner; B adds a point. Before borrowed geometry had an identity,
// the tie lost its borrowed end in the recipe and the rebuild placed a one-ended constraint.
TEST_F(SketchRecipeMergeTest, aTieToBorrowedGeometryKeepsBothEnds)
{
    Branches br = makeBranches(lineBesideABorrowedPoint);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    addCoincident(br.a, 0, Sketcher::PointPos::start, firstBorrowed, Sketcher::PointPos::start);
    addPoint(br.b, 3, 3);
    br.b->getDocument()->recompute();

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_TRUE(report.resolutions.empty());
    bool tied = false;
    for (const auto& [id, node] : report.mergedConstraints) {
        if (node.type == "Coincident") {
            tied = true;
            ASSERT_EQ(node.refs.size(), 2U);
            EXPECT_EQ(node.refs[1].target.rfind("borrowed:", 0), 0U);
        }
    }
    EXPECT_TRUE(tied);
    EXPECT_TRUE(report.regen.fullyRealized);
    EXPECT_EQ(report.regen.solverStatus, 0);
    EXPECT_FALSE(report.regen.hasMalformed);
}

// A stops borrowing the corner; B ties the line to it. A never saw B's tie: a conflict, kept.
TEST_F(SketchRecipeMergeTest, aTieToGeometryTheOtherSideStoppedBorrowingIsAConflict)
{
    Branches br = makeBranches(lineBesideABorrowedPoint);
    ASSERT_NE(br.a, nullptr);
    ASSERT_NE(br.b, nullptr);
    ASSERT_EQ(br.a->delExternal(0), 0);
    br.a->getDocument()->recompute();
    addCoincident(br.b, 0, Sketcher::PointPos::start, firstBorrowed, Sketcher::PointPos::start);

    auto* ancestor = reopenAncestor(tempPath("recipe_base.FCStd"));
    ASSERT_NE(ancestor, nullptr);
    const Sketcher::MergeReport report = Sketcher::mergeSketches(*ancestor, *br.a, *br.b);

    ASSERT_EQ(report.conflicts.size(), 1U);
    EXPECT_EQ(report.conflicts.front().kind, App::MergeConflict::Kind::Referential);
    EXPECT_EQ(report.conflicts.front().type, "Coincident");
}

}  // namespace
