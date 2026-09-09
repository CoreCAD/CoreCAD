// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <filesystem>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/GeometryCache.h>
#include <App/StoredRecipe.h>
#include <Base/FileInfo.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/FeaturePartCut.h>
#include <src/App/InitApplication.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// Whether a solid the recipe describes has to be built again, or can be handed back.
///
/// The document file carries the steps and never the solid, so opening a part means building it.
/// The project cache is what stops that being paid every time -- and the whole question is not
/// keeping a solid but knowing when a kept solid has stopped being true. Nothing here counts
/// versions: an entry is named by a digest of the text that produced it and of the entries its
/// inputs came from, so the name and the content cannot come apart.
class GeometryCacheTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("GeometryCache_test", "testUser");
        _cache = Base::FileInfo::getTempFileName();
        Base::FileInfo(_cache).createDirectory();
    }

    void TearDown() override
    {
        if (_rebuilt != nullptr) {
            App::GetApplication().closeDocument(_rebuilt->getName());
        }
        App::GetApplication().closeDocument(_doc->getName());
        std::error_code failed;
        std::filesystem::remove_all(_cache, failed);
    }

    /// The document as a reader of the file would have it: every object present, nothing built.
    /// Deliberately NOT recomputed -- a shape that turns up in here can only have come from the
    /// cache, which is what makes the assertions below say something.
    App::Document* readBackWithoutBuilding(const std::string& written)
    {
        _rebuilt = App::GetApplication().newDocument("GeometryCache_rebuilt", "testUser");
        std::istringstream text(written);
        App::restoreStoredRecipe(*_rebuilt, text);
        return _rebuilt;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc = nullptr;
    App::Document* _rebuilt = nullptr;
    std::string _cache;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The point of the whole mechanism: a part whose recipe has not changed comes back with its solid
// already made. The document read here is never recomputed, so a solid in it is proof the cache
// answered rather than the kernel.
TEST_F(GeometryCacheTest, aKeptSolidIsHandedBackInsteadOfBuiltAgain)
{
    // Arrange
    auto* box = _doc->addObject<Part::Box>("Block");
    box->Length.setValue(10.0);
    box->Width.setValue(20.0);
    box->Height.setValue(30.0);
    _doc->recompute();
    App::storeBuiltGeometry(*_doc, _cache);

    // Act
    App::Document* rebuilt = readBackWithoutBuilding(App::formatStoredRecipe(*_doc));
    const std::set<App::DocumentObject*> toBuild = App::restoreBuiltGeometry(*rebuilt, _cache);

    // Assert -- nothing left to build, and the solid is there without a recompute having run.
    EXPECT_TRUE(toBuild.empty());
    auto* returned = dynamic_cast<Part::Box*>(rebuilt->getObject("Block"));
    ASSERT_NE(returned, nullptr);
    ASSERT_FALSE(returned->Shape.getShape().isNull());
    EXPECT_NEAR(returned->Shape.getShape().getBoundBox().MaxZ, 30.0, 1e-7);
    // And it is settled: the recompute that follows an open has nothing to do with it.
    EXPECT_FALSE(returned->isTouched());
}

// Without the cache the same document has everything to build. This is the reading that makes the
// test above mean something: the metric tells the two situations apart.
TEST_F(GeometryCacheTest, withNothingKeptEveryObjectHasToBeBuilt)
{
    // Arrange -- built, but never stored.
    auto* box = _doc->addObject<Part::Box>("Block");
    box->Length.setValue(10.0);
    _doc->recompute();

    // Act
    App::Document* rebuilt = readBackWithoutBuilding(App::formatStoredRecipe(*_doc));
    const std::set<App::DocumentObject*> toBuild = App::restoreBuiltGeometry(*rebuilt, _cache);

    // Assert
    EXPECT_EQ(toBuild.size(), 1U);
    auto* returned = dynamic_cast<Part::Box*>(rebuilt->getObject("Block"));
    ASSERT_NE(returned, nullptr);
    EXPECT_TRUE(returned->Shape.getShape().isNull());
}

// One changed number rebuilds what stands on it and nothing else. This is the reason the key is
// per object rather than the whole file: a part with a hundred features that had one edited would
// otherwise pay for all hundred.
TEST_F(GeometryCacheTest, oneChangedNumberInvalidatesOnlyWhatStandsOnIt)
{
    // Arrange -- two blocks and a cut that stands on both.
    auto* base = _doc->addObject<Part::Box>("Base");
    base->Length.setValue(10.0);
    base->Width.setValue(10.0);
    base->Height.setValue(10.0);
    auto* tool = _doc->addObject<Part::Box>("Tool");
    tool->Length.setValue(4.0);
    tool->Width.setValue(4.0);
    tool->Height.setValue(4.0);
    auto* cut = _doc->addObject<Part::Cut>("Cut");
    cut->Base.setValue(base);
    cut->Tool.setValue(tool);
    _doc->recompute();
    App::storeBuiltGeometry(*_doc, _cache);

    App::Document* rebuilt = readBackWithoutBuilding(App::formatStoredRecipe(*_doc));
    ASSERT_TRUE(App::restoreBuiltGeometry(*rebuilt, _cache).empty());

    // Act -- edit the tool only.
    auto* editedTool = dynamic_cast<Part::Box*>(rebuilt->getObject("Tool"));
    ASSERT_NE(editedTool, nullptr);
    editedTool->Length.setValue(6.0);
    const std::set<App::DocumentObject*> toBuild = App::restoreBuiltGeometry(*rebuilt, _cache);

    // Assert -- the tool and the cut standing on it, never the untouched base.
    EXPECT_EQ(toBuild.count(editedTool), 1U);
    EXPECT_EQ(toBuild.count(rebuilt->getObject("Cut")), 1U);
    EXPECT_EQ(toBuild.count(rebuilt->getObject("Base")), 0U);
}

// The name of an entry is a digest of its content, so storing the same result twice stores it
// once. A save that changed nothing writes nothing, which is what lets version control ignore
// the whole directory without ignoring anything that matters.
TEST_F(GeometryCacheTest, storingTheSameResultTwiceKeepsOneEntry)
{
    // Arrange
    auto* box = _doc->addObject<Part::Box>("Block");
    box->Length.setValue(10.0);
    _doc->recompute();

    // Act
    App::storeBuiltGeometry(*_doc, _cache);
    App::storeBuiltGeometry(*_doc, _cache);

    // Assert
    const std::filesystem::path entries = std::filesystem::path(_cache) / "geometry";
    ASSERT_TRUE(std::filesystem::is_directory(entries));
    EXPECT_EQ(
        std::distance(
            std::filesystem::directory_iterator(entries),
            std::filesystem::directory_iterator {}
        ),
        1
    );
}

// A digest is not a version number: it is derived from the content and from nothing else, so two
// people who never spoke arrive at the same name for the same part, and neither has to agree with
// the other about a count.
TEST_F(GeometryCacheTest, twoDocumentsWithTheSameRecipeAgreeOnTheName)
{
    // Arrange
    auto* box = _doc->addObject<Part::Box>("Block");
    box->Length.setValue(10.0);
    _doc->recompute();
    const std::string first = App::builtGeometryKey(*box);

    // Act -- the same part as a second party would hold it, read from the file.
    App::Document* rebuilt = readBackWithoutBuilding(App::formatStoredRecipe(*_doc));
    auto* returned = rebuilt->getObject("Block");
    ASSERT_NE(returned, nullptr);
    const std::string second = App::builtGeometryKey(*returned);

    // Assert
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
