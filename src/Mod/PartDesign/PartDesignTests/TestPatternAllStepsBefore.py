# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Picking the body in a pattern's list copies every step before the pattern, and none after it.

import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square

CUBE = 10 * 10 * 10
HOLE = 4 * 4 * 4


class TestPatternAllStepsBefore(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternAllStepsBefore", type="Part")
        self.sketch = _square(self.Doc, "S1")
        self.pad = PartDesign.makeFeature(self.sketch, "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _pocket(self):
        pocket = PartDesign.makeFeature(
            _square(self.Doc, "S2", x0=3, y0=3, side=4), "Pocket", body=self.body
        )
        pocket.Length = 4
        pocket.Reversed = True
        self.Doc.recompute()
        return pocket

    def _row(self, body=None, axis="H_Axis", **settings):
        row = self.Doc.addObject("PartDesign::LinearPattern", "Row")
        row.Direction = (self.sketch, [axis])
        row.Length = 30
        row.Occurrences = 3
        row.AllStepsBefore = True
        for name, value in settings.items():
            setattr(row, name, value)
        (body or self.body).addFeature(row)
        self.Doc.recompute()
        return row

    def testCopiesEveryStepBefore(self):
        self._pocket()
        row = self._row()
        self.assertTrue(row.isValid(), row.getStatusString())
        self.assertEqual(row.Originals, [])
        self.assertAlmostEqual(self.body.Tip.Shape.Volume, 3 * (CUBE - HOLE))

    def testStepAfterThePatternIsNotCopied(self):
        row = self._row()
        self._pocket()
        self.assertAlmostEqual(row.Shape.Volume, 3 * CUBE)

    def testStepInsertedBeforeThePatternIsCopied(self):
        row = self._row()
        pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = _square(self.Doc, "S2", x0=3, y0=3, side=4)
        pocket.Length = 4
        pocket.Reversed = True
        self.body.insertObject(pocket, row)
        self.Doc.recompute()
        self.assertIs(row.BaseFeature, pocket)
        self.assertAlmostEqual(self.body.Tip.Shape.Volume, 3 * (CUBE - HOLE))

    def testChangesPerCopyStillWork(self):
        pocket = self._pocket()
        row = self._row()
        PartDesign.varyMeasurement(row, pocket.Name + ".Length", "4 mm + n * 1 mm")
        self.Doc.recompute()
        self.assertTrue(row.isValid(), row.getStatusString())
        holes = sum(4 * 4 * depth for depth in (4, 5, 6))
        self.assertAlmostEqual(self.body.Tip.Shape.Volume, 3 * CUBE - holes)

    def _copyBodies(self, pattern):
        bodies = [
            o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip == pattern
        ]
        return sorted(bodies, key=lambda b: b.Shape.BoundBox.XMin)

    def testOriginalsBodyPassesBackOverACopiesApartPattern(self):
        apart = self._row(MultiBody=True)
        original = self._copyBodies(apart)[0]
        column = self._row(body=original, axis="V_Axis", Occurrences=2)
        self.assertEqual(column.BaseInstance, 0)
        self.assertTrue(column.isValid(), column.getStatusString())
        self.assertAlmostEqual(column.AddSubShape.Volume, 2 * CUBE)

    def testABodyThatBeganAsACopyIsRefused(self):
        apart = self._row(MultiBody=True)
        copy = self._copyBodies(apart)[1]
        column = self._row(body=copy, axis="V_Axis", Occurrences=2)
        self.assertEqual(column.BaseInstance, 1)
        self.assertFalse(column.isValid())
