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
import os
import re
import shutil
import sys
import tempfile
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
from PartDesignTests.TestTablePatternGui import TestTablePatternCommand, TestTablePatternGui


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

    def testPatternOfAFeatureOutsideEveryBodyIsRefused(self):
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
        Gui.runCommand("PartDesign_LinearPattern")
        timer.stop()

        self.assertEqual(seen, ["Wrong selection"])
        self.assertFalse(
            [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::LinearPattern")]
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

    def testCloseDialogOfANewRevolveOrHelixRestoresTheOrigin(self):
        # The #131 crash in other panels: Revolve and Groove read the deleted feature and
        # crashed; Helix and the primitives skipped the clean-up and left the origin shown.
        Gui.activateView("Gui::View3DInventor", True)
        self.startPad()
        Gui.ActiveDocument.resetEdit()
        before = {o.Name for o in self.Doc.Objects}
        axis = self.Doc.X_Axis
        shown = axis.ViewObject.isVisible()
        for command, selected in (
            ("PartDesign_Revolution", self.Sketch),
            ("PartDesign_Groove", self.Sketch),
            ("PartDesign_AdditiveHelix", self.Sketch),
            ("PartDesign_CompPrimitiveAdditive", None),
        ):
            with self.subTest(command=command):
                Gui.Selection.clearSelection()
                if selected:
                    Gui.Selection.addSelection(selected)
                Gui.runCommand(command, 0)
                self.assertTrue(Gui.Control.activeDialog())
                self.assertTrue(axis.ViewObject.isVisible())
                Gui.Control.closeDialog()
                self.assertEqual({o.Name for o in self.Doc.Objects}, before)
                self.assertEqual(axis.ViewObject.isVisible(), shown)


class TestFeatureCommands(unittest.TestCase):
    """Cruth #143: the feature commands, driven from the selection as a user would."""

    def setUp(self):
        self.Doc = App.newDocument("FeatureCommands", type="Part")
        self.Pad = PartDesign.makeFeature(self.square("Profile", 0, 10), "Pad")
        self.Doc.recompute()
        Gui.activateWorkbench("PartDesignWorkbench")
        Gui.activateView("Gui::View3DInventor", True)

    def tearDown(self):
        if Gui.Control.activeDialog():
            Gui.Control.closeDialog()
        App.closeDocument(self.Doc.Name)

    def square(self, name, x0, x1):
        sketch = self.Doc.addObject("Sketcher::SketchObject", name)
        corners = [(x0, 0), (x1, 0), (x1, 10), (x0, 10)]
        for start, end in zip(corners, corners[1:] + corners[:1]):
            sketch.addGeometry(Part.LineSegment(App.Vector(*start, 0), App.Vector(*end, 0)))
        self.Doc.recompute()
        return sketch

    def runCommand(self, command):
        """Runs a command, dismissing and recording any message box it raises."""
        seen = []

        def dismiss():
            dialog = QApplication.activeModalWidget()
            if dialog is not None:
                seen.append(dialog.windowTitle() or dialog.text())
                dialog.reject()

        timer = QtCore.QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(dismiss)
        timer.start(500)
        Gui.runCommand(command)
        timer.stop()
        return seen

    def testFaceDressUpsDropEveryPickedEdge(self):
        # Removing an edge used to skip the element after it, so a second edge survived.
        for command, kind in (
            ("PartDesign_Thickness", "PartDesign::Thickness"),
            ("PartDesign_Draft", "PartDesign::Draft"),
        ):
            with self.subTest(command=command):
                Gui.Selection.clearSelection()
                for element in ("Edge1", "Edge2", "Face6"):
                    Gui.Selection.addSelection(self.Doc.Name, self.Pad.Name, element)
                self.assertEqual(self.runCommand(command), [])
                feature = [o for o in self.Doc.Objects if o.isDerivedFrom(kind)][0]
                self.assertEqual(feature.Base[1], ["Face6"])
                Gui.Control.closeDialog()

    def testEveryCutFindsTheOnlyBody(self):
        # Cruth §8.3: a cut drawn on an origin plane cuts the one body there is. Only Pocket
        # used to; the other cuts said there was no solid to subtract from.
        cutter = self.square("Cutter", 2, 4)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(cutter)
        self.assertEqual(self.runCommand("PartDesign_Groove"), [])
        Gui.ActiveDocument.resetEdit()
        grooves = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Groove")]
        self.assertEqual(len(grooves), 1)
        self.assertEqual(grooves[0].BaseFeature, self.Pad)


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


def plain(tooltip):
    """The Bodies column tooltip as text: a line per <br>, tags dropped, the square as ■."""
    text = tooltip.replace("<br>", "\n").replace("&#9632;", "■")
    return re.sub(r"<[^>]+>", "", text)


def swatchColours(tooltip):
    return re.findall(r"color:(#[0-9a-f]{6})", tooltip)


def colourName(body):
    return QtGui.QColor.fromRgbF(*body.Color[:3]).name()


def click(widget, point, kind=QtCore.QEvent.MouseButtonPress):
    """A left click at @p point, or with @p kind a double-click, delivered as a person's would."""
    for event_type in (kind, QtCore.QEvent.MouseButtonRelease):
        buttons = (
            QtCore.Qt.NoButton
            if event_type == QtCore.QEvent.MouseButtonRelease
            else (QtCore.Qt.LeftButton)
        )
        event = QtGui.QMouseEvent(
            event_type,
            QtCore.QPointF(point),
            QtCore.QPointF(widget.mapToGlobal(point)),
            QtCore.Qt.LeftButton,
            buttons,
            QtCore.Qt.NoModifier,
        )
        QApplication.sendEvent(widget, event)
    # The tree passes its selection on after a short timer; let it run, as it would for a person.
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(20, loop.quit)
    loop.exec_()


class TestBodyColumn(unittest.TestCase):
    """Cruth ARCHITECTURE §8.7: the tree names the body each step builds, with its swatch. A body
    a step only references is not shown; a row may already hold several swatches. A profile
    sketch builds nothing and shows nothing."""

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
                texts[child.text(0)] = (
                    child.text(3),
                    child.toolTip(3),
                    not child.icon(3).isNull(),
                )
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
        # Swatches only; the names are in the tooltip, each after a square of its colour.
        self.assertEqual(texts["Pad"][0::2], ("", True))
        self.assertEqual(plain(texts["Pad"][1]), "■ Housing")
        self.assertEqual(texts["S1"], ("", "", False))
        self.assertEqual(texts["S2"], ("", "", False))
        self.assertEqual(swatchColours(texts["Pad"][1]), [colourName(body)])

    def testAStepPastTheStopRowShowsNoBodyAndIsNotDrawn(self):
        # Past the stop row nothing exists yet: the second body's pad shows no swatch and is not
        # drawn, though it keeps its own visibility for when the model is rolled forward.
        PartDesign.makeFeature(self.square("S1", 0), "Pad")
        s2 = self.square("S2", 50)
        pad2 = PartDesign.makeFeature(s2, "Pad")
        self.Doc.recompute()
        self.assertTrue(self.columnTexts()[pad2.Label][2])

        self.Doc.rollBackTo(s2)
        self.Doc.recompute()
        texts = self.columnTexts()
        self.assertEqual(texts[pad2.Label], ("", "", False))
        self.assertTrue(texts[[o for o in self.Doc.Objects if o.Name == "Pad"][0].Label][2])
        self.assertFalse(pad2.ViewObject.isVisible())
        self.assertTrue(pad2.ViewObject.Visibility)

        self.Doc.rollBackTo(None)
        self.Doc.recompute()
        self.assertTrue(self.columnTexts()[pad2.Label][2])
        self.assertTrue(pad2.ViewObject.isVisible())

    def testDatumShowsNoBody(self):
        # A datum builds nothing; attached to a body's geometry or not, its row shows no swatch.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        attached = {}
        for kind in ("DatumPlane", "DatumLine", "DatumPoint", "LocalCoordinateSystem"):
            datum = self.Doc.addObject("Part::" + kind, "Attached" + kind)
            datum.AttachmentSupport = (pad, [""])
            datum.MapMode = "ObjectXY" if kind != "DatumPoint" else "ObjectOrigin"
            attached[kind] = datum
        loose = self.Doc.addObject("Part::DatumPlane", "LoosePlane")
        self.Doc.recompute()
        texts = self.columnTexts()
        for kind, datum in attached.items():
            self.assertEqual(texts[datum.Label], ("", "", False), kind)
        self.assertEqual(texts[loose.Label], ("", "", False))

    def testDatumFeedingItsOwnBodyShowsNothing(self):
        # A datum on a body's face, carrying the sketch of that same body's next step, is not a
        # reference to a foreign body: like the sketch it carries, it shows nothing.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        plane = self.Doc.addObject("Part::DatumPlane", "TopPlane")
        plane.AttachmentSupport = (pad, [""])
        plane.MapMode = "ObjectXY"
        sketch = self.square("S2", 2)
        sketch.AttachmentSupport = (plane, [""])
        sketch.MapMode = "ObjectXY"
        pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = sketch
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        body.addFeature(pocket)
        self.Doc.recompute()
        texts = self.columnTexts()
        self.assertEqual(texts[plane.Label], ("", "", False))

    def pattern(self, occurrences, length=60.0, apart=True):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        origin = next(o for o in self.Doc.Objects if o.isDerivedFrom("App::Origin"))
        y_axis = next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == "Y_Axis")
        pattern = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.TransformMode = "Whole shape"
        pattern.MultiBody = apart
        pattern.Direction = (y_axis, [""])
        pattern.Length = length
        pattern.Occurrences = occurrences
        body.addFeature(pattern)
        self.Doc.recompute()
        bodies = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        if apart:
            self.assertEqual(len(bodies), occurrences)
        return pad, pattern, bodies

    def testPatternBodiesShowThreeAndACount(self):
        # §5.5 / §8.7: past three, a pattern's bodies are three swatches, an ellipsis and a count, and
        # the tooltip lists every body in its own colour.
        pad, pattern, bodies = self.pattern(5)
        copies = bodies[1:]
        texts = self.columnTexts()
        tip = texts[pattern.Label][1]
        self.assertEqual(len(plain(tip).splitlines()), 4)
        self.assertEqual(sorted(swatchColours(tip)), sorted(colourName(b) for b in copies))
        self.assertEqual(len(set(swatchColours(tip))), 4)
        # The pattern left the original as it was, so its swatch stays on the pad.
        self.assertEqual(swatchColours(texts[pad.Label][1]), [colourName(bodies[0])])
        # Three swatches, then the ellipsis and count after them.
        self.assertGreater(self.columnIconWidth(pattern.Label), 3 * 10 + 2 * 2)

    def testABodysSwatchStandsOnTheStepThatLastShapedIt(self):
        # The rows that build a body are washed when it is selected; its swatch is on one row.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        sketch = self.square("S2", 2)
        pocket = self.Doc.addObject("PartDesign::Pocket", "Pocket")
        pocket.Profile = sketch
        pocket.Type = "ThroughAll"
        body.addFeature(pocket)
        self.Doc.recompute()
        texts = self.columnTexts()
        self.assertEqual(swatchColours(texts[pocket.Label][1]), [colourName(body)])
        self.assertEqual(texts[pad.Label], ("", "", False))

        self.Doc.rollBackTo(pad)
        self.Doc.recompute()
        self.assertEqual(swatchColours(self.columnTexts()[pad.Label][1]), [colourName(body)])
        self.Doc.rollBackTo(None)
        self.Doc.recompute()

    def testRolledBackToTheOriginAPatternAndItsCopiesDrawNothing(self):
        # A body none of whose steps is built has nothing to draw, and the pattern's eye reads off.
        pad, pattern, bodies = self.pattern(3)
        origin = next(o for o in self.Doc.Objects if o.isDerivedFrom("App::Origin"))
        self.Doc.rollBackTo(origin)
        self.Doc.recompute()
        self.columnTexts()
        self.assertFalse(pattern.ViewObject.isVisible())
        self.assertEqual([b.Name for b in bodies if b.ViewObject.isVisible()], [])
        self.Doc.rollBackTo(None)
        self.Doc.recompute()
        self.columnTexts()
        self.assertTrue(pattern.ViewObject.isVisible())
        self.assertEqual([b.Name for b in bodies if not b.ViewObject.isVisible()], [])

    def testAPatternFusedIntoTheOriginalTakesItsSwatch(self):
        pad, pattern, bodies = self.pattern(3, length=10.0, apart=False)
        self.assertEqual(len(bodies), 1)
        texts = self.columnTexts()
        self.assertEqual(swatchColours(texts[pattern.Label][1]), [colourName(bodies[0])])
        self.assertEqual(texts[pad.Label], ("", "", False))

    def featurePattern(self, pad):
        # As the pattern command makes it: the pad copied along its own sketch's H axis.
        body = next(
            b for b in self.Doc.Objects if b.isDerivedFrom("PartDesign::Body") and b.Tip == pad
        )
        pattern = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = [pad]
        pattern.Direction = (pad.Profile[0], ["H_Axis"])
        pattern.Length = 60.0
        pattern.Occurrences = 2
        pattern.MultiBody = True
        body.addFeature(pattern)
        self.Doc.recompute()
        copy = next(
            b
            for b in self.Doc.Objects
            if b.isDerivedFrom("PartDesign::Body") and b != body and b.Tip == pattern
        )
        return pattern, body, copy

    def testAPatternLeavesAStepItOnlyReferencesShown(self):
        # A pad told to pad up to another body's face references that body; patterning it hides
        # only the pattern's own chain.
        other = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        pad = PartDesign.makeFeature(self.square("S2", 100), "Pad")
        pad.UpToFace = (other, ["Face3"])
        self.Doc.recompute()
        self.featurePattern(pad)
        self.tree()
        self.assertTrue(other.Visibility)
        self.assertTrue(other.ViewObject.isVisible())

    def testSelectingACopyWashesTheStepItCopiedButNotItsSketch(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        pattern, _, copy = self.featurePattern(pad)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, copy.Name)
        self.assertEqual(self.tintedRows(), sorted([pattern.Label, pad.Label]))
        self.assertEqual(self.rowTint(pad.Label), self.rowTint(pattern.Label))
        Gui.Selection.clearSelection()

    def testSelectingAnOriginalWashesWhatMadeItNotWhatCameAfter(self):
        # The pattern left the original as it was, so its swatch stays on the pad, and the
        # pattern, made from the pad, is not part of what made it.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        _, body, _ = self.featurePattern(pad)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body.Name)
        self.assertEqual(self.tintedRows(), sorted([pad.Label, pad.Profile[0].Label]))
        Gui.Selection.clearSelection()

    def testAPatternsEyeShowsItsBodiesAndDrawsNothingItself(self):
        # Its pieces are drawn once, by their bodies; the pattern's eye shows and hides them.
        _, pattern, bodies = self.pattern(3)
        self.tree()
        self.assertTrue(pattern.ViewObject.isVisible())
        self.assertEqual(pattern.ViewObject.SwitchNode.whichChild.getValue(), -1)
        self.assertTrue(all(b.ViewObject.isVisible() for b in bodies))

        # The tree's eye toggles the object, and reads back what it set.
        pattern.Visibility = False
        self.assertFalse(pattern.ViewObject.isVisible())
        self.assertFalse(any(b.Visibility for b in bodies))
        pattern.Visibility = True
        self.assertTrue(pattern.ViewObject.isVisible())
        self.assertTrue(all(b.Visibility for b in bodies))
        self.assertEqual(pattern.ViewObject.SwitchNode.whichChild.getValue(), -1)

    def testHidingEveryPieceTurnsThePatternsEyeOff(self):
        _, pattern, bodies = self.pattern(3)
        bodies[0].ViewObject.Visibility = False
        self.assertTrue(pattern.Visibility)
        for body in bodies[1:]:
            body.ViewObject.Visibility = False
        self.assertFalse(pattern.Visibility)

    def testThreePatternBodiesSitSideBySide(self):
        _, pattern, _ = self.pattern(4)
        self.assertEqual(self.columnIconWidth(pattern.Label), 3 * 10 + 2 * 2)

    def columnIconWidth(self, label):
        """The Bodies column icon's width at its drawn height, read while the row is live."""

        def walk(item):
            for i in range(item.childCount()):
                child = item.child(i)
                if child.text(0) == label:
                    return child.icon(3).actualSize(QtCore.QSize(1000, 10)).width()
                found = walk(child)
                if found is not None:
                    return found
            return None

        width = walk(self.tree().invisibleRootItem())
        self.assertIsNotNone(width, label)
        return width

    def tree(self):
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(300, loop.quit)
        loop.exec_()
        return [
            t for t in Gui.getMainWindow().findChildren(QtGui.QTreeWidget) if t.columnCount() == 4
        ][0]

    def rootRows(self):
        """(label, child count) of every visible row directly under this document, in one pass."""
        root = self.tree().invisibleRootItem()
        for i in range(root.childCount()):
            doc = root.child(i)
            if doc.text(0) == self.Doc.Label:
                return [
                    (doc.child(j).text(0), doc.child(j).childCount())
                    for j in range(doc.childCount())
                    if not doc.child(j).isHidden()
                ]
        self.fail("document not in the tree")

    def testTreeIsTheTimeline(self):
        # §8.1: every step at the document level, in the order it was made. A body is not a step:
        # it has no row, and the Body column names it beside each step that builds it.
        pad1 = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        pad2 = PartDesign.makeFeature(self.square("S2", 50), "Pad")
        self.Doc.recompute()
        rows = self.rootRows()
        labels = [label for label, _ in rows]
        bodies = [o.Label for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")]
        self.assertEqual(len(bodies), 2)
        for body in bodies:
            self.assertNotIn(body, labels)
        self.assertIn(pad1.Label, labels)
        self.assertIn(pad2.Label, labels)
        made = [o.Label for o in self.Doc.Objects if o.Label in labels]
        self.assertEqual([label for label in labels if label in made], made)

    def testSketchHasItsOwnRow(self):
        # §8.1: a sketch is a step of its own, not a child of the feature that consumes it. Two
        # features sharing one sketch show it once, before both.
        sketch = self.square("S1", 0)
        pad1 = PartDesign.makeFeature(sketch, "Pad")
        pad2 = PartDesign.makeFeature(sketch, "Pad")
        self.Doc.recompute()
        rows = self.rootRows()
        labels = [label for label, _ in rows]
        self.assertEqual(labels.count(sketch.Label), 1)
        self.assertIn((pad1.Label, 0), rows)
        self.assertIn((pad2.Label, 0), rows)
        self.assertLess(labels.index(sketch.Label), labels.index(pad1.Label))
        self.assertLess(labels.index(sketch.Label), labels.index(pad2.Label))

    def tintedRows(self):
        """Labels of the rows painted with the lineage tint, read from the pixels at the far
        right of each row, where only the row tint paints."""
        tree = self.tree()
        tree.viewport().repaint()
        image = tree.viewport().grab().toImage()
        x = image.width() - 3
        plain = tree.palette().color(QtGui.QPalette.Base).rgb()
        rows = []
        root = tree.invisibleRootItem()
        stack = [root.child(i) for i in range(root.childCount())]
        while stack:
            item = stack.pop()
            rect = tree.visualItemRect(item)
            if rect.isValid() and not item.isHidden() and not item.isSelected():
                if image.pixel(x, rect.center().y()) != plain:
                    rows.append(item.text(0))
            stack.extend(item.child(i) for i in range(item.childCount()))
        return sorted(rows)

    def rowTint(self, label):
        """The colour at the far right of @p label's row, where only the row tint paints."""
        tree = self.tree()
        tree.viewport().repaint()
        image = tree.viewport().grab().toImage()
        item = tree.findItems(label, QtCore.Qt.MatchExactly | QtCore.Qt.MatchRecursive)[0]
        return image.pixel(image.width() - 3, tree.visualItemRect(item).center().y())

    def rowCell(self, label):
        """The tree and the index of @p label's Bodies cell, scrolled into view."""
        tree = self.tree()
        item = tree.findItems(label, QtCore.Qt.MatchExactly | QtCore.Qt.MatchRecursive, 0)[0]
        tree.scrollToItem(item)
        return tree, tree.indexFromItem(item, 3)

    def selectionsAcross(self, label):
        """Click along the middle of @p label's Bodies cell, one pixel at a time, and return each
        distinct selection a click made, as sorted names, in the order first seen."""
        tree, index = self.rowCell(label)
        cell = tree.visualRect(index)
        seen = []
        for x in range(cell.left(), cell.right()):
            Gui.Selection.clearSelection()
            click(tree.viewport(), QtCore.QPoint(x, cell.center().y()))
            names = sorted(o.Name for o in Gui.Selection.getSelection())
            if names not in seen:
                seen.append(names)
        Gui.Selection.clearSelection()
        return seen

    def testClickingASwatchSelectsItsBody(self):
        # The swatch is the handle for its body: a click on it selects the body; a click past
        # it in the same cell does not.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        seen = self.selectionsAcross(pad.Label)
        self.assertIn([body.Name], seen)
        self.assertEqual([names for names in seen if body.Name in names], [[body.Name]])

    def testSelectingABodyKeepsItsRowOutOfTheTree(self):
        # A body has no row (§8.1). Selecting it must not reveal one, or every row under the
        # pointer shifts.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        before = self.rootRows()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body.Name)
        self.assertEqual(self.rootRows(), before)
        Gui.Selection.clearSelection()

    def testClickingEmptyTreeSpaceClearsASelectedBody(self):
        # A body picked by its swatch has no row; a click below the rows still lets it go,
        # and its family's tint with it.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body.Name)
        self.assertNotEqual(self.tintedRows(), [])
        tree = self.tree()
        empty = QtCore.QPoint(tree.viewport().width() // 2, tree.viewport().height() - 3)
        self.assertIsNone(tree.itemAt(empty))
        click(tree.viewport(), empty)
        self.settle()
        self.assertEqual(Gui.Selection.getSelection(), [])
        self.assertEqual(self.tintedRows(), [])

    def testClickingAPatternCountSelectsEveryCopy(self):
        # Each of the three squares drawn is one body; the count after them stands for them all.
        _, pattern, bodies = self.pattern(5)
        seen = [names for names in self.selectionsAcross(pattern.Label) if names != [pattern.Name]]
        singles = [names for names in seen if len(names) == 1]
        self.assertEqual(len(singles), 3, seen)
        self.assertIn(sorted(b.Name for b in bodies[1:]), seen)

    def swatchPoint(self, label, body):
        """A point in @p label's Bodies cell where a click selects @p body alone."""
        tree, index = self.rowCell(label)
        cell = tree.visualRect(index)
        for x in range(cell.left(), cell.right()):
            Gui.Selection.clearSelection()
            point = QtCore.QPoint(x, cell.center().y())
            click(tree.viewport(), point)
            if [o.Name for o in Gui.Selection.getSelection()] == [body.Name]:
                Gui.Selection.clearSelection()
                return tree, point
        self.fail("no swatch for %s in %s's Bodies cell" % (body.Label, label))

    def swatchMenu(self, label, body, choose=None, answer=None):
        """Right-click @p body's swatch on @p label's row and return the menu as
        [(text, [submenu texts])]. With @p choose, trigger that entry (a "Used by/<name>" path
        reaches into the submenu); a rename dialog it opens is answered with @p answer."""
        tree, point = self.swatchPoint(label, body)
        viewport = tree.viewport()
        QApplication.sendEvent(
            viewport,
            QtGui.QMouseEvent(
                QtCore.QEvent.MouseButtonPress,
                QtCore.QPointF(point),
                QtCore.QPointF(viewport.mapToGlobal(point)),
                QtCore.Qt.RightButton,
                QtCore.Qt.RightButton,
                QtCore.Qt.NoModifier,
            ),
        )
        self.selectedOnPress = [o.Name for o in Gui.Selection.getSelection()]
        seen = []

        def answerDialog():
            dialog = QApplication.activeModalWidget()
            if isinstance(dialog, QtGui.QInputDialog):
                dialog.setTextValue(answer)
                dialog.accept()

        def readMenu():
            menu = QApplication.activePopupWidget()
            target = None
            for action in menu.actions():
                if action.isSeparator():
                    continue
                sub = [a.text() for a in action.menu().actions()] if action.menu() else []
                seen.append((action.text(), sub))
                if choose == action.text():
                    target = action
                elif choose and action.menu() and choose.startswith(action.text() + "/"):
                    name = choose.split("/", 1)[1]
                    target = [a for a in action.menu().actions() if a.text() == name][0]
            menu.close()
            if target:
                if answer is not None:
                    QtCore.QTimer.singleShot(0, answerDialog)
                target.trigger()

        QtCore.QTimer.singleShot(0, readMenu)
        QApplication.sendEvent(
            viewport,
            QtGui.QContextMenuEvent(
                QtGui.QContextMenuEvent.Mouse, point, viewport.mapToGlobal(point)
            ),
        )
        return seen

    def testRightClickingASwatchSelectsItsBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        self.swatchMenu(pad.Label, body)
        self.assertEqual(self.selectedOnPress, [body.Name])

    def testSwatchMenuOffersRenameAndHide(self):
        # Nothing in another body uses this body, so the menu has no Used by entry.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        self.assertEqual(self.swatchMenu(pad.Label, body), [("Rename...", []), ("Hide", [])])

    def testSwatchMenuHidesAndShowsTheBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        self.swatchMenu(pad.Label, body, choose="Hide")
        self.assertFalse(body.Visibility)
        self.assertEqual(self.swatchMenu(pad.Label, body, choose="Show")[1], ("Show", []))
        self.assertTrue(body.Visibility)

    def testSwatchMenuRenamesTheBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        self.swatchMenu(pad.Label, body, choose="Rename...", answer="Bracket")
        self.assertEqual(body.Label, "Bracket")
        self.Doc.undo()
        self.assertNotEqual(body.Label, "Bracket")

    def testSwatchMenuListsTheStepsThatUseTheBody(self):
        # The steps listed are the ones whose own swatch brackets this body; choosing one
        # selects it.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        s2 = self.square("S2", 50)
        s2.AttachmentSupport = (pad, [""])
        s2.MapMode = "ObjectXY"
        self.Doc.recompute()
        menu = self.swatchMenu(pad.Label, body, choose="Used by/" + s2.Label)
        self.assertIn(("Used by", [s2.Label]), menu)
        self.assertEqual([o.Name for o in Gui.Selection.getSelection()], [s2.Name])
        Gui.Selection.clearSelection()

    def testDoubleClickingASwatchDoesNotEditTheStep(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        self.Doc.recompute()
        tree, index = self.rowCell(pad.Label)
        cell = tree.visualRect(index)
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        for x in range(cell.left(), cell.right()):
            Gui.Selection.clearSelection()
            point = QtCore.QPoint(x, cell.center().y())
            click(tree.viewport(), point)
            if [o.Name for o in Gui.Selection.getSelection()] == [body.Name]:
                click(tree.viewport(), point, QtCore.QEvent.MouseButtonDblClick)
                break
        else:
            self.fail("no swatch found in the Bodies cell")
        self.assertIsNone(Gui.ActiveDocument.getInEdit())
        Gui.Selection.clearSelection()

    def bodyPanel(self):
        """The Body panel's dock, or None when it was never opened."""
        return Gui.getMainWindow().findChild(QtGui.QDockWidget, "Body")

    def openBodyPanel(self, label, body):
        tree, point = self.swatchPoint(label, body)
        click(tree.viewport(), point)
        click(tree.viewport(), point, QtCore.QEvent.MouseButtonDblClick)
        dock = self.bodyPanel()
        self.assertIsNotNone(dock)
        self.assertTrue(dock.isVisible())
        return dock

    def settle(self):
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(20, loop.quit)
        loop.exec_()

    def tearDownPanel(self):
        dock = self.bodyPanel()
        if dock:
            dock.hide()

    def testDoubleClickingASwatchOpensItsBodyPanel(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        dock = self.openBodyPanel(pad.Label, body)
        self.assertEqual(dock.windowTitle(), "Body \u2014 " + body.Label)
        name = dock.findChild(QtGui.QLineEdit)
        self.assertEqual(name.text(), body.Label)
        self.tearDownPanel()

    def testBodyPanelListsTheStepsThatUseTheBody(self):
        # A pad in a second body that reaches up to a face of the first uses the first.
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        user = PartDesign.makeFeature(self.square("S2", 50), "Pad")
        self.Doc.recompute()
        user.Type = "UpToFace"
        user.UpToFace = (pad, ["Face6"])
        self.Doc.recompute()
        dock = self.openBodyPanel(pad.Label, body)
        usedBy = dock.findChild(QtGui.QTreeWidget)
        entries = [usedBy.topLevelItem(i) for i in range(usedBy.topLevelItemCount())]
        self.assertEqual([e.text(0) for e in entries], [user.Label])
        # An entry is a link: clicking it selects that step, and the panel stays on the body
        # rather than following the step to the body it builds.
        usedBy.itemClicked.emit(entries[0], 0)
        self.assertEqual([o.Name for o in Gui.Selection.getSelection()], [user.Name])
        self.settle()
        self.assertEqual(dock.windowTitle(), "Body \u2014 " + body.Label)
        Gui.Selection.clearSelection()
        self.tearDownPanel()

    def testBodyPanelRenamesAndHidesTheBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        dock = self.openBodyPanel(pad.Label, body)
        name = dock.findChild(QtGui.QLineEdit)
        name.setText("Bracket")
        name.editingFinished.emit()
        self.assertEqual(body.Label, "Bracket")
        shown = dock.findChild(QtGui.QCheckBox)
        self.assertTrue(shown.isChecked())
        shown.setChecked(False)
        self.assertFalse(body.Visibility)
        # Live: a change made elsewhere shows in the panel.
        body.Visibility = True
        body.Label = "Plate"
        self.settle()
        self.assertTrue(shown.isChecked())
        self.assertEqual(name.text(), "Plate")
        self.tearDownPanel()

    def testBodyPanelRecoloursTheBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        self.Doc.recompute()
        dock = self.openBodyPanel(pad.Label, body)
        colour = dock.findChild(QtGui.QPushButton, "BodyColour")
        before = body.Color
        colour.setProperty("color", "#ff0000")
        QtCore.QMetaObject.invokeMethod(colour, "changed")
        self.assertEqual(body.Color[:3], (1.0, 0.0, 0.0))
        # One undo step puts the old colour back, and the panel shows it.
        self.Doc.undo()
        self.settle()
        self.assertEqual(body.Color, before)
        shown = colour.property("color")
        self.assertAlmostEqual(shown.redF(), before[0], places=2)
        self.tearDownPanel()

    def testBodyPanelFollowsTheSelectedStepsBody(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        first = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        pad2 = PartDesign.makeFeature(self.square("S2", 100), "Pad")
        self.Doc.recompute()
        second = [
            o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o != first
        ][0]
        dock = self.openBodyPanel(pad.Label, first)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, pad2.Name)
        self.settle()
        self.assertEqual(dock.windowTitle(), "Body \u2014 " + second.Label)
        Gui.Selection.clearSelection()
        self.tearDownPanel()

    def testSelectingAStepTintsWhatItCameFrom(self):
        # §8.2: selecting a step tints the steps it came from, and nothing that merely shares
        # an input with it.
        sketch = self.square("S1", 0)
        pad1 = PartDesign.makeFeature(sketch, "Pad")
        pad2 = PartDesign.makeFeature(sketch, "Pad")
        other = self.square("S2", 50)
        PartDesign.makeFeature(other, "Pad")
        self.Doc.recompute()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, pad2.Name)
        self.assertEqual(self.tintedRows(), [sketch.Label])
        # A face picked in 3D arrives through the body; the lineage is the same.
        body = [
            o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip == pad1
        ]
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body[0].Name, pad1.Name + ".Face6")
        self.assertEqual(self.tintedRows(), [sketch.Label])
        Gui.Selection.clearSelection()
        self.assertEqual(self.tintedRows(), [])

    def testAStepOfAnotherBodyIsTintedInTheSelectedStepsColour(self):
        # Selecting a step that reaches into another body: that body's step shows, in the
        # selected step's colour, and what went into it does not.
        first = self.square("S1", 0)
        pad = PartDesign.makeFeature(first, "Pad")
        second = self.square("S2", 50)
        user = PartDesign.makeFeature(second, "Pad")
        self.Doc.recompute()
        user.Type = "UpToFace"
        user.UpToFace = (pad, ["Face6"])
        self.Doc.recompute()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, user.Name)
        self.assertEqual(self.tintedRows(), sorted([second.Label, pad.Label]))
        self.assertEqual(self.rowTint(pad.Label), self.rowTint(second.Label))
        Gui.Selection.clearSelection()

    def testSelectingABodyTintsOnlyTheStepsThatMadeIt(self):
        # A body's swatch asks which steps made that body: the step of another body it was
        # built on is one, in this body's colour; what went into that step is not.
        first = self.square("S1", 0)
        pad = PartDesign.makeFeature(first, "Pad")
        second = self.square("S2", 50)
        user = PartDesign.makeFeature(second, "Pad")
        self.Doc.recompute()
        user.Type = "UpToFace"
        user.UpToFace = (pad, ["Face6"])
        self.Doc.recompute()
        body = [
            o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip == user
        ]
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body[0].Name)
        self.assertEqual(self.tintedRows(), sorted([second.Label, user.Label, pad.Label]))
        self.assertEqual(self.rowTint(pad.Label), self.rowTint(user.Label))
        Gui.Selection.clearSelection()

    def testTheSelectedStepIsTintedLikeItsFamily(self):
        # The body marker shows the tip, so the selected row needs no stronger tint than its
        # sketch's, however it was selected.
        sketch = self.square("S1", 0)
        pad = PartDesign.makeFeature(sketch, "Pad")
        self.Doc.recompute()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, pad.Name)
        for item in self.tree().selectedItems():
            item.setSelected(False)
        self.assertEqual(self.rowTint(pad.Label), self.rowTint(sketch.Label))
        Gui.Selection.clearSelection()

    def testTheOriginIsNotTinted(self):
        # Every step comes from the origin, so tinting it says nothing.
        sketch = self.square("S1", 0)
        origin = [o for o in self.Doc.Objects if o.isDerivedFrom("App::Origin")][0]
        xy = [f for f in origin.OriginFeatures if f.Role == "XY_Plane"][0]
        sketch.AttachmentSupport = (xy, [""])
        sketch.MapMode = "FlatFace"
        pad = PartDesign.makeFeature(sketch, "Pad")
        self.Doc.recompute()
        tree = self.tree()
        tree.expandAll()
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, pad.Name)
        self.assertEqual(self.tintedRows(), [sketch.Label])
        Gui.Selection.clearSelection()

    def testFacePickedThroughABodyHighlightsTheStep(self):
        pad = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        self.Doc.recompute()
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.Doc.Name, body.Name, pad.Name + ".Face6")
        selected = [i.text(0) for i in self.tree().selectedItems()]
        Gui.Selection.clearSelection()
        self.assertEqual(selected, [pad.Label])


class TestDeleteInABody(unittest.TestCase):
    """A Body is a marker its steps produce, so Delete does not remove it; a body goes when its
    steps go, and deleting a step another body leans on names that body's steps first."""

    def setUp(self):
        self.Doc = App.newDocument("DeleteInABody", type="Part")
        Gui.activateWorkbench("PartDesignWorkbench")
        self.dialogs = []
        self.answer = QtGui.QMessageBox.No
        self.watcher = QtCore.QTimer()
        self.watcher.timeout.connect(self.closeDialog)
        self.watcher.start(50)

    def tearDown(self):
        self.watcher.stop()
        App.closeDocument(self.Doc.Name)

    def closeDialog(self):
        box = QApplication.activeModalWidget()
        if isinstance(box, QtGui.QMessageBox):
            self.dialogs.append(box.text())
            button = box.button(self.answer)
            (button or box.defaultButton() or box.buttons()[0]).click()

    def square(self, name):
        sketch = self.Doc.addObject("Sketcher::SketchObject", name)
        pts = [(0, 0), (10, 0), (10, 10), (0, 10)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            sketch.addGeometry(Part.LineSegment(App.Vector(*a, 0), App.Vector(*b, 0)))
        self.Doc.recompute()
        return sketch

    def twoBodiesOneOnTheOther(self):
        base = PartDesign.makeFeature(self.square("S1"), "Pad")
        self.Doc.recompute()
        top = self.square("S2")
        top.AttachmentSupport = [(base, "Face6")]
        top.MapMode = "FlatFace"
        PartDesign.makeFeature(top, "Pad")
        self.Doc.recompute()
        return base

    def delete(self, obj):
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(obj)
        Gui.runCommand("Std_Delete")
        Gui.Selection.clearSelection()

    def testDeleteLeavesABodyAndItsSteps(self):
        base = self.twoBodiesOneOnTheOther()
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        before = sorted(o.Uid for o in self.Doc.Objects)
        self.delete(body)
        self.assertEqual(self.dialogs, [])
        self.assertEqual(sorted(o.Uid for o in self.Doc.Objects), before)
        self.assertTrue(base.isValid())

    def testDeletingAStepNamesTheStepsThatLeanOnIt(self):
        base = self.twoBodiesOneOnTheOther()
        before = sorted(o.Uid for o in self.Doc.Objects)
        self.delete(base)
        self.assertEqual(len(self.dialogs), 1)
        self.assertIn("S2", self.dialogs[0])
        self.assertEqual(sorted(o.Uid for o in self.Doc.Objects), before)

    def testConfirmingTheWarningStillLeavesABody(self):
        base = self.twoBodiesOneOnTheOther()
        body = [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        origin = [o for o in self.Doc.Objects if o.TypeId == "App::Origin"][0]
        bodyUid, originUid = body.Uid, origin.Uid
        self.answer = QtGui.QMessageBox.Yes
        Gui.Selection.clearSelection()
        for obj in (body, base, origin):
            Gui.Selection.addSelection(obj)
        Gui.runCommand("Std_Delete")
        Gui.Selection.clearSelection()
        self.assertEqual(len(self.dialogs), 1)
        uids = [o.Uid for o in self.Doc.Objects]
        self.assertNotIn(base.Uid, uids)
        self.assertIn(bodyUid, uids)
        self.assertIn(originUid, uids)


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
        datum = body.Document.addObject("Part::DatumPlane", "DatumPlane")
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


class TestNoEffectNotice(unittest.TestCase):
    """Cruth #39: a step that changes nothing carries a notice on its row until accepted."""

    def setUp(self):
        import TestSketcherApp

        self.Doc = App.newDocument("NoEffectNotice", type="Part")
        block = self.Doc.addObject("Sketcher::SketchObject", "BlockSketch")
        TestSketcherApp.CreateRectangleSketch(block, (0, 0), (100, 100))
        self.Pad = PartDesign.makeFeature(block, "Pad")
        self.Doc.recompute()
        missing = self.Doc.addObject("Sketcher::SketchObject", "PocketSketch")
        TestSketcherApp.CreateRectangleSketch(missing, (200, 200), (10, 10))
        missing.AttachmentSupport = (self.Pad, ["Face6"])
        missing.MapMode = "FlatFace"
        self.Pocket = PartDesign.makeFeature(missing, "Pocket")
        self.Doc.recompute()
        Gui.updateGui()

    def tearDown(self):
        App.closeDocument(self.Doc.Name)

    def rowToolTip(self, obj):
        """The hover text of obj's timeline row, waiting for the tree's deferred update."""
        import time
        from PySide import QtWidgets

        deadline = time.monotonic() + 2
        seen = []
        while time.monotonic() < deadline:
            QApplication.processEvents()
            seen = []
            for tree in Gui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
                tree.expandAll()
                rows = QtWidgets.QTreeWidgetItemIterator(tree)
                while rows.value():
                    row = rows.value()
                    if row.text(0) == obj.Label:
                        return row.toolTip(0)
                    seen.append(row.text(0))
                    rows += 1
        self.fail(f"no tree row for {obj.Label}: {seen}")

    def testTheRowSaysTheStepChangesNothing(self):
        self.assertTrue(self.Pocket.hasNoEffect())
        self.assertIn("changes nothing", self.rowToolTip(self.Pocket))
        self.assertEqual(self.rowToolTip(self.Pad), "")

    def testAcceptingClearsTheNotice(self):
        self.Pocket.NoEffectAcknowledged = True
        self.Doc.recompute()
        Gui.updateGui()
        self.assertEqual(self.rowToolTip(self.Pocket), "")

    def testMovingTheSketchOntoTheBodyClearsTheNotice(self):
        self.Pocket.Profile[0].AttachmentOffset = App.Placement(
            App.Vector(-190, -190, 0), App.Rotation()
        )
        self.Doc.recompute()
        Gui.updateGui()
        self.assertFalse(self.Pocket.hasNoEffect())
        self.assertEqual(self.rowToolTip(self.Pocket), "")


class TestSelectingAStepShowsItsSolid(unittest.TestCase):
    """A body draws its steps' solids under its own node; selecting a step by itself, as the tree
    does, still lights that solid in the 3D view."""

    def setUp(self):
        self.Doc = App.newDocument("SelectionIn3D", type="Part")
        self.First = PartDesign.makeFeature(self.square("S1", 0), "Pad")
        self.Second = PartDesign.makeFeature(self.square("S2", 30), "Pad")
        self.Doc.recompute()
        self.View = Gui.ActiveDocument.ActiveView
        self.View.viewIsometric()
        self.View.fitAll()
        self.Folder = tempfile.mkdtemp()

    def tearDown(self):
        Gui.Selection.clearSelection()
        App.closeDocument(self.Doc.Name)
        shutil.rmtree(self.Folder, ignore_errors=True)

    def square(self, name, x):
        sketch = self.Doc.addObject("Sketcher::SketchObject", name)
        sketch.Placement.Base = App.Vector(x, 0, 0)
        pts = [(0, 0), (10, 0), (10, 10), (0, 10)]
        for i in range(4):
            a, b = pts[i], pts[(i + 1) % 4]
            sketch.addGeometry(Part.LineSegment(App.Vector(*a, 0), App.Vector(*b, 0)))
        return sketch

    def picture(self, name):
        path = os.path.join(self.Folder, name + ".png")
        self.View.saveImage(path, 200, 150, "White")
        return QtGui.QImage(path)

    def changedPixels(self, before, after):
        return sum(
            before.pixel(x, y) != after.pixel(x, y)
            for x in range(0, before.width(), 2)
            for y in range(0, before.height(), 2)
        )

    def testEachStepLightsUpWhenSelected(self):
        # The navigation cube is missing from the view's first picture; compare later ones only.
        self.picture("first")
        plain = self.picture("plain")
        for step in (self.First, self.Second):
            Gui.Selection.clearSelection()
            Gui.Selection.addSelection(self.Doc.Name, step.Name)
            self.assertGreater(self.changedPixels(plain, self.picture(step.Name)), 100, step.Name)
        Gui.Selection.clearSelection()
        self.assertEqual(self.changedPixels(plain, self.picture("cleared")), 0)
