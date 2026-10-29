# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: every pattern's panel lists the measurements that change for each copy and offers the
# copied step's measurements to add.

import unittest

import FreeCAD
import FreeCADGui
import PartDesign
from PySide import QtWidgets
from PartDesignTests.TestBodyEmergence import _square
from PartDesignTests.TestVariedMeasurement import _circle


def _widget(name):
    return next(w for w in QtWidgets.QApplication.allWidgets() if w.objectName() == name)


def _hasWidget(name):
    return any(w.objectName() == name for w in QtWidgets.QApplication.allWidgets())


class TestVariedMeasurementGui(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestVariedMeasurementGui", type="Part")
        pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=60), "Pad")
        pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pad)
        self.sketch = _circle(self.Doc, "Hole", 10, 10, 1)
        self.pocket = PartDesign.makeFeature(self.sketch, "Pocket", body=self.body)
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()
        self.pattern = self.Doc.addObject("PartDesign::LinearPattern", "Linear")
        self.pattern.Originals = [self.pocket]
        self.pattern.Direction = (self.sketch, ["H_Axis"])
        self.pattern.Length = 30
        self.pattern.Occurrences = 3
        self.body.addFeature(self.pattern)
        self.Doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(self.pattern)
        QtWidgets.QApplication.processEvents()

    def tearDown(self):
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        FreeCAD.closeDocument(self.Doc.Name)

    def _choose(self, text):
        combo = _widget("comboVaryMeasurement")
        index = combo.findText(text)
        self.assertGreaterEqual(index, 0, [combo.itemText(i) for i in range(combo.count())])
        combo.activated.emit(index)
        QtWidgets.QApplication.processEvents()

    def testOffersTheCopiedStepsMeasurements(self):
        combo = _widget("comboVaryMeasurement")
        offered = [combo.itemText(i) for i in range(combo.count())]
        self.assertIn("Radius (Hole)", offered)
        self.assertIn("Length (Pocket)", offered)

    def testChoosingOneMakesEachCopyLarger(self):
        self._choose("Radius (Hole)")
        self.assertEqual(
            PartDesign.variedMeasurements(self.pattern),
            [("Hole.Constraints.Radius", "Hole.Constraints.Radius + n * 1 mm")],
        )
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())
        self.assertAlmostEqual(_widget("spinVaried1").property("rawValue"), 2)
        combo = _widget("comboVaryMeasurement")
        self.assertEqual(combo.findText("Radius (Hole)"), -1)

    def testStoppingRemovesTheRow(self):
        self._choose("Radius (Hole)")
        _widget("buttonStopVarying1").click()
        QtWidgets.QApplication.processEvents()
        self.assertEqual(PartDesign.variedMeasurements(self.pattern), [])
        self.assertFalse(_hasWidget("spinVaried1"))

    def testCancelLeavesThePatternAsItWas(self):
        self._choose("Length (Pocket)")
        FreeCADGui.Control.activeTaskDialog().reject()
        QtWidgets.QApplication.processEvents()
        self.Doc.recompute()
        self.assertEqual(PartDesign.variedMeasurements(self.pattern), [])

    def testOKKeepsIt(self):
        self._choose("Length (Pocket)")
        FreeCADGui.Control.activeTaskDialog().accept()
        QtWidgets.QApplication.processEvents()
        self.assertEqual(
            PartDesign.variedMeasurements(self.pattern),
            [("Pocket.Length", "Pocket.Length + n * 1 mm")],
        )
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())
