// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include "Mod/Part/App/SubShapeSignature.h"

using Part::subShapeSignature;

namespace
{
std::vector<TopoDS_Shape> subShapesOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape, type, map);
    std::vector<TopoDS_Shape> out;
    for (int i = 1; i <= map.Extent(); ++i) {
        out.push_back(map(i));
    }
    return out;
}

double faceArea(const TopoDS_Shape& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.Mass();
}
}  // namespace

// A primitive box's leaves carry no element-map name (getElementName returns
// ""), which resolves silently to the whole solid. The signature must give each
// leaf a non-empty, distinct identity instead.
TEST(SubShapeSignatureTest, primitiveLeavesGetNonEmptyDistinctSignatures)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();

    const auto faces = subShapesOf(box, TopAbs_FACE);
    const auto edges = subShapesOf(box, TopAbs_EDGE);
    ASSERT_EQ(faces.size(), 6U);
    ASSERT_EQ(edges.size(), 12U);

    std::set<std::string> faceSigs;
    for (const auto& face : faces) {
        const std::string sig = subShapeSignature(face);
        EXPECT_FALSE(sig.empty());
        faceSigs.insert(sig);
    }
    std::set<std::string> edgeSigs;
    for (const auto& edge : edges) {
        const std::string sig = subShapeSignature(edge);
        EXPECT_FALSE(sig.empty());
        edgeSigs.insert(sig);
    }

    EXPECT_EQ(faceSigs.size(), 6U);   // no two faces collide
    EXPECT_EQ(edgeSigs.size(), 12U);  // no two edges collide
}

// Metric-distinguishing: every face of a cube shares the same surface type and
// area, so an identity built from those alone would collapse all six to one.
// The six distinct signatures prove the separating power comes from geometry
// (centroid + normal) -- the exact thing a position-blind naming lacks.
TEST(SubShapeSignatureTest, symmetryBrokenOnlyByGeometry)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const auto faces = subShapesOf(box, TopAbs_FACE);
    ASSERT_EQ(faces.size(), 6U);

    const double area0 = faceArea(faces.front());
    std::set<std::string> sigs;
    for (const auto& face : faces) {
        EXPECT_NEAR(faceArea(face), area0, 1e-6);  // all faces same area
        sigs.insert(subShapeSignature(face));
    }
    EXPECT_EQ(sigs.size(), 6U);  // yet all six identities are distinct
}

// A signature is a deterministic function of the geometry, so an independently
// rebuilt identical cube produces the identical set of signatures. (Contrast:
// the element-map hash is seeded per document tag and differs on rebuild.)
TEST(SubShapeSignatureTest, signatureIsStableAcrossRebuild)
{
    const TopoDS_Shape first = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const TopoDS_Shape second = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();

    std::set<std::string> firstSigs;
    std::set<std::string> secondSigs;
    for (const auto& face : subShapesOf(first, TopAbs_FACE)) {
        firstSigs.insert(subShapeSignature(face));
    }
    for (const auto& face : subShapesOf(second, TopAbs_FACE)) {
        secondSigs.insert(subShapeSignature(face));
    }
    EXPECT_EQ(firstSigs, secondSigs);
}

TEST(SubShapeSignatureTest, nullAndUnsupportedTypesReturnEmpty)
{
    EXPECT_TRUE(subShapeSignature(TopoDS_Shape()).empty());

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    EXPECT_TRUE(subShapeSignature(box).empty());  // a solid is not a leaf sub-shape
}
