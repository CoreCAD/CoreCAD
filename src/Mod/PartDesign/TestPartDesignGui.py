# SPDX-License-Identifier: LGPL-2.1-or-later

# **************************************************************************
#   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
#                                                                         *
#   This file is part of the FreeCAD CAx development system.              *
#                                                                         *
#   This program is free software; you can redistribute it and/or modify  *
#   it under the terms of the GNU Lesser General Public License (LGPL)    *
#   as published by the Free Software Foundation; either version 2 of     *
#   the License, or (at your option) any later version.                   *
#   for detail see the LICENCE text file.                                 *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful,            *
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#   GNU Library General Public License for more details.                  *
#                                                                         *
#   You should have received a copy of the GNU Library General Public     *
#   License along with FreeCAD; if not, write to the Free Software        *
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#   USA                                                                   *
# **************************************************************************

import FreeCAD
import FreeCADGui
import sys
import unittest
import Sketcher
import Part
import PartDesign
import PartDesignGui

from PySide import QtGui, QtCore
from PySide.QtGui import QApplication

from PartDesignTests.TestMaterial import TestMaterial
from PartDesignTests.TestActiveObject import TestActiveObject
from PartDesignTests.TestSuppressed import TestSuppressedStrikethrough


class CallableCheckWarning:
    def __init__(self, test):
        self.test = test

    def __call__(self):
        dialog = QApplication.activeModalWidget()
        self.test.assertIsNotNone(dialog, "Input dialog box could not be found")
        if dialog is not None:
            QtCore.QTimer.singleShot(0, dialog, QtCore.SLOT("accept()"))


class CallableComboBox:
    def __init__(self, test):
        self.test = test

    def __call__(self):
        dialog = QApplication.activeModalWidget()
        self.test.assertIsNotNone(dialog, "Warning dialog box could not be found")
        if dialog is not None:
            cbox = dialog.findChild(QtGui.QComboBox)
            self.test.assertIsNotNone(cbox, "ComboBox widget could not be found")
            if cbox is not None:
                QtCore.QTimer.singleShot(0, dialog, QtCore.SLOT("accept()"))


class CallableCheckExemptionDialog:
    def __init__(self, test):
        self.test = test

    def __call__(self):
        dialog = QApplication.activeModalWidget()
        if dialog is not None:
            dialogcheck = CallableCheckExemptionDialogWasClosed(self.test)
            QtCore.QTimer.singleShot(100, dialogcheck)
            QtCore.QTimer.singleShot(0, dialog, QtCore.SLOT("accept()"))


class CallableCheckExemptionDialogWasClosed:
    def __init__(self, test):
        self.test = test

    def __call__(self):
        dialog = QApplication.activeModalWidget()
        self.test.assertIsNone(dialog, "Dialog box was not closed by accept()")


App = FreeCAD
Gui = FreeCADGui


# ---------------------------------------------------------------------------
# define the test cases to test the FreeCAD PartDesign module
# ---------------------------------------------------------------------------
class PartDesignGuiTestCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("SketchGuiTest")

    @unittest.skip(
        "Moving a feature from one body to another is not part of the marker model — a body "
        "records nothing to move, and there is a single shared origin, so both this test's "
        "per-body Group counts and its per-body Origin-membership checks describe the old "
        "container paradigm. Cross-body move is not feasible in the new design; parked."
    )
    def testRefuseToMoveSingleFeature(self):
        FreeCAD.Console.PrintMessage(
            "Testing refuse to move the feature with dependencies from one body to another\n"
        )
        self.BodySource = self.Doc.addObject("PartDesign::Body", "Body")
        Gui.activateView("Gui::View3DInventor", True)

        self.BoxObj = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.BoxObj.Length = 10.0
        self.BoxObj.Width = 10.0
        self.BoxObj.Height = 10.0
        self.BodySource.addFeature(self.BoxObj)

        App.ActiveDocument.recompute()

        self.Sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        self.Sketch.AttachmentSupport = (self.BoxObj, ("Face3",))
        self.Sketch.MapMode = "FlatFace"
        self.BodySource.addFeature(self.Sketch)

        geoList = []
        geoList.append(Part.LineSegment(App.Vector(2.0, 8.0, 0), App.Vector(8.0, 8.0, 0)))
        geoList.append(Part.LineSegment(App.Vector(8.0, 8.0, 0), App.Vector(8.0, 2.0, 0)))
        geoList.append(Part.LineSegment(App.Vector(8.0, 2.0, 0), App.Vector(2.0, 2.0, 0)))
        geoList.append(Part.LineSegment(App.Vector(2.0, 2.0, 0), App.Vector(2.0, 8.0, 0)))
        self.Sketch.addGeometry(geoList, False)
        conList = []
        conList.append(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        conList.append(Sketcher.Constraint("Coincident", 1, 2, 2, 1))
        conList.append(Sketcher.Constraint("Coincident", 2, 2, 3, 1))
        conList.append(Sketcher.Constraint("Coincident", 3, 2, 0, 1))
        conList.append(Sketcher.Constraint("Horizontal", 0))
        conList.append(Sketcher.Constraint("Horizontal", 2))
        conList.append(Sketcher.Constraint("Vertical", 1))
        conList.append(Sketcher.Constraint("Vertical", 3))
        self.Sketch.addConstraint(conList)

        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.Pad.Profile = self.Sketch
        self.Pad.Length = 10.000000
        self.Pad.Length2 = 100.000000
        self.Pad.Type = 0
        self.Pad.UpToFace = None
        self.Pad.Reversed = 0
        self.Pad.SideType = "One side"
        self.Pad.Offset = 0.000000

        self.BodySource.addFeature(self.Pad)

        self.Doc.recompute()
        Gui.SendMsgToActiveView("ViewFit")

        self.BodyTarget = self.Doc.addObject("PartDesign::Body", "Body")

        Gui.Selection.addSelection(App.ActiveDocument.Pad)
        cobj = CallableCheckWarning(self)
        QtCore.QTimer.singleShot(500, cobj)
        Gui.runCommand("PartDesign_MoveFeature")
        # assert dependencies of the Sketch
        self.assertEqual(len(self.BodySource.Group), 3, "Source body feature count is wrong")
        self.assertEqual(len(self.BodyTarget.Group), 0, "Target body feature count is wrong")

    @unittest.skip(
        "Moving a feature from one body to another is not part of the marker model — a body "
        "records nothing to move, and there is a single shared origin, so both this test's "
        "per-body Group counts and its per-body Origin-membership checks describe the old "
        "container paradigm. Cross-body move is not feasible in the new design; parked."
    )
    def testMoveSingleFeature(self):
        FreeCAD.Console.PrintMessage("Testing moving one feature from one body to another\n")
        self.BodySource = self.Doc.addObject("PartDesign::Body", "Body")
        Gui.activateView("Gui::View3DInventor", True)

        self.Sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        self.BodySource.addFeature(self.Sketch)
        self.Sketch.AttachmentSupport = (self.BodySource.Origin.OriginFeatures[3], [""])
        self.Sketch.MapMode = "FlatFace"

        geoList = []
        geoList.append(
            Part.LineSegment(
                App.Vector(-10.000000, 10.000000, 0), App.Vector(10.000000, 10.000000, 0)
            )
        )
        geoList.append(
            Part.LineSegment(
                App.Vector(10.000000, 10.000000, 0), App.Vector(10.000000, -10.000000, 0)
            )
        )
        geoList.append(
            Part.LineSegment(
                App.Vector(10.000000, -10.000000, 0), App.Vector(-10.000000, -10.000000, 0)
            )
        )
        geoList.append(
            Part.LineSegment(
                App.Vector(-10.000000, -10.000000, 0), App.Vector(-10.000000, 10.000000, 0)
            )
        )
        self.Sketch.addGeometry(geoList, False)
        conList = []
        conList.append(Sketcher.Constraint("Coincident", 0, 2, 1, 1))
        conList.append(Sketcher.Constraint("Coincident", 1, 2, 2, 1))
        conList.append(Sketcher.Constraint("Coincident", 2, 2, 3, 1))
        conList.append(Sketcher.Constraint("Coincident", 3, 2, 0, 1))
        conList.append(Sketcher.Constraint("Horizontal", 0))
        conList.append(Sketcher.Constraint("Horizontal", 2))
        conList.append(Sketcher.Constraint("Vertical", 1))
        conList.append(Sketcher.Constraint("Vertical", 3))
        self.Sketch.addConstraint(conList)

        self.Pad = self.Doc.addObject("PartDesign::Pad", "Pad")
        self.BodySource.addFeature(self.Pad)
        self.Pad.Profile = self.Sketch
        self.Pad.Length = 10.000000
        self.Pad.Length2 = 100.000000
        self.Pad.Type = 0
        self.Pad.UpToFace = None
        self.Pad.Reversed = 0
        self.Pad.SideType = "One side"
        self.Pad.Offset = 0.000000

        self.Doc.recompute()
        Gui.SendMsgToActiveView("ViewFit")

        self.BodyTarget = self.Doc.addObject("PartDesign::Body", "Body")

        Gui.Selection.addSelection(App.ActiveDocument.Pad)
        cobj = CallableComboBox(self)
        QtCore.QTimer.singleShot(500, cobj)
        Gui.runCommand("PartDesign_MoveFeature")
        # assert dependencies of the Sketch
        self.Doc.recompute()

        self.assertFalse(
            self.Sketch.AttachmentSupport[0][0] in self.BodySource.Origin.OriginFeatures
        )
        self.assertTrue(
            self.Sketch.AttachmentSupport[0][0] in self.BodyTarget.Origin.OriginFeatures
        )
        self.assertEqual(len(self.BodySource.Group), 0, "Source body feature count is wrong")
        self.assertEqual(len(self.BodyTarget.Group), 2, "Target body feature count is wrong")

    def tearDown(self):
        FreeCAD.closeDocument("SketchGuiTest")


class PartDesignTransformed(unittest.TestCase):
    def setUp(self):
        self.Doc = App.newDocument("PartDesignTransformed", type="Part")
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Body.addFeature(self.Doc.addObject("PartDesign::AdditiveBox", "BodyBox"))
        # A second box left outside every body.
        self.BoxObj = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Doc.recompute()

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def testMultiTransformCase(self):
        App.Console.PrintMessage("Testing applying MultiTransform to the Box outside the body\n")
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.BoxObj)
        seen = []

        def dismiss():
            dialog = QApplication.activeModalWidget()
            if dialog is not None:
                seen.append(dialog.windowTitle())
                dialog.reject()

        # A timer the test owns and stops, so a missed dialog cannot fire into a later test.
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(dismiss)
        timer.start(500)
        Gui.runCommand("PartDesign_MultiTransform")
        timer.stop()

        self.assertEqual(seen, ["Wrong selection"])
        self.assertFalse(
            [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::MultiTransform")]
        )

    def testPatternGoesToTheSelectedBody(self):
        """Cruth #130: the selection decides the body, not whichever body was active."""
        sketch = self.Doc.addObject("Sketcher::SketchObject", "OtherProfile")
        corners = [(50, 0), (60, 0), (60, 10), (50, 10)]
        for start, end in zip(corners, corners[1:] + corners[:1]):
            sketch.addGeometry(Part.LineSegment(App.Vector(*start, 0), App.Vector(*end, 0)))
        self.Doc.recompute()
        pad = PartDesign.makeFeature(sketch, "Pad")
        self.Doc.recompute()
        other = [
            b for b in self.Doc.Objects if b.isDerivedFrom("PartDesign::Body") and b.Tip == pad
        ][0]
        Gui.activateView("Gui::View3DInventor", True)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(other.Tip)
        seen = []

        def dismiss():
            dialog = QApplication.activeModalWidget()
            if dialog is not None:
                seen.append(dialog.windowTitle())
                dialog.reject()

        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(dismiss)
        timer.start(500)
        Gui.runCommand("PartDesign_LinearPattern")
        timer.stop()
        if Gui.Control.activeDialog():
            Gui.Control.activeTaskDialog().accept()

        # The pattern extends the selected body's chain; the other body is untouched.
        # (A pattern whose copies do not touch splits into one body per solid, so the selected
        # body object itself may be replaced; its chain is what carries on.)
        self.assertEqual(seen, [])
        patterns = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::LinearPattern")]
        self.assertEqual(len(patterns), 1)
        self.assertEqual(patterns[0].BaseFeature, pad)
        self.assertEqual(self.Body.Tip.Name, "BodyBox")


class CreateSketch(unittest.TestCase):

    def testPDCreateSketch(self):
        App.Console.PrintMessage("Testing the creation of a sketch\n")
        param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/PartDesign")
        useAttachmentSaved = param.GetBool("NewSketchUseAttachmentDialog", False)
        param.SetBool("NewSketchUseAttachmentDialog", False)
        App.newDocument(type="Part")
        App.activeDocument().addObject("PartDesign::Body", "Body")
        App.ActiveDocument.getObject("Body").Label = "Body"
        FreeCADGui.activateView("Gui::View3DInventor", True)
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.runCommand("Std_OrthographicCamera", 1)
        # Owned and stopped: a leaked single-shot used to fire into the next test and accept
        # whatever dialog it had open.
        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(CallableCheckExemptionDialog(self))
        timer.start(100)
        FreeCADGui.runCommand("PartDesign_CompSketches", 0)
        timer.stop()
        activeDialog = FreeCADGui.Control.activeDialog()
        self.assertIsNotNone(activeDialog)
        if activeDialog is not None:
            FreeCADGui.Control.closeDialog()
        App.closeDocument(App.ActiveDocument.Name)
        param.SetBool("NewSketchUseAttachmentDialog", useAttachmentSaved)


class TestClosingAFeatureDialog(unittest.TestCase):
    """Cruth #20: a feature dialog removed with a bare closeDialog() -- neither OK nor
    Cancel -- used to leave its "Make ..." step open, stranding the half-made feature and
    the Body spawned for it. Closing without OK now means Cancel."""

    def setUp(self):
        self.Doc = App.newDocument("ClosingAFeatureDialog", type="Part")
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("sketch")
        self.Sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        pts = [
            App.Vector(0, 0, 0),
            App.Vector(10, 0, 0),
            App.Vector(10, 10, 0),
            App.Vector(0, 10, 0),
        ]
        for i in range(4):
            self.Sketch.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
        self.Doc.commitTransaction()
        self.Doc.recompute()
        Gui.activateWorkbench("PartDesignWorkbench")
        self.Before = {o.Name for o in self.Doc.Objects}

    def tearDown(self):
        if Gui.Control.activeDialog():
            Gui.Control.closeDialog()
        App.closeDocument(self.Doc.Name)

    def startPad(self):
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Sketch)
        Gui.runCommand("PartDesign_Pad")
        self.assertTrue(Gui.Control.activeDialog())
        self.assertIn("Pad", {o.Name for o in self.Doc.Objects})

    def testCloseDialogRollsBackANewFeature(self):
        self.startPad()
        Gui.Control.closeDialog()
        self.assertEqual({o.Name for o in self.Doc.Objects} - self.Before, set())
        self.assertFalse(self.Doc.HasPendingTransaction)
        self.assertEqual(self.Doc.UndoNames, ["sketch"])

    def testResetEditKeepsANewFeature(self):
        # Finishing the edit is a different gesture: the feature stays, as its own undo step.
        self.startPad()
        Gui.ActiveDocument.resetEdit()
        self.assertEqual({o.Name for o in self.Doc.Objects} - self.Before, {"Body", "Pad"})
        self.assertFalse(self.Doc.HasPendingTransaction)
        self.assertEqual(self.Doc.UndoNames, ["Make Pad", "sketch"])

    def testCloseDialogOfANewPatternDoesNotCrash(self):
        # Cruth #131: rolling back a new pattern or mirror deleted it before its panel was
        # destroyed, and the panel's clean-up (hiding the origin planes/axes it had shown)
        # then read the deleted feature and brought the application down.
        Gui.activateView("Gui::View3DInventor", True)
        self.startPad()
        Gui.ActiveDocument.resetEdit()
        pad = self.Doc.getObject("Pad")
        before = {o.Name for o in self.Doc.Objects}
        for command in (
            "PartDesign_LinearPattern",
            "PartDesign_PolarPattern",
            "PartDesign_Mirrored",
        ):
            with self.subTest(command=command):
                Gui.Selection.clearSelection()
                Gui.Selection.addSelection(pad)
                Gui.runCommand(command)
                self.assertTrue(Gui.Control.activeDialog())
                Gui.Control.closeDialog()
                self.assertEqual({o.Name for o in self.Doc.Objects}, before)


# class PartDesignGuiTestCases(unittest.TestCase):
#   def setUp(self):
#       self.Doc = FreeCAD.newDocument("SketchGuiTest")
#
#   def testBoxCase(self):
#       self.Box = self.Doc.addObject('PartDesign::SketchObject','SketchBox')
#       self.Box.addGeometry(Part.LineSegment(App.Vector(-99.230339,36.960674,0),App.Vector(69.432587,36.960674,0)))
#       self.Box.addGeometry(Part.LineSegment(App.Vector(69.432587,36.960674,0),App.Vector(69.432587,-53.196629,0)))
#       self.Box.addGeometry(Part.LineSegment(App.Vector(69.432587,-53.196629,0),App.Vector(-99.230339,-53.196629,0)))
#       self.Box.addGeometry(Part.LineSegment(App.Vector(-99.230339,-53.196629,0),App.Vector(-99.230339,36.960674,0)))
#
#   def tearDown(self):
#       #closing doc
#       FreeCAD.closeDocument("SketchGuiTest")


class TestBodyColumn(unittest.TestCase):
    """Cruth ARCHITECTURE §8.7: the tree names the body each step builds, with its swatch, and in
    brackets the bodies a step only references. A profile sketch builds nothing and shows
    nothing."""

    def setUp(self):
        self.Doc = App.newDocument("BodyColumn", type="Part")
        Gui.activateWorkbench("PartDesignWorkbench")

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def square(self, name, x):
        sketch = self.Doc.addObject("Sketcher::SketchObject", name)
        sketch.Placement.Base = App.Vector(x, 0, 0)
        pts = [(0, 0), (10, 0), (10, 10), (0, 10)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            sketch.addGeometry(Part.LineSegment(App.Vector(*a, 0), App.Vector(*b, 0)))
        self.Doc.recompute()
        return sketch

    def columnTexts(self):
        # The tree applies changes on a short timer; let it run.
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(300, loop.quit)
        loop.exec_()
        trees = [
            t for t in Gui.getMainWindow().findChildren(QtGui.QTreeWidget) if t.columnCount() == 4
        ]
        self.assertTrue(trees)
        texts = {}

        def walk(item):
            for i in range(item.childCount()):
                child = item.child(i)
                texts[child.text(0)] = (child.text(3), not child.icon(3).isNull())
                walk(child)

        walk(trees[0].invisibleRootItem())
        return texts

    def testBodyColumnNamesWhatEachStepBuilds(self):
        s1 = self.square("S1", 0)
        pad = PartDesign.makeFeature(s1, "Pad")
        s2 = self.square("S2", 50)
        # S2 references a face of the first body; it builds nothing itself.
        s2.AttachmentSupport = (pad, [""])
        s2.MapMode = "ObjectXY"
        self.Doc.recompute()
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        body.Label = "Housing"
        self.Doc.recompute()
        texts = self.columnTexts()
        self.assertIn("Pad", texts, sorted(texts))
        self.assertEqual(texts["Pad"], ("Housing", True))
        self.assertEqual(texts["S1"], ("", False))
        self.assertEqual(texts["S2"], ("(Housing)", False))
        self.assertEqual(texts["Housing"], ("", True))

    def tree(self):
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(300, loop.quit)
        loop.exec_()
        return [
            t for t in Gui.getMainWindow().findChildren(QtGui.QTreeWidget) if t.columnCount() == 4
        ][0]

    def rootRows(self):
        """(label, child count) of every row directly under this document, read in one pass."""
        root = self.tree().invisibleRootItem()
        for i in range(root.childCount()):
            doc = root.child(i)
            if doc.text(0) == self.Doc.Label:
                return [
                    (doc.child(j).text(0), doc.child(j).childCount())
                    for j in range(doc.childCount())
                ]
        self.fail("document not in the tree")

    def testTreeIsTheTimeline(self):
        # §8.1: every step at the document level, in the order it was made; a body holds none.
        pad1 = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        pad2 = PartDesign.makeFeature(self.square("S2", 50), "Pad")
        self.Doc.recompute()
        rows = self.rootRows()
        labels = [label for label, _ in rows]
        bodies = [o.Label for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        self.assertEqual(len(bodies), 2)
        for body in bodies:
            self.assertIn((body, 0), rows)
        self.assertIn(pad1.Label, labels)
        self.assertIn(pad2.Label, labels)
        made = [o.Label for o in self.Doc.Objects if o.Label in labels]
        self.assertEqual([label for label in labels if label in made], made)

    def testFacePickedThroughABodyHighlightsTheStep(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        self.Doc.recompute()
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body.Name, pad.Name + ".Face6")
        selected = [i.text(0) for i in self.tree().selectedItems()]
        Gui.Selection.clearSelection()
        self.assertEqual(selected, [pad.Label])


class TestDatumPlane(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestDatumPlane", type="Part")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def testDefaultColor(self):
        """
        A datum object uses a different default color than a Part feature.
        This color must still be set after its creation.
        """
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)

        self.Doc.recompute()
        datum = body.addFeature(body.Document.addObject("Part::DatumPlane", "DatumPlane"))
        datum.AttachmentSupport = [(box, "Face6")]
        datum.MapMode = "FlatFace"
        self.Doc.recompute()

        # Datums share the coordinate-system light blue (ViewProviderCoordinateSystem), not
        # PartDesign's old yellow: the PartDesign datum types were retired (#45).
        packed_color = 0x3296FAFF
        r, g, b, a = datum.ViewObject.ShapeColor
        color = (
            int(r * 255.0 + 0.5) << 24
            | int(g * 255.0 + 0.5) << 16
            | int(b * 255.0 + 0.5) << 8
            | int(a * 255.0 + 0.5)
        )

        self.assertEqual(packed_color, color)
