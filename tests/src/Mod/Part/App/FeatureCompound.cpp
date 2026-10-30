// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include "Mod/Part/App/FeatureCompound.h"
#include "Mod/Part/App/PartFeature.h"
#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

class FeatureCompoundTest: public ::testing::Test, public PartTestHelpers::PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }


    void SetUp() override
    {
        createTestDoc();
        _compound = _doc->addObject<Part::Compound>();
    }

    void TearDown() override
    {}

    Part::Compound* _compound = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(FeatureCompoundTest, aCompoundHoldsNoAuthoredPlacement)
{
    EXPECT_TRUE(_compound->isDerivedFrom<Part::ShapeFeature>());
    EXPECT_FALSE(_compound->isDerivedFrom<Part::Feature>());
    EXPECT_FALSE(_compound->holdsAuthoredPlacement());
    EXPECT_EQ(nullptr, _compound->getPropertyByName("Placement"));

    // The contrast the rule turns on: a primitive IS an anchor and does author one.
    EXPECT_TRUE(_boxes[0]->holdsAuthoredPlacement());
}

TEST_F(FeatureCompoundTest, aCompoundKeepsEachMemberWhereItIs)
{
    // Two boxes that do not touch: one at the origin, one at 0, 3, 0. If the bundle
    // dropped its members' positions they would pile up on each other and it would
    // measure 1 x 2 x 3 instead of spanning both.
    _compound->Links.setValues({_boxes[0], _boxes[2]});

    // Through a real recompute, not a bare execute(): only the recompute reaches the
    // store that would drop a position left in the shape's location.
    _doc->recompute();

    Part::TopoShape ts = _compound->Shape.getValue();
    ASSERT_FALSE(ts.isNull());
    EXPECT_TRUE(
        PartTestHelpers::boxesMatch(ts.getBoundBox(), Base::BoundBox3d(0.0, 0.0, 0.0, 1.0, 5.0, 3.0))
    ) << "the bundle collapsed its members onto each other";
    EXPECT_EQ(ts.countSubShapes(TopAbs_SHAPE), 2);
}

TEST_F(FeatureCompoundTest, testIntersecting)
{
    // Arrange
    _compound->Links.setValues({_boxes[0], _boxes[1]});
    // Act
    _compound->execute();
    Part::TopoShape ts = _compound->Shape.getValue();
    double volume = PartTestHelpers::getVolume(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    // Assert
    EXPECT_DOUBLE_EQ(volume, 12.0);
    EXPECT_TRUE(PartTestHelpers::boxesMatch(bb, Base::BoundBox3d(0.0, 0.0, 0.0, 1.0, 3.0, 3.0)));
    EXPECT_EQ(ts.countSubShapes(TopAbs_SHAPE), 2);
}

TEST_F(FeatureCompoundTest, testNonIntersecting)
{
    // Arrange
    _compound->Links.setValues({_boxes[0], _boxes[2]});
    // Act
    _compound->execute();
    Part::TopoShape ts = _compound->Shape.getValue();
    double volume = PartTestHelpers::getVolume(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    // Assert
    EXPECT_DOUBLE_EQ(volume, 12.0);
    EXPECT_TRUE(PartTestHelpers::boxesMatch(bb, Base::BoundBox3d(0.0, 0.0, 0.0, 1.0, 5.0, 3.0)));
    EXPECT_EQ(ts.countSubShapes(TopAbs_SHAPE), 2);
}
