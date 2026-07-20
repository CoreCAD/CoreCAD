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

}  // namespace
