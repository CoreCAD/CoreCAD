# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# A pattern copy keeps its Body across recomputes while the copy exists (CoreCAD/CoreCAD#150).

import os
import tempfile
import unittest

import FreeCAD
import Part
import PartDesign
import Sketcher


def _x_axis(doc):
    origin = next(o for o in doc.Objects if o.isDerivedFrom("App::Origin"))
    return next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == "X_Axis")


class TestPatternCopyBodies(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternCopyBodies", type="Part")
        body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(self.Box)
        self.Box.Length = self.Box.Width = self.Box.Height = 10.0
        self.Doc.recompute()
        self.Pattern = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        self.Pattern.TransformMode = "Whole shape"
        self.Pattern.MultiBody = True
        self.Pattern.Direction = (_x_axis(self.Doc), [""])
        self.Pattern.Length = 90.0
        self.Pattern.Occurrences = 4
        body.addFeature(self.Pattern)
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _copy_bodies(self):
        """Uid of each copy's Body, ordered along X."""
        bodies = [
            o
            for o in self.Doc.Objects
            if o.isDerivedFrom("PartDesign::Body") and o.Tip == self.Pattern
        ]
        bodies.sort(key=lambda b: b.Shape.BoundBox.XMin)
        return [b.Uid for b in bodies]

    def testRecomputeKeepsEveryBody(self):
        before = self._copy_bodies()
        self.assertEqual(len(before), 4)
        self.Pattern.touch()
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), before)

    def testSpacingChangeKeepsEveryBody(self):
        before = self._copy_bodies()
        self.Pattern.Length = 120.0
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), before)

    def testBaseEditKeepsEveryBody(self):
        before = self._copy_bodies()
        self.Box.Height = 20.0
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), before)

    def testAddedCopiesGetNewBodies(self):
        before = self._copy_bodies()
        self.Pattern.Length = 150.0
        self.Pattern.Occurrences = 6
        self.Doc.recompute()
        after = self._copy_bodies()
        self.assertEqual(after[:4], before)
        self.assertEqual(len(set(after)), 6)

    def testRemovedCopiesRetireOnlyTheirBodies(self):
        before = self._copy_bodies()
        self.Pattern.Occurrences = 2
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), before[:2])

    def testSkippedCopyRetiresOnlyItsBody(self):
        before = self._copy_bodies()
        self.Pattern.SkipInstances = [2]
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), [before[0], before[1], before[3]])

    def testRecomputeAfterUndoAddsNoUndoStep(self):
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("Spacing")
        self.Pattern.Length = 120.0
        self.Doc.recompute()
        self.Doc.commitTransaction()
        before = self._copy_bodies()

        self.Doc.undo()
        self.Doc.recompute()
        self.assertEqual(self._copy_bodies(), before)
        self.assertEqual(self.Doc.RedoNames, ["Spacing"])


def _rectangle(doc, name, x0, y0, x1, y1):
    sk = doc.addObject("Sketcher::SketchObject", name)
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(*a, 0), FreeCAD.Vector(*b, 0)), False)
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    doc.recompute()
    return sk


# #164: the original is not changed by its pattern, only copied, so when the copies come apart
# or join up again the piece holding the original keeps the original's Body.
class TestPatternOriginalKeepsBody(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternOriginalKeepsBody", type="Part")
        self.sketch = _rectangle(self.Doc, "S1", 0, -5, 10, 5)
        self.pad = PartDesign.makeFeature(self.sketch, "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        self.uid = self.body.Uid

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _bodies(self):
        bodies = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        return sorted(bodies, key=lambda b: b.Shape.BoundBox.XMin)

    def _pattern(self, originals, multiBody=False):
        pattern = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = originals
        pattern.Direction = (self.sketch, ["H_Axis"])
        pattern.Length = 50
        pattern.Occurrences = 3
        pattern.MultiBody = multiBody
        self.body.addFeature(pattern)
        self.Doc.recompute()
        self.assertTrue(pattern.isValid(), pattern.getStatusString())
        return pattern

    def _assertOriginalKept(self, pieces):
        bodies = self._bodies()
        self.assertEqual(len(bodies), pieces)
        self.assertEqual(bodies[0].Uid, self.uid)
        self.assertAlmostEqual(bodies[0].Shape.BoundBox.XMin, 0.0)

    def testPatternAfterAMirrorKeepsTheBody(self):
        mirror = self.Doc.addObject("PartDesign::Mirrored", "Mirrored")
        mirror.Originals = [self.pad]
        mirror.MirrorPlane = (self.sketch, ["H_Axis"])
        self.body.addFeature(mirror)
        self.Doc.recompute()
        self._pattern([mirror])
        self._assertOriginalKept(3)

    def testCopiesComingApartKeepTheOriginalsBody(self):
        self._pattern([self.pad])
        self._assertOriginalKept(3)

    def testCopiesKeptApartKeepTheOriginalsBody(self):
        self._pattern([self.pad], multiBody=True)
        self._assertOriginalKept(3)

    def testCopiesJoiningUpKeepTheOriginalsBody(self):
        pattern = self._pattern([self.pad])
        copies = [b.Uid for b in self._bodies()[1:]]
        pattern.Length = 10
        self.Doc.recompute()
        self._assertOriginalKept(1)

        pattern.Length = 50
        self.Doc.recompute()
        self._assertOriginalKept(3)
        self.assertFalse(set(copies) & {b.Uid for b in self._bodies()})

    def testJoiningUpAfterReopenKeepsTheOriginalsBody(self):
        self._pattern([self.pad])
        path = os.path.join(tempfile.mkdtemp(), "original.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        pattern = next(o for o in self.Doc.Objects if o.TypeId == "PartDesign::LinearPattern")
        pattern.Length = 10
        self.Doc.recompute()
        self._assertOriginalKept(1)

    def testPatternCuttingTheOriginalApartRetiresItsBody(self):
        # A notch past the middle, and its mirror image from the other side: together they cut
        # the bar in two, so the original itself is split and neither half is it.
        notch = PartDesign.makeFeature(
            _rectangle(self.Doc, "S2", 4, -6, 6, 1), "Pocket", body=self.body
        )
        notch.Type = "ThroughAll"
        notch.Midplane = True
        self.Doc.recompute()
        self.assertEqual(self._bodies()[0].Uid, self.uid)
        mirror = self.Doc.addObject("PartDesign::Mirrored", "Mirrored")
        mirror.Originals = [notch]
        mirror.MirrorPlane = (self.sketch, ["H_Axis"])
        self.body.addFeature(mirror)
        self.Doc.recompute()
        bodies = self._bodies()
        self.assertEqual(len(bodies), 2)
        self.assertNotIn(self.uid, [b.Uid for b in bodies])
