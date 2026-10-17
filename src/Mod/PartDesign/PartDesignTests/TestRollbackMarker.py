# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# The document's roll-back marker. It stands on any row of the history; every step after it, in
# any body, keeps its place and its Body and is not built until the marker moves past it. A
# Body's Tip stays at the end of its chain; the Body shows its last step before the marker.

import os
import tempfile
import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square

PAD = 500.0  # 10 * 10 * 5
POCKET_2 = PAD - 4 * 4 * 2
POCKET_3 = PAD - 4 * 4 * 3


class TestRollbackMarker(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestRollbackMarker", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "S1"), "Pad")
        self.pad.Length = 5
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        self.pocket = PartDesign.makeFeature(
            _square(self.Doc, "S2", x0=2, y0=2, side=4), "Pocket", body=self.body
        )
        self.pocket.Length = 2
        self.pocket.Reversed = True  # into the pad, which rises from the sketch plane
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _rollBackTo(self, step):
        self.Doc.rollBackTo(step)
        self.Doc.recompute()

    def _marker(self):
        uid = self.Doc.RollbackMarker
        return next((o for o in self.Doc.Objects if o.Uid == uid), None) if uid else None

    def _bodies(self):
        return [o for o in self.Doc.Objects if o.TypeId == "PartDesign::Body"]

    def testRollingBackShowsTheMarkerAndKeepsTheTip(self):
        self._rollBackTo(self.pad)
        self.assertEqual(self.body.Tip, self.pocket)
        self.assertAlmostEqual(self.body.Shape.Volume, PAD, places=3)
        self.assertEqual(len(self._bodies()), 1)

    def testAStepPastTheMarkerKeepsItsBody(self):
        self._rollBackTo(self.pad)
        self.assertEqual(PartDesign.findBodyOf(self.pocket), self.body)

    def testAStepPastTheMarkerIsSkippedUntilRolledForward(self):
        self._rollBackTo(self.pad)
        self.pocket.Length = 3
        self.Doc.recompute()
        self.assertAlmostEqual(self.pocket.Shape.Volume, POCKET_2, places=3)

        self._rollBackTo(None)
        self.assertAlmostEqual(self.pocket.Shape.Volume, POCKET_3, places=3)
        self.assertAlmostEqual(self.body.Shape.Volume, POCKET_3, places=3)

    def testAnEarlierEditReachesLaterStepsOnRollForward(self):
        self._rollBackTo(self.pad)
        self.pad.Length = 6
        self.Doc.recompute()
        self.assertAlmostEqual(self.body.Shape.Volume, 600.0, places=3)

        self._rollBackTo(None)
        self.assertAlmostEqual(self.body.Shape.Volume, 600.0 - 32.0, places=3)

    def testRollingToTheLastStepClearsTheMarker(self):
        self._rollBackTo(self.pad)
        self._rollBackTo(self.pocket)
        self.assertEqual(self.Doc.RollbackMarker, "")
        self.assertAlmostEqual(self.body.Shape.Volume, POCKET_2, places=3)

    def testANewStepGoesInAtTheMarker(self):
        self._rollBackTo(self.pad)
        sketch = _square(self.Doc, "S3", side=3)
        boss = PartDesign.makeFeature(sketch, "Pad", body=self.body)
        boss.Length = 1
        boss.Reversed = True
        self.Doc.recompute()

        self.assertEqual(boss.BaseFeature, self.pad)
        self.assertEqual(self.pocket.BaseFeature, boss)
        self.assertEqual(self.body.Tip, self.pocket)
        self.assertEqual(self._marker(), boss)
        self.assertAlmostEqual(self.body.Shape.Volume, PAD + 9.0, places=3)
        # The new rows sit at the marker, ahead of the held-back ones.
        names = [o.Name for o in self.Doc.Objects]
        self.assertLess(names.index(self.pad.Name), names.index(sketch.Name))
        self.assertLess(names.index(sketch.Name), names.index(boss.Name))
        self.assertLess(names.index(boss.Name), names.index("S2"))

    def testDeletingTheMarkerStepMovesItBack(self):
        boss = PartDesign.makeFeature(_square(self.Doc, "S3", side=3), "Pad", body=self.body)
        boss.Length = 1
        boss.Reversed = True
        self.Doc.recompute()
        self._rollBackTo(self.pocket)
        self.body.removeFeature(self.pocket)
        self.Doc.removeObject(self.pocket.Name)
        self.Doc.recompute()
        self.assertEqual(self._marker().Name, "S2")
        self.assertEqual(boss.BaseFeature, self.pad)

    def testEveryStepPastTheMarkerIsHeldBackWhateverItsBody(self):
        other = PartDesign.makeFeature(_square(self.Doc, "S4", x0=20, side=3), "Pad")
        self.Doc.recompute()
        self.assertNotEqual(PartDesign.findBodyOf(other), self.body)

        self._rollBackTo(self.pad)
        for held in (self.Doc.getObject("S2"), self.pocket, self.Doc.getObject("S4"), other):
            self.assertTrue(self.Doc.isRolledBackPast(held), held.Name)
        for built in (self.Doc.getObject("S1"), self.pad):
            self.assertFalse(self.Doc.isRolledBackPast(built), built.Name)

    def testTheMarkerCanStandOnASketch(self):
        sketch = self.Doc.getObject("S2")
        self._rollBackTo(sketch)
        self.assertFalse(self.Doc.isRolledBackPast(sketch))
        self.assertTrue(self.Doc.isRolledBackPast(self.pocket))
        self.assertAlmostEqual(self.body.Shape.Volume, PAD, places=3)

    def testASketchPastTheMarkerIsNotBuilt(self):
        sketch = self.Doc.getObject("S2")
        self._rollBackTo(self.pad)
        sketch.delGeometry(0)
        self.Doc.recompute()
        self.assertEqual(len(sketch.Shape.Edges), 4)
        self._rollBackTo(None)
        self.assertEqual(len(sketch.Shape.Edges), 3)

    def _origin(self):
        return next(o for o in self.Doc.Objects if o.TypeId == "App::Origin")

    def testTheMarkerCanStandOnTheOriginToBuildNothing(self):
        self._rollBackTo(self._origin())
        self.assertEqual(self._marker(), self._origin())
        for held in (self.Doc.getObject("S1"), self.pad, self.Doc.getObject("S2"), self.pocket):
            self.assertTrue(self.Doc.isRolledBackPast(held), held.Name)
        self._rollBackTo(None)
        self.assertAlmostEqual(self.body.Shape.Volume, POCKET_2, places=3)

    def testANewStepAtTheOriginStartsTheBody(self):
        self._rollBackTo(self._origin())
        boss = PartDesign.makeFeature(
            _square(self.Doc, "S3", x0=-2, y0=-2, side=1), "Pad", body=self.body
        )
        boss.Length = 1
        self.Doc.recompute()
        self.assertIsNone(boss.BaseFeature)
        self.assertEqual(self.pad.BaseFeature, boss)
        self.assertEqual(self.body.Tip, self.pocket)
        self.assertEqual(self._marker(), boss)

    def testDeletingTheFirstStepUnderTheMarkerLeavesItAtTheOrigin(self):
        sketch = self.Doc.getObject("S1")
        self._rollBackTo(sketch)
        self.Doc.removeObject(sketch.Name)
        self.assertEqual(self._marker(), self._origin())
        self.assertTrue(self.Doc.isRolledBackPast(self.pad))

    def testTheMarkerSurvivesReopen(self):
        path = os.path.join(tempfile.mkdtemp(), "rolled.FCStd")
        self._rollBackTo(self.pad)
        self.Doc.saveAs(path)
        bodyUid, padUid = self.body.Uid, self.pad.Uid
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        body = next(o for o in self.Doc.Objects if o.Uid == bodyUid)
        self.assertEqual(self.Doc.RollbackMarker, padUid)
        self.assertAlmostEqual(body.Shape.Volume, PAD, places=3)

        self.Doc.rollBackTo(None)
        self.Doc.recompute()
        self.assertAlmostEqual(body.Shape.Volume, POCKET_2, places=3)


if __name__ == "__main__":
    unittest.main()
