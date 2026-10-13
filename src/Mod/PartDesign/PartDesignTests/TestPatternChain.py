# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #160: a pattern is a step another pattern can copy. A grid is one pattern step after another,
# each building on the result before it, so the roll-back marker can stop between them.

import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square

CUBE = 10 * 10 * 10
HOLE = 4 * 4 * 4


class TestPatternChain(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternChain", type="Part")
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

    def _step(self, typeName, originals, **settings):
        step = self.Doc.addObject("PartDesign::" + typeName, typeName)
        for name, value in settings.items():
            setattr(step, name, value)
        step.Originals = originals
        self.body.addFeature(step)
        self.Doc.recompute()
        self.assertTrue(step.isValid(), step.getStatusString())
        return step

    def _mirror(self, originals):
        return self._step("Mirrored", originals, MirrorPlane=(self.sketch, ["H_Axis"]))

    def _row(self, originals, length=20, count=3):
        return self._step(
            "LinearPattern",
            originals,
            Direction=(self.sketch, ["H_Axis"]),
            Length=length,
            Occurrences=count,
        )

    def testPatternOfPatternsCopiesEveryCopy(self):
        mirror = self._mirror([self.pad])
        row = self._row([mirror])
        ring = self._step(
            "PolarPattern", [row], Axis=(self.sketch, ["N_Axis"]), Angle=360, Occurrences=4
        )
        # 2 x 3 x 4 cubes, the ring's quarters overlapping in pairs: the shape the combined
        # pattern made of the same three settings.
        self.assertAlmostEqual(self.body.Shape.Volume, 20 * CUBE)
        self.assertEqual(self.body.Tip, ring)

    def testRollbackStopsBetweenPatternSteps(self):
        mirror = self._mirror([self.pad])
        self._row([mirror])
        self.assertAlmostEqual(self.body.Shape.Volume, 2 * 3 * CUBE)

        self.body.RollbackMarker = mirror
        self.Doc.recompute()
        self.assertAlmostEqual(self.body.Shape.Volume, 2 * CUBE)

    def testPatternOfACutCopiesTheHoles(self):
        mirror = self._mirror([self._pocket()])
        self._row([mirror], length=6, count=2)
        # The mirrored holes miss the cube; of the row's copies, one bites 1 mm into it.
        self.assertAlmostEqual(self.body.Shape.Volume, CUBE - HOLE - HOLE / 4)

    def testPieceHoldsWhatThePatternAddedAndCut(self):
        mirror = self._mirror([self.pad, self._pocket()])
        added, cut = mirror.AddSubShape.childShapes()
        self.assertAlmostEqual(added.Volume, 2 * CUBE)
        self.assertAlmostEqual(cut.Volume, 2 * HOLE)

    def testPatternCannotCopyItself(self):
        mirror = self._mirror([self.pad])
        mirror.Originals = [self.pad, mirror]
        self.Doc.recompute()
        self.assertFalse(mirror.isValid())
