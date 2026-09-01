// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include "Mod/Part/App/PrimitiveFaceRoleRef.h"
#include "Mod/Part/App/NeutralRef.h"
#include "Mod/Part/App/FeatureExtrusion.h"
#include "Mod/Part/App/FeatureMirroring.h"
#include "Mod/Part/App/FeatureOffset.h"
#include "Mod/Part/App/FeatureProjectOnSurface.h"
#include "Mod/Part/App/FeatureRevolution.h"
#include "Mod/Part/App/FeatureScale.h"
#include "Mod/Part/App/PartFeatures.h"
#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

using namespace Part;
using namespace PartTestHelpers;

namespace
{
// Rebuild a solid with its faces enumerated in reverse -- same geometry, a different
// internal face numbering; stands in for an independent rebuild of the same design.
TopoDS_Shape withReversedFaceOrder(const TopoDS_Shape& solid)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(solid, TopAbs_FACE, faces);
    BRep_Builder builder;
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    for (int i = faces.Extent(); i >= 1; --i) {
        builder.Add(shell, faces(i));
    }
    TopoDS_Solid rebuilt;
    builder.MakeSolid(rebuilt);
    builder.Add(rebuilt, shell);
    return rebuilt;
}
}  // namespace

class PartFeaturesTest: public ::testing::Test, public PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
    }

    void TearDown() override
    {}
};

TEST_F(PartFeaturesTest, testRuledSurface)
{
    // Arrange
    auto _edge1 = _doc->addObject<Line>();
    auto _edge2 = _doc->addObject<Line>();
    _edge1->X1.setValue(0);
    _edge1->Y1.setValue(0);
    _edge1->Z1.setValue(0);
    _edge1->X2.setValue(2);
    _edge1->Y2.setValue(0);
    _edge1->Z2.setValue(0);
    _edge2->X1.setValue(0);
    _edge2->Y1.setValue(2);
    _edge2->Z1.setValue(0);
    _edge2->X2.setValue(2);
    _edge2->Y2.setValue(2);
    _edge2->Z2.setValue(0);
    auto _ruled = _doc->addObject<RuledSurface>();
    _ruled->Curve1.setValue(_edge1);
    _ruled->Curve2.setValue(_edge2);
    // Act
    _ruled->execute();
    TopoShape ts = _ruled->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    // Assert shape is correct
    EXPECT_DOUBLE_EQ(volume, 0.0);
    EXPECT_DOUBLE_EQ(area, 4.0);
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, 0, 0, 2, 2, 0)));
    // Assert element map is correct
    EXPECT_EQ(9, elementMap.size());
}

TEST_F(PartFeaturesTest, testLoft)
{
    // Arrange
    auto _plane1 = _doc->addObject<Plane>();
    _plane1->Length.setValue(4);
    _plane1->Width.setValue(4);
    auto _plane2 = _doc->addObject<Plane>();
    _plane2->Length.setValue(4);
    _plane2->Width.setValue(4);
    _plane2->Placement.setValue(Base::Placement(Base::Vector3d(0, 0, 2), Base::Rotation()));
    auto _loft = _doc->addObject<Loft>();
    _loft->Sections.setValues({_plane1, _plane2});
    _loft->Solid.setValue((true));
    // Act
    _loft->execute();
    TopoShape ts = _loft->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    // Assert shape is correct
    EXPECT_DOUBLE_EQ(volume, 32.0);
    EXPECT_DOUBLE_EQ(area, 64.0);
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, 0, 0, 4, 4, 2)));
    // Assert element map is correct
    EXPECT_EQ(26, elementMap.size());
}

TEST_F(PartFeaturesTest, testSweep)
{
    // Arrange
    auto _edge1 = _doc->addObject<Line>();
    _edge1->X1.setValue(0);
    _edge1->Y1.setValue(0);
    _edge1->Z1.setValue(0);
    _edge1->X2.setValue(0);
    _edge1->Y2.setValue(0);
    _edge1->Z2.setValue(3);
    auto _plane1 = _doc->addObject<Plane>();
    _plane1->Length.setValue(4);
    _plane1->Width.setValue(4);
    auto _sweep = _doc->addObject<Sweep>();
    _sweep->Sections.setValues({_plane1});
    _sweep->Spine.setValue(_edge1);
    _sweep->Solid.setValue((false));
    // Act
    _sweep->execute();
    TopoShape ts = _sweep->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    // Assert shape is correct
    EXPECT_DOUBLE_EQ(volume, 32.0);
    EXPECT_DOUBLE_EQ(area, 48.0);
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, 0, 0, 4, 4, 3)));
    // Assert element map is correct
    EXPECT_EQ(24, elementMap.size());
}

TEST_F(PartFeaturesTest, testThickness)
{
    // Arrange
    auto _thickness = _doc->addObject<Thickness>();
    _thickness->Faces.setValue(_boxes[0], {"Face1"});
    _thickness->Value.setValue(0.25);
    _thickness->Join.setValue("Intersection");
    // Act
    _thickness->execute();
    TopoShape ts = _thickness->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    // Assert shape is correct
    EXPECT_DOUBLE_EQ(volume, 4.9375);
    EXPECT_DOUBLE_EQ(area, 42.5);
    EXPECT_TRUE(boxesMatch(bb, Base::BoundBox3d(0, -0.25, -0.25, 1.25, 2.25, 3.25)));
    // Assert element map is correct
    EXPECT_EQ(51, elementMap.size());
}

// A real feature carries the durable reference layer. Thickness captures an NRef per
// selected face on execute; when the base is later rebuilt with a different face
// numbering (the merge situation), its stored positional sub names the WRONG physical
// face, and rebindFacesFromRefs() heals the selection back onto the intended face from
// the durable ref -- the first proof the layer survives contact with a live feature.
TEST_F(PartFeaturesTest, thicknessRebindsFacesFromDurableRefsAfterRebuild)
{
    _doc->recompute();
    Part::Box* box = _boxes[0];

    // Select the +X face of the box.
    std::string plusX;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (capturePrimitiveFaceRole(*box, sub) == "+X") {
            plusX = sub;
        }
    }
    ASSERT_FALSE(plusX.empty());

    auto* th = _doc->addObject<Thickness>();
    th->Faces.setValue(box, {plusX});
    th->Value.setValue(0.25);
    th->execute();  // captures the durable ref for the selection

    ASSERT_EQ(th->FaceRefs.getValues().size(), 1U);
    ASSERT_FALSE(th->FaceRefs.getValues()[0].empty());

    // The merge: the base is rebuilt with reversed face numbering. The stored
    // positional sub now denotes a DIFFERENT physical face -- no longer the +X face.
    box->Shape.setValue(withReversedFaceOrder(box->Shape.getValue()));
    ASSERT_EQ(th->Faces.getSubValues()[0], plusX);           // sub string unchanged...
    ASSERT_NE(capturePrimitiveFaceRole(*box, plusX), "+X");  // ...but now names the wrong face

    // Heal from the durable ref: the selection returns to the intended +X face, at
    // whatever ordinal it now carries.
    const int changed = th->rebindFacesFromRefs();
    EXPECT_EQ(changed, 1);
    const std::string healed = th->Faces.getSubValues()[0];
    EXPECT_NE(healed, plusX);
    EXPECT_EQ(capturePrimitiveFaceRole(*box, healed), "+X");
}

// Thick: the reference drives execute itself. After the base is rebuilt with a
// different face numbering, a plain recompute -- with NO manual rebind -- computes on
// the intended face, because execute resolves the selection through the durable ref
// before computing and refreshes the ref from the healed selection. Distinguisher: the
// refreshed ref still reads +X; without the self-heal, execute would have recaptured
// from the stale positional sub and the ref's role would be some other face.
TEST_F(PartFeaturesTest, thicknessSelfHealsSelectionOnExecuteAfterRebuild)
{
    _doc->recompute();
    Part::Box* box = _boxes[0];

    std::string plusX;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (capturePrimitiveFaceRole(*box, sub) == "+X") {
            plusX = sub;
        }
    }
    ASSERT_FALSE(plusX.empty());

    auto* th = _doc->addObject<Thickness>();
    th->Faces.setValue(box, {plusX});
    th->Value.setValue(0.25);
    th->execute();  // first execute captures the ref (role +X)
    ASSERT_EQ(th->FaceRefs.getValues().size(), 1U);
    ASSERT_EQ(fromNeutralString(th->FaceRefs.getValues()[0]).role, "+X");

    // The merge: rebuild the base with reversed face numbering. The stored positional
    // sub now names the wrong physical face.
    box->Shape.setValue(withReversedFaceOrder(box->Shape.getValue()));
    ASSERT_NE(capturePrimitiveFaceRole(*box, plusX), "+X");

    // A plain recompute of the feature -- no manual rebind -- self-heals.
    th->execute();
    EXPECT_EQ(fromNeutralString(th->FaceRefs.getValues()[0]).role, "+X")
        << "execute must compute on the ref-resolved +X face, not the stale positional sub";
}

TEST_F(PartFeaturesTest, testRefine)
{
    // Arrange
    auto _fuse = _doc->addObject<Part::Fuse>();
    _fuse->Base.setValue(_boxes[0]);
    _fuse->Tool.setValue(_boxes[3]);
    _fuse->Refine.setValue(false);
    _fuse->execute();
    Part::TopoShape fusedts = _fuse->Shape.getShape();
    auto _refine = _doc->addObject<Refine>();
    _refine->Source.setValue(_fuse);
    // Act
    _refine->execute();
    TopoShape ts = _refine->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    auto edges = fusedts.getSubTopoShapes(TopAbs_EDGE);
    auto refinedEdges = ts.getSubTopoShapes(TopAbs_EDGE);
    // Assert shape is correct
    EXPECT_EQ(edges.size(), 20);
    EXPECT_EQ(refinedEdges.size(), 12);
    EXPECT_DOUBLE_EQ(volume, 12.0);
    EXPECT_DOUBLE_EQ(area, 38.0);
    EXPECT_TRUE(PartTestHelpers::boxesMatch(bb, Base::BoundBox3d(0, 0, 0, 1, 4, 3)));
    // Assert element map is correct
    EXPECT_EQ(0, elementMap.size());  // TODO: Expect this to be non-zero.
}

TEST_F(PartFeaturesTest, testReverse)
{
    // Arrange
    auto _reverse = _doc->addObject<Reverse>();
    _reverse->Source.setValue(_boxes[0]);
    // Act
    _reverse->execute();
    TopoShape ts = _reverse->Shape.getShape();
    double volume = getVolume(ts.getShape());
    double area = getArea(ts.getShape());
    Base::BoundBox3d bb = ts.getBoundBox();
    auto elementMap = ts.getElementMap();
    auto faces = ts.getSubTopoShapes(TopAbs_FACE);
    auto originalFaces = _boxes[0]->Shape.getShape().getSubTopoShapes(TopAbs_FACE);
    // Assert shape is correct
    EXPECT_EQ(faces[0].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_EQ(faces[1].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(faces[2].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_EQ(faces[3].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(faces[4].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_EQ(faces[5].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(originalFaces[0].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(originalFaces[1].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_EQ(originalFaces[2].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(originalFaces[3].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_EQ(originalFaces[4].getShape().Orientation(), TopAbs_REVERSED);
    EXPECT_EQ(originalFaces[5].getShape().Orientation(), TopAbs_FORWARD);
    EXPECT_DOUBLE_EQ(volume, 6.0);
    EXPECT_DOUBLE_EQ(area, 22.0);
    EXPECT_TRUE(PartTestHelpers::boxesMatch(bb, Base::BoundBox3d(0, 0, 0, 1, 2, 3)));
    // Assert element map is correct
    EXPECT_EQ(0, elementMap.size());  // TODO: Expect this to be non-zero.
}

// Amendment 4: a feature that transforms, hollows or tidies what it consumes is
// derived, so it authors no position of its own. The premise is structural -- these
// types derive from the unplaced Part::ShapeFeature and carry no Placement property
// at all -- and it is stated here so that reparenting any of them back onto the
// placed Part::Feature fails loudly rather than quietly restoring a placement that
// lies about where the result lives.
TEST_F(PartFeaturesTest, transformOpsHoldNoAuthoredPlacement)
{
    auto* thickness = _doc->addObject<Part::Thickness>();
    auto* refine = _doc->addObject<Part::Refine>();
    auto* reverse = _doc->addObject<Part::Reverse>();
    auto* mirroring = _doc->addObject<Part::Mirroring>();
    auto* offset = _doc->addObject<Part::Offset>();
    auto* scale = _doc->addObject<Part::Scale>();

    const std::vector<App::GeoFeature*> derived
        = {thickness, refine, reverse, mirroring, offset, scale};
    for (auto* obj : derived) {
        EXPECT_TRUE(obj->isDerivedFrom<Part::ShapeFeature>()) << obj->getTypeId().getName();
        EXPECT_FALSE(obj->isDerivedFrom<Part::Feature>()) << obj->getTypeId().getName();
        EXPECT_FALSE(obj->holdsAuthoredPlacement()) << obj->getTypeId().getName();
        EXPECT_EQ(nullptr, obj->getPropertyByName("Placement")) << obj->getTypeId().getName();
    }

    // The contrast the rule turns on: a primitive IS an anchor and does author one.
    EXPECT_TRUE(_boxes[0]->holdsAuthoredPlacement());
}

// Holding no placement means the position has to live in the geometry. OCC will
// happily leave a rigid move in the shape's location instead, and a feature that
// authors no placement stores its shape at identity -- so the location is dropped
// and the result snaps back to the world origin. These are the two operations where
// that actually bites: a reverse (a pure re-orientation) and a scale by a factor of
// one (a rigid transform as far as OCC is concerned).
TEST_F(PartFeaturesTest, derivedOpsStayWhereTheirBaseIs)
{
    // _boxes[2] is a 1 x 2 x 3 box parked away from the origin, at 0, 3, 0.
    const Base::BoundBox3d base = _boxes[2]->Shape.getShape().getBoundBox();
    ASSERT_TRUE(PartTestHelpers::boxesMatch(base, Base::BoundBox3d(0, 3, 0, 1, 5, 3)));

    // Drive a real recompute, not execute() alone: it is the recompute that stores
    // the shape at the feature's own placement, which is where an unbaked location
    // is lost.
    auto* reverse = _doc->addObject<Part::Reverse>();
    reverse->Source.setValue(_boxes[2]);
    auto* scale = _doc->addObject<Part::Scale>();
    scale->Base.setValue(_boxes[2]);
    scale->UniformScale.setValue(1.0);
    _doc->recompute();

    EXPECT_TRUE(PartTestHelpers::boxesMatch(reverse->Shape.getShape().getBoundBox(), base))
        << "the reversed shape left its base behind";
    EXPECT_TRUE(PartTestHelpers::boxesMatch(scale->Shape.getShape().getBoundBox(), base))
        << "scaling by one moved the shape";

    // A real scale grows the part in place: same corner, twice the extent.
    scale->UniformScale.setValue(2.0);
    _doc->recompute();
    EXPECT_TRUE(
        PartTestHelpers::boxesMatch(
            scale->Shape.getShape().getBoundBox(),
            Base::BoundBox3d(0, 3, 0, 2, 7, 6)
        )
    );
}

// Amendment 4 again, one level along the same argument: a feature that builds a
// shape by sweeping a profile it consumes is derived too. It stands on its profile
// and goes where the profile, the axis or the spine sends it, so it authors no
// position of its own. Stated structurally for the same reason as the transform
// ops above -- putting any of these back onto the placed Part::Feature should fail
// here rather than quietly hand the result a placement that lies.
TEST_F(PartFeaturesTest, profileBuildersHoldNoAuthoredPlacement)
{
    auto* extrusion = _doc->addObject<Part::Extrusion>();
    auto* revolution = _doc->addObject<Part::Revolution>();
    auto* loft = _doc->addObject<Part::Loft>();
    auto* sweep = _doc->addObject<Part::Sweep>();
    auto* ruled = _doc->addObject<Part::RuledSurface>();
    auto* projection = _doc->addObject<Part::ProjectOnSurface>();

    const std::vector<App::GeoFeature*> builders
        = {extrusion, revolution, loft, sweep, ruled, projection};
    for (auto* obj : builders) {
        EXPECT_TRUE(obj->isDerivedFrom<Part::ShapeFeature>()) << obj->getTypeId().getName();
        EXPECT_FALSE(obj->isDerivedFrom<Part::Feature>()) << obj->getTypeId().getName();
        EXPECT_FALSE(obj->holdsAuthoredPlacement()) << obj->getTypeId().getName();
        EXPECT_EQ(nullptr, obj->getPropertyByName("Placement")) << obj->getTypeId().getName();
    }

    // The contrast the rule turns on: a primitive IS an anchor and does author one.
    EXPECT_TRUE(_boxes[0]->holdsAuthoredPlacement());
}

// And the behaviour that goes with it: an extrusion grows from where its profile
// is, not from the world origin. Driven through a real recompute, since it is the
// recompute that stores the shape at the feature's own placement -- the step where
// a position left in the shape's OCC location would be lost.
TEST_F(PartFeaturesTest, anExtrusionGrowsFromWhereItsProfileIs)
{
    // _boxes[2] is a 1 x 2 x 3 box parked away from the origin, at 0, 3, 0. Take its
    // top face as a profile so the profile is off the origin in all three axes.
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(_boxes[2]->Shape.getShape().getShape(), TopAbs_FACE, faces);
    TopoDS_Shape topFace;
    for (int i = 1; i <= faces.Extent(); ++i) {
        const Base::BoundBox3d bb = Part::TopoShape(faces(i)).getBoundBox();
        if (bb.MinZ > 3.0 - Precision::Confusion()) {
            topFace = faces(i);
            break;
        }
    }
    ASSERT_FALSE(topFace.IsNull());

    auto* profile = _doc->addObject<Part::Feature>();
    profile->Shape.setValue(topFace);

    auto* extrusion = _doc->addObject<Part::Extrusion>();
    extrusion->Base.setValue(profile);
    extrusion->DirMode.setValue(static_cast<long>(Part::Extrusion::dmCustom));
    extrusion->Dir.setValue(Base::Vector3d(0, 0, 1));
    extrusion->LengthFwd.setValue(4.0);
    extrusion->Solid.setValue(true);
    _doc->recompute();

    // Sits on the profile and grows upward from it -- it does not fall to the origin.
    EXPECT_TRUE(
        PartTestHelpers::boxesMatch(
            extrusion->Shape.getShape().getBoundBox(),
            Base::BoundBox3d(0, 3, 3, 1, 5, 7)
        )
    ) << "the extrusion left its profile behind";
}
