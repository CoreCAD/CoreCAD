// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Cruth contributors

/* A sketch entity carries a durable tag, and that tag is persisted. Persistence is
 * what lets identity survive save/reopen — but the copy path also routes through
 * save/restore, so without a rule to tell the two apart a pasted sketch comes back
 * wearing the source's identity.
 *
 * The rule: duplication mints, continuation preserves. These tests pin both sides,
 * because a check that only ever asserts one of them cannot tell a working
 * implementation from one that mints (or preserves) unconditionally.
 */

#include <gtest/gtest.h>

#include <FCConfig.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace
{

class SketchEntityIdentityTest: public ::testing::Test
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

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("entityIdentity");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    /// A sketch with two lines, each carrying a tag of its own.
    Sketcher::SketchObject* makeSketch(App::Document* doc, const char* name)
    {
        auto* sketch = static_cast<Sketcher::SketchObject*>(
            doc->addObject("Sketcher::SketchObject", name)
        );
        Part::GeomLineSegment first;
        first.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
        Part::GeomLineSegment second;
        second.setPoints(Base::Vector3d(10, 0, 0), Base::Vector3d(10, 10, 0));
        sketch->addGeometry(&first);
        sketch->addGeometry(&second);
        doc->recompute();
        return sketch;
    }

    static std::vector<std::string> tagsOf(const Sketcher::SketchObject* sketch)
    {
        std::vector<std::string> tags;
        for (const auto* geom : sketch->Geometry.getValues()) {
            tags.emplace_back(boost::uuids::to_string(geom->getTag()));
        }
        return tags;
    }

    static std::vector<std::string> constraintTagsOf(const Sketcher::SketchObject* sketch)
    {
        std::vector<std::string> tags;
        for (const auto* c : sketch->Constraints.getValues()) {
            tags.emplace_back(boost::uuids::to_string(c->getTag()));
        }
        return tags;
    }

    /// Add a horizontal constraint on the given geometry, so the sketch carries a
    /// constraint with an identity of its own.
    static void addHorizontal(Sketcher::SketchObject* sketch, int geoId)
    {
        Sketcher::Constraint c;
        c.Type = Sketcher::Horizontal;
        c.setElement(0, Sketcher::GeoElementId(geoId, Sketcher::PointPos::none));
        sketch->addConstraint(&c);
        sketch->getDocument()->recompute();
    }

    App::Document* _doc {nullptr};
};

// Duplication: the pasted entities are new entities and must say so.
TEST_F(SketchEntityIdentityTest, copyIntoSameDocumentMintsEntityTags)
{
    auto* source = makeSketch(_doc, "Src");
    const auto sourceTags = tagsOf(source);

    const auto copies = _doc->copyObject({source}, false);
    ASSERT_EQ(copies.size(), 1U);
    _doc->recompute();
    const auto copyTags = tagsOf(static_cast<Sketcher::SketchObject*>(copies.front()));

    ASSERT_EQ(copyTags.size(), sourceTags.size());
    const std::set<std::string> sourceSet(sourceTags.begin(), sourceTags.end());
    for (const auto& tag : copyTags) {
        EXPECT_EQ(sourceSet.count(tag), 0U) << "pasted entity kept the source's identity";
    }
    // ...and the copies are distinct from one another, not one tag reused twice.
    const std::set<std::string> copySet(copyTags.begin(), copyTags.end());
    EXPECT_EQ(copySet.size(), copyTags.size());
}

// The object's own identity follows the same rule, at its own grain.
TEST_F(SketchEntityIdentityTest, copyIntoSameDocumentMintsObjectIdentity)
{
    auto* source = makeSketch(_doc, "Src");
    const std::string sourceUid = source->Uid.getValueStr();

    const auto copies = _doc->copyObject({source}, false);
    ASSERT_EQ(copies.size(), 1U);

    EXPECT_NE(copies.front()->Uid.getValueStr(), sourceUid);
}

// Relocation is not duplication: a move keeps identity at every grain. This is the
// case that fails if minting is wired to the import path rather than to duplication.
TEST_F(SketchEntityIdentityTest, moveToAnotherDocumentPreservesIdentity)
{
    auto* source = makeSketch(_doc, "Src");
    const auto sourceTags = tagsOf(source);
    const std::string sourceUid = source->Uid.getValueStr();

    App::Document* other = App::GetApplication().newDocument("entityIdentityDest");
    auto* moved = static_cast<Sketcher::SketchObject*>(other->moveObject(source, true));
    ASSERT_NE(moved, nullptr);
    other->recompute();

    EXPECT_EQ(moved->Uid.getValueStr(), sourceUid);
    EXPECT_EQ(tagsOf(moved), sourceTags);

    App::GetApplication().closeDocument(other->getName());
}

// A constraint is an authored entity too: duplication mints it a fresh identity.
// This passes today only by accident (the tag is discarded at save, so a pasted
// constraint is reborn from its constructor); once the tag persists, it holds only
// if duplication actively re-mints. That is the regression this pins.
TEST_F(SketchEntityIdentityTest, copyIntoSameDocumentMintsConstraintTags)
{
    auto* source = makeSketch(_doc, "Src");
    addHorizontal(source, 0);
    const auto sourceTags = constraintTagsOf(source);
    ASSERT_FALSE(sourceTags.empty());

    const auto copies = _doc->copyObject({source}, false);
    ASSERT_EQ(copies.size(), 1U);
    _doc->recompute();
    const auto copyTags = constraintTagsOf(static_cast<Sketcher::SketchObject*>(copies.front()));

    ASSERT_EQ(copyTags.size(), sourceTags.size());
    const std::set<std::string> sourceSet(sourceTags.begin(), sourceTags.end());
    for (const auto& tag : copyTags) {
        EXPECT_EQ(sourceSet.count(tag), 0U) << "pasted constraint kept the source's identity";
    }
}

// Persistence: a constraint's identity must survive a real save and reopen. This
// fails today — the tag is thrown away at save and the constraint comes back with a
// fresh one.
TEST_F(SketchEntityIdentityTest, constraintTagSurvivesSaveAndReload)
{
    auto* source = makeSketch(_doc, "Src");
    addHorizontal(source, 0);
    const auto before = constraintTagsOf(source);
    ASSERT_FALSE(before.empty());

    const std::string path
        = (std::filesystem::temp_directory_path() / "trait1_constraint_reload.FCStd").string();
    const std::string docName = _doc->getName();
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(docName.c_str());

    _doc = App::GetApplication().openDocument(path.c_str());  // hand to TearDown
    ASSERT_NE(_doc, nullptr);
    auto* reloaded = static_cast<Sketcher::SketchObject*>(_doc->getObject("Src"));
    ASSERT_NE(reloaded, nullptr);

    EXPECT_EQ(constraintTagsOf(reloaded), before)
        << "constraint identity did not survive save/reload";

    std::filesystem::remove(path);
}

// Brick two, end-to-end: a constraint's reference survives a real save/reopen still bound
// to the same geometry, resolved through the geometry's durable tag. This drives the full
// container path — PropertyConstraintList::Save writes the tags, onSketchRestore rebinds
// GeoIds from them — that the Constraint-level unit tests bypass. A plain round-trip does
// not reorder geometry, so this is a regression guard on that path, not the reorder proof.
TEST_F(SketchEntityIdentityTest, constraintReferenceStaysBoundAcrossSaveAndReload)
{
    auto* source = makeSketch(_doc, "Src");
    addHorizontal(source, 1);  // constrain the second line
    ASSERT_FALSE(source->Constraints.getValues().empty());

    const int refGeoId = source->Constraints.getValues().front()->getElement(0).GeoId;
    ASSERT_GE(refGeoId, 0);
    const std::string refTag = boost::uuids::to_string(
        source->getInternalGeometry()[refGeoId]->getTag()
    );

    const std::string path
        = (std::filesystem::temp_directory_path() / "trait1_constraint_ref_reload.FCStd").string();
    const std::string docName = _doc->getName();
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(docName.c_str());

    _doc = App::GetApplication().openDocument(path.c_str());  // hand to TearDown
    ASSERT_NE(_doc, nullptr);
    auto* reloaded = static_cast<Sketcher::SketchObject*>(_doc->getObject("Src"));
    ASSERT_NE(reloaded, nullptr);
    ASSERT_FALSE(reloaded->Constraints.getValues().empty());

    const int reloadedGeoId = reloaded->Constraints.getValues().front()->getElement(0).GeoId;
    ASSERT_GE(reloadedGeoId, 0);
    EXPECT_EQ(boost::uuids::to_string(reloaded->getInternalGeometry()[reloadedGeoId]->getTag()), refTag)
        << "constraint reference did not resolve to the same geometry after reload";

    std::filesystem::remove(path);
}

// The two Geometry-level primitives the rule is built on, pinned in opposite
// directions so neither can drift into the other's behaviour unnoticed.
TEST_F(SketchEntityIdentityTest, copyMintsAndCloneAndMintDurableIdentityAgree)
{
    Part::GeomLineSegment line;
    line.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(1, 0, 0));

    std::unique_ptr<Part::Geometry> copied(line.copy());
    EXPECT_NE(copied->getTag(), line.getTag()) << "copy() is duplication";

    std::unique_ptr<Part::Geometry> cloned(line.clone());
    EXPECT_EQ(cloned->getTag(), line.getTag()) << "clone() is continuation";

    const auto before = cloned->getTag();
    cloned->mintDurableIdentity();
    EXPECT_NE(cloned->getTag(), before);
}

// ---------------------------------------------------------------------------
// Brick three: recorded parentage across a split (§4.7 retire+mint; Amendment 10).
// ---------------------------------------------------------------------------

// A canonical, order-independent view of the parentage log, so two logs compare
// by content regardless of entry or uuid order.
static std::vector<std::string> parentageStrings(const Sketcher::SketchObject* sketch)
{
    std::vector<std::string> out;
    for (const auto& entry : sketch->ParentageLog.getEntries()) {
        std::vector<std::string> parents;
        std::vector<std::string> children;
        for (const auto& u : entry.parents) {
            parents.emplace_back(boost::uuids::to_string(u));
        }
        for (const auto& u : entry.children) {
            children.emplace_back(boost::uuids::to_string(u));
        }
        std::sort(parents.begin(), parents.end());
        std::sort(children.begin(), children.end());
        std::string s = std::to_string(static_cast<int>(entry.op)) + "|";
        for (const auto& p : parents) {
            s += p + ",";
        }
        s += "|";
        for (const auto& c : children) {
            s += c + ",";
        }
        out.push_back(s);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Splitting an open curve is a 1->2 retire+mint: the parent retires and the log
// records exactly {parent -> child1, child2}.
TEST_F(SketchEntityIdentityTest, openCurveSplitRecordsParentage)
{
    auto* sketch = makeSketch(_doc, "Src");
    const std::string parentTag = boost::uuids::to_string(sketch->getInternalGeometry()[0]->getTag());
    ASSERT_TRUE(sketch->ParentageLog.isEmpty());

    sketch->split(0, Base::Vector3d(5, 0, 0));
    _doc->recompute();

    const auto& entries = sketch->ParentageLog.getEntries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front().op, Sketcher::ParentageOp::Split);

    ASSERT_EQ(entries.front().parents.size(), 1U);
    EXPECT_EQ(boost::uuids::to_string(entries.front().parents.front()), parentTag);

    std::set<std::string> childStrs;
    for (const auto& c : entries.front().children) {
        childStrs.insert(boost::uuids::to_string(c));
    }
    EXPECT_EQ(childStrs.size(), 2U) << "a split yields two distinct children";
    EXPECT_EQ(childStrs.count(parentTag), 0U) << "no child is its parent";

    // The parent has retired: it names no live geometry.
    for (const auto& tag : tagsOf(sketch)) {
        EXPECT_NE(tag, parentTag) << "the split parent's identity must retire";
    }
}

// The discriminating opposite: splitting a closed curve (a circle) at one point
// yields a single open arc — the entity stays one entity, so nothing is recorded.
// This shares the exact if/else the open-curve case takes, so it proves the record
// is written for a real retirement, not for every split.
TEST_F(SketchEntityIdentityTest, closedCurveSplitRecordsNothing)
{
    auto* sketch = static_cast<Sketcher::SketchObject*>(
        _doc->addObject("Sketcher::SketchObject", "Circ")
    );
    Part::GeomCircle circle;
    circle.setCenter(Base::Vector3d(0, 0, 0));
    circle.setRadius(5.0);
    sketch->addGeometry(&circle);
    _doc->recompute();
    ASSERT_TRUE(sketch->ParentageLog.isEmpty());

    sketch->split(0, Base::Vector3d(5, 0, 0));
    _doc->recompute();

    EXPECT_TRUE(sketch->ParentageLog.isEmpty())
        << "a closed-curve split keeps one identity and records no succession";
}

// The record is persisted document data: it survives a real save and reopen.
TEST_F(SketchEntityIdentityTest, splitParentageSurvivesSaveAndReload)
{
    auto* sketch = makeSketch(_doc, "Src");
    sketch->split(0, Base::Vector3d(5, 0, 0));
    _doc->recompute();
    const auto before = parentageStrings(sketch);
    ASSERT_EQ(before.size(), 1U);

    const std::string path
        = (std::filesystem::temp_directory_path() / "trait1_parentage_reload.FCStd").string();
    const std::string docName = _doc->getName();
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(docName.c_str());

    _doc = App::GetApplication().openDocument(path.c_str());  // hand to TearDown
    ASSERT_NE(_doc, nullptr);
    auto* reloaded = static_cast<Sketcher::SketchObject*>(_doc->getObject("Src"));
    ASSERT_NE(reloaded, nullptr);

    EXPECT_EQ(parentageStrings(reloaded), before) << "split parentage did not survive save/reload";

    std::filesystem::remove(path);
}

// A duplicate's entities descend from nothing in their new home, so its log is
// empty even though the source's is not (Amendment 10, Clause 10.2).
TEST_F(SketchEntityIdentityTest, splitLogEmptiesOnDuplication)
{
    auto* source = makeSketch(_doc, "Src");
    source->split(0, Base::Vector3d(5, 0, 0));
    _doc->recompute();
    ASSERT_FALSE(source->ParentageLog.isEmpty());

    const auto copies = _doc->copyObject({source}, false);
    ASSERT_EQ(copies.size(), 1U);
    _doc->recompute();

    auto* copy = static_cast<Sketcher::SketchObject*>(copies.front());
    EXPECT_TRUE(copy->ParentageLog.isEmpty()) << "a duplicate's parentage log must be empty";
}

// A line crossed at two points, trimmed between them, is severed in two: a 1->2
// retire+mint just like an open-curve split. The parent retires and the log
// records exactly {parent -> child1, child2}, tagged as a TrimSever.
TEST_F(SketchEntityIdentityTest, trimThatSeversRecordsParentage)
{
    auto* sketch = static_cast<Sketcher::SketchObject*>(
        _doc->addObject("Sketcher::SketchObject", "Sev")
    );
    Part::GeomLineSegment horizontal;
    horizontal.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(20, 0, 0));
    Part::GeomLineSegment crossLeft;
    crossLeft.setPoints(Base::Vector3d(5, -5, 0), Base::Vector3d(5, 5, 0));
    Part::GeomLineSegment crossRight;
    crossRight.setPoints(Base::Vector3d(15, -5, 0), Base::Vector3d(15, 5, 0));
    sketch->addGeometry(&horizontal);  // GeoId 0
    sketch->addGeometry(&crossLeft);   // GeoId 1
    sketch->addGeometry(&crossRight);  // GeoId 2
    _doc->recompute();

    const std::string parentTag = boost::uuids::to_string(sketch->getInternalGeometry()[0]->getTag());
    ASSERT_TRUE(sketch->ParentageLog.isEmpty());

    // Trim the middle of the horizontal line, between the two crossing lines.
    sketch->trim(0, Base::Vector3d(10, 0, 0));
    _doc->recompute();

    const auto& entries = sketch->ParentageLog.getEntries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front().op, Sketcher::ParentageOp::TrimSever);

    ASSERT_EQ(entries.front().parents.size(), 1U);
    EXPECT_EQ(boost::uuids::to_string(entries.front().parents.front()), parentTag);

    std::set<std::string> childStrs;
    for (const auto& c : entries.front().children) {
        childStrs.insert(boost::uuids::to_string(c));
    }
    EXPECT_EQ(childStrs.size(), 2U) << "a severing trim yields two distinct children";
    EXPECT_EQ(childStrs.count(parentTag), 0U) << "no child is its parent";

    for (const auto& tag : tagsOf(sketch)) {
        EXPECT_NE(tag, parentTag) << "the severed parent's identity must retire";
    }
}

// The discriminating opposite: a line crossed at only one point, trimmed toward an
// end, keeps its single identity — a shorten, not a sever — so nothing is recorded.
// This shares the exact 1->1 vs 1->2 branch the severing case takes, proving the
// record is written for a real retirement and not for every trim.
TEST_F(SketchEntityIdentityTest, shortenOnlyTrimRecordsNothing)
{
    auto* sketch = static_cast<Sketcher::SketchObject*>(
        _doc->addObject("Sketcher::SketchObject", "Short")
    );
    Part::GeomLineSegment horizontal;
    horizontal.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(20, 0, 0));
    Part::GeomLineSegment cross;
    cross.setPoints(Base::Vector3d(5, -5, 0), Base::Vector3d(5, 5, 0));
    sketch->addGeometry(&horizontal);  // GeoId 0
    sketch->addGeometry(&cross);       // GeoId 1
    _doc->recompute();
    ASSERT_TRUE(sketch->ParentageLog.isEmpty());

    // Trim the portion past the single intersection: one piece survives, one identity.
    sketch->trim(0, Base::Vector3d(12, 0, 0));
    _doc->recompute();

    EXPECT_TRUE(sketch->ParentageLog.isEmpty())
        << "a shorten-only trim keeps one identity and records no succession";
}

}  // namespace
