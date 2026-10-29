# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: copies placed by formulas of the copy number n; X, Y and Z are the copy's offset from
# the original along the world's axes.

import os
import tempfile
import unittest

import FreeCAD
import Part
import PartDesign
from FreeCAD import Vector
from PartDesignTests.TestBodyEmergence import _square

PLATE = 60 * 60 * 10
HOLE = 4 * 4 * 4


class TestFormulaPattern(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFormulaPattern", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=60), "Pad")
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

    def _pattern(self, occurrences, **formulas):
        pattern = self.Doc.addObject("PartDesign::FormulaPattern", "FormulaPattern")
        pattern.Occurrences = occurrences
        pattern.setExpression("X", None)
        for axis, formula in formulas.items():
            pattern.setExpression(axis, formula)
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        return pattern

    def _holeAt(self, x, y):
        cut = self.body.Shape.common(Part.makeBox(4, 4, 4, Vector(x, y, 0)))
        return cut.Volume < 1e-6

    def testStartsWithAStepAlongX(self):
        pattern = self.Doc.addObject("PartDesign::FormulaPattern", "FormulaPattern")
        pattern.Originals = [self.pocket]
        self.body.addFeature(pattern)
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertTrue(self._holeAt(13, 3))

    def testEachCopyIsWorkedOutFromItsNumber(self):
        # Spaced wider each time: 0, 5, 15, 30 mm.
        pattern = self._pattern(4, X="n * (n + 1) / 2 * 5 mm")
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertAlmostEqual(self.body.Shape.Volume, PLATE - 4 * HOLE)
        for x in (8, 18, 33):
            self.assertTrue(self._holeAt(x, 3), x)

    def testFormulasOnSeveralAxes(self):
        pattern = self._pattern(3, X="n * 20 mm", Y="n * n * 10 mm")
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertTrue(self._holeAt(23, 13))
        self.assertTrue(self._holeAt(43, 43))

    def testAValueWithoutAFormulaIsTheSameForEveryCopy(self):
        pattern = self._pattern(3, X="n * 20 mm")
        pattern.Y = 30
        self.Doc.recompute()
        self.assertTrue(self._holeAt(23, 33))
        self.assertTrue(self._holeAt(43, 33))

    def testShowsTheFirstCopysOffset(self):
        pattern = self._pattern(4, X="n * 7 mm + 3 mm")
        self.assertAlmostEqual(pattern.X.Value, 10)
        self.assertEqual(pattern.n, 1)
        self.assertNotIn("Touched", pattern.State)

    def testABareNumberIsRefused(self):
        pattern = self._pattern(3, X="n * 10")
        self.assertFalse(pattern.isValid())
        self.assertIn("unit", pattern.getStatusString())

    def testACopyOnTheOriginalAddsNothing(self):
        # Copy 2 lands back on the original.
        pattern = self._pattern(4, X="(n - 2) * 20 mm")
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertEqual(len(pattern.AddSubShape.childShapes()[1].Solids), 3)

    def testOnlyTheOriginalFails(self):
        self.assertFalse(self._pattern(1, X="n * 10 mm").isValid())

    def testEveryCopyOnTheOriginalFails(self):
        self.assertFalse(self._pattern(3, X="0 mm").isValid())

    def testFollowsASpreadsheet(self):
        # Copy 1 sits at (n - 1) * pitch = 0, so only the other copies show the pitch.
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "=10 mm")
        sheet.setAlias("A1", "pitch")
        self.Doc.recompute()
        pattern = self._pattern(3, X="(n - 1) * Sheet.pitch + 20 mm")
        self.assertTrue(self._holeAt(33, 3))
        sheet.set("A1", "=30 mm")
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertTrue(self._holeAt(53, 3))
        self.assertFalse(self._holeAt(33, 3))

    def testFormulasSurviveSaveAndReopen(self):
        self._pattern(3, X="n * 20 mm", Y="n * 5 mm")
        path = os.path.join(tempfile.mkdtemp(), "formula.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        pattern = self.Doc.getObject("FormulaPattern")
        self.assertEqual(pattern.ExpressionEngine, [("X", "n * 20 mm"), ("Y", "n * 5 mm")])
        pattern.touch()
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pattern)
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        self.assertTrue(self._holeAt(43, 13))
