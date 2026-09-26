# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Locks the MultiBody pattern break-out contract (Cruth ARCHITECTURE §5.6 / §4.7).
#
# A "Whole shape" pattern with MultiBody emits one Body per instance (the multi-output
# reconciler). Break-out re-homes a selected instance into an independent BakedShape Body
# and records a skip so the pattern drops that instance and never silently re-merges it.
#
# Two contracts are guarded here:
#   * The skip keys on each instance's ORIGINAL ORDINAL (index in the transform sequence),
#     translated once from the selected Body's component-id where the ids are self-consistent.
#     An earlier design keyed it on the element-map component-id, which is context-dependent
#     and silently failed to match at execute time — especially on a second break-out. (#4)
#   * Break-out of the instance sitting at the origin position must NOT destroy the shared
#     world frame: the document-level Origin is owned by no Body, so the pattern's Direction
#     (a link to that Origin's X_Axis) survives and the survivors never collapse to zero. (#4)

import os
import tempfile
import unittest

import FreeCAD


def _x_axis(doc):
    origin = next(o for o in doc.Objects if o.isDerivedFrom("App::Origin"))
    return next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == "X_Axis")


class TestBreakOut(unittest.TestCase):
    def setUp(self):
        # A CAD (Part-type) document mints the shared document-level Origin the pattern's
        # Direction anchors to.
        self.Doc = FreeCAD.newDocument("PartDesignTestBreakOut", type="Part")

    def _pattern(self, occurrences=4, length=90.0):
        """A MultiBody 'Whole shape' LinearPattern of a 10-cube along X, well separated so
        every instance is its own disjoint solid. Returns the pattern feature."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()

        lp = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        lp.TransformMode = "Whole shape"
        lp.MultiBody = True
        lp.Direction = (_x_axis(self.Doc), [""])
        lp.Length = length
        lp.Occurrences = occurrences
        body.addFeature(lp)
        self.Doc.recompute()
        return lp

    def _instances(self, lp):
        """Emitted instance Bodies, left-to-right by x."""
        return sorted(
            [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip is lp],
            key=lambda b: b.Shape.Solids[0].CenterOfMass.x,
        )

    def _solid_x(self, lp):
        return sorted(round(s.CenterOfMass.x, 1) for s in lp.Shape.Solids)

    def testBreakOutDropsSelectedInstance(self):
        lp = self._pattern(occurrences=4, length=90.0)  # spacing 30 -> x = 5,35,65,95
        self.assertEqual(len(self._instances(lp)), 4)
        self.assertEqual(len(lp.Shape.Solids), 4)
        self.assertEqual(list(lp.SkipInstances), [])

        # Break out the second instance (kept-position 1 -> ordinal 1).
        target = self._instances(lp)[1]
        target_x = target.Shape.Solids[0].CenterOfMass.x
        newb = target.breakOutInstance()
        self.Doc.recompute()

        self.assertIsNotNone(newb)
        self.assertEqual(list(lp.SkipInstances), [1])
        self.assertEqual(len(lp.Shape.Solids), 3)
        # The broken-out Body is independent (frozen BakedShape, no pattern link) and in place.
        self.assertTrue(newb.Tip.isDerivedFrom("PartDesign::BakedShape"))
        self.assertAlmostEqual(newb.Shape.Volume, 1000.0, places=3)
        self.assertAlmostEqual(newb.Shape.Solids[0].CenterOfMass.x, target_x, places=3)
        self.assertNotIn(target, self.Doc.Objects)  # originating instance Body retired (§4.7)

    def testSecondBreakOutRemapsOrdinal(self):
        # The crux of the ordinal contract: a second break-out must step over the already
        # -skipped ordinal. Under the old component-id design this silently failed to match.
        lp = self._pattern(occurrences=4, length=90.0)
        self._instances(lp)[1].breakOutInstance()
        self.Doc.recompute()
        self.assertEqual(list(lp.SkipInstances), [1])

        # Now the second surviving instance is original ordinal 2.
        self._instances(lp)[1].breakOutInstance()
        self.Doc.recompute()
        self.assertEqual(sorted(lp.SkipInstances), [1, 2])
        self.assertEqual(len(lp.Shape.Solids), 2)

    def testBreakOutAtOriginPositionKeepsWorldFrame(self):
        # #4: breaking out the instance at the origin position must not delete the shared
        # world frame the pattern depends on, nor collapse the survivors onto x=0.
        lp = self._pattern(occurrences=3, length=120.0)  # x = 5, 65, 125
        xaxis = _x_axis(self.Doc)
        self.assertEqual(self._solid_x(lp), [5.0, 65.0, 125.0])

        self._instances(lp)[0].breakOutInstance()  # the x=5 (origin-position) instance
        self.Doc.recompute()

        self.assertIn(xaxis, self.Doc.Objects)  # world frame intact
        self.assertIsNotNone(lp.Direction)  # Direction not silently nulled
        self.assertEqual(self._solid_x(lp), [65.0, 125.0])  # survivors NOT collapsed to ~0

    def testLiveBodiesNeverShareAColour(self):
        # §4.6: pattern copies retire and respawn as the count changes, and break-out mints one
        # more. A colour picked from a count of bodies repeats once bodies are gone; each new
        # body must take a colour no live body wears while the palette lasts.
        lp = self._pattern(occurrences=4, length=90.0)
        lp.Occurrences = 2
        self.Doc.recompute()
        lp.Occurrences = 6
        self.Doc.recompute()
        bodies = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        bodies[2].breakOutInstance()
        self.Doc.recompute()

        colours = [
            tuple(b.Color[:3]) for b in self.Doc.Objects if b.isDerivedFrom("PartDesign::Body")
        ]
        self.assertEqual(len(colours), 6)
        self.assertEqual(len(set(colours)), len(colours))


class TestStepOnOneCopy(unittest.TestCase):
    """#3: a step added to one copy's body builds on that copy alone. The other copies keep
    their bodies, the pattern spawns no replacement for the copy the step carries, and the step
    fails -- rather than fall back to the whole pattern -- once its copy is gone."""

    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestStepOnOneCopy", type="Part")
        body = self.Doc.addObject("PartDesign::Body", "Body")
        self.box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(self.box)
        self.box.Length = self.box.Width = self.box.Height = 10.0
        self.Doc.recompute()
        self.lp = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        self.lp.TransformMode = "Whole shape"
        self.lp.MultiBody = True
        self.lp.Direction = (_x_axis(self.Doc), [""])
        self.lp.Length = 90.0  # x = 5, 35, 65, 95
        self.lp.Occurrences = 4
        body.addFeature(self.lp)
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _bodies(self):
        return [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]

    def _filletThirdCopy(self):
        third = sorted(
            [b for b in self._bodies() if b.Tip is self.lp],
            key=lambda b: b.Shape.Solids[0].CenterOfMass.x,
        )[2]
        edge = next(
            i
            for i, e in enumerate(self.lp.Shape.Edges, 1)
            if 59 < e.BoundBox.XMin < 71 and e.BoundBox.ZLength > 9
        )
        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (self.lp, ["Edge%d" % edge])
        fillet.Radius = 2.0
        third.addFeature(fillet)
        self.Doc.recompute()
        return third, fillet

    def testFilletOnOneCopyRoundsThatCopyAlone(self):
        third, fillet = self._filletThirdCopy()
        # Four bodies, not eight: three still end at the pattern, one at the fillet.
        self.assertEqual(len(self._bodies()), 4)
        self.assertTrue(fillet.isValid(), fillet.getStatusString())
        self.assertEqual(len(fillet.Shape.Solids), 1)
        self.assertAlmostEqual(fillet.Shape.Solids[0].CenterOfMass.x, 65.0, delta=0.5)
        self.assertLess(fillet.Shape.Volume, 1000.0)
        self.assertEqual(fillet.BaseInstance, 2)
        self.assertEqual(sorted(b.Tip.Name for b in self._bodies()).count("LinearPattern"), 3)
        self.assertIs(third.Tip, fillet)

        # A pattern edit re-runs the pattern; it must not spawn a body for the carried copy.
        self.box.Length = 12.0
        self.Doc.recompute()
        self.assertTrue(fillet.isValid(), fillet.getStatusString())
        self.assertAlmostEqual(fillet.Shape.Solids[0].CenterOfMass.x, 66.0, delta=0.5)
        self.assertEqual(len(self._bodies()), 4)

    def testStepFailsWhenItsCopyIsGone(self):
        _, fillet = self._filletThirdCopy()
        self.lp.Occurrences = 2
        self.Doc.recompute()
        self.assertFalse(fillet.isValid())
        self.assertIn("no longer exists", fillet.getStatusString())

    def testPickOnCopyBodyNamesThatCopysEdgeOnThePattern(self):
        """#136: a pick in the 3D view lands on the copy's Body, numbered against that one
        solid; the step names its edge against the whole pattern. The translation matches the
        edge itself, so the fillet rounds the edge that was clicked."""
        third = sorted(
            [b for b in self._bodies() if b.Tip is self.lp],
            key=lambda b: b.Shape.Solids[0].CenterOfMass.x,
        )[2]
        bodyIndex, bodyEdge = next(
            (i, e)
            for i, e in enumerate(third.Shape.Edges, 1)
            if e.BoundBox.ZLength > 9 and e.BoundBox.XMin > 69
        )
        tipSub = third.tipSubElement("Edge%d" % bodyIndex)
        self.assertTrue(tipSub.startswith("Edge"), tipSub)
        tipEdge = self.lp.Shape.getElement(tipSub)
        self.assertAlmostEqual(tipEdge.CenterOfMass.distanceToPoint(bodyEdge.CenterOfMass), 0, 6)
        # The raw number means another edge on the pattern (a different copy).
        rawEdge = self.lp.Shape.getElement("Edge%d" % bodyIndex)
        self.assertGreater(rawEdge.CenterOfMass.distanceToPoint(bodyEdge.CenterOfMass), 1.0)
        self.assertEqual(third.tipSubElement("Edge999"), "")

        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (self.lp, [tipSub])
        fillet.Radius = 2.0
        third.addFeature(fillet)
        self.Doc.recompute()
        self.assertTrue(fillet.isValid(), fillet.getStatusString())
        self.assertEqual(fillet.BaseInstance, 2)
        self.assertLess(fillet.Shape.Volume, 1000.0)

    def testStepsOnTwoCopiesStayApart(self):
        """#136: a step on a second copy must not be spliced in front of the step already on
        the first; each builds on the pattern, on its own copy."""
        third, fillet = self._filletThirdCopy()
        first = sorted(
            [b for b in self._bodies() if b.Tip is self.lp],
            key=lambda b: b.Shape.Solids[0].CenterOfMass.x,
        )[0]
        chamfer = self.Doc.addObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (self.lp, [first.tipSubElement("Edge1")])
        chamfer.Size = 1.0
        first.addFeature(chamfer)
        self.Doc.recompute()
        self.assertIs(fillet.BaseFeature, self.lp)
        self.assertIs(chamfer.BaseFeature, self.lp)
        self.assertEqual((fillet.BaseInstance, chamfer.BaseInstance), (2, 0))
        self.assertTrue(chamfer.isValid(), chamfer.getStatusString())
        self.assertEqual(len(self._bodies()), 4)
        self.assertEqual(sorted(b.Tip.Name for b in self._bodies()).count("LinearPattern"), 2)


class TestPatternDirectionSurvivesReopen(unittest.TestCase):
    """#137: a direction naming the X axis with one empty part came back with no part after
    save and reopen, and the pattern built nothing."""

    def testDirectionToTheXAxisSurvivesReopen(self):
        doc = FreeCAD.newDocument("PartDesignTestDirectionReopen", type="Part")
        body = doc.addObject("PartDesign::Body", "Body")
        box = doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        doc.recompute()
        lp = doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        lp.TransformMode = "Whole shape"
        lp.MultiBody = True
        lp.Direction = (_x_axis(doc), [""])
        lp.Length = 90.0
        lp.Occurrences = 4
        body.addFeature(lp)
        doc.recompute()
        uid = lp.Uid
        path = os.path.join(tempfile.mkdtemp(), "direction.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)

        doc = FreeCAD.openDocument(path)
        try:
            lp = next(o for o in doc.Objects if o.Uid == uid)
            self.assertEqual(lp.Direction[1], [""])
            lp.touch()
            doc.recompute()
            self.assertTrue(lp.isValid(), lp.getStatusString())
            self.assertEqual(len(lp.Shape.Solids), 4)
        finally:
            FreeCAD.closeDocument(doc.Name)
