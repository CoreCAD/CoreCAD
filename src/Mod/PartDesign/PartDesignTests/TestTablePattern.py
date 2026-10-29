# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: one copy for each row of a table. Measured from the original, a row is how far the
# copy sits from it along the world's X, Y and Z; measured from a plane, a row is a position
# on the plane that the original's centre slides to.

import unittest

import FreeCAD
import Part
import PartDesign
from FreeCAD import Placement, Rotation, Vector
from PartDesignTests.TestBodyEmergence import _square
from PartDesignTests.TestKeepCopiesApart import _origin_feature

PLATE = 40 * 40 * 10
HOLE = 4 * 4 * 4


class TestTablePattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestTablePattern", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        # A 4 mm square hole from (3, 3) to (7, 7), 4 mm deep.
        self.pocket = PartDesign.makeFeature(
            _square(self.Doc, "Hole", x0=3, y0=3, side=4), "Pocket", body=self.body
        )
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _pattern(self, rows, plane=None):
        pattern = self.Doc.addObject("PartDesign::TablePattern", "TablePattern")
        if plane:
            pattern.MeasuredFrom = "Plane"
            pattern.Plane = plane
        pattern.Rows = [Vector(*row) for row in rows]
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        return pattern

    def _holeAt(self, x, y):
        cut = self.body.Shape.common(Part.makeBox(4, 4, 4, Vector(x, y, 0)))
        return cut.Volume < 1e-6

    def testOneCopyForEachRow(self):
        pattern = self._pattern([(20, 0, 0), (0, 25, 0), (12.5, 17, 0)])
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * HOLE)
        self.assertTrue(self._holeAt(23, 3))
        self.assertTrue(self._holeAt(3, 28))
        self.assertTrue(self._holeAt(15.5, 20))

    def testRowOfZeroAddsNothing(self):
        pattern = self._pattern([(0, 0, 0), (20, 20, 0)])
        self.assertEqual(len(pattern.AddSubShape.childShapes()[1].Solids), 2)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testUpwardOffsetLiftsTheCopy(self):
        # Lifted 20 mm, the copy is above the plate and cuts nothing.
        pattern = self._pattern([(20, 20, 20)])
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - HOLE)

    def testEmptyTableFails(self):
        pattern = self._pattern([])
        self.assertFalse(pattern.isValid())

    def testOnlyZeroRowsFails(self):
        pattern = self._pattern([(0, 0, 0)])
        self.assertFalse(pattern.isValid())

    def testEditingARowMovesItsCopy(self):
        pattern = self._pattern([(20, 0, 0)])
        pattern.Rows = [Vector(0, 20, 0)]
        self.Doc.recompute()
        self.assertTrue(self._holeAt(3, 23))
        self.assertFalse(self._holeAt(23, 3))

    def testAnotherPatternCanCopyIt(self):
        pattern = self._pattern([(20, 0, 0)])
        row = self.Doc.addObject("PartDesign::LinearPattern", "Row")
        row.Direction = (self.Doc.getObject("Hole"), ["V_Axis"])
        row.Length = 20
        row.Occurrences = 2
        row.Originals = [pattern]
        self.body.addFeature(row)
        self.Doc.recompute()
        self.assertTrue(row.isValid(), row.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * HOLE)

    def _sketch(self, placement):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Positions")
        sketch.Placement = placement
        self.Doc.recompute()
        return sketch

    def testPositionsOnAPlaneSlideTheCentreThere(self):
        # The hole's centre is (5, 5); positions are read on the plane, not added to it.
        sketch = self._sketch(Placement())
        pattern = self._pattern([(20, 20, 0), (35, 10, 0)], plane=sketch)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 3 * HOLE)
        self.assertTrue(self._holeAt(18, 18))
        self.assertTrue(self._holeAt(33, 8))

    def testPositionUnderTheOriginalAddsNothing(self):
        pattern = self._pattern([(5, 5, 0), (20, 20, 0)], plane=self._sketch(Placement()))
        self.assertEqual(len(pattern.AddSubShape.childShapes()[1].Solids), 2)

    def testPositionsAreReadInThePlanesAxes(self):
        # Turned a quarter about Z, the plane's (20, -30) lies at (30, 20) in the world.
        turned = Placement(Vector(0, 0, 0), Rotation(Vector(0, 0, 1), 90))
        self._pattern([(20, -30, 0)], plane=self._sketch(turned))
        self.assertTrue(self._holeAt(28, 18))

    def testCopiesStayLevelWithTheOriginal(self):
        # The plane floats 50 mm above the plate, and its third column is ignored.
        lifted = self._sketch(Placement(Vector(0, 0, 50), Rotation()))
        self._pattern([(20, 20, 30)], plane=lifted)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testPositionsOnADatumPlane(self):
        self._pattern([(20, 20, 0)], plane=_origin_feature(self.Doc, "XY_Plane"))
        self.assertTrue(self._holeAt(18, 18))

    def _face(self, test):
        return [
            "Face%d" % (i + 1) for i, f in enumerate(self.pad.Shape.Faces) if test(f.CenterOfMass)
        ][0]

    def testPositionsOnAFaceAreMeasuredFromTheWorldsAxes(self):
        top = self._face(lambda c: abs(c.z - 10) < 1e-6)
        pattern = self._pattern([(20, 30, 0)], plane=(self.pad, [top]))
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertTrue(self._holeAt(18, 28))

    def testPositionsOnAnUprightFace(self):
        # Seen from outside the face at y = 0, X runs along the world's X and Y runs up. The
        # hole's centre is 2 mm up, so (25, 2) moves it only along X.
        front = self._face(lambda c: abs(c.y) < 1e-6)
        self._pattern([(25, 2, 0)], plane=(self.pad, [front]))
        self.assertTrue(self._holeAt(23, 3))
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 2 * HOLE)

    def testPlaneModeWithoutAPlaneFails(self):
        pattern = self._pattern([(20, 20, 0)])
        pattern.MeasuredFrom = "Plane"
        self.Doc.recompute()
        self.assertFalse(pattern.isValid())
