// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <cmath>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/BoundBox.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/Part/App/Datums.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// Amendment 4 (world-frame feature geometry), Stage A regression coverage.
//
// A derived feature (here a Pad) no longer copies its consumed frame into its own Placement; its
// geometry is produced directly in the document world frame. These tests pin that behaviour on a
// deliberately *non-XY* sketch, because the old copied-frame model only misbehaved off the XY
// plane (issue #15: a reference reaching a sketch through a feature picked up a stray transform).
class WorldFrameTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("WorldFrame_test", "testUser", {.documentType = "Part"});
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addFeature(_sketch);

        // Tilt the anchor sketch into the world XZ plane (90 deg about world X). Its local XY now
        // maps to world XZ, so any residual local-frame handling would show up as a wrong bbox.
        _sketch->Placement.setValue(
            Base::Placement(Base::Vector3d(0, 0, 0), Base::Rotation(Base::Vector3d(1, 0, 0), M_PI / 2))
        );

        Part::GeomCircle circle;
        circle.setRadius(5.0);
        _sketch->addGeometry(&circle, false);
        _doc->recompute();

        _pad = _doc->addObject<PartDesign::Pad>("Pad");
        _body->addFeature(_pad);
        _pad->Profile.setValue(_sketch, {""});
        _pad->Length.setValue(4.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    PartDesign::Pad* _pad = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The derived feature carries no copied position: its Placement is neutral even though it was built
// on a tilted sketch. Before Amendment 4 the Pad copied the sketch's tilted frame here.
TEST_F(WorldFrameTest, DerivedFeatureHoldsNeutralPlacement)
{
    // Post member-removal (Amendment 4): the derived feature carries no Placement property at
    // all, so it holds no authored placement and answers the world-frame query with identity.
    EXPECT_FALSE(_pad->holdsAuthoredPlacement());
    EXPECT_EQ(_pad->getPlacementProperty(), nullptr);
    EXPECT_TRUE(_pad->getPlacement().isIdentity());
}

// Dropping the copied frame did not move the geometry: the solid still lands in the world XZ plane
// (X and Z each span the circle's 10mm diameter; Y spans the 4mm pad length).
TEST_F(WorldFrameTest, PadGeometryIsWorldPlaced)
{
    ASSERT_FALSE(_pad->Shape.getShape().isNull());
    Base::BoundBox3d bb = _pad->Shape.getShape().getBoundBox();

    EXPECT_NEAR(bb.MaxX - bb.MinX, 10.0, 1e-6);  // circle diameter in world X
    EXPECT_NEAR(bb.MaxZ - bb.MinZ, 10.0, 1e-6);  // circle diameter in world Z (proves XZ, not XY)
    EXPECT_NEAR(bb.MaxY - bb.MinY, 4.0, 1e-6);   // pad length along the sketch normal (world Y)
    EXPECT_NEAR(bb.MinX, -5.0, 1e-6);
    EXPECT_NEAR(bb.MinZ, -5.0, 1e-6);
}

// Issue #15: a reference that reaches the sketch *through* the pad feature must resolve to the same
// world geometry as a direct reference to the sketch. The through path exercises
// PartDesign::Feature::getSubObject, which used to apply the feature's (copied, tilted) placement
// inverse; with the position now neutral that inverse is identity and the two paths agree.
TEST_F(WorldFrameTest, ThroughFeatureReferenceEqualsDirect)
{
    const auto opts = Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform;
    const std::string throughName = std::string(_sketch->getNameInDocument()) + ".";

    Part::TopoShape through = Part::Feature::getTopoShape(_pad, opts, throughName.c_str());
    Part::TopoShape direct = Part::Feature::getTopoShape(_sketch, opts, nullptr);

    ASSERT_FALSE(through.isNull());
    ASSERT_FALSE(direct.isNull());

    Base::BoundBox3d bt = through.getBoundBox();
    Base::BoundBox3d bd = direct.getBoundBox();

    EXPECT_NEAR(bt.MinX, bd.MinX, 1e-6);
    EXPECT_NEAR(bt.MaxX, bd.MaxX, 1e-6);
    EXPECT_NEAR(bt.MinY, bd.MinY, 1e-6);
    EXPECT_NEAR(bt.MaxY, bd.MaxY, 1e-6);
    EXPECT_NEAR(bt.MinZ, bd.MinZ, 1e-6);
    EXPECT_NEAR(bt.MaxZ, bd.MaxZ, 1e-6);
}

// The trickiest case the build plan flags (#5): a sketch attached to a support that itself has a
// non-identity placement, *plus* a non-zero attachment offset. The old code copied the support
// datum's placement (not the sketch's offset placement) into the Pad, so the "undo the frame" move
// left a residual offset. With the position neutral the residual is gone: the Pad holds an identity
// placement and a reference through it still matches a direct reference to the sketch.
TEST_F(WorldFrameTest, PadWithAttachmentOffsetOnPlacedSupport)
{
    auto* xy = _doc->getObject("XY_Plane");
    ASSERT_NE(xy, nullptr);

    // A datum plane with a genuinely non-identity placement (offset + tilt off XY_Plane).
    auto* datum = _doc->addObject<Part::DatumPlane>("Datum");
    _body->addFeature(datum);
    datum->AttachmentSupport.setValue(xy, "");
    datum->MapMode.setValue("FlatFace");
    datum->AttachmentOffset.setValue(
        Base::Placement(Base::Vector3d(2, 3, 7), Base::Rotation(Base::Vector3d(1, 0, 0), M_PI / 2))
    );

    // A sketch attached to that placed datum, with its own non-zero attachment offset.
    auto* sketch = _doc->addObject<Sketcher::SketchObject>("OffsetSketch");
    _body->addFeature(sketch);
    sketch->AttachmentSupport.setValue(datum, "");
    sketch->MapMode.setValue("FlatFace");
    sketch->AttachmentOffset.setValue(
        Base::Placement(Base::Vector3d(1, 0, 0), Base::Rotation(Base::Vector3d(0, 1, 0), M_PI / 6))
    );
    Part::GeomCircle circle;
    circle.setRadius(5.0);
    sketch->addGeometry(&circle, false);
    _doc->recompute();

    auto* pad = _doc->addObject<PartDesign::Pad>("OffsetPad");
    _body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(4.0);
    _doc->recompute();

    // Support genuinely placed off the origin (guards against a degenerate no-op fixture).
    ASSERT_FALSE(datum->Placement.getValue().isIdentity());

    // Derived feature holds no copied frame despite the placed, offset support.
    EXPECT_TRUE(pad->getPlacement().isIdentity());

    // A reference through the pad still matches a direct reference to the sketch (no residual).
    const auto opts = Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform;
    const std::string throughName = std::string(sketch->getNameInDocument()) + ".";
    Part::TopoShape through = Part::Feature::getTopoShape(pad, opts, throughName.c_str());
    Part::TopoShape direct = Part::Feature::getTopoShape(sketch, opts, nullptr);
    ASSERT_FALSE(through.isNull());
    ASSERT_FALSE(direct.isNull());
    Base::BoundBox3d bt = through.getBoundBox();
    Base::BoundBox3d bd = direct.getBoundBox();
    EXPECT_NEAR(bt.MinX, bd.MinX, 1e-6);
    EXPECT_NEAR(bt.MaxX, bd.MaxX, 1e-6);
    EXPECT_NEAR(bt.MinY, bd.MinY, 1e-6);
    EXPECT_NEAR(bt.MaxY, bd.MaxY, 1e-6);
    EXPECT_NEAR(bt.MinZ, bd.MinZ, 1e-6);
    EXPECT_NEAR(bt.MaxZ, bd.MaxZ, 1e-6);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
