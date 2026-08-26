// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>

#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <src/App/InitApplication.h>

#include "Mod/Part/App/BoxFaceRoleRef.h"
#include "Mod/Part/App/FeaturePartBox.h"
#include "Mod/Part/App/NeutralRef.h"
#include "Mod/Part/App/PrimitiveFeature.h"

using Part::captureBoxFaceRole;
using Part::captureFaceRef;
using Part::NRef;
using Part::resolveFaceRef;

namespace
{
// Rebuild a solid with its faces enumerated in the reverse order -- same geometry,
// a different internal face numbering. This stands in for what an independent
// rebuild or a re-import of the same shape produces: the physical faces are
// identical, but the kernel's positional "FaceN" ordinals no longer line up.
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

// NRef is the neutral stored form of a leaf reference. These tests exercise the
// two-regime capture/resolve on live features: a box (role regime, symmetry-proof)
// and a cylinder (signature regime -- a non-role-bearing leaf).
class NeutralRefTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("neutralRef");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    Part::Box* makeBox()
    {
        auto* box = _doc->addObject<Part::Box>();
        box->Length.setValue(30.0);
        box->Width.setValue(20.0);
        box->Height.setValue(10.0);
        _doc->recompute();
        return box;
    }

    Part::Cylinder* makeCylinder()
    {
        auto* cyl = _doc->addObject<Part::Cylinder>();
        cyl->Radius.setValue(6.0);
        cyl->Height.setValue(15.0);
        _doc->recompute();
        return cyl;
    }

    App::Document* _doc {nullptr};
};

// A box face captures the full leaf identity: the owning feature's Uid, the
// symmetry-proof role, and a geometric signature as cross-check.
TEST_F(NeutralRefTest, captureFillsRoleAndSignatureForBox)
{
    Part::Box* box = makeBox();

    const NRef ref = captureFaceRef(*box, "Face1");
    EXPECT_EQ(ref.kind, "face");
    EXPECT_EQ(ref.featureUid, box->Uid.getValueStr());
    EXPECT_FALSE(ref.featureUid.empty());
    EXPECT_FALSE(ref.signature.empty());

    const std::set<std::string> axes {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    EXPECT_EQ(axes.count(ref.role), 1U);
}

// The role regime resolves back to the captured sub-name, and carries the
// reference through a real recompute that rebuilds the whole shape.
TEST_F(NeutralRefTest, roleRegimeRoundTripsAndSurvivesEdit)
{
    Part::Box* box = makeBox();

    const std::array<const char*, 6> faces {"Face1", "Face2", "Face3", "Face4", "Face5", "Face6"};
    for (const char* sub : faces) {
        const NRef ref = captureFaceRef(*box, sub);
        ASSERT_FALSE(ref.role.empty());
        EXPECT_EQ(resolveFaceRef(ref, *box), sub);
    }

    const NRef ref = captureFaceRef(*box, "Face3");
    box->Length.setValue(48.0);
    box->Height.setValue(6.0);
    _doc->recompute();

    const std::string resolved = resolveFaceRef(ref, *box);
    ASSERT_FALSE(resolved.empty());
    EXPECT_EQ(captureFaceRef(*box, resolved).role, ref.role);
}

// A cylinder is not a role-bearing primitive here, so its faces fall to the
// signature regime: no role captured, but the geometric signature identifies each
// face and resolves back to it.
TEST_F(NeutralRefTest, signatureRegimeForNonBoxRoundTrips)
{
    Part::Cylinder* cyl = makeCylinder();

    const int faceCount = static_cast<int>(cyl->Shape.getShape().countSubShapes(TopAbs_FACE));
    ASSERT_GT(faceCount, 1);

    for (int i = 1; i <= faceCount; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        const NRef ref = captureFaceRef(*cyl, sub);
        EXPECT_EQ(ref.kind, "face");
        EXPECT_TRUE(ref.role.empty()) << "a cylinder face should carry no role";
        ASSERT_FALSE(ref.signature.empty());
        EXPECT_EQ(resolveFaceRef(ref, *cyl), sub);
    }
}

// A ref that is not a face, or whose leaf target matches nothing, degrades to the
// empty string; a capture on a non-existent sub-name yields a null ref.
TEST_F(NeutralRefTest, degradesOnBadRef)
{
    Part::Box* box = makeBox();

    EXPECT_TRUE(captureFaceRef(*box, "Face99").kind.empty());  // null ref, no such face

    NRef notAFace;
    notAFace.kind = "edge";
    notAFace.role = "+X";
    EXPECT_TRUE(resolveFaceRef(notAFace, *box).empty());

    NRef goneSignature;
    goneSignature.kind = "face";
    goneSignature.signature = "not-a-real-signature";
    EXPECT_TRUE(resolveFaceRef(goneSignature, *box).empty());
}

// The payoff. Capture a reference on one box, then resolve it against an
// independently-numbered rebuild of the same box -- the merge situation, where the
// two branches wrote the same geometry with different kernel face ordinals. The
// stored "FaceN" now denotes a DIFFERENT physical face on the rebuild (the bug the
// whole layer exists to cure); the NRef, held by role, still finds the correct +X
// face at its new ordinal.
TEST_F(NeutralRefTest, nRefBindsAcrossIndependentRebuildWhereRawNumberFails)
{
    Part::Box* branchA = makeBox();

    // Find the +X face's sub-name on branch A and capture a reference to it.
    std::string plusXOnA;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (captureBoxFaceRole(*branchA, sub) == "+X") {
            plusXOnA = sub;
        }
    }
    ASSERT_FALSE(plusXOnA.empty());
    const NRef ref = captureFaceRef(*branchA, plusXOnA);
    ASSERT_EQ(ref.role, "+X");

    // Branch B: the same box, rebuilt with a different internal face numbering.
    Part::Box* branchB = makeBox();
    branchB->Shape.setValue(withReversedFaceOrder(branchA->Shape.getValue()));
    // Sanity: the rebuild is a well-formed box that still has all six axis faces.
    std::set<std::string> rolesOnB;
    for (int i = 1; i <= 6; ++i) {
        rolesOnB.insert(captureBoxFaceRole(*branchB, "Face" + std::to_string(i)));
    }
    ASSERT_EQ(rolesOnB, (std::set<std::string> {"+X", "-X", "+Y", "-Y", "+Z", "-Z"}));

    // The raw stored ordinal is now STALE: on branch B it names a different face,
    // not the +X face the reference meant.
    EXPECT_NE(captureBoxFaceRole(*branchB, plusXOnA), "+X");

    // The NRef resolves correctly: a different ordinal than was stored, but genuinely
    // the +X face -- the reference survived the renumbering the raw index could not.
    const std::string resolvedOnB = resolveFaceRef(ref, *branchB);
    ASSERT_FALSE(resolvedOnB.empty());
    EXPECT_NE(resolvedOnB, plusXOnA);
    EXPECT_EQ(captureBoxFaceRole(*branchB, resolvedOnB), "+X");
}
