// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>

#include <BRepPrimAPI_MakeBox.hxx>

#include <App/Application.h>
#include <Base/FileInfo.h>
#include <App/Document.h>
#include <App/StoredRecipe.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <src/App/InitApplication.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// What the stored recipe does with a solid, which depends entirely on where the solid came from.
///
/// A feature's shape is the OUTPUT of the recipe and is rebuilt from it, so storing it would put
/// the answer next to the question and let the file disagree with itself. An imported solid is
/// the opposite case: nothing in the document can produce it, so it is the authored input itself
/// and a file that dropped it would be describing a part that cannot be rebuilt.
class StoredRecipeGeometryTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("StoredRecipeGeometry_test", "testUser");
    }

    void TearDown() override
    {
        if (_rebuilt != nullptr) {
            App::GetApplication().closeDocument(_rebuilt->getName());
        }
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* readBack(const std::string& written)
    {
        _rebuilt = App::GetApplication().newDocument("StoredRecipeGeometry_rebuilt", "testUser");
        std::istringstream text(written);
        App::restoreStoredRecipe(*_rebuilt, text);
        _rebuilt->recompute();
        return _rebuilt;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc = nullptr;
    App::Document* _rebuilt = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// An imported solid IS the authored content: no property of the document produces it, so a recipe
// that left it out would rebuild an empty document and call that the same part.
TEST_F(StoredRecipeGeometryTest, anImportedSolidIsCarriedBecauseNothingCanRebuildIt)
{
    // Arrange -- exactly what an import leaves behind: a plain feature holding a shape.
    auto* imported = _doc->addObject<Part::Feature>("Imported");
    imported->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    _doc->recompute();
    const Base::BoundBox3d expected = imported->Shape.getShape().getBoundBox();

    // Act
    App::Document* rebuilt = readBack(App::formatStoredRecipe(*_doc));

    // Assert
    auto* returned = dynamic_cast<Part::Feature*>(rebuilt->getObject("Imported"));
    ASSERT_NE(returned, nullptr);
    ASSERT_FALSE(returned->Shape.getShape().isNull());
    const Base::BoundBox3d built = returned->Shape.getShape().getBoundBox();
    EXPECT_NEAR(built.MaxX, expected.MaxX, 1e-7);
    EXPECT_NEAR(built.MaxY, expected.MaxY, 1e-7);
    EXPECT_NEAR(built.MaxZ, expected.MaxZ, 1e-7);
}

// A primitive builds its own solid from the sizes it was given, so the solid is output and stays
// out of the file -- and the part still comes back, because the recipe is enough to rebuild it.
TEST_F(StoredRecipeGeometryTest, aBuiltSolidStaysOutOfTheFileAndIsRebuilt)
{
    // Arrange
    auto* box = _doc->addObject<Part::Box>("Block");  // NOLINT
    box->Length.setValue(10.0);
    box->Width.setValue(20.0);
    box->Height.setValue(30.0);
    _doc->recompute();

    // Act
    const std::string written = App::formatStoredRecipe(*_doc);
    App::Document* rebuilt = readBack(written);

    // Assert -- the sizes are in the file, the solid they produce is not.
    EXPECT_NE(written.find("\"Length\""), std::string::npos);
    EXPECT_EQ(written.find("<Part "), std::string::npos);
    auto* returned = dynamic_cast<Part::Box*>(rebuilt->getObject("Block"));
    ASSERT_NE(returned, nullptr);
    ASSERT_FALSE(returned->Shape.getShape().isNull());
    EXPECT_NEAR(returned->Shape.getShape().getBoundBox().MaxZ, 30.0, 1e-7);
}

// Handed-in geometry does not belong inside the recipe: it is source material, not a
// description of anything, and thousands of lines of coordinates in the middle of the file
// would defeat the one property the file exists for. It goes to the project's own folder, named
// by what it holds, and the recipe names it.
TEST_F(StoredRecipeGeometryTest, anImportedSolidIsKeptBesideTheRecipeNotInsideIt)
{
    // Arrange
    const std::string assets = Base::FileInfo::getTempFileName();
    Base::FileInfo(assets).createDirectory();
    auto* first = _doc->addObject<Part::Feature>("First");
    first->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    // The same body a second time: one import used twice is one thing, and the file it is kept
    // in is named after its contents, so it is stored once.
    auto* second = _doc->addObject<Part::Feature>("Second");
    second->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    _doc->recompute();
    const Base::BoundBox3d expected = first->Shape.getShape().getBoundBox();

    // Act
    const std::string written = App::formatStoredRecipe(*_doc, assets);

    // Assert -- the recipe names the geometry and does not contain it.
    EXPECT_EQ(written.find("DBRep_DrawableShape"), std::string::npos);
    EXPECT_NE(written.find("asset=\""), std::string::npos);
    EXPECT_EQ(
        std::distance(
            std::filesystem::directory_iterator(assets),
            std::filesystem::directory_iterator {}
        ),
        1
    );

    // And it comes back.
    _rebuilt = App::GetApplication().newDocument("StoredRecipeGeometry_rebuilt", "testUser");
    std::istringstream text(written);
    App::restoreStoredRecipe(*_rebuilt, text, /*finish=*/true, assets);
    _rebuilt->recompute();
    auto* returned = dynamic_cast<Part::Feature*>(_rebuilt->getObject("First"));
    ASSERT_NE(returned, nullptr);
    ASSERT_FALSE(returned->Shape.getShape().isNull());
    EXPECT_NEAR(returned->Shape.getShape().getBoundBox().MaxZ, expected.MaxZ, 1e-7);

    std::filesystem::remove_all(assets);
}

// A handed-in shape carries the names of its faces, and those names are the whole basis on which a
// later feature says which face it was attached to. Nothing can rebuild them -- unlike a built
// shape's, which come back with the rebuild -- so losing them on the way to the source store is the
// exact failure durable identity exists to prevent. Measured before the fix: 26 names in, 0 back.
TEST_F(StoredRecipeGeometryTest, handedInGeometryKeepsTheNamesOfItsFaces)
{
    // Arrange -- a shape that carries mapped element names, handed to a plain feature, which is
    // what a copy and a bake leave behind.
    const std::string assets = Base::FileInfo::getTempFileName();
    Base::FileInfo(assets).createDirectory();
    auto* box = _doc->addObject<Part::Box>("Block");  // NOLINT
    box->Length.setValue(10.0);
    box->Width.setValue(20.0);
    box->Height.setValue(30.0);
    _doc->recompute();
    auto* handed = _doc->addObject<Part::Feature>("Handed");
    handed->Shape.setValue(box->Shape.getShape());
    _doc->recompute();
    const size_t named = handed->Shape.getShape().getElementMapSize();
    ASSERT_GT(named, 0U) << "the arrangement itself is wrong if there are no names to lose";
    const Data::MappedName first = handed->Shape.getShape().getMappedName(Data::IndexedName("Face", 1));
    ASSERT_FALSE(first.empty());

    // Act
    const std::string written = App::formatStoredRecipe(*_doc, assets);
    _rebuilt = App::GetApplication().newDocument("StoredRecipeGeometry_rebuilt", "testUser");
    std::istringstream text(written);
    App::restoreStoredRecipe(*_rebuilt, text, /*finish=*/true, assets);

    // Assert -- the same count and the same name, not merely a shape of the right size.
    auto* returned = dynamic_cast<Part::Feature*>(_rebuilt->getObject("Handed"));
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(returned->Shape.getShape().getElementMapSize(), named);
    EXPECT_EQ(
        returned->Shape.getShape().getMappedName(Data::IndexedName("Face", 1)).toString(),
        first.toString()
    );

    std::filesystem::remove_all(assets);
}

// A save that changed nothing must write nothing new. The source store names an entry by what it
// holds, so the same body has to reach the same name every time -- otherwise a document nobody
// edited is stored twice, the line in the recipe that names it changes, and two people who saved
// the identical part get a conflicting diff on a line that means the same thing. Measured before
// the fix: one document, two saves, two entries.
TEST_F(StoredRecipeGeometryTest, aSaveThatChangedNothingWritesNothingNew)
{
    // Arrange -- a handed-in shape carrying mapped names, which is what brings a hasher table with
    // it, and the hasher table is what was being written once and then left out.
    const std::string folder = Base::FileInfo::getTempFileName();
    Base::FileInfo(folder).createDirectory();
    const std::string file = folder + "/Part.FCStd";
    auto* box = _doc->addObject<Part::Box>("Block");  // NOLINT
    box->Length.setValue(10.0);
    box->Width.setValue(20.0);
    box->Height.setValue(30.0);
    _doc->recompute();
    auto* handed = _doc->addObject<Part::Feature>("Handed");
    handed->Shape.setValue(box->Shape.getShape());
    _doc->recompute();
    ASSERT_GT(handed->Shape.getShape().getElementMapSize(), 0U);

    // Act -- saved twice, with nothing changed in between.
    _doc->saveAs(file.c_str());
    _doc->save();

    // Assert
    const std::filesystem::path assets = std::filesystem::path(folder) / "assets";
    ASSERT_TRUE(std::filesystem::is_directory(assets));
    EXPECT_EQ(
        std::distance(
            std::filesystem::directory_iterator(assets),
            std::filesystem::directory_iterator {}
        ),
        1
    );

    std::filesystem::remove_all(folder);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
