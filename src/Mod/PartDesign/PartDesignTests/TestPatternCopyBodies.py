# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# A pattern copy keeps its Body across recomputes while the copy exists (CoreCAD/CoreCAD#150).

import unittest

import FreeCAD


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
