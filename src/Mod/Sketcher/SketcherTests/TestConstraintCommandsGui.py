# SPDX-License-Identifier: LGPL-2.1-or-later

"""Each constraint command, run selection-first and tool-first on the same picks, makes the same,
expected constraint. Tool-first clicks in the 3D view, so the command's step-by-step path runs as it
does for a person; each pick is highlighted the way hovering it in the tree does, because the
offscreen view cannot pick by itself."""

import math
import os
import unittest

import FreeCAD
import Part
import Sketcher

try:
    import FreeCADGui

    GUI_AVAILABLE = FreeCADGui.getMainWindow() is not None
except (ImportError, AttributeError):
    GUI_AVAILABLE = False

from PySide import QtCore, QtGui

V = FreeCAD.Vector
PREFS = "User parameter:BaseApp/Preferences/Mod/Sketcher"

START, END = 1, 2


def addGeometry(sketch):
    sketch.addGeometry(Part.LineSegment(V(0, 0, 0), V(10, 2, 0)))  # 0
    sketch.addGeometry(Part.LineSegment(V(0, 8, 0), V(9, 11, 0)))  # 1
    sketch.addGeometry(Part.LineSegment(V(22, 0, 0), V(24, 10, 0)))  # 2
    sketch.addGeometry(Part.Circle(V(40, 0, 0), V(0, 0, 1), 5))  # 3
    sketch.addGeometry(Part.ArcOfCircle(Part.Circle(V(60, 0, 0), V(0, 0, 1), 4), 0.2, 1.4))  # 4


@unittest.skipIf(not GUI_AVAILABLE, "GUI not available")
class TestConstraintCommandsGui(unittest.TestCase):
    def setUp(self):
        FreeCADGui.activateWorkbench("SketcherWorkbench")
        self.prefs = FreeCAD.ParamGet(PREFS)
        self.showDialog = self.prefs.GetBool("ShowDialogOnDistanceConstraint", True)
        self.prefs.SetBool("ShowDialogOnDistanceConstraint", False)
        self.doc = FreeCAD.newDocument("TestConstraintCommandsGui")
        self.sketch = None

    def tearDown(self):
        self.prefs.SetBool("ShowDialogOnDistanceConstraint", self.showDialog)
        if FreeCADGui.ActiveDocument is not None:
            FreeCADGui.ActiveDocument.resetEdit()
        FreeCADGui.Selection.clearSelection()
        if self.doc.Name in FreeCAD.listDocuments():
            FreeCAD.closeDocument(self.doc.Name)

    def pump(self, ms=50):
        loop = QtCore.QEventLoop()
        QtCore.QTimer.singleShot(ms, loop.quit)
        loop.exec_()

    def freshSketch(self):
        if self.sketch is not None:
            FreeCADGui.ActiveDocument.resetEdit()
            self.pump(50)
            self.doc.removeObject(self.sketch.Name)
        FreeCADGui.Selection.clearSelection()
        self.sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        addGeometry(self.sketch)
        self.doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(self.sketch.Name)
        self.pump(150)
        view = FreeCADGui.ActiveDocument.ActiveView
        view.viewTop()
        view.fitAll()
        self.pump(100)

    # --- naming picks -------------------------------------------------------------------------

    def subName(self, pick):
        kind, geoId = pick[0], pick[1]
        if kind == "edge":
            return f"Edge{geoId + 1}"
        for i in range(32):
            if self.sketch.getGeoVertexIndex(i) == (geoId, pick[2]):
                return f"Vertex{i + 1}"
        raise AssertionError(f"no vertex for {pick}")

    def pickPoint(self, pick):
        kind, geoId = pick[0], pick[1]
        if kind == "vertex":
            return self.sketch.getPoint(geoId, pick[2])
        geo = self.sketch.Geometry[geoId]
        if isinstance(geo, Part.LineSegment):
            return (geo.StartPoint + geo.EndPoint) * 0.5
        if isinstance(geo, Part.ArcOfCircle):
            return geo.value((geo.FirstParameter + geo.LastParameter) / 2)
        if isinstance(geo, Part.Circle):
            return geo.value(math.pi / 3)
        raise AssertionError(type(geo))

    # --- running a command --------------------------------------------------------------------

    def newConstraints(self, before):
        return [
            (c.Type, c.First, c.FirstPos, c.Second, c.SecondPos, round(c.Value, 6))
            for c in self.sketch.Constraints[before:]
        ]

    def selectionFirst(self, command, picks):
        self.freshSketch()
        before = len(self.sketch.Constraints)
        for pick in picks:
            FreeCADGui.Selection.addSelection(self.doc.Name, self.sketch.Name, self.subName(pick))
        FreeCADGui.runCommand(command)
        self.pump(100)
        self.doc.recompute()
        return self.newConstraints(before)

    @property
    def view(self):
        return FreeCADGui.ActiveDocument.ActiveView

    @property
    def viewport(self):
        return self.view.graphicsView().viewport()

    def mouse(self, kind, point, button, buttons):
        event = QtGui.QMouseEvent(
            kind, point, self.viewport.mapToGlobal(point), button, buttons, QtCore.Qt.NoModifier
        )
        QtGui.QApplication.sendEvent(self.viewport, event)

    def click(self, pick):
        modelPoint = self.pickPoint(pick)
        point = QtCore.QPoint(*self.view.getPointOnScreen(modelPoint))
        FreeCADGui.Selection.setPreselection(self.sketch, self.subName(pick))
        self.pump(60)
        self.mouse(
            QtCore.QEvent.MouseButtonPress, point, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton
        )
        self.mouse(
            QtCore.QEvent.MouseButtonRelease, point, QtCore.Qt.LeftButton, QtCore.Qt.NoButton
        )
        self.pump(100)

    def toolFirst(self, command, picks):
        self.freshSketch()
        before = len(self.sketch.Constraints)
        FreeCADGui.runCommand(command)
        self.pump(150)
        for pick in picks:
            self.click(pick)
        self.doc.recompute()
        return self.newConstraints(before)

    def assertBothWays(self, command, picks, expected):
        byTool = self.toolFirst(command, picks)
        bySelection = self.selectionFirst(command, picks)
        record = os.environ.get("SKETCHER_CONSTRAINT_TESTS_RECORD")
        if record:
            with open(record, "a") as out:
                out.write(f"{self._testMethodName}\n  tool {byTool}\n  sel  {bySelection}\n")
            return
        self.assertEqual(byTool, expected, f"{command} tool-first")
        self.assertEqual(bySelection, expected, f"{command} selection-first")

    def assertCoordinateOnBlockedPointIsReference(self, command):
        self.freshSketch()
        self.sketch.addConstraint(Sketcher.Constraint("Block", L0))
        self.doc.recompute()
        before = len(self.sketch.Constraints)
        FreeCADGui.Selection.addSelection(
            self.doc.Name, self.sketch.Name, self.subName(("vertex", L0, END))
        )
        FreeCADGui.runCommand(command)
        self.pump(100)
        self.doc.recompute()
        added = self.sketch.Constraints[before:]
        self.assertEqual(len(added), 1, command)
        self.assertFalse(added[0].Driving, command)

    def test_fixedXOnABlockedPointIsAReference(self):
        self.assertCoordinateOnBlockedPointIsReference("Sketcher_ConstrainDistanceX")

    def test_fixedYOnABlockedPointIsAReference(self):
        self.assertCoordinateOnBlockedPointIsReference("Sketcher_ConstrainDistanceY")


L0, L1, L2, CIRCLE, ARC = 0, 1, 2, 3, 4

# name: (command, picks in tool-first order,
#        expected constraints as (Type, First, FirstPos, Second, SecondPos, Value))
CASES = {
    "horizontalOnALine": (
        "Sketcher_ConstrainHorizontal",
        [("edge", L0)],
        [("Horizontal", 0, 0, -2000, 0, 0.0)],
    ),
    "horizontalBetweenTwoPoints": (
        "Sketcher_ConstrainHorizontal",
        [("vertex", L0, END), ("vertex", L2, START)],
        [("Horizontal", 0, 2, 2, 1, 0.0)],
    ),
    "verticalOnALine": (
        "Sketcher_ConstrainVertical",
        [("edge", L2)],
        [("Vertical", 2, 0, -2000, 0, 0.0)],
    ),
    "verticalBetweenTwoPoints": (
        "Sketcher_ConstrainVertical",
        [("vertex", L0, START), ("vertex", L1, START)],
        [("Vertical", 0, 1, 1, 1, 0.0)],
    ),
    "horizontalOrVerticalOnALine": (
        "Sketcher_ConstrainHorVer",
        [("edge", L0)],
        [("Horizontal", 0, 0, -2000, 0, 0.0)],
    ),
    "horizontalOrVerticalOnASteepLine": (
        "Sketcher_ConstrainHorVer",
        [("edge", L2)],
        [("Vertical", 2, 0, -2000, 0, 0.0)],
    ),
    "lockAPoint": (
        "Sketcher_ConstrainLock",
        [("vertex", L0, END)],
        [("DistanceX", 0, 2, -2000, 0, 10.0), ("DistanceY", 0, 2, -2000, 0, 2.0)],
    ),
    "blockALine": ("Sketcher_ConstrainBlock", [("edge", L0)], [("Block", 0, 0, -2000, 0, 0.0)]),
    "coincidentTwoPoints": (
        "Sketcher_ConstrainCoincident",
        [("vertex", L0, END), ("vertex", L1, START)],
        [("Coincident", 0, 2, 1, 1, 0.0)],
    ),
    "coincidentUnifiedTwoPoints": (
        "Sketcher_ConstrainCoincidentUnified",
        [("vertex", L0, END), ("vertex", L1, START)],
        [("Coincident", 0, 2, 1, 1, 0.0)],
    ),
    "coincidentUnifiedPointOnALine": (
        "Sketcher_ConstrainCoincidentUnified",
        [("vertex", L1, START), ("edge", L2)],
        [("PointOnObject", 1, 1, 2, 0, 0.0)],
    ),
    "pointOnObjectPointOnALine": (
        "Sketcher_ConstrainPointOnObject",
        [("vertex", L1, START), ("edge", L2)],
        [("PointOnObject", 1, 1, 2, 0, 0.0)],
    ),
    "pointOnObjectPointOnACircle": (
        "Sketcher_ConstrainPointOnObject",
        [("vertex", L0, END), ("edge", CIRCLE)],
        [("PointOnObject", 0, 2, 3, 0, 0.0)],
    ),
    "distanceLengthOfALine": (
        "Sketcher_ConstrainDistance",
        [("edge", L0)],
        [("Distance", 0, 0, -2000, 0, 10.198039)],
    ),
    "distanceBetweenTwoPoints": (
        "Sketcher_ConstrainDistance",
        [("vertex", L0, START), ("vertex", L1, END)],
        [("Distance", 0, 1, 1, 2, 14.21267)],
    ),
    "distanceFromAPointToALine": (
        "Sketcher_ConstrainDistance",
        [("vertex", L1, START), ("edge", L2)],
        [("Distance", 1, 1, 2, 0, 23.141704)],
    ),
    "horizontalDistanceOnALine": (
        "Sketcher_ConstrainDistanceX",
        [("edge", L0)],
        [("DistanceX", 0, 1, 0, 2, 10.0)],
    ),
    "horizontalDistanceBetweenTwoPoints": (
        "Sketcher_ConstrainDistanceX",
        [("vertex", L0, END), ("vertex", L2, START)],
        [("DistanceX", 0, 2, 2, 1, 12.0)],
    ),
    "verticalDistanceOnALine": (
        "Sketcher_ConstrainDistanceY",
        [("edge", L2)],
        [("DistanceY", 2, 1, 2, 2, 10.0)],
    ),
    "verticalDistanceBetweenTwoPoints": (
        "Sketcher_ConstrainDistanceY",
        [("vertex", L0, START), ("vertex", L1, END)],
        [("DistanceY", 0, 1, 1, 2, 11.0)],
    ),
    "radiusOfACircle": (
        "Sketcher_ConstrainRadius",
        [("edge", CIRCLE)],
        [("Radius", 3, 0, -2000, 0, 5.0)],
    ),
    "radiusOfAnArc": (
        "Sketcher_ConstrainRadius",
        [("edge", ARC)],
        [("Radius", 4, 0, -2000, 0, 4.0)],
    ),
    "diameterOfACircle": (
        "Sketcher_ConstrainDiameter",
        [("edge", CIRCLE)],
        [("Diameter", 3, 0, -2000, 0, 10.0)],
    ),
    "diameterOfAnArc": (
        "Sketcher_ConstrainDiameter",
        [("edge", ARC)],
        [("Diameter", 4, 0, -2000, 0, 8.0)],
    ),
    "radiusOrDiameterOfACircle": (
        "Sketcher_ConstrainRadiam",
        [("edge", CIRCLE)],
        [("Diameter", 3, 0, -2000, 0, 10.0)],
    ),
    "radiusOrDiameterOfAnArc": (
        "Sketcher_ConstrainRadiam",
        [("edge", ARC)],
        [("Radius", 4, 0, -2000, 0, 4.0)],
    ),
    "angleBetweenTwoLines": (
        "Sketcher_ConstrainAngle",
        [("edge", L0), ("edge", L1)],
        [("Angle", 0, 1, 1, 1, 0.124355)],
    ),
    "parallelTwoLines": (
        "Sketcher_ConstrainParallel",
        [("edge", L0), ("edge", L1)],
        [("Parallel", 0, 0, 1, 0, 0.0)],
    ),
    "perpendicularTwoLines": (
        "Sketcher_ConstrainPerpendicular",
        [("edge", L0), ("edge", L2)],
        [("Perpendicular", 2, 0, 0, 0, 0.0)],
    ),
    "tangentLineAndCircle": (
        "Sketcher_ConstrainTangent",
        [("edge", L2), ("edge", CIRCLE)],
        [("Tangent", 2, 0, 3, 0, 0.0)],
    ),
    "tangentCircleAndArc": (
        "Sketcher_ConstrainTangent",
        [("edge", CIRCLE), ("edge", ARC)],
        [("Tangent", 3, 0, 4, 0, 0.0)],
    ),
    "equalTwoLines": (
        "Sketcher_ConstrainEqual",
        [("edge", L0), ("edge", L1)],
        [("Equal", 0, 0, 1, 0, 0.0)],
    ),
    "equalCircleAndArc": (
        "Sketcher_ConstrainEqual",
        [("edge", CIRCLE), ("edge", ARC)],
        [("Equal", 3, 0, 4, 0, 0.0)],
    ),
    "symmetricTwoPointsAboutALine": (
        "Sketcher_ConstrainSymmetric",
        [("vertex", L1, START), ("edge", L2), ("vertex", L1, END)],
        [("Symmetric", 1, 1, 1, 2, 0.0)],
    ),
}


def addCase(name, command, picks, expected):
    def test(self):
        self.assertBothWays(command, picks, expected)

    test.__name__ = "test_" + name
    setattr(TestConstraintCommandsGui, test.__name__, test)


for _name, (_command, _picks, _expected) in CASES.items():
    addCase(_name, _command, _picks, _expected)
