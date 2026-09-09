// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <BRepPrimAPI_MakeBox.hxx>

#include <App/Application.h>
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

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
