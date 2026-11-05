# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Picking the body in a pattern panel's list adds every step before the pattern as one row.

import unittest

import FreeCAD
import FreeCADGui
import PartDesign
from PySide import QtWidgets
from PartDesignTests.TestBodyEmergence import _square


def _widget(name):
    return next(w for w in QtWidgets.QApplication.allWidgets() if w.objectName() == name)


class TestPatternAllStepsBeforeGui(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestPatternAllStepsBeforeGui", type="Part")
        self.sketch = _square(self.Doc, "S1")
        self.pad = PartDesign.makeFeature(self.sketch, "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(self.pad)
        self.pattern = self.Doc.addObject("PartDesign::LinearPattern", "Linear")
        self.pattern.Direction = (self.sketch, ["H_Axis"])
        self.pattern.Length = 30
        self.pattern.Occurrences = 3
        self.body.addFeature(self.pattern)
        self.Doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(self.pattern)
        QtWidgets.QApplication.processEvents()
        self.list = _widget("listWidgetFeatures")

    def tearDown(self):
        FreeCADGui.Selection.clearSelection()
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        FreeCAD.closeDocument(self.Doc.Name)

    def _pick(self, button, obj):
        _widget(button).setChecked(True)
        FreeCADGui.Selection.addSelection(self.Doc.Name, obj.Name)
        QtWidgets.QApplication.processEvents()

    def _rows(self):
        return [self.list.item(i).text() for i in range(self.list.count())]

    def testPickingTheBodyAddsItsRow(self):
        self._pick("buttonAddFeature", self.body)
        self.assertTrue(self.pattern.AllStepsBefore)
        self.assertEqual(self._rows(), [self.body.Label])
        self.assertEqual(self.pattern.Originals, [])
        self.assertAlmostEqual(self.pattern.Shape.Volume, 3 * 1000)

    def testPickingItAgainInRemoveModeTakesItOut(self):
        self._pick("buttonAddFeature", self.body)
        FreeCADGui.Selection.clearSelection()
        self._pick("buttonRemoveFeature", self.body)
        self.assertFalse(self.pattern.AllStepsBefore)
        self.assertEqual(self._rows(), [])

    def testRemovingTheRowKeepsTheListedSteps(self):
        self._pick("buttonAddFeature", self.pad)
        FreeCADGui.Selection.clearSelection()
        self._pick("buttonAddFeature", self.body)
        self.assertEqual(self._rows(), [self.body.Label, self.pad.Label])
        self.list.setCurrentRow(0)
        self.list.actions()[0].trigger()
        QtWidgets.QApplication.processEvents()
        self.assertFalse(self.pattern.AllStepsBefore)
        self.assertEqual(self.pattern.Originals, [self.pad])
        self.assertEqual(self._rows(), [self.pad.Label])
