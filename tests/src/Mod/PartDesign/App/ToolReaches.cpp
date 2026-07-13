// SPDX-License-Identifier: LGPL-2.1-or-later

// Locks the ARCHITECTURE Amendment 5 §5.1 reach test implemented by Body::toolReaches: a Body is
// "reached" by a multi-body subtractive/intersective tool only when the two solids share positive
// volume (tool ∩ Body ≠ ∅), so cutting the tool would actually change the Body. Disjoint solids and
// mere surface contact (a shared face, zero shared volume) are NOT reaches. This is the pure
// geometry predicate the multi-body gesture fans out over to decide which Bodies to spawn a sibling
// on; these tests exist so it cannot silently start counting bare contact — or missing a real
// overlap — as a reach.

#include <gtest/gtest.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

#include <Mod/Part/App/TopoShape.h>
#include <Mod/PartDesign/App/Body.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
// A 10×10×10 axis-aligned box with its near corner at (x, y, z).
Part::TopoShape boxAt(double x, double y, double z)
{
    return Part::TopoShape(BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), 10.0, 10.0, 10.0).Shape());
}
}  // namespace

TEST(ToolReachesTest, OverlappingSolidsAreReached)
{
    // Boxes sharing a 10×10×5 slab of volume.
    EXPECT_TRUE(PartDesign::Body::toolReaches(boxAt(0, 0, 0), boxAt(0, 0, 5)));
}

TEST(ToolReachesTest, DisjointSolidsAreNotReached)
{
    // Boxes 40 units apart on X — no shared volume.
    EXPECT_FALSE(PartDesign::Body::toolReaches(boxAt(0, 0, 0), boxAt(40, 0, 0)));
}

TEST(ToolReachesTest, FaceContactIsNotAReach)
{
    // Boxes flush against each other on the X = 10 plane: they share a face but zero volume, so
    // cutting one from the other changes nothing. Not a reach.
    EXPECT_FALSE(PartDesign::Body::toolReaches(boxAt(0, 0, 0), boxAt(10, 0, 0)));
}

TEST(ToolReachesTest, ContainmentIsAReach)
{
    // A small box wholly inside a large one — the strongest reach (a Cut here would empty the Body).
    const Part::TopoShape big(BRepPrimAPI_MakeBox(gp_Pnt(-10, -10, -10), 30.0, 30.0, 30.0).Shape());
    EXPECT_TRUE(PartDesign::Body::toolReaches(big, boxAt(0, 0, 0)));
    EXPECT_TRUE(PartDesign::Body::toolReaches(boxAt(0, 0, 0), big));
}

TEST(ToolReachesTest, NullShapesAreNotReached)
{
    const Part::TopoShape empty;
    EXPECT_FALSE(PartDesign::Body::toolReaches(empty, boxAt(0, 0, 0)));
    EXPECT_FALSE(PartDesign::Body::toolReaches(boxAt(0, 0, 0), empty));
    EXPECT_FALSE(PartDesign::Body::toolReaches(empty, empty));
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
