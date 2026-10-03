# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Cruth contributors

"""A step that changes nothing says so, and the user can accept it (#39)."""

import os
import tempfile
import unittest

import FreeCAD
import PartDesign
import TestSketcherApp


class TestNoEffect(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestNoEffect", type="Part")
        sketch = self.Doc.addObject("Sketcher::SketchObject", "BlockSketch")
        TestSketcherApp.CreateRectangleSketch(sketch, (0, 0), (100, 100))
        self.Block = PartDesign.makeFeature(sketch, "Pad")
        self.Block.Length = 20
        self.Doc.recompute()

    def pocketOnTop(self, corner, size, depth=5):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "PocketSketch")
        TestSketcherApp.CreateRectangleSketch(sketch, corner, size)
        sketch.AttachmentSupport = (self.Block, ["Face6"])
        sketch.MapMode = "FlatFace"
        pocket = PartDesign.makeFeature(sketch, "Pocket")
        pocket.Length = depth
        self.Doc.recompute()
        return pocket

    def testPocketOffTheBodyHasNoEffect(self):
        pocket = self.pocketOnTop((200, 200), (10, 10))
        self.assertNotIn("Invalid", pocket.State)
        self.assertTrue(pocket.hasNoEffect())

    def testPocketThatCutsHasEffect(self):
        pocket = self.pocketOnTop((10, 10), (10, 10))
        self.assertLess(pocket.Shape.Volume, self.Block.Shape.Volume)
        self.assertFalse(pocket.hasNoEffect())

    def testTinyCutStillHasEffect(self):
        pocket = self.pocketOnTop((10, 10), (0.5, 0.5), depth=0.1)
        self.assertFalse(pocket.hasNoEffect())

    def testPadBuriedInTheBodyHasNoEffect(self):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "BuriedSketch")
        TestSketcherApp.CreateRectangleSketch(sketch, (10, 10), (10, 10))
        sketch.AttachmentSupport = (self.Block, ["Face6"])
        sketch.MapMode = "FlatFace"
        pad = PartDesign.makeFeature(sketch, "Pad")
        pad.Reversed = True
        pad.Length = 5
        self.Doc.recompute()
        self.assertNotIn("Invalid", pad.State)
        self.assertTrue(pad.hasNoEffect())

    def booleanWithTool(self, corner, size, height, z, kind):
        sketch = self.Doc.addObject("Sketcher::SketchObject", "ToolSketch")
        TestSketcherApp.CreateRectangleSketch(sketch, corner, size)
        sketch.AttachmentOffset = FreeCAD.Placement(FreeCAD.Vector(0, 0, z), FreeCAD.Rotation())
        toolPad = PartDesign.makeFeature(sketch, "Pad")
        toolPad.Length = height
        self.Doc.recompute()
        boolean = self.Doc.addObject("PartDesign::Boolean", "Boolean")
        PartDesign.findBodyOf(self.Block).addFeature(boolean)
        boolean.Tools = [PartDesign.findBodyOf(toolPad)]
        boolean.Type = kind
        self.Doc.recompute()
        self.assertNotIn("Invalid", boolean.State)
        return boolean

    def testCommonWithToolAroundTheBodyHasNoEffect(self):
        common = self.booleanWithTool((-10, -10), (120, 120), 40, -10, kind=2)
        self.assertTrue(common.hasNoEffect())

    def testCommonThatTrimsHasEffect(self):
        common = self.booleanWithTool((50, 0), (100, 100), 20, 0, kind=2)
        self.assertFalse(common.hasNoEffect())

    def testCutWithToolOffTheBodyHasNoEffect(self):
        cut = self.booleanWithTool((200, 200), (10, 10), 20, 0, kind=1)
        self.assertTrue(cut.hasNoEffect())

    def testFuseOfSeparatePieceHasEffect(self):
        fuse = self.booleanWithTool((200, 200), (10, 10), 20, 0, kind=0)
        self.assertFalse(fuse.hasNoEffect())

    def testFirstStepIsNeverReported(self):
        self.assertFalse(self.Block.hasNoEffect())

    def testSuppressedStepIsNotReported(self):
        pocket = self.pocketOnTop((200, 200), (10, 10))
        pocket.Suppressed = True
        self.Doc.recompute()
        self.assertFalse(pocket.hasNoEffect())

    def testMovingTheSketchBackRestoresTheEffect(self):
        pocket = self.pocketOnTop((200, 200), (10, 10))
        self.assertTrue(pocket.hasNoEffect())
        sketch = pocket.Profile[0]
        sketch.AttachmentOffset = FreeCAD.Placement(
            FreeCAD.Vector(-190, -190, 0), FreeCAD.Rotation()
        )
        self.Doc.recompute()
        self.assertFalse(pocket.hasNoEffect())

    def testAcknowledgementChangesNothingAndSurvivesReopen(self):
        pocket = self.pocketOnTop((200, 200), (10, 10))
        uid = pocket.Uid
        pocket.NoEffectAcknowledged = True
        self.Doc.recompute()
        self.assertTrue(pocket.hasNoEffect())
        path = os.path.join(tempfile.mkdtemp(), "noeffect.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        reopened = next(o for o in self.Doc.Objects if getattr(o, "Uid", None) == uid)
        self.assertTrue(reopened.NoEffectAcknowledged)
        self.assertTrue(reopened.hasNoEffect())

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)
