// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

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

// A feature-like node carrying several authored fields (e.g. a Pad's Length/Type/Reversed),
// the unit the field-granular refinement pass reconciles.
RecipeNode feature(
    const std::string& id,
    std::map<std::string, std::string> fields,
    std::vector<RecipeRef> refs = {}
)
{
    return RecipeNode {id, "PartDesign::Pad", std::move(fields), std::move(refs)};
}

// Count the refined Value conflicts naming a given object id.
std::size_t valueConflictsFor(const std::vector<MergeConflict>& conflicts, const std::string& id)
{
    std::size_t count = 0;
    for (const MergeConflict& c : conflicts) {
        if (c.kind == MergeConflict::Kind::Value && c.id == id) {
            ++count;
        }
    }
    return count;
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

// The presence half of the Amendment 21 proposal: A deletes g2, B adds a Vertical on it. A could
// not see B's constraint, so it is a conflict, not a drop. Control: the coincidences on g2 that B
// left as they were in the ancestor are A's to retire, and raise nothing.
TEST(RecipeMergeTest, aReferenceOneSideAddedToWhatTheOtherDeletedIsAConflict)
{
    RecipeSection baseGeo = baseGeometry();
    RecipeSection baseCon = baseConstraints();
    RecipeSection aGeo = baseGeo;
    aGeo.erase("g2");
    RecipeSection aCon = baseCon;
    RecipeSection bCon = baseCon;
    bCon["cV"] = con("cV", "Vertical", {{"g2", 0}});

    std::vector<MergeConflict> conflicts;
    RecipeSection mergedGeo = RecipeMerge::threeWay(baseGeo, aGeo, baseGeo, conflicts);
    RecipeSection mergedCon = RecipeMerge::threeWay(baseCon, aCon, bCon, conflicts);
    RecipeMerge::presenceConflicts(baseCon, aCon, bCon, baseGeo, aGeo, baseGeo, mergedCon, conflicts);

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].kind, MergeConflict::Kind::Referential);
    EXPECT_EQ(conflicts[0].id, "cV");
}

// Changing a constraint counts as much as adding one, from either side: B retargets c3 onto g2
// while A deletes g2, and the same with the sides swapped.
TEST(RecipeMergeTest, aReferenceOneSideChangedToWhatTheOtherDeletedIsAConflict)
{
    RecipeSection baseGeo = baseGeometry();
    RecipeSection baseCon = baseConstraints();
    RecipeSection deleting = baseGeo;
    deleting.erase("g2");
    RecipeSection changed = baseCon;
    changed["c3"] = con("c3", "DistanceX", {{"g2", 1}, {"g2", 2}}, "10");

    for (const bool aDeletes : {true, false}) {
        const RecipeSection& aGeo = aDeletes ? deleting : baseGeo;
        const RecipeSection& bGeo = aDeletes ? baseGeo : deleting;
        const RecipeSection& aCon = aDeletes ? baseCon : changed;
        const RecipeSection& bCon = aDeletes ? changed : baseCon;

        std::vector<MergeConflict> conflicts;
        RecipeSection mergedCon = RecipeMerge::threeWay(baseCon, aCon, bCon, conflicts);
        RecipeMerge::presenceConflicts(baseCon, aCon, bCon, baseGeo, aGeo, bGeo, mergedCon, conflicts);

        ASSERT_EQ(conflicts.size(), 1u) << (aDeletes ? "A deletes" : "B deletes");
        EXPECT_EQ(conflicts[0].id, "c3");
    }
}

// Slice 5 — the field-granular refinement pass layered over the object-granular merge. It
// takes the SAME base/A/B sections and the object-level conflicts threeWay produced and makes
// them finer, without re-representing anything.

// Disjoint field edits to the same object: object-granular reports one whole-object conflict;
// the refinement pass dissolves it (the branches touched different fields) and rebuilds the
// merged node to carry BOTH edits. (A pass that only kept A's side would drop B's Type change.)
TEST(RecipeMergeTest, disjointFieldEditsDissolveTheConflict)
{
    RecipeSection base = {{"p", feature("p", {{"Length", "10 mm"}, {"Type", "Length"}})}};

    RecipeSection a = base;
    a["p"].fields["Length"] = "20 mm";  // A edits Length only

    RecipeSection b = base;
    b["p"].fields["Type"] = "TwoLengths";  // B edits Type only

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);
    ASSERT_EQ(valueConflictsFor(conflicts, "p"), 1u);  // object-granular: one whole-object clash

    std::vector<MergeConflict> refined = RecipeMerge::refineConflicts(conflicts, base, a, b, merged);

    EXPECT_EQ(valueConflictsFor(refined, "p"), 0u);             // dissolved — disjoint dimensions
    EXPECT_EQ(merged.at("p").fields.at("Length"), "20 mm");     // A's edit landed
    EXPECT_EQ(merged.at("p").fields.at("Type"), "TwoLengths");  // and B's, on the same node
}

// The same field changed to different values on both sides stays a conflict, now named at the
// field. (Distinguishes real overlap from the disjoint case above.)
TEST(RecipeMergeTest, sameFieldEditsRemainAFieldConflict)
{
    RecipeSection base = {{"p", feature("p", {{"Length", "10 mm"}})}};

    RecipeSection a = base;
    a["p"].fields["Length"] = "20 mm";
    RecipeSection b = base;
    b["p"].fields["Length"] = "30 mm";

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);

    std::vector<MergeConflict> refined = RecipeMerge::refineConflicts(conflicts, base, a, b, merged);

    ASSERT_EQ(valueConflictsFor(refined, "p"), 1u);
    EXPECT_NE(refined[0].detail.find("Length"), std::string::npos);  // names the clashing field
    EXPECT_EQ(merged.at("p").fields.at("Length"), "20 mm");          // A kept provisionally
}

// A mix: one field clashes, another is disjoint. Only the overlap survives, and the disjoint
// edit from each side still merges. (A whole-object pass would report the object once and lose
// the disjoint merge; an over-fine pass that re-represented everything would report both.)
TEST(RecipeMergeTest, mixedEditsKeepOnlyTheOverlappingField)
{
    RecipeSection base = {{"p", feature("p", {{"Length", "10 mm"}, {"Midplane", "false"}})}};

    RecipeSection a = base;
    a["p"].fields["Length"] = "20 mm";   // clashes
    a["p"].fields["Midplane"] = "true";  // disjoint (B leaves it)

    RecipeSection b = base;
    b["p"].fields["Length"] = "30 mm";  // clashes

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);

    std::vector<MergeConflict> refined = RecipeMerge::refineConflicts(conflicts, base, a, b, merged);

    ASSERT_EQ(valueConflictsFor(refined, "p"), 1u);  // only Length remains
    EXPECT_NE(refined[0].detail.find("Length"), std::string::npos);
    EXPECT_EQ(merged.at("p").fields.at("Midplane"), "true");  // A's disjoint edit kept
    EXPECT_EQ(merged.at("p").fields.at("Length"), "20 mm");   // clash keeps A provisionally
}

// A changed link is a disjoint dimension from a changed field: they auto-merge. (Proves links
// participate in the refinement, not just scalar fields.)
TEST(RecipeMergeTest, disjointFieldAndLinkEditsDissolve)
{
    RecipeSection base = {{"p", feature("p", {{"Length", "10 mm"}}, {{"sketch1", 0}})}};

    RecipeSection a = base;
    a["p"].fields["Length"] = "20 mm";  // A edits a field

    RecipeSection b = base;
    b["p"].refs = {{"sketch2", 0}};  // B re-points the profile link

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);
    ASSERT_EQ(valueConflictsFor(conflicts, "p"), 1u);

    std::vector<MergeConflict> refined = RecipeMerge::refineConflicts(conflicts, base, a, b, merged);

    EXPECT_EQ(valueConflictsFor(refined, "p"), 0u);          // dissolved
    EXPECT_EQ(merged.at("p").fields.at("Length"), "20 mm");  // A's field edit
    ASSERT_EQ(merged.at("p").refs.size(), 1u);
    EXPECT_EQ(merged.at("p").refs[0].target, "sketch2");  // B's link edit
}

// A delete-vs-edit conflict has no two field sets to reconcile, so it is NOT refined — it
// passes through as the whole-object conflict it is. (The refinement must not fabricate a
// field merge against a deleted object.)
TEST(RecipeMergeTest, deleteVersusEditPassesThroughUnrefined)
{
    RecipeSection base = {{"p", feature("p", {{"Length", "10 mm"}})}};

    RecipeSection a = base;
    a.erase("p");  // A deletes the object

    RecipeSection b = base;
    b["p"].fields["Length"] = "20 mm";  // B edits it

    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, a, b, conflicts);
    ASSERT_EQ(valueConflictsFor(conflicts, "p"), 1u);

    std::vector<MergeConflict> refined = RecipeMerge::refineConflicts(conflicts, base, a, b, merged);

    ASSERT_EQ(valueConflictsFor(refined, "p"), 1u);     // still one whole-object conflict
    EXPECT_EQ(refined[0].detail, conflicts[0].detail);  // unchanged, not field-decomposed
}

// A Referential conflict is about identity across objects, not a field, so the refinement pass
// leaves it exactly as it was (negative control: refinement only touches Value conflicts).
TEST(RecipeMergeTest, referentialConflictPassesThroughRefinement)
{
    RecipeSection empty;
    std::vector<MergeConflict> conflicts = {
        {MergeConflict::Kind::Referential, "cV", "Vertical", "references deleted entity g2"}
    };

    RecipeSection merged;
    std::vector<MergeConflict> refined
        = RecipeMerge::refineConflicts(conflicts, empty, empty, empty, merged);

    ASSERT_EQ(refined.size(), 1u);
    EXPECT_EQ(refined[0].kind, MergeConflict::Kind::Referential);
    EXPECT_EQ(refined[0].id, "cV");
    EXPECT_EQ(refined[0].detail, "references deleted entity g2");
}

// --- Two-way comparison (history): what changed from one version to the next ---------------

TEST(RecipeDiff, equalSectionsHaveNoChanges)
{
    EXPECT_TRUE(RecipeDiff::twoWay(baseConstraints(), baseConstraints()).empty());
}

TEST(RecipeDiff, oneChangedFieldIsOneChangeNotAWholeNode)
{
    const RecipeSection before {{"p", feature("p", {{"Length", "10 mm"}, {"Reversed", "false"}})}};
    const RecipeSection after {{"p", feature("p", {{"Length", "40 mm"}, {"Reversed", "false"}})}};

    const std::vector<RecipeChange> changes = RecipeDiff::twoWay(before, after);

    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].kind, RecipeChange::Kind::FieldChanged);
    EXPECT_EQ(changes[0].field, "Length");
    EXPECT_EQ(changes[0].before, std::optional<std::string>("10 mm"));
    EXPECT_EQ(changes[0].after, std::optional<std::string>("40 mm"));
}

TEST(RecipeDiff, aFieldSetOrClearedHasNoValueOnTheMissingSide)
{
    const RecipeSection before {{"p", feature("p", {{"Old", "1"}})}};
    const RecipeSection after {{"p", feature("p", {{"New", "2"}})}};

    const std::vector<RecipeChange> changes = RecipeDiff::twoWay(before, after);

    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].field, "New");
    EXPECT_FALSE(changes[0].before.has_value());
    EXPECT_EQ(changes[1].field, "Old");
    EXPECT_FALSE(changes[1].after.has_value());
}

TEST(RecipeDiff, addedAndRemovedNodesCarryTheirType)
{
    RecipeSection after = baseGeometry();
    after.erase("g3");
    after["g9"] = geo("g9", "Circle");

    const std::vector<RecipeChange> changes = RecipeDiff::twoWay(baseGeometry(), after);

    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].kind, RecipeChange::Kind::Removed);
    EXPECT_EQ(changes[0].id, "g3");
    EXPECT_EQ(changes[1].kind, RecipeChange::Kind::Added);
    EXPECT_EQ(changes[1].type, "Circle");
}

TEST(RecipeDiff, aMovedReferenceIsOneLinksChangeWithBothSets)
{
    RecipeSection after = baseConstraints();
    after["c0"].refs = {{"g0", 2}, {"g3", 1}};

    const std::vector<RecipeChange> changes = RecipeDiff::twoWay(baseConstraints(), after);

    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].kind, RecipeChange::Kind::LinksChanged);
    EXPECT_EQ(changes[0].refsBefore, baseConstraints().at("c0").refs);
    EXPECT_EQ(changes[0].refsAfter, after.at("c0").refs);
}

namespace
{

RecipeRef section(const std::string& target)
{
    RecipeRef ref;
    ref.target = target;
    ref.link = "Sections";
    return ref;
}

// A loft-like node whose Sections link is ordered, merged from the ancestor s1 s2 s3.
std::optional<std::vector<std::string>> mergedSections(
    const std::vector<std::string>& a,
    const std::vector<std::string>& b
)
{
    const auto node = [](const std::vector<std::string>& targets) {
        std::vector<RecipeRef> refs;
        for (const std::string& target : targets) {
            refs.push_back(section(target));
        }
        return RecipeNode {"loft", "Loft", {}, refs};
    };
    const RecipeSection base {{"loft", node({"s1", "s2", "s3"})}};
    const RecipeSection sideA {{"loft", node(a)}};
    const RecipeSection sideB {{"loft", node(b)}};
    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, sideA, sideB, conflicts);
    if (!RecipeMerge::refineConflicts(conflicts, base, sideA, sideB, merged).empty()) {
        return std::nullopt;
    }
    std::vector<std::string> order;
    for (const RecipeRef& ref : merged.at("loft").refs) {
        order.push_back(ref.target);
    }
    return order;
}

}  // namespace

// Both sides change an ordered list: the one sequence that keeps both sides' changes is taken,
// and a conflict stands where there is none or more than one.
TEST(RecipeMerge, anOrderedLinkTakesTheOneSequenceThatKeepsBothSidesChanges)
{
    using Order = std::vector<std::string>;
    EXPECT_EQ(
        mergedSections({"s1", "x", "s2", "s3"}, {"s1", "s2", "s3", "y"}),
        (Order {"s1", "x", "s2", "s3", "y"})
    ) << "inserts at different places both land";
    EXPECT_EQ(mergedSections({"s3", "s1", "s2"}, {"s1", "s3"}), (Order {"s3", "s1"}))
        << "a move on one side and a delete on the other";
    EXPECT_EQ(mergedSections({"s1", "x", "s2", "s3"}, {"s1", "y", "s2", "s3"}), std::nullopt)
        << "two inserts at one place: more than one sequence";
    EXPECT_EQ(mergedSections({"s2", "s1", "s3"}, {"s1", "s3", "s2"}), std::nullopt)
        << "the sides' moves contradict: no sequence";
}

// A constraint's element places are slots, not a collection: two changes stay one conflict.
TEST(RecipeMerge, aConstraintsReferencesStayOneValue)
{
    const RecipeSection base {{"c", con("c", "Coincident", {{"p", 1}, {"q", 1}})}};
    const RecipeSection sideA {{"c", con("c", "Coincident", {{"q", 1}, {"p", 1}})}};
    const RecipeSection sideB {{"c", con("c", "Coincident", {{"p", 1}, {"r", 1}})}};
    std::vector<MergeConflict> conflicts;
    RecipeSection merged = RecipeMerge::threeWay(base, sideA, sideB, conflicts);
    EXPECT_EQ(RecipeMerge::refineConflicts(conflicts, base, sideA, sideB, merged).size(), 1U);
}
