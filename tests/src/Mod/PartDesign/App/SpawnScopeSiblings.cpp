// SPDX-License-Identifier: LGPL-2.1-or-later

// Locks the ARCHITECTURE Amendment 5 §5.1 multi-body fan-out implemented by
// Body::spawnScopeSiblings: one shared tool reaching several Bodies resolves to one ordinary Cut/
// Common feature PER Body, each advancing that Body's OWN chain and referencing the one tool by
// reference. The invariants under test are exactly the ones Clause 5.1 and
// ANALYSIS_ownership-query-multibody.md forbid regressing: no feature extends two chains (each
// sibling's BaseFeature is its own Body's prior Tip, each Body's Tip is its own sibling), the tool
// is shared not owned (every sibling's Tools is the same one object), and the cut actually removes
// the shared volume from each Body.

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <vector>

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureBoolean.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class SpawnScopeSiblingsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("Scope_test", "testUser", {.documentType = "Part"});
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    static void addRect(Sketcher::SketchObject* sk, double x0, double y0, double x1, double y1)
    {
        const std::vector<Base::Vector3d> corners {
            Base::Vector3d(x0, y0, 0),
            Base::Vector3d(x1, y0, 0),
            Base::Vector3d(x1, y1, 0),
            Base::Vector3d(x0, y1, 0)
        };
        for (std::size_t i = 0; i < corners.size(); ++i) {
            Part::GeomLineSegment seg;
            seg.setPoints(corners[i], corners[(i + 1) % corners.size()]);
            sk->addGeometry(&seg, false);
        }
    }

    // A Body whose single feature is a 10-tall Pad of the given rectangle.
    PartDesign::Body* padBody(double x0, double y0, double x1, double y1)
    {
        auto* body = _doc->addObject<PartDesign::Body>();
        auto* sk = _doc->addObject<Sketcher::SketchObject>();
        body->addFeature(sk);
        sk->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        sk->MapMode.setValue("FlatFace");
        addRect(sk, x0, y0, x1, y1);
        auto* pad = _doc->addObject<PartDesign::Pad>();
        body->addFeature(pad);
        pad->Profile.setValue(sk, {""});
        pad->Length.setValue(10.0);
        return body;
    }

    static double volumeOf(App::DocumentObject* feat)
    {
        auto* pf = dynamic_cast<Part::ShapeFeature*>(feat);
        if (!pf) {
            return 0.0;
        }
        GProp_GProps props;
        BRepGProp::VolumeProperties(pf->Shape.getShape().getShape(), props);
        return props.Mass();
    }

    App::Document* _doc = nullptr;
};

// The core fan-out: a tool overlapping two Bodies, cut from both, yields one Cut sibling per Body,
// each rooted on its own Body's chain and sharing the one tool.
TEST_F(SpawnScopeSiblingsTest, CutFansOutOneSiblingPerBody)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);   // X 0..20
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);  // X 30..50
    PartDesign::Body* tool = padBody(10, 5, 40, 15);   // X 10..40 — overlaps both
    _doc->recompute();

    App::DocumentObject* tipA = bodyA->Tip.getValue();
    App::DocumentObject* tipB = bodyB->Tip.getValue();
    const double volA = volumeOf(tipA);
    const double volB = volumeOf(tipB);

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA, bodyB}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 2U);
    auto* cutA = static_cast<PartDesign::Boolean*>(siblings[0]);
    auto* cutB = static_cast<PartDesign::Boolean*>(siblings[1]);

    // Each sibling advances its OWN Body's chain: BaseFeature = that Body's prior Tip, and the
    // Body's Tip now names the sibling. No feature extends two chains.
    EXPECT_EQ(cutA->BaseFeature.getValue(), tipA);
    EXPECT_EQ(cutB->BaseFeature.getValue(), tipB);
    EXPECT_EQ(bodyA->Tip.getValue(), cutA);
    EXPECT_EQ(bodyB->Tip.getValue(), cutB);

    // The tool is shared by reference, not owned or duplicated: both siblings' Tools is the one tool.
    ASSERT_EQ(cutA->Tools.getValues().size(), 1U);
    ASSERT_EQ(cutB->Tools.getValues().size(), 1U);
    EXPECT_EQ(cutA->Tools.getValues().front(), tool);
    EXPECT_EQ(cutB->Tools.getValues().front(), tool);
    EXPECT_EQ(cutA->Type.getValueAsString(), std::string("Cut"));

    // The cut actually removed the shared volume from each Body, and left the tool untouched.
    EXPECT_LT(volumeOf(cutA), volA) << "Body A must lose the overlapped volume";
    EXPECT_LT(volumeOf(cutB), volB) << "Body B must lose the overlapped volume";
    EXPECT_GT(volumeOf(tool->Tip.getValue()), 0.0) << "the shared tool is not consumed";
}

// The spawn honours the caller's chosen set: cutting only one of the two reached Bodies leaves the
// other's chain untouched.
TEST_F(SpawnScopeSiblingsTest, RespectsChosenSubset)
{
    PartDesign::Body* bodyA = padBody(0, 0, 20, 20);
    PartDesign::Body* bodyB = padBody(30, 0, 50, 20);
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();

    App::DocumentObject* tipB = bodyB->Tip.getValue();

    auto siblings = PartDesign::Body::spawnScopeSiblings(tool, {bodyA}, "Cut");
    _doc->recompute();

    ASSERT_EQ(siblings.size(), 1U);
    EXPECT_EQ(bodyA->Tip.getValue(), siblings[0]);
    EXPECT_EQ(bodyB->Tip.getValue(), tipB) << "an unchosen Body's chain must be untouched";
}

TEST_F(SpawnScopeSiblingsTest, EmptyTargetsIsNoOp)
{
    PartDesign::Body* tool = padBody(10, 5, 40, 15);
    _doc->recompute();
    EXPECT_TRUE(PartDesign::Body::spawnScopeSiblings(tool, {}, "Cut").empty());
    EXPECT_TRUE(PartDesign::Body::spawnScopeSiblings(nullptr, {}, "Cut").empty());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
