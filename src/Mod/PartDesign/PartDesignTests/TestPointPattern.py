# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: one copy on each point of a sketch. The original's centre slides across the sketch's
# plane onto each point; construction points and line ends are not points to copy onto.

import unittest

import FreeCAD
import Part
import PartDesign
from FreeCAD import Placement, Rotation, Vector
from PartDesignTests.TestBodyEmergence import _square

PLATE = 40 * 40 * 10
HOLE = 4 * 4 * 4


class TestPointPattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPointPattern", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        # A 4 mm square hole centred on (5, 5), 4 mm deep.
        self.pocket = PartDesign.makeFeature(
            _square(self.Doc, "Hole", x0=3, y0=3, side=4), "Pocket", body=self.body
        )
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _points(self, points, construction=(), placement=None):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Points")
        if placement:
            sketch.Placement = placement
        for x, y in points:
            sketch.addGeometry(Part.Point(Vector(x, y, 0)), False)
        for x, y in construction:
            sketch.addGeometry(Part.Point(Vector(x, y, 0)), True)
        self.Doc.recompute()
        return sketch

    def _pattern(self, sketch):
        pattern = self.Doc.addObject("PartDesign::PointPattern", "PointPattern")
        pattern.Points = sketch
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        return pattern

    def testOneCopyOnEachPoint(self):
        pattern = self._pattern(self._points([(20, 20), (35, 10), (10, 30)]))
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * HOLE)
        hole = self.body.Shape.common(Part.makeBox(4, 4, 4, Vector(33, 8, 0)))
        self.assertAlmostEqual(hole.Volume, 0)

    def testPointUnderTheOriginalAddsNothing(self):
        pattern = self._pattern(self._points([(5, 5), (20, 20)]))
        self.assertEqual(len(pattern.AddSubShape.childShapes()[1].Solids), 2)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testConstructionPointsAndLineEndsAreNotUsed(self):
        sketch = self._points([(20, 20)], construction=[(30, 30)])
        sketch.addGeometry(Part.LineSegment(Vector(10, 10, 0), Vector(30, 10, 0)), False)
        self.Doc.recompute()
        self._pattern(sketch)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testCopiesStayLevelWithTheOriginal(self):
        # The points sketch floats 50 mm above the plate; the holes stay in the plate.
        self._pattern(self._points([(20, 20)], placement=Placement(Vector(0, 0, 50), Rotation())))
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testPointsAreReadInTheSketchsPlace(self):
        # Turned a quarter about Z, the sketch's (20, 10) lies at (-10, 20) in the world: off
        # the plate, so the copy cuts nothing.
        turned = Placement(Vector(0, 0, 0), Rotation(Vector(0, 0, 1), 90))
        self._pattern(self._points([(20, 10)], placement=turned))
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - HOLE)

    def testSketchWithoutPointsFails(self):
        pattern = self._pattern(_square(self.Doc, "NoPoints", side=5))
        self.assertFalse(pattern.isValid())

    def testAnotherPatternCanCopyIt(self):
        pattern = self._pattern(self._points([(20, 5)]))
        row = self.Doc.addObject("PartDesign::LinearPattern", "Row")
        row.Direction = (self.Doc.getObject("Hole"), ["V_Axis"])
        row.Length = 20
        row.Occurrences = 2
        row.Originals = [pattern]
        self.body.addFeature(row)
        self.Doc.recompute()
        self.assertTrue(row.isValid(), row.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * HOLE)
