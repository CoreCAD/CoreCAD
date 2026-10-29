# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #165: the Table Pattern panel. Rows are typed or pasted from a spreadsheet; the list at the top
# chooses between offsets from the original and positions on a plane.

import unittest

import FreeCAD
import FreeCADGui
import PartDesign
from PySide import QtCore, QtGui, QtWidgets
from PartDesignTests.TestBodyEmergence import _square


def _widget(name):
    return next(w for w in QtWidgets.QApplication.allWidgets() if w.objectName() == name)


class TestTablePatternGui(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestTablePatternGui", type="Part")
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
        FreeCADGui.runCommand("PartDesign_TablePattern")
        QtWidgets.QApplication.processEvents()
        self.pattern = self.Doc.getObject("TablePattern")
        self.table = _widget("tableRows")

    def tearDown(self):
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        FreeCAD.closeDocument(self.Doc.Name)

    def _rows(self):
        return [tuple(round(c, 6) for c in row) for row in self.pattern.Rows]

    def testStartsWithOneOffset(self):
        self.assertEqual(self._rows(), [(10, 0, 0)])
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())

    def testAddRowRepeatsTheLastStep(self):
        _widget("buttonAddRow").click()
        _widget("buttonAddRow").click()
        self.assertEqual(self._rows(), [(10, 0, 0), (20, 0, 0), (30, 0, 0)])

    def testTypedCellIsReadInTheDocumentsUnit(self):
        self.table.item(0, 1).setText("15")
        self.assertEqual(self._rows(), [(10, 15, 0)])
        self.table.item(0, 2).setText("2 cm")
        self.assertEqual(self._rows(), [(10, 15, 20)])

    def testCellThatIsNotALengthIsPutBack(self):
        self.table.item(0, 0).setText("rubbish")
        self.assertEqual(self._rows(), [(10, 0, 0)])
        self.assertEqual(self.table.item(0, 0).text(), "10.00 mm")

    def testPastedSpreadsheetCellsFillTheTable(self):
        # A header line and the empty line a spreadsheet leaves at the end add no rows.
        QtWidgets.QApplication.clipboard().setText("X\tY\tZ\n20\t25\t0\n5 mm\t30\t0\n")
        self.table.setCurrentCell(0, 0)
        QtWidgets.QApplication.sendEvent(
            self.table,
            QtGui.QKeyEvent(QtCore.QEvent.KeyPress, QtCore.Qt.Key_V, QtCore.Qt.ControlModifier),
        )
        self.assertEqual(self._rows(), [(20, 25, 0), (5, 30, 0)])

    def testChoosingAPlaneMeasuresOnItAndHidesZ(self):
        combo = _widget("comboMeasuredFrom")
        index = combo.findText("Hole")
        combo.setCurrentIndex(index)
        combo.activated.emit(index)
        self.assertEqual(self.pattern.MeasuredFrom, "Plane")
        self.assertEqual(self.pattern.Plane[0], self.Doc.getObject("Hole"))
        self.assertTrue(self.table.isColumnHidden(2))

        index = combo.findText("The original")
        combo.setCurrentIndex(index)
        combo.activated.emit(index)
        self.assertEqual(self.pattern.MeasuredFrom, "Original")
        self.assertIsNone(self.pattern.Plane)
        self.assertFalse(self.table.isColumnHidden(2))
