// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <BRepBuilderAPI_MakePolygon.hxx>
#include <gp_Pnt.hxx>

#include "Mod/Part/App/FeatureFace.h"
#include "Mod/Part/App/PartFeature.h"
#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

class FeatureFaceTest: public ::testing::Test, public PartTestHelpers::PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
        _face = _doc->addObject<Part::Face>();
    }

    void TearDown() override
    {}

    // A closed rectangular wire, 4 x 6, with its near corner at the given point.
    static TopoDS_Shape rectangleAt(double x, double y, double z)
    {
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(gp_Pnt(x, y, z));
        poly.Add(gp_Pnt(x + 4, y, z));
        poly.Add(gp_Pnt(x + 4, y + 6, z));
        poly.Add(gp_Pnt(x, y + 6, z));
        poly.Close();
        return poly.Shape();
    }

    Part::Face* _face = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(FeatureFaceTest, aFaceHoldsNoAuthoredPlacement)
{
    EXPECT_TRUE(_face->isDerivedFrom<Part::ShapeFeature>());
    EXPECT_FALSE(_face->isDerivedFrom<Part::Feature>());
    EXPECT_FALSE(_face->holdsAuthoredPlacement());
    EXPECT_EQ(nullptr, _face->getPropertyByName("Placement"));

    // The contrast the rule turns on: a primitive IS an anchor and does author one.
    EXPECT_TRUE(_boxes[0]->holdsAuthoredPlacement());
}

TEST_F(FeatureFaceTest, aFaceLandsWhereItsSourceWireIs)
{
    // A wire parked well away from the origin in all three axes.
    auto* source = _doc->addObject<Part::Feature>();
    source->Shape.setValue(rectangleAt(10, 20, 30));

    _face->Sources.setValues({source});

    // Through a real recompute, not a bare execute(): only the recompute reaches the
    // store that would drop a position left in the shape's location.
    _doc->recompute();

    Part::TopoShape ts = _face->Shape.getValue();
    ASSERT_FALSE(ts.isNull());
    EXPECT_DOUBLE_EQ(PartTestHelpers::getArea(ts.getShape()), 24.0);
    EXPECT_TRUE(
        PartTestHelpers::boxesMatch(ts.getBoundBox(), Base::BoundBox3d(10.0, 20.0, 30.0, 14.0, 26.0, 30.0))
    ) << "the face left its source wire behind";
}
