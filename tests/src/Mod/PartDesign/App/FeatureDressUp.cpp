// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <src/App/InitApplication.h>

#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/NeutralRef.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureFillet.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

using Part::captureSubRef;
using Part::fromNeutralString;
using Part::NRef;

namespace
{
// Rebuild a solid with its faces (and hence edges) enumerated in reverse order --
// same geometry, a different internal sub-shape numbering. Stands in for what an
// independent rebuild or a merge produces: the physical edges are identical, but the
// kernel's positional "EdgeN" ordinals no longer line up.
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

// A DressUp (here a Fillet) carries durable references for its picked sub-elements
// through the standard PropertyLinkSub Base. This proves the neutral-reference layer
// travels off Part::Thickness onto the property every DressUp shares -- so Fillet,
// Chamfer and Draft all inherit it. An edge selection captured against the base
// self-heals after the base is rebuilt with a different edge numbering.
class DressUpRefTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("dressUpRef");
        _box = _doc->addObject<Part::Box>();
        _box->Length.setValue(30.0);
        _box->Width.setValue(20.0);
        _box->Height.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc {nullptr};
    Part::Box* _box {nullptr};
};

// Picking edges into Base captures a durable edge reference for each, in order.
TEST_F(DressUpRefTest, pickingEdgesCapturesDurableRefs)
{
    auto* fillet = _doc->addObject<PartDesign::Fillet>();
    const std::vector<std::string> picked {"Edge1", "Edge6"};
    fillet->Base.setValue(_box, picked);

    const std::vector<std::string> refs = fillet->SubRefs.getValues();
    ASSERT_EQ(refs.size(), picked.size());
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const NRef ref = fromNeutralString(refs[i]);
        EXPECT_EQ(ref.kind, "edge") << "a filleted box edge is an edge-grain ref";
        EXPECT_TRUE(ref.role.empty()) << "an edge carries no parametric role";
        EXPECT_FALSE(ref.signature.empty());
    }
}

// The payoff at the PropertyLinkSub level. After the base is rebuilt with a different
// edge numbering, the raw "EdgeN" sub-names in Base are stale -- they now name
// different physical edges. rebindSubsFromRefs rewrites them from the durable
// references so they point back at the originally-picked physical edges.
TEST_F(DressUpRefTest, staleEdgeSelectionSelfHealsThroughRefs)
{
    // Remember the physical identity (signature) of each picked edge before anything
    // is renumbered, so we can check the heal lands on the SAME edges.
    const std::vector<std::string> picked {"Edge1", "Edge6"};
    std::vector<std::string> pickedSignatures;
    for (const std::string& sub : picked) {
        pickedSignatures.push_back(captureSubRef(*_box, sub).signature);
    }

    auto* fillet = _doc->addObject<PartDesign::Fillet>();
    fillet->Base.setValue(_box, picked);
    ASSERT_EQ(fillet->SubRefs.getValues().size(), picked.size());

    // Rebuild the base with a reversed sub-shape numbering. The stored "EdgeN" names
    // in Base now denote different physical edges.
    _box->Shape.setValue(withReversedFaceOrder(_box->Shape.getValue()));

    // Self-heal: rewrite Base's sub-names from the durable references.
    const int changed = fillet->rebindSubsFromRefs();
    EXPECT_GT(changed, 0) << "the renumber must have made at least one sub-name stale";

    // Every healed sub-name now denotes the same physical edge it was picked for.
    const std::vector<std::string> healed = fillet->Base.getSubValues();
    ASSERT_EQ(healed.size(), picked.size());
    for (std::size_t i = 0; i < healed.size(); ++i) {
        EXPECT_EQ(captureSubRef(*_box, healed[i]).signature, pickedSignatures[i])
            << "healed sub " << healed[i] << " is not the originally-picked edge";
    }
}

// The thick half: the self-heal runs automatically inside execute(), not only via an
// explicit call. Executing the feature after a base renumber rewrites Base's stale
// sub-names. (A bare box base has no body, so the fillet geometry step errors out --
// but the heal is the first statement in execute(), so it has already run.) Were the
// heal not wired into execute, the stale "EdgeN" names would still name the wrong
// physical edges here.
TEST_F(DressUpRefTest, executeHealsStaleSelectionAutomatically)
{
    const std::vector<std::string> picked {"Edge1", "Edge6"};
    std::vector<std::string> pickedSignatures;
    for (const std::string& sub : picked) {
        pickedSignatures.push_back(captureSubRef(*_box, sub).signature);
    }

    auto* fillet = _doc->addObject<PartDesign::Fillet>();
    fillet->Base.setValue(_box, picked);

    _box->Shape.setValue(withReversedFaceOrder(_box->Shape.getValue()));

    App::DocumentObjectExecReturn* ret = fillet->execute();
    if (ret != nullptr && ret != App::DocumentObject::StdReturn) {
        delete ret;  // an error object (no body) -- the heal already ran before it
    }

    const std::vector<std::string> healed = fillet->Base.getSubValues();
    ASSERT_EQ(healed.size(), picked.size());
    for (std::size_t i = 0; i < healed.size(); ++i) {
        EXPECT_EQ(captureSubRef(*_box, healed[i]).signature, pickedSignatures[i])
            << "execute did not heal sub " << i << " back onto the picked edge";
    }
}

// The real-base guard. The base of an actual dress-up is a PartDesign feature, which
// in this fork derives from Part::ShapeFeature but NOT Part::Feature (it carries no
// placement of its own -- the world-frame de-ownership). Capture must key on
// ShapeFeature; if it narrowed back to Part::Feature, every real Fillet/Chamfer/Draft
// would silently record nothing. The Part::Box-based tests above cannot catch that,
// because a Box IS a Part::Feature. This builds a genuine Pad base and proves the
// reference is captured against it.
TEST(DressUpPadBaseTest, capturesAgainstPartDesignFeatureBase)
{
    tests::initApplication();
    App::Document* doc
        = App::GetApplication().newDocument("dressUpPadBase", "testUser", {.documentType = "Part"});

    auto* body = doc->addObject<PartDesign::Body>();
    auto* sketch = doc->addObject<Sketcher::SketchObject>("Sketch");
    body->addFeature(sketch);
    sketch->AttachmentSupport.setValue(doc->getObject("XY_Plane"), "");
    sketch->MapMode.setValue("FlatFace");
    Part::GeomCircle circle;
    circle.setRadius(10.0);
    sketch->addGeometry(&circle, false);

    auto* pad = doc->addObject<PartDesign::Pad>("Pad");
    body->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Length.setValue(5.0);
    doc->recompute();
    ASSERT_FALSE(pad->Shape.getShape().isNull());

    // The premise the layer rests on: a PartDesign feature is a ShapeFeature, not a Feature.
    EXPECT_TRUE(pad->isDerivedFrom<Part::ShapeFeature>());
    EXPECT_FALSE(pad->isDerivedFrom<Part::Feature>());

    auto* fillet = doc->addObject<PartDesign::Fillet>();
    body->addFeature(fillet);
    fillet->Base.setValue(pad, std::vector<std::string> {"Edge1"});

    // Selecting an edge on a real PartDesign base captured a durable reference to it.
    const std::vector<std::string> refs = fillet->SubRefs.getValues();
    ASSERT_EQ(refs.size(), 1U);
    const NRef ref = fromNeutralString(refs[0]);
    EXPECT_EQ(ref.featureUid, pad->Uid.getValueStr());
    EXPECT_FALSE(ref.signature.empty());

    App::GetApplication().closeDocument(doc->getName());
}

// A base with no durable references (nothing was captured) leaves the selection
// untouched -- the heal never guesses.
TEST_F(DressUpRefTest, rebindWithoutRefsIsNoOp)
{
    auto* fillet = _doc->addObject<PartDesign::Fillet>();
    fillet->Base.setValue(_box, std::vector<std::string> {"Edge1"});
    fillet->SubRefs.setValues(std::vector<std::string>());  // drop the refs

    EXPECT_EQ(fillet->rebindSubsFromRefs(), 0);
    EXPECT_EQ(fillet->Base.getSubValues(), (std::vector<std::string> {"Edge1"}));
}
