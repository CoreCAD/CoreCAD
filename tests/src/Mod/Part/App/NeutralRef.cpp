// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <src/App/InitApplication.h>

#include "Mod/Part/App/BoxFaceRoleRef.h"
#include "Mod/Part/App/FeaturePartBox.h"
#include "Mod/Part/App/NeutralRef.h"
#include "Mod/Part/App/PrimitiveFeature.h"

using Part::captureFaceRef;
using Part::NRef;
using Part::resolveFaceRef;

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
