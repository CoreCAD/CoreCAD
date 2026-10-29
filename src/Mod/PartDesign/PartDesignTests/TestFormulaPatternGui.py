# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: the Formula Pattern panel. X, Y and Z take formulas of the copy number n the way any
# value box takes a formula.

import unittest

import FreeCAD
import FreeCADGui
import PartDesign
from PySide import QtWidgets
from PartDesignTests.TestBodyEmergence import _square


def _widget(name):
    return next(w for w in QtWidgets.QApplication.allWidgets() if w.objectName() == name)


class TestFormulaPatternGui(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFormulaPatternGui", type="Part")
        pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        pad.Length = 10
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pad)
        self.pocket = PartDesign.makeFeature(
            _square(self.Doc, "Hole", x0=3, y0=3, side=4), "Pocket", body=self.body
        )
        self.pocket.Length = 4
        self.pocket.Reversed = True
        self.Doc.recompute()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(self.pocket)
        FreeCADGui.runCommand("PartDesign_FormulaPattern")
        QtWidgets.QApplication.processEvents()
        self.pattern = self.Doc.getObject("FormulaPattern")

    def tearDown(self):
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        FreeCAD.closeDocument(self.Doc.Name)

    def _accept(self):
        FreeCADGui.Control.activeTaskDialog().accept()
        QtWidgets.QApplication.processEvents()

    def testStartsWithOneCopyAStepAlongX(self):
        self.assertEqual(self.pattern.Originals, [self.pocket])
        self.assertEqual(self.pattern.ExpressionEngine, [("X", "n * 10 mm")])
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())
        self.assertEqual(len(self.pattern.AddSubShape.childShapes()[1].Solids), 2)

    def testOccurrencesAddCopies(self):
        _widget("spinOccurrences").setValue(3)
        self.assertEqual(self.pattern.Occurrences, 3)
        self.assertEqual(len(self.pattern.AddSubShape.childShapes()[1].Solids), 3)

    def testAValueTypedWithoutAFormulaIsKeptOnOK(self):
        _widget("spinY").setProperty("rawValue", 15)
        self._accept()
        self.assertAlmostEqual(self.pattern.Y.Value, 15)
        self.assertEqual(self.pattern.ExpressionEngine, [("X", "n * 10 mm")])
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())

    def testReopenedPanelShowsTheFirstCopysOffset(self):
        self._accept()
        self.pattern.setExpression("X", "n * 7 mm + 3 mm")
        self.Doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(self.pattern)
        QtWidgets.QApplication.processEvents()
        spin = _widget("spinX")
        self.assertEqual(spin.property("expression"), "n * 7 mm + 3 mm")
        self.assertAlmostEqual(spin.property("rawValue"), 10)
