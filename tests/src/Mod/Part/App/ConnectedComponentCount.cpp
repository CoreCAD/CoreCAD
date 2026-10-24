// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <Mod/Part/App/SpatialInterference.h>
#include <Mod/Part/App/TopoShape.h>

// Cruth P7 (#34): a set of things meant to stand apart is asked how many separate pieces it
// actually forms. A pattern uses this to notice that the copies it was asked for ran into
// each other and silently fused -- the user asks for four and receives one, with nothing said.
//
// The distinction that matters: members are counted against EACH OTHER, never against a
// finished result. Four bosses patterned across a plate are one solid by design, and that is
// not evidence the bosses collided.

namespace
{

Part::TopoShape boxAt(double x, double size = 10.0)
{
    return Part::TopoShape(BRepPrimAPI_MakeBox(gp_Pnt(x, 0, 0), size, size, size).Shape());
}

}  // namespace

// Nothing to count.
TEST(ConnectedComponentCountTest, EmptySetHasNoComponents)
{
    EXPECT_EQ(Part::connectedComponentCount({}), 0u);
}

// One thing is one piece.
TEST(ConnectedComponentCountTest, SingleShapeIsOneComponent)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0)}), 1u);
}

// Well-separated copies stay separate -- the correctly-spaced pattern, which must say nothing.
TEST(ConnectedComponentCountTest, DisjointShapesEachCountSeparately)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), boxAt(20), boxAt(40), boxAt(60)}), 4u);
}

// Copies that meet along a face are one piece (#126): a fuse welds them exactly as it welds
// overlapping ones, so a pattern spaced exactly one part-width apart has collapsed too.
TEST(ConnectedComponentCountTest, ShapesMeetingAlongAFaceAreOnePiece)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), boxAt(10)}), 1u);
}

// Contact along an edge only has no area, and a fuse does not make one piece of it.
TEST(ConnectedComponentCountTest, ShapesMeetingAlongAnEdgeAreStillSeparate)
{
    const Part::TopoShape diagonal(BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 0), 10.0, 10.0, 10.0).Shape());
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), diagonal}), 2u);
}

// A small gap is still a gap.
TEST(ConnectedComponentCountTest, ShapesWithAGapAreSeparate)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), boxAt(10.01)}), 2u);
}

// The defect: overlapping copies collapse into one piece.
TEST(ConnectedComponentCountTest, OverlappingShapesCollapseToOneComponent)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), boxAt(5), boxAt(10), boxAt(15)}), 1u);
}

// Overlap is transitive through a chain: A meets B, B meets C, A never touches C, and all
// three are still one piece. This is what the union-find is for -- counting overlapping PAIRS
// would answer two here.
TEST(ConnectedComponentCountTest, OverlapIsTransitiveAlongAChain)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), boxAt(8), boxAt(16)}), 1u);
}

// Two clusters, far apart, each internally overlapping: two pieces from five members.
TEST(ConnectedComponentCountTest, SeparateClustersAreCountedSeparately)
{
    EXPECT_EQ(
        Part::connectedComponentCount({boxAt(0), boxAt(5), boxAt(100), boxAt(105), boxAt(110)}),
        2u
    );
}

// A null member is nobody's neighbour, and counts as its own piece rather than vanishing --
// a shape we cannot reason about must not silently reduce the reported count.
TEST(ConnectedComponentCountTest, NullShapeIsItsOwnComponent)
{
    EXPECT_EQ(Part::connectedComponentCount({boxAt(0), Part::TopoShape(), boxAt(20)}), 3u);
}
