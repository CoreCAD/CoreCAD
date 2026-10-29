# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: copies packed into a flat region, in a grid through the original or in staggered rows.

import unittest

import FreeCAD
import Part
import PartDesign
from FreeCAD import Vector
from PartDesignTests.TestBodyEmergence import _square
from PartDesignTests.TestCurvePattern import _rectangle

PLATE = 40 * 40 * 10
HOLE = 2 * 2 * 4


def _outline(doc, name, *rectangles):
    sketch = doc.addObject("Sketcher::SketchObject", name)
    for x0, y0, width, height in rectangles:
        pts = [(x0, y0), (x0 + width, y0), (x0 + width, y0 + height), (x0, y0 + height)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            sketch.addGeometry(Part.LineSegment(Vector(*a, 0), Vector(*b, 0)), False)
    doc.recompute()
    return sketch


class TestFillPattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFillPattern", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        # A 2 mm square hole centred on (10, 10), 4 mm deep from the bottom.
        self.pocket = PartDesign.makeFeature(
            _rectangle(self.Doc, "Hole", 9, 9, 2, 2), "Pocket", body=self.body
        )
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _pattern(self, boundary, **settings):
        pattern = self.Doc.addObject("PartDesign::FillPattern", "FillPattern")
        pattern.Boundary = boundary
        for name, value in settings.items():
            setattr(pattern, name, value)
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        return pattern

    def _region(self):
        return _outline(self.Doc, "Region", (5, 5, 30, 30))

    def _holes(self):
        return round((PLATE - self.body.Shape.Volume) / HOLE)

    def _holeAt(self, x, y):
        cut = self.body.Shape.common(Part.makeBox(2, 2, 4, Vector(x - 1, y - 1, 0))).Volume
        return abs(cut) < 1e-6

    def testGridFillsTheRegion(self):
        pattern = self._pattern(self._region())
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(self._holes(), 9)
        self.assertTrue(self._holeAt(30, 30))

    def testStaggeredRowsShiftByHalf(self):
        pattern = self._pattern(self._region(), Layout="Staggered")
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(self._holes(), 8)
        self.assertTrue(self._holeAt(15, 20))
        self.assertFalse(self._holeAt(20, 20))

    def testMarginKeepsCopiesOffTheEdge(self):
        pattern = self._pattern(self._region(), Margin=6)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        # Only the centre is 6 mm clear of the edge; the original stays where it is.
        self.assertEqual(self._holes(), 2)
        self.assertTrue(self._holeAt(20, 20))

    def testAngleTurnsTheRows(self):
        pattern = self._pattern(
            self._region(), Angle=45, Spacing=10 * 2**0.5, RowSpacing=10 * 2**0.5
        )
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(self._holes(), 5)
        self.assertTrue(self._holeAt(20, 20))
        self.assertFalse(self._holeAt(20, 10))

    def testInnerOutlineLeavesAGap(self):
        region = _outline(self.Doc, "Region", (5, 5, 30, 30), (15, 15, 10, 10))
        pattern = self._pattern(region)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(self._holes(), 8)
        self.assertFalse(self._holeAt(20, 20))

    def testFaceOfTheSolid(self):
        # The plate's top face, 10 mm above the hole: copies slide across its plane, and the
        # centres on its edges at x or y = 0 and 40 are left out.
        top = next(
            "Face%d" % (i + 1)
            for i, face in enumerate(self.pad.Shape.Faces)
            if abs(face.CenterOfMass.z - 10) < 1e-6
        )
        pattern = self._pattern((self.pad, [top]))
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(self._holes(), 9)

    def testNoCopiesFitFails(self):
        pattern = self._pattern(self._region(), Spacing=50, RowSpacing=50)
        self.assertFalse(pattern.isValid())

    def testOpenOutlineFails(self):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Open")
        sketch.addGeometry(Part.LineSegment(Vector(5, 5, 0), Vector(35, 5, 0)), False)
        self.Doc.recompute()
        pattern = self._pattern(sketch)
        self.assertFalse(pattern.isValid())
