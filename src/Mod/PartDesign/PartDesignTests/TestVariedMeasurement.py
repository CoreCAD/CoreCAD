# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: each copy of a pattern is its original rebuilt with chosen measurements changed by a
# formula of n, the copy's number.

import math
import os
import tempfile
import unittest

import FreeCAD
import Part
import PartDesign
import Sketcher
from FreeCAD import Vector
from PartDesignTests.TestBodyEmergence import _square

PLATE = 60 * 60 * 10


def _circle(doc, name, cx, cy, radius):
    sketch = doc.addObject("Sketcher::SketchObject", name)
    sketch.addGeometry(Part.Circle(Vector(cx, cy, 0), Vector(0, 0, 1), radius), False)
    sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, 3, cx))
    sketch.addConstraint(Sketcher.Constraint("DistanceY", 0, 3, cy))
    sketch.renameConstraint(
        sketch.addConstraint(Sketcher.Constraint("Radius", 0, radius)), "Radius"
    )
    doc.recompute()
    return sketch


def _hole(radius, depth):
    return math.pi * radius * radius * depth


class TestVariedMeasurement(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestVariedMeasurement", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=60), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        # A hole of radius 1 at (10, 10), 4 mm deep.
        self.sketch = _circle(self.Doc, "Hole", 10, 10, 1)
        self.pocket = PartDesign.makeFeature(self.sketch, "Pocket", body=self.body)
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _pattern(self, occurrences=4, varied=()):
        pattern = self.Doc.addObject("PartDesign::FormulaPattern", "Pattern")
        pattern.setExpression("X", "n * 15 mm")
        pattern.Occurrences = occurrences
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        for target, formula in varied:
            PartDesign.varyMeasurement(pattern, target, formula)
        self.Doc.recompute()
        return pattern

    def _radiusAt(self, x, y, depth=4):
        """The radius of the round hole centred on (x, y), read from what was cut away."""
        probe = Part.makeCylinder(4, depth - 0.5, Vector(x, y, 0))
        removed = probe.Volume - self.body.Shape.common(probe).Volume
        return math.sqrt(removed / (math.pi * (depth - 0.5)))

    def testEachCopyIsRebuiltWithItsOwnValue(self):
        pattern = self._pattern(varied=[("Hole.Constraints.Radius", "1 mm + n * 0.5 mm")])
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        for x, radius in ((10, 1), (25, 1.5), (40, 2), (55, 2.5)):
            self.assertAlmostEqual(self._radiusAt(x, 10), radius, places=3)
        holes = sum(_hole(r, 4) for r in (1, 1.5, 2, 2.5))
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - holes, places=2)

    def testTheOriginalIsLeftAsItWas(self):
        self._pattern(varied=[("Hole.Constraints.Radius", "1 mm + n * 0.5 mm")])
        self.assertAlmostEqual(self.sketch.getDatum("Radius").Value, 1)
        self.assertAlmostEqual(self.pocket.AddSubShape.Volume, _hole(1, 4), places=3)
        for obj in (self.sketch, self.pocket):
            self.assertNotIn("Touched", obj.State, obj.Label)

    def testAFormulaReadingTheMeasurementItSetsReadsItAsStated(self):
        self._pattern(varied=[("Hole.Constraints.Radius", "Hole.Constraints.Radius + n * 0.5 mm")])
        for x, radius in ((10, 1), (25, 1.5), (40, 2), (55, 2.5)):
            self.assertAlmostEqual(self._radiusAt(x, 10), radius, places=3)

    def testAMeasurementOfTheStepItself(self):
        pattern = self._pattern(3, varied=[("Pocket.Length", "4 mm + n * 1 mm")])
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(
            self.body.Shape.Volume, PLATE - _hole(1, 4) - _hole(1, 5) - _hole(1, 6), places=2
        )
        self.assertAlmostEqual(self.pocket.Length.Value, 4)

    def testSeveralMeasurements(self):
        pattern = self._pattern(
            3,
            varied=[
                ("Hole.Constraints.Radius", "1 mm + n * 1 mm"),
                ("Pocket.Length", "4 mm + n * 2 mm"),
            ],
        )
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(
            self.body.Shape.Volume, PLATE - _hole(1, 4) - _hole(2, 6) - _hole(3, 8), places=2
        )

    def testOnALinearPattern(self):
        pattern = self.Doc.addObject("PartDesign::LinearPattern", "Linear")
        pattern.Originals = [self.pocket]
        pattern.Direction = (self.sketch, ["H_Axis"])
        pattern.Length = 30
        pattern.Occurrences = 3
        self.body.addFeature(pattern)
        PartDesign.varyMeasurement(pattern, "Hole.Constraints.Radius", "1 mm + n * 1 mm")
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        for x, radius in ((10, 1), (25, 2), (40, 3)):
            self.assertAlmostEqual(self._radiusAt(x, 10), radius, places=3)

    def testFollowsASpreadsheet(self):
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=0.5 mm")
        sheet.setAlias("A1", "growth")
        self.Doc.recompute()
        pattern = self._pattern(2, varied=[("Hole.Constraints.Radius", "1 mm + n * Sheet.growth")])
        self.assertAlmostEqual(self._radiusAt(25, 10), 1.5, places=3)
        sheet.set("A1", "=2 mm")
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._radiusAt(25, 10), 3, places=3)

    def testReadsEachCopysRowFromASpreadsheetColumn(self):
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Sheet")
        for row, radius in ((2, "3 mm"), (3, "2 mm")):
            sheet.set(f"B{row}", radius)
        self.Doc.recompute()
        pattern = self._pattern(3, varied=[("Hole.Constraints.Radius", "index(Sheet.B2:B3; n)")])
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        for x, radius in ((10, 1), (25, 3), (40, 2)):
            self.assertAlmostEqual(self._radiusAt(x, 10), radius, places=3)
        sheet.set("B2", "1.5 mm")
        sheet.insertRows("1", 1)
        self.Doc.recompute()
        self.assertEqual(
            PartDesign.variedMeasurements(pattern),
            [("Hole.Constraints.Radius", "index(Sheet.B3:B4; n)")],
        )
        self.assertAlmostEqual(self._radiusAt(25, 10), 1.5, places=3)

    def testASpreadsheetColumnSurvivesSaveAndReopen(self):
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("B2", "3 mm")
        self.Doc.recompute()
        pattern = self._pattern(2, varied=[("Hole.Constraints.Radius", "index(Sheet.B2:B2; n)")])
        uid = pattern.Uid
        path = os.path.join(tempfile.mkdtemp(), "column.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        pattern = next(o for o in self.Doc.Objects if o.Uid == uid)
        self.assertEqual(
            PartDesign.variedMeasurements(pattern),
            [("Hole.Constraints.Radius", "index(Sheet.B2:B2; n)")],
        )
        pattern.touch()
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pattern)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._radiusAt(25, 10), 3, places=3)

    def testACopyWithNoRowFails(self):
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("B2", "2 mm")
        self.Doc.recompute()
        pattern = self._pattern(3, varied=[("Hole.Constraints.Radius", "index(Sheet.B2:B2; n)")])
        self.assertFalse(pattern.isValid())
        self.assertIn("outside", pattern.getStatusString())

    def testABareNumberIsRefused(self):
        pattern = self._pattern(3, varied=[("Pocket.Length", "4 + n")])
        self.assertFalse(pattern.isValid())
        self.assertIn("unit", pattern.getStatusString())

    def testAMeasurementTheCopiedStepDoesNotUseIsRefused(self):
        box = self.Doc.addObject("Part::Box", "Box")
        self.Doc.recompute()
        pattern = self._pattern(3, varied=[("Box.Length", "n * 1 mm")])
        self.assertFalse(pattern.isValid())
        self.assertIn("not used", pattern.getStatusString())
        self.assertAlmostEqual(box.Length.Value, 10)

    def testANameThatIsNotOneMeasurementIsRefused(self):
        pattern = self._pattern(3)
        with self.assertRaises(ValueError):
            PartDesign.varyMeasurement(pattern, "Pocket.Length + 1 mm", "n * 1 mm")

    def testOffersTheOriginalsMeasurementsAndNamedDimensions(self):
        offered = PartDesign.measurementsToVary(self._pattern(3))
        self.assertIn("Pocket.Length", offered)
        self.assertIn("Hole.Constraints.Radius", offered)
        self.assertNotIn("Plate.Constraints.Radius", offered)

    def testStopVarying(self):
        pattern = self._pattern(
            3,
            varied=[
                ("Hole.Constraints.Radius", "1 mm + n * 1 mm"),
                ("Pocket.Length", "4 mm + n * 2 mm"),
            ],
        )
        PartDesign.stopVarying(pattern, 0)
        self.Doc.recompute()
        self.assertEqual(
            PartDesign.variedMeasurements(pattern), [("Pocket.Length", "4 mm + n * 2 mm")]
        )
        self.assertAlmostEqual(
            self.body.Shape.Volume, PLATE - _hole(1, 4) - _hole(1, 6) - _hole(1, 8), places=2
        )
        PartDesign.stopVarying(pattern, 0)
        self.Doc.recompute()
        self.assertEqual(PartDesign.variedMeasurements(pattern), [])
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 3 * _hole(1, 4), places=2)

    def testACopyLeftOnTheOriginalStillCountsWhenItIsBuiltDifferently(self):
        # A stepped hole: every copy sits on the original, each narrower and deeper.
        pattern = self._pattern(
            3,
            varied=[
                ("Hole.Constraints.Radius", "4 mm - n * 1 mm"),
                ("Pocket.Length", "4 mm + n * 2 mm"),
            ],
        )
        pattern.setExpression("X", "0 mm")
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        stepped = _hole(3, 6) + _hole(2, 8) - _hole(2, 6)
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - stepped, places=2)

    def testSurvivesSaveAndReopen(self):
        pattern = self._pattern(varied=[("Hole.Constraints.Radius", "1 mm + n * 0.5 mm")])
        uid = pattern.Uid
        path = os.path.join(tempfile.mkdtemp(), "varied.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        pattern = next(o for o in self.Doc.Objects if o.Uid == uid)
        self.assertEqual(
            PartDesign.variedMeasurements(pattern),
            [("Hole.Constraints.Radius", "1 mm + n * 0.5 mm")],
        )
        pattern.touch()
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pattern)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self._radiusAt(55, 10), 2.5, places=3)
