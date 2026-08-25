// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
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

using Part::primitiveBoxFaceRole;
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
    const gp_Trsf identity;
    for (const auto& face : facesOf(box)) {
        if (primitiveBoxFaceRole(face, box, identity) == role) {
            return face;
        }
    }
    return {};
}
}  // namespace

// A box's six faces map onto exactly the six axis roles -- each once.
TEST(PrimitiveFaceRoleTest, boxFacesGetSixDistinctRoles)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(30.0, 20.0, 10.0).Shape();
    const gp_Trsf identity;

    std::set<std::string> roles;
    for (const auto& face : facesOf(box)) {
        const std::string role = primitiveBoxFaceRole(face, box, identity);
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
        const std::string before = primitiveBoxFaceRole(face, box, identity);
        const TopoDS_Shape movedFace = xform.ModifiedShape(face);
        const std::string after = primitiveBoxFaceRole(movedFace, movedBox, motion);
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
    EXPECT_EQ(primitiveBoxFaceRole(movedPlusX, movedCube, sym), "+X");
    EXPECT_NE(
        primitiveBoxFaceRole(movedPlusX, movedCube, sym),
        primitiveBoxFaceRole(minusX, cube, identity)
    );
}

TEST(PrimitiveFaceRoleTest, nullNonFaceAndNonPlanarReturnEmpty)
{
    const gp_Trsf identity;

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    EXPECT_TRUE(primitiveBoxFaceRole(TopoDS_Shape(), box, identity).empty());  // null face
    EXPECT_TRUE(primitiveBoxFaceRole(facesOf(box).front(), TopoDS_Shape(), identity).empty());  // null solid
    EXPECT_TRUE(primitiveBoxFaceRole(box, box, identity).empty());  // a solid is not a face

    // A cylinder's curved side face is not planar; it is not this regime's concern.
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
    bool sawCurved = false;
    for (const auto& face : facesOf(cyl)) {
        BRepAdaptor_Surface surf(TopoDS::Face(face));
        if (surf.GetType() != GeomAbs_Plane) {
            sawCurved = true;
            EXPECT_TRUE(primitiveBoxFaceRole(face, cyl, identity).empty());
        }
    }
    EXPECT_TRUE(sawCurved);
}
