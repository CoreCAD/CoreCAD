// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <set>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>
#include <src/App/InitApplication.h>

#include "Mod/Part/App/PrimitiveFaceRoleRef.h"
#include "Mod/Part/App/FeaturePartBox.h"
#include "Mod/Part/App/PrimitiveFeature.h"

using Part::capturePrimitiveFaceRole;
using Part::resolvePrimitiveFaceRole;

// The role compute seam (PrimitiveFaceRole tests) proves the decisive property --
// a role beats a world-read signature under a self-symmetry. These tests prove the
// layer above it: that capture/resolve can be driven from a *live feature* -- its
// stored Shape and its placement -- and round-trip through a real recompute and a
// rigid move, handing back a valid current sub-name.
class PrimitiveFaceRoleRefTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("boxFaceRoleRef");
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

    static constexpr std::array<const char*, 6>
        allFaces {"Face1", "Face2", "Face3", "Face4", "Face5", "Face6"};
};

// Every one of a box feature's six faces captures a non-empty role, and the six
// roles are exactly the six axes -- read off the live feature, not raw geometry.
TEST_F(PrimitiveFaceRoleRefTest, everyFaceCapturesItsAxisRole)
{
    std::set<std::string> roles;
    for (const char* sub : allFaces) {
        const std::string role = capturePrimitiveFaceRole(*_box, sub);
        EXPECT_FALSE(role.empty()) << sub;
        roles.insert(role);
    }
    const std::set<std::string> expected {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    EXPECT_EQ(roles, expected);
}

// Capture the role a sub-name plays, resolve it back: with nothing changed the
// role resolves to the very sub-name it was captured from.
TEST_F(PrimitiveFaceRoleRefTest, resolveRoundTripsToTheCapturedSubName)
{
    for (const char* sub : allFaces) {
        const std::string role = capturePrimitiveFaceRole(*_box, sub);
        ASSERT_FALSE(role.empty());
        EXPECT_EQ(resolvePrimitiveFaceRole(*_box, role), sub);
    }
}

// The reference survives a real recompute. Store a face's role, change a box
// dimension so the whole shape is rebuilt, and resolve: the returned sub-name's
// face still plays the captured role.
TEST_F(PrimitiveFaceRoleRefTest, referenceSurvivesDimensionEdit)
{
    const std::string role = capturePrimitiveFaceRole(*_box, "Face1");
    ASSERT_FALSE(role.empty());

    _box->Length.setValue(45.0);
    _box->Height.setValue(7.5);
    _doc->recompute();

    const std::string resolved = resolvePrimitiveFaceRole(*_box, role);
    ASSERT_FALSE(resolved.empty());
    EXPECT_EQ(capturePrimitiveFaceRole(*_box, resolved), role);
}

// The reference survives a rigid move of the feature -- including a self-symmetry
// of the body, the case that fools a world-read signature. A 180-degree rotation
// of the cube about its own centre lands each face in its opposite's world slot;
// a reference held by world position would silently follow the slot to the wrong
// physical face. The role does not: it resolves to the physically-correct face,
// which is a *different* sub-name than the one now occupying its old world slot.
TEST_F(PrimitiveFaceRoleRefTest, referenceCarriesAcrossSelfSymmetryMove)
{
    _box->Length.setValue(15.0);
    _box->Width.setValue(15.0);
    _box->Height.setValue(15.0);  // a cube: 180-degree turns are self-symmetries
    _doc->recompute();

    // Identify the +X and -X sub-names before the move.
    std::string plusXSub;
    std::string minusXSub;
    for (const char* sub : allFaces) {
        const std::string role = capturePrimitiveFaceRole(*_box, sub);
        if (role == "+X") {
            plusXSub = sub;
        }
        else if (role == "-X") {
            minusXSub = sub;
        }
    }
    ASSERT_FALSE(plusXSub.empty());
    ASSERT_FALSE(minusXSub.empty());
    ASSERT_NE(plusXSub, minusXSub);

    // 180 degrees about the vertical axis through the cube's own centre: a
    // self-symmetry that carries the +X face into the -X world slot.
    _box->Placement.setValue(
        Base::Placement(
            Base::Vector3d(),
            Base::Rotation(Base::Vector3d(0, 0, 1), std::acos(-1.0)),
            Base::Vector3d(7.5, 7.5, 7.5)
        )
    );
    _doc->recompute();

    // The +X reference resolves to the physical +X face (still plusXSub), NOT to
    // minusXSub, which now sits where +X used to be.
    const std::string resolved = resolvePrimitiveFaceRole(*_box, "+X");
    EXPECT_EQ(resolved, plusXSub);
    EXPECT_NE(resolved, minusXSub);
}

// A role that is unknown, or a sub-name that names no planar face, degrades to the
// empty string -- the stop-and-ask, never a guessed sub-name.
TEST_F(PrimitiveFaceRoleRefTest, degradesToEmptyOnBadInput)
{
    EXPECT_TRUE(capturePrimitiveFaceRole(*_box, "").empty());
    EXPECT_TRUE(capturePrimitiveFaceRole(*_box, "Face99").empty());
    EXPECT_TRUE(capturePrimitiveFaceRole(*_box, "Edge1").empty());  // an edge is not a box face role
    EXPECT_TRUE(resolvePrimitiveFaceRole(*_box, "").empty());
    EXPECT_TRUE(resolvePrimitiveFaceRole(*_box, "+W").empty());  // unknown axis
}

// The same reference contract, driven off a live cylinder rather than a box: the
// round primitive's roles capture and resolve from a real feature just as well.
class CylinderFaceRoleRefTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("cylinderFaceRoleRef");
        _cyl = _doc->addObject<Part::Cylinder>();
        _cyl->Radius.setValue(5.0);
        _cyl->Height.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc {nullptr};
    Part::Cylinder* _cyl {nullptr};

    static constexpr std::array<const char*, 3> allFaces {"Face1", "Face2", "Face3"};
};

// A live cylinder's three faces capture exactly the round-primitive vocabulary:
// the lateral "Side" and the two axis caps "+Z"/"-Z".
TEST_F(CylinderFaceRoleRefTest, threeFacesCaptureSideAndAxisCaps)
{
    std::set<std::string> roles;
    for (const char* sub : allFaces) {
        const std::string role = capturePrimitiveFaceRole(*_cyl, sub);
        EXPECT_FALSE(role.empty()) << sub;
        roles.insert(role);
    }
    const std::set<std::string> expected {"Side", "+Z", "-Z"};
    EXPECT_EQ(roles, expected);
}

// The reference survives a real recompute: change the height, and each role still
// resolves to a face that plays it.
TEST_F(CylinderFaceRoleRefTest, referenceSurvivesDimensionEdit)
{
    for (const char* role : {"Side", "+Z", "-Z"}) {
        const std::string before = resolvePrimitiveFaceRole(*_cyl, role);
        ASSERT_FALSE(before.empty()) << role;
    }

    _cyl->Height.setValue(25.0);
    _cyl->Radius.setValue(3.0);
    _doc->recompute();

    for (const char* role : {"Side", "+Z", "-Z"}) {
        const std::string resolved = resolvePrimitiveFaceRole(*_cyl, role);
        ASSERT_FALSE(resolved.empty()) << role;
        EXPECT_EQ(capturePrimitiveFaceRole(*_cyl, resolved), role);
    }
}

// The decisive live case for a round primitive. A cylinder's two caps are always
// equal, so flipping it end-for-end is a self-symmetry that a world-read signature
// cannot tell apart -- the moved top cap lands in the bottom's world slot. Held by
// role, the "+Z" reference resolves to the physical top cap (its own sub-name,
// unchanged by a pure placement move), NOT the bottom cap now in its old slot.
TEST_F(CylinderFaceRoleRefTest, capReferenceCarriesAcrossEndForEndFlip)
{
    std::string topSub;
    std::string botSub;
    for (const char* sub : allFaces) {
        const std::string role = capturePrimitiveFaceRole(*_cyl, sub);
        if (role == "+Z") {
            topSub = sub;
        }
        else if (role == "-Z") {
            botSub = sub;
        }
    }
    ASSERT_FALSE(topSub.empty());
    ASSERT_FALSE(botSub.empty());
    ASSERT_NE(topSub, botSub);

    // 180 degrees about a horizontal axis through the cylinder's own mid-height:
    // an end-for-end self-symmetry that carries the top cap into the bottom's slot.
    _cyl->Placement.setValue(
        Base::Placement(
            Base::Vector3d(),
            Base::Rotation(Base::Vector3d(1, 0, 0), std::acos(-1.0)),
            Base::Vector3d(0, 0, 5.0)
        )
    );
    _doc->recompute();

    const std::string resolved = resolvePrimitiveFaceRole(*_cyl, "+Z");
    EXPECT_EQ(resolved, topSub);
    EXPECT_NE(resolved, botSub);
}
