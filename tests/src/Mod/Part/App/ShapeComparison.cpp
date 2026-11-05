// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/FeaturePartCut.h>
#include <Mod/Part/App/ShapeComparison.h>
#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

Part::TopoShape box(double x, double length)
{
    return Part::TopoShape(BRepPrimAPI_MakeBox(gp_Pnt(x, 0, 0), length, 1.0, 1.0).Shape());
}

double volume(const Part::TopoShape& shape)
{
    return shape.isNull() ? 0.0 : PartTestHelpers::getVolume(shape.getShape());
}

}  // namespace

class ShapeComparisonTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("compare");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _docName;
    App::Document* _doc {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(ShapeComparisonTest, aLengthenedBoxAddsOnlyTheNewLength)
{
    const Part::ShapeComparison result = Part::compareShapes(box(0, 10), box(0, 40));

    EXPECT_NEAR(volume(result.shared), 10.0, 1e-6);
    EXPECT_NEAR(volume(result.added), 30.0, 1e-6);
    EXPECT_NEAR(volume(result.removed), 0.0, 1e-6);
}

TEST_F(ShapeComparisonTest, aMovedBoxAddsAtOneEndAndRemovesAtTheOther)
{
    const Part::ShapeComparison result = Part::compareShapes(box(0, 10), box(4, 10));

    EXPECT_NEAR(volume(result.shared), 6.0, 1e-6);
    EXPECT_NEAR(volume(result.added), 4.0, 1e-6);
    EXPECT_NEAR(volume(result.removed), 4.0, 1e-6);
}

TEST_F(ShapeComparisonTest, anUnchangedShapeIsAllShared)
{
    const Part::ShapeComparison result = Part::compareShapes(box(0, 10), box(0, 10));

    EXPECT_NEAR(volume(result.shared), 10.0, 1e-6);
    EXPECT_TRUE(result.added.isNull()) << volume(result.added);
    EXPECT_TRUE(result.removed.isNull()) << volume(result.removed);
}

TEST_F(ShapeComparisonTest, nothingBeforeMeansEverythingWasAdded)
{
    const Part::ShapeComparison result = Part::compareShapes(Part::TopoShape(), box(0, 10));

    EXPECT_NEAR(volume(result.added), 10.0, 1e-6);
    EXPECT_TRUE(result.removed.isNull());
    EXPECT_TRUE(result.shared.isNull());
}

// What a person sees is the cut, not the two boxes it was made from.
TEST_F(ShapeComparisonTest, aDocumentShowsOnlyWhatNothingElseBuildsOn)
{
    auto* stock = _doc->addObject<Part::Box>("Stock");
    stock->Length.setValue(10.0);
    auto* tool = _doc->addObject<Part::Box>("Tool");
    tool->Length.setValue(4.0);
    auto* cut = _doc->addObject<Part::Cut>("Cut");
    cut->Base.setValue(stock);
    cut->Tool.setValue(tool);
    _doc->recompute();

    const Part::TopoShape solids = Part::documentSolids(*_doc);

    EXPECT_NEAR(volume(solids), 6.0 * 10.0 * 10.0, 1e-6);
}

TEST_F(ShapeComparisonTest, anEmptyDocumentHasNoSolids)
{
    EXPECT_TRUE(Part::documentSolids(*_doc).isNull());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
