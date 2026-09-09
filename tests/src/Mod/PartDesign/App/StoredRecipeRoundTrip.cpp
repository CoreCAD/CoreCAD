// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/StoredRecipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <sstream>
#include <string>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// The claim the stored form exists to make: a real part -- a sketch, the pad built on it, and the
// body they belong to -- can be written out and read back, and the document that comes back
// builds the same solid. Everything smaller than this can pass while the file is still useless.
class StoredRecipeRoundTripTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication()
                   .newDocument("StoredRoundTrip_test", "testUser", {.documentType = "Part"});
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addFeature(_sketch);
        _sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        _sketch->MapMode.setValue("FlatFace");
        Part::GeomCircle circle;
        circle.setRadius(10.0);
        _sketch->addGeometry(&circle, false);
        _doc->recompute();

        _pad = _doc->addObject<PartDesign::Pad>("Pad");
        _body->addFeature(_pad);
        _pad->Profile.setValue(_sketch, {""});
        _pad->Length.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        if (_rebuilt != nullptr) {
            App::GetApplication().closeDocument(_rebuilt->getName());
        }
        App::GetApplication().closeDocument(_doc->getName());
    }

    /// Read the written recipe into a document of its own. Deliberately a plain document: the
    /// world frame and its planes are objects the file already carries, so a destination that
    /// made its own would be holding two of each.
    App::Document* readBack(const std::string& written)
    {
        _rebuilt = App::GetApplication().newDocument("StoredRoundTrip_rebuilt", "testUser");
        std::istringstream text(written);
        App::restoreStoredRecipe(*_rebuilt, text);
        _rebuilt->recompute();
        return _rebuilt;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc = nullptr;
    App::Document* _rebuilt = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    PartDesign::Pad* _pad = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// Every object returns, keeping the identity it had -- which is what lets two versions of a
// document be compared at all.
TEST_F(StoredRecipeRoundTripTest, everyObjectReturnsWithTheIdentityItHad)
{
    // Act
    App::Document* rebuilt = readBack(App::formatStoredRecipe(*_doc));

    // Assert
    ASSERT_EQ(rebuilt->getObjects().size(), _doc->getObjects().size());
    for (const App::DocumentObject* original : _doc->getObjects()) {
        const App::DocumentObject* returned = rebuilt->getObject(original->getNameInDocument());
        ASSERT_NE(returned, nullptr) << original->getNameInDocument();
        EXPECT_EQ(returned->getTypeId().getName(), original->getTypeId().getName());
        EXPECT_EQ(returned->Uid.getValueStr(), original->Uid.getValueStr());
    }
}

// The pad still knows the sketch it was built on, and the sketch still knows the plane it was
// drawn on. A part is its references; values alone would rebuild nothing.
TEST_F(StoredRecipeRoundTripTest, theFeatureStillKnowsWhatItWasBuiltOn)
{
    // Act
    App::Document* rebuilt = readBack(App::formatStoredRecipe(*_doc));

    // Assert
    auto* pad = dynamic_cast<PartDesign::Pad*>(rebuilt->getObject("Pad"));
    auto* sketch = dynamic_cast<Sketcher::SketchObject*>(rebuilt->getObject("Sketch"));
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(sketch, nullptr);
    EXPECT_EQ(pad->Profile.getValue(), sketch);
    EXPECT_EQ(sketch->AttachmentSupport.getValue(), rebuilt->getObject("XY_Plane"));
    EXPECT_EQ(pad->Length.getValue(), 10.0);
}

// The point of all of it: the document that comes back builds the same solid. The geometry is
// never stored -- it is rebuilt from the recipe -- so this is the measurement that says the
// recipe was enough.
TEST_F(StoredRecipeRoundTripTest, theRebuiltDocumentBuildsTheSameSolid)
{
    // Arrange
    const Base::BoundBox3d expected = _pad->Shape.getShape().getBoundBox();
    ASSERT_FALSE(_pad->Shape.getShape().isNull());

    // Act
    App::Document* rebuilt = readBack(App::formatStoredRecipe(*_doc));

    // Assert
    auto* pad = dynamic_cast<PartDesign::Pad*>(rebuilt->getObject("Pad"));
    ASSERT_NE(pad, nullptr);
    ASSERT_FALSE(pad->Shape.getShape().isNull());
    const Base::BoundBox3d built = pad->Shape.getShape().getBoundBox();
    EXPECT_NEAR(built.MinX, expected.MinX, 1e-7);
    EXPECT_NEAR(built.MinY, expected.MinY, 1e-7);
    EXPECT_NEAR(built.MinZ, expected.MinZ, 1e-7);
    EXPECT_NEAR(built.MaxX, expected.MaxX, 1e-7);
    EXPECT_NEAR(built.MaxY, expected.MaxY, 1e-7);
    EXPECT_NEAR(built.MaxZ, expected.MaxZ, 1e-7);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
