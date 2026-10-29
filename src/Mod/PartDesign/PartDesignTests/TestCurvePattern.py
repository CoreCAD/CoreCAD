# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: copies spaced along a path. The pattern starts where the path passes closest to the
# original; by default each copy turns with the path.

import math
import unittest

import FreeCAD
import Part
import PartDesign
import Sketcher
from FreeCAD import Vector
from PartDesignTests.TestBodyEmergence import _square

PLATE = 40 * 40 * 10
SLOT = 2 * 6 * 4


def _rectangle(doc, name, x0, y0, width, height):
    sketch = doc.addObject("Sketcher::SketchObject", name)
    pts = [(x0, y0), (x0 + width, y0), (x0 + width, y0 + height), (x0, y0 + height)]
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sketch.addGeometry(Part.LineSegment(Vector(a[0], a[1], 0), Vector(b[0], b[1], 0)), False)
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    doc.recompute()
    return sketch


class TestCurvePattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestCurvePattern", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        # A slot 2 mm along X and 6 mm along Y, centred on (5, 20), 4 mm deep.
        self.pocket = PartDesign.makeFeature(
            _rectangle(self.Doc, "Slot", 4, 17, 2, 6), "Pocket", body=self.body
        )
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _path(self, *curves):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Path")
        for curve in curves:
            sketch.addGeometry(curve, False)
        self.Doc.recompute()
        return sketch

    def _line(self, a, b):
        return Part.LineSegment(Vector(*a, 0), Vector(*b, 0))

    def _circle(self):
        return Part.Circle(Vector(20, 20, 0), Vector(0, 0, 1), 15)

    def _pattern(self, path, occurrences, length=None, **settings):
        pattern = self.Doc.addObject("PartDesign::CurvePattern", "CurvePattern")
        pattern.Path = path
        pattern.Occurrences = occurrences
        if length is not None:
            pattern.Length = length
        for name, value in settings.items():
            setattr(pattern, name, value)
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        return pattern

    def _cut(self, x0, y0, width, height):
        return self.body.Shape.common(Part.makeBox(width, height, 4, Vector(x0, y0, 0))).Volume

    def testCopiesSpacedAlongAStraightPath(self):
        pattern = self._pattern(self._path(self._line((5, 20), (35, 20))), 3, length=30)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 3 * SLOT)
        self.assertAlmostEqual(self._cut(34, 17, 2, 6), 0)

    def testPatternStartsNearestTheOriginal(self):
        # The path starts well before the slot; the copies begin where it passes the slot.
        pattern = self._pattern(self._path(self._line((-20, 20), (35, 20))), 2, length=30)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._cut(34, 17, 2, 6), 0)

    def testSpacingMode(self):
        path = self._path(self._line((5, 20), (35, 20)))
        self._pattern(path, 3, Mode="Spacing", Offset=10)
        self.assertAlmostEqual(self._cut(24, 17, 2, 6), 0)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 3 * SLOT)

    def testReversedRunsTheOtherWay(self):
        path = self._path(self._line((-20, 20), (35, 20)))
        pattern = self._pattern(path, 2, length=10, Reversed=True)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        # The copy lands at x = -5, off the plate.
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - SLOT)

    def testCopiesRunningPastTheEndFail(self):
        pattern = self._pattern(self._path(self._line((5, 20), (35, 20))), 3, length=40)
        self.assertFalse(pattern.isValid())

    def testCopiesTurnWithAClosedPath(self):
        # Round a circle through the slot: the slot is radial at (5, 20) and stays radial at
        # (20, 35), so there it runs along X.
        pattern = self._pattern(self._path(self._circle()), 4, length=2 * math.pi * 15)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * SLOT)
        self.assertAlmostEqual(self._cut(17, 34, 6, 2), 0)

    def testKeepOrientation(self):
        pattern = self._pattern(
            self._path(self._circle()), 4, length=2 * math.pi * 15, KeepOrientation=True
        )
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._cut(19, 32, 2, 6), 0)
        self.assertAlmostEqual(self._cut(17, 34, 6, 2), 6 * 2 * 4 - 2 * 2 * 4)

    def testConnectedEdgesFormOnePath(self):
        path = self._path(self._line((5, 20), (20, 20)), self._line((20, 20), (20, 35)))
        pattern = self._pattern(path, 3, length=30)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        # The last copy has turned the corner: radial to the path, so along X.
        self.assertAlmostEqual(self._cut(17, 34, 6, 2), 0)

    def testNamedEdgesOnly(self):
        path = self._path(self._line((5, 20), (35, 20)), self._line((5, 5), (35, 5)))
        pattern = self._pattern((path, ["Edge1"]), 2, length=30)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._cut(34, 17, 2, 6), 0)

    def testSeparateEdgesFail(self):
        path = self._path(self._line((5, 20), (35, 20)), self._line((5, 5), (35, 5)))
        pattern = self._pattern(path, 2, length=30)
        self.assertFalse(pattern.isValid())
