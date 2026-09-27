# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #148: a Body's roll-back marker. The Tip stays at the end of the chain; the marker says where
# the Body stops computing. Steps past it keep their place and their Body, and are skipped until
# the marker moves past them.

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
        self.body.RollbackMarker = step
        self.Doc.recompute()

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

    def testRollingToTheTipClearsTheMarker(self):
        self._rollBackTo(self.pad)
        self._rollBackTo(self.pocket)
        self.assertIsNone(self.body.RollbackMarker)
        self.assertAlmostEqual(self.body.Shape.Volume, POCKET_2, places=3)

    def testANewStepGoesInAtTheMarker(self):
        self._rollBackTo(self.pad)
        boss = PartDesign.makeFeature(_square(self.Doc, "S3", side=3), "Pad", body=self.body)
        boss.Length = 1
        boss.Reversed = True
        self.Doc.recompute()

        self.assertEqual(boss.BaseFeature, self.pad)
        self.assertEqual(self.pocket.BaseFeature, boss)
        self.assertEqual(self.body.Tip, self.pocket)
        self.assertEqual(self.body.RollbackMarker, boss)
        self.assertAlmostEqual(self.body.Shape.Volume, PAD + 9.0, places=3)

    def testDeletingTheMarkerStepMovesItBack(self):
        boss = PartDesign.makeFeature(_square(self.Doc, "S3", side=3), "Pad", body=self.body)
        boss.Length = 1
        boss.Reversed = True
        self.Doc.recompute()
        self._rollBackTo(self.pocket)
        self.body.removeFeature(self.pocket)
        self.Doc.removeObject(self.pocket.Name)
        self.Doc.recompute()
        self.assertEqual(self.body.RollbackMarker, self.pad)
        self.assertEqual(boss.BaseFeature, self.pad)

    def testAStepAnotherBodyUsesIsStillComputed(self):
        # A second Body forks from the pocket, mid-chain; rolling the first back past the
        # pocket must not starve it.
        boss = PartDesign.makeFeature(_square(self.Doc, "S3", side=3), "Pad", body=self.body)
        boss.Length = 1
        boss.Reversed = True
        self.Doc.recompute()
        fork = PartDesign.makeFeature(_square(self.Doc, "S4", x0=7, y0=7, side=3), "Pad")
        fork.Length = 1
        fork.Reversed = True
        fork.BaseFeature = self.pocket
        self.Doc.recompute()
        forkBody = PartDesign.findBodyOf(fork)
        self.assertNotEqual(forkBody, self.body)

        self._rollBackTo(self.pad)
        self.pocket.Length = 3
        self.Doc.recompute()
        self.assertAlmostEqual(self.pocket.Shape.Volume, POCKET_3, places=3)
        self.assertAlmostEqual(forkBody.Shape.Volume, POCKET_3 + 9.0, places=3)
        self.assertAlmostEqual(self.body.Shape.Volume, PAD, places=3)
        self.assertAlmostEqual(boss.Shape.Volume, POCKET_2 + 9.0, places=3)

    def testTheMarkerSurvivesReopen(self):
        path = os.path.join(tempfile.mkdtemp(), "rolled.FCStd")
        self._rollBackTo(self.pad)
        self.Doc.saveAs(path)
        bodyUid, padUid = self.body.Uid, self.pad.Uid
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        body = next(o for o in self.Doc.Objects if o.Uid == bodyUid)
        self.assertEqual(body.RollbackMarker.Uid, padUid)
        self.assertAlmostEqual(body.Shape.Volume, PAD, places=3)

        body.RollbackMarker = None
        self.Doc.recompute()
        self.assertAlmostEqual(body.Shape.Volume, POCKET_2, places=3)


if __name__ == "__main__":
    unittest.main()
