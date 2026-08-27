// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include "Mod/Part/App/PrimitiveFaceRole.h"
#include "Mod/Part/App/SubShapeSignature.h"

using Part::primitiveFaceRole;
using Part::resolveFaceByRole;
using Part::subShapeSignature;

namespace
{
std::vector<TopoDS_Shape> facesOf(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, TopAbs_FACE, map);
    std::vector<TopoDS_Shape> out;
    for (int i = 1; i <= map.Extent(); ++i) {
        out.push_back(map(i));
    }
    return out;
}

// The face of a box whose role (in the identity frame) matches the wanted axis.
TopoDS_Shape faceWithRole(const TopoDS_Shape& box, const std::string& role)
{
    return resolveFaceByRole(box, gp_Trsf(), role);
}
}  // namespace

// A box's six faces map onto exactly the six axis roles -- each once.
TEST(PrimitiveFaceRoleTest, boxFacesGetSixDistinctRoles)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(30.0, 20.0, 10.0).Shape();
    const gp_Trsf identity;

    std::set<std::string> roles;
    for (const auto& face : facesOf(box)) {
        const std::string role = primitiveFaceRole(face, box, identity);
        EXPECT_FALSE(role.empty());
        roles.insert(role);
    }

    const std::set<std::string> expected {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    EXPECT_EQ(roles, expected);
}

// A role is a property of the parametric frame, not the world pose: moving the
// whole feature by an arbitrary rigid motion leaves every face's role unchanged.
// (Contrast subShapeSignature, which is read in world coordinates and shifts.)
TEST(PrimitiveFaceRoleTest, roleInvariantUnderRigidMotion)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(30.0, 20.0, 10.0).Shape();
    const gp_Trsf identity;

    gp_Trsf motion;
    motion.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 1, 1)), std::acos(-1.0) / 2.0);
    motion.SetTranslationPart(gp_Vec(10.0, -5.0, 3.0));
    BRepBuilderAPI_Transform xform(box, motion, /*copy*/ true);
    const TopoDS_Shape movedBox = xform.Shape();

    for (const auto& face : facesOf(box)) {
        const std::string before = primitiveFaceRole(face, box, identity);
        const TopoDS_Shape movedFace = xform.ModifiedShape(face);
        const std::string after = primitiveFaceRole(movedFace, movedBox, motion);
        EXPECT_FALSE(after.empty());
        EXPECT_EQ(before, after);
    }
}

// The decisive case. A 180-degree rotation about a cube's centre maps the cube
// onto itself, carrying the +X face into the -X face's world position and
// orientation. The world-read signature therefore ALIASES: the moved +X face now
// has the identical signature to the original -X face -- a silent mis-bind. The
// local-frame role does NOT alias: it still reads +X, because the motion it is
// invariant to is exactly the one that fooled the signature.
TEST(PrimitiveFaceRoleTest, roleSurvivesSelfSymmetryThatSignatureAliases)
{
    const TopoDS_Shape cube = BRepPrimAPI_MakeBox(15.0, 15.0, 15.0).Shape();
    const gp_Trsf identity;

    const TopoDS_Shape plusX = faceWithRole(cube, "+X");
    const TopoDS_Shape minusX = faceWithRole(cube, "-X");
    ASSERT_FALSE(plusX.IsNull());
    ASSERT_FALSE(minusX.IsNull());

    // 180 degrees about the vertical axis through the cube centre: a self-symmetry.
    gp_Trsf sym;
    sym.SetRotation(gp_Ax1(gp_Pnt(7.5, 7.5, 0), gp_Dir(0, 0, 1)), std::acos(-1.0));
    BRepBuilderAPI_Transform xform(cube, sym, /*copy*/ true);
    const TopoDS_Shape movedCube = xform.Shape();
    const TopoDS_Shape movedPlusX = xform.ModifiedShape(plusX);

    // The signature is fooled: moved +X now looks exactly like the original -X.
    EXPECT_EQ(subShapeSignature(movedPlusX), subShapeSignature(minusX));

    // The role is not: it still names the face the parametric definition knows.
    EXPECT_EQ(primitiveFaceRole(movedPlusX, movedCube, sym), "+X");
    EXPECT_NE(primitiveFaceRole(movedPlusX, movedCube, sym), primitiveFaceRole(minusX, cube, identity));
}

// Capture then resolve, with no motion: the role stored for a face resolves back
// to that very face. This is the base of the leaf-regime reference contract.
TEST(PrimitiveFaceRoleTest, resolveRoundTripsToTheCapturedFace)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(30.0, 20.0, 10.0).Shape();
    const gp_Trsf identity;

    for (const auto& face : facesOf(box)) {
        const std::string role = primitiveFaceRole(face, box, identity);       // capture
        const TopoDS_Shape resolved = resolveFaceByRole(box, identity, role);  // resolve
        ASSERT_FALSE(resolved.IsNull());
        EXPECT_TRUE(resolved.IsSame(face));
    }
}

// The decisive resolve case, mirroring the decisive capture case above. We store
// a reference to the +X face as its role, apply a 180-degree self-symmetry of the
// cube, and resolve. Resolution carries the reference to the physical +X face
// under the motion -- NOT to the -X world slot the moved +X face now occupies,
// which is exactly where a signature match would (wrongly) bind it.
TEST(PrimitiveFaceRoleTest, resolveCarriesReferenceAcrossSelfSymmetry)
{
    const TopoDS_Shape cube = BRepPrimAPI_MakeBox(15.0, 15.0, 15.0).Shape();
    const gp_Trsf identity;

    // Capture: the reference is "the +X face", stored as its role.
    const TopoDS_Shape plusX = faceWithRole(cube, "+X");
    ASSERT_FALSE(plusX.IsNull());
    const std::string captured = primitiveFaceRole(plusX, cube, identity);
    ASSERT_EQ(captured, "+X");

    // 180 degrees about the vertical centre axis: a self-symmetry that lands the
    // +X face in -X's world position and orientation.
    gp_Trsf sym;
    sym.SetRotation(gp_Ax1(gp_Pnt(7.5, 7.5, 0), gp_Dir(0, 0, 1)), std::acos(-1.0));
    BRepBuilderAPI_Transform xform(cube, sym, /*copy*/ true);
    const TopoDS_Shape movedCube = xform.Shape();

    // Resolve the stored role against the moved solid: it binds the physical +X
    // face carried through the motion, the correct answer.
    const TopoDS_Shape resolved = resolveFaceByRole(movedCube, sym, captured);
    ASSERT_FALSE(resolved.IsNull());
    EXPECT_TRUE(resolved.IsSame(xform.ModifiedShape(plusX)));

    // It is genuinely the right face and not the -X-slot occupant a signature
    // would alias onto: the resolved face still reads +X in the local frame.
    EXPECT_EQ(primitiveFaceRole(resolved, movedCube, sym), "+X");
}

// An unresolvable or malformed request degrades to null -- it never guesses a
// face. This is the stop-and-ask the merge contract sits on.
TEST(PrimitiveFaceRoleTest, resolveDegradesToNullOnBadInput)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const gp_Trsf identity;

    EXPECT_TRUE(resolveFaceByRole(box, identity, "").IsNull());               // no role asked
    EXPECT_TRUE(resolveFaceByRole(box, identity, "+W").IsNull());             // unknown role
    EXPECT_TRUE(resolveFaceByRole(TopoDS_Shape(), identity, "+X").IsNull());  // null solid
}

TEST(PrimitiveFaceRoleTest, nullAndNonFaceReturnEmpty)
{
    const gp_Trsf identity;

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    EXPECT_TRUE(primitiveFaceRole(TopoDS_Shape(), box, identity).empty());  // null face
    EXPECT_TRUE(primitiveFaceRole(facesOf(box).front(), TopoDS_Shape(), identity).empty());  // null
                                                                                             // solid
    EXPECT_TRUE(primitiveFaceRole(box, box, identity).empty());  // a solid is not a face
}

// A cylinder's three faces map onto three distinct roles: the curved lateral face
// is the "Side", and its two flat caps take the same axis vocabulary a box uses --
// "+Z" and "-Z". The caps are named exactly as a box's top and bottom would be.
TEST(PrimitiveFaceRoleTest, cylinderFacesGetSideAndAxisCaps)
{
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
    const gp_Trsf identity;

    std::set<std::string> roles;
    for (const auto& face : facesOf(cyl)) {
        const std::string role = primitiveFaceRole(face, cyl, identity);
        EXPECT_FALSE(role.empty());
        roles.insert(role);
    }
    const std::set<std::string> expected {"Side", "+Z", "-Z"};
    EXPECT_EQ(roles, expected);
}

// A truncated cone: a conical "Side" plus two caps of unequal radius, at "+Z" and
// "-Z". (BRepPrimAPI_MakeCone(r1, r2, h) puts the r1 cap at -Z, r2 cap at +Z.)
TEST(PrimitiveFaceRoleTest, truncatedConeFacesGetSideAndAxisCaps)
{
    const TopoDS_Shape cone = BRepPrimAPI_MakeCone(8.0, 4.0, 12.0).Shape();
    const gp_Trsf identity;

    std::set<std::string> roles;
    for (const auto& face : facesOf(cone)) {
        roles.insert(primitiveFaceRole(face, cone, identity));
    }
    const std::set<std::string> expected {"Side", "+Z", "-Z"};
    EXPECT_EQ(roles, expected);
}

// A full sphere is a single closed face: the whole "Surface".
TEST(PrimitiveFaceRoleTest, sphereIsOneSurface)
{
    const TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(6.0).Shape();
    const gp_Trsf identity;

    const auto faces = facesOf(sphere);
    ASSERT_EQ(faces.size(), 1U);
    EXPECT_EQ(primitiveFaceRole(faces.front(), sphere, identity), "Surface");
}

// A full torus is likewise a single closed face: the whole "Surface".
TEST(PrimitiveFaceRoleTest, torusIsOneSurface)
{
    const TopoDS_Shape torus = BRepPrimAPI_MakeTorus(10.0, 3.0).Shape();
    const gp_Trsf identity;

    const auto faces = facesOf(torus);
    ASSERT_EQ(faces.size(), 1U);
    EXPECT_EQ(primitiveFaceRole(faces.front(), torus, identity), "Surface");
}

// The decisive case for a round primitive. A cylinder with equal-radius caps has a
// self-symmetry -- flip it end-for-end -- that lands the top cap in the bottom's
// world slot. The world-read signature therefore ALIASES the two caps; the
// local-frame role does not, because the flip is exactly the motion it cancels.
TEST(PrimitiveFaceRoleTest, cylinderCapsSurviveEndForEndSymmetry)
{
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
    const gp_Trsf identity;

    const TopoDS_Shape topCap = resolveFaceByRole(cyl, identity, "+Z");
    const TopoDS_Shape botCap = resolveFaceByRole(cyl, identity, "-Z");
    ASSERT_FALSE(topCap.IsNull());
    ASSERT_FALSE(botCap.IsNull());

    // 180 degrees about a horizontal axis through the cylinder centre: end-for-end.
    gp_Trsf sym;
    sym.SetRotation(gp_Ax1(gp_Pnt(0, 0, 5), gp_Dir(1, 0, 0)), std::acos(-1.0));
    BRepBuilderAPI_Transform xform(cyl, sym, /*copy*/ true);
    const TopoDS_Shape movedCyl = xform.Shape();
    const TopoDS_Shape movedTop = xform.ModifiedShape(topCap);

    // The signature is fooled: the moved top cap now looks exactly like the bottom.
    EXPECT_EQ(subShapeSignature(movedTop), subShapeSignature(botCap));

    // The role is not: it still reads "+Z", the cap the parametric definition knows.
    EXPECT_EQ(primitiveFaceRole(movedTop, movedCyl, sym), "+Z");

    // And resolve carries the reference to the physical top cap, not the -Z slot it
    // now occupies -- exactly where a signature match would wrongly bind it.
    const TopoDS_Shape resolved = resolveFaceByRole(movedCyl, sym, "+Z");
    ASSERT_FALSE(resolved.IsNull());
    EXPECT_TRUE(resolved.IsSame(movedTop));
}

// Roles carry across an arbitrary rigid motion for a round primitive too: every
// cylinder face reads the same role before and after the feature is posed anew.
TEST(PrimitiveFaceRoleTest, cylinderRoleInvariantUnderRigidMotion)
{
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
    const gp_Trsf identity;

    gp_Trsf motion;
    motion.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 2, 3)), std::acos(-1.0) / 3.0);
    motion.SetTranslationPart(gp_Vec(-4.0, 8.0, 2.0));
    BRepBuilderAPI_Transform xform(cyl, motion, /*copy*/ true);
    const TopoDS_Shape movedCyl = xform.Shape();

    for (const auto& face : facesOf(cyl)) {
        const std::string before = primitiveFaceRole(face, cyl, identity);
        const std::string after = primitiveFaceRole(xform.ModifiedShape(face), movedCyl, motion);
        EXPECT_FALSE(after.empty());
        EXPECT_EQ(before, after);
    }
}
