// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <App/Recipe.h>

using namespace App;

namespace
{

// A geometry-like node (no refs) and a constraint-like node (refs by target id).
RecipeNode geo(const std::string& id, const std::string& type = "LineSegment")
{
    return RecipeNode {id, type, {}, {}};
}

RecipeNode con(
    const std::string& id,
    const std::string& type,
    std::vector<RecipeRef> refs,
    const std::string& value = ""
)
{
    RecipeNode n {id, type, {}, std::move(refs)};
    if (!value.empty()) {
        n.fields["value"] = value;
    }
    return n;
}

// A 4-line polyline with three coincidences and one dimension, the spike's base sketch.
RecipeSection baseGeometry()
{
    return {{"g0", geo("g0")}, {"g1", geo("g1")}, {"g2", geo("g2")}, {"g3", geo("g3")}};
}

RecipeSection baseConstraints()
{
    return {
        {"c0", con("c0", "Coincident", {{"g0", 2}, {"g1", 1}})},
        {"c1", con("c1", "Coincident", {{"g1", 2}, {"g2", 1}})},
        {"c2", con("c2", "Coincident", {{"g2", 2}, {"g3", 1}})},
        {"c3", con("c3", "DistanceX", {{"g0", 1}, {"g0", 2}}, "10")},
    };
}

}  // namespace

// An untouched three-way merge introduces nothing and conflicts on nothing (the metric
// would be blind if it reported spurious changes on an identity merge).
TEST(RecipeMergeTest, identityMergeIsCleanAndUnchanged)
{
    RecipeSection base = baseGeometry();
    std::vector<MergeConflict> conflicts;

    RecipeSection merged = RecipeMerge::threeWay(base, base, base, conflicts);

    EXPECT_EQ(merged, base);
    EXPECT_TRUE(conflicts.empty());
}

// Scenario 1: A deletes g0, B adds g4 -> both edits combine, no conflict. The map is
// keyed by durable id, so it is inherently position-independent: g1..g3 keep their ids
// no matter what index they would occupy after g0's removal.
TEST(RecipeMergeTest, concurrentDeleteAndAddMergesCleanly)
{
    RecipeSection base = baseGeometry();

    RecipeSection a = base;
    a.erase("g0");  // branch A deletes the first line

    RecipeSection b = base;
    b["g4"] = geo("g4");  // branch B adds a new line

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);

    EXPECT_TRUE(conflicts.empty());
    EXPECT_EQ(merged.count("g0"), 0u);  // A's delete applied
    EXPECT_EQ(merged.count("g4"), 1u);  // B's add applied
    EXPECT_EQ(merged.count("g1"), 1u);  // untouched survivors kept
    EXPECT_EQ(merged.size(), 4u);       // g1,g2,g3,g4
}

// Scenario 2: both branches change the SAME dimension's value differently -> exactly one
// Value conflict, on that constraint. (A control that both-same would NOT conflict is the
// identity test above.)
TEST(RecipeMergeTest, divergentValueEditConflicts)
{
    RecipeSection base = baseConstraints();

    RecipeSection a = base;
    a["c3"].fields["value"] = "20";  // A: DistanceX -> 20

    RecipeSection b = base;
    b["c3"].fields["value"] = "30";  // B: DistanceX -> 30

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].kind, MergeConflict::Kind::Value);
    EXPECT_EQ(conflicts[0].id, "c3");
    // the other three constraints merged clean
    EXPECT_EQ(merged.size(), 4u);
}

// Same-value edit on both sides is agreement, not a conflict (distinguishes the conflict
// rule from "any edit conflicts").
TEST(RecipeMergeTest, matchingEditOnBothSidesIsNotAConflict)
{
    RecipeSection base = baseConstraints();
    RecipeSection a = base;
    a["c3"].fields["value"] = "25";
    RecipeSection b = base;
    b["c3"].fields["value"] = "25";

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);

    EXPECT_TRUE(conflicts.empty());
    EXPECT_EQ(merged.at("c3").fields.at("value"), "25");
}

// Scenario 3: A deletes a line; B adds a constraint referencing it. Merging geometry and
// constraints independently is clean on each -> but the cross-section check catches that
// the merged Vertical dangles onto deleted geometry. "Merges clean, regenerates broken."
TEST(RecipeMergeTest, danglingReferenceAfterMergeIsCaught)
{
    RecipeSection baseGeo = baseGeometry();
    RecipeSection baseCon = baseConstraints();

    // branch A: delete g2 (and, as the sketch would, its incident coincidences)
    RecipeSection aGeo = baseGeo;
    aGeo.erase("g2");
    RecipeSection aCon = baseCon;
    aCon.erase("c1");  // Coincident g1-g2
    aCon.erase("c2");  // Coincident g2-g3

    // branch B: add a Vertical constraint on g2
    RecipeSection bGeo = baseGeo;
    RecipeSection bCon = baseCon;
    bCon["cV"] = con("cV", "Vertical", {{"g2", 0}});

    std::vector<MergeConflict> conflicts;
    RecipeSection mergedGeo = RecipeMerge::threeWay(baseGeo, aGeo, bGeo, conflicts);
    RecipeSection mergedCon = RecipeMerge::threeWay(baseCon, aCon, bCon, conflicts);

    // per-section merge is clean: no Value conflicts
    EXPECT_TRUE(conflicts.empty());
    EXPECT_EQ(mergedGeo.count("g2"), 0u);  // A's delete won
    EXPECT_EQ(mergedCon.count("cV"), 1u);  // B's new constraint kept

    // the cross-section referential pass finds the dangling Vertical
    RecipeMerge::checkReferences(mergedCon, mergedGeo, conflicts);

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].kind, MergeConflict::Kind::Referential);
    EXPECT_EQ(conflicts[0].id, "cV");
}

// A constraint whose target survives the merge is NOT flagged (negative control: the
// referential check must distinguish a live ref from a dangling one).
TEST(RecipeMergeTest, liveReferenceIsNotFlagged)
{
    RecipeSection mergedGeo = baseGeometry();
    RecipeSection mergedCon = baseConstraints();  // all refs point at surviving g0..g3

    std::vector<MergeConflict> conflicts;
    RecipeMerge::checkReferences(mergedCon, mergedGeo, conflicts);

    EXPECT_TRUE(conflicts.empty());
}
