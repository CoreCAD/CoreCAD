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

    def _paste(self, text):
        QtWidgets.QApplication.clipboard().setText(text)
        QtWidgets.QApplication.sendEvent(
            self.table,
            QtGui.QKeyEvent(QtCore.QEvent.KeyPress, QtCore.Qt.Key_V, QtCore.Qt.ControlModifier),
        )

    def testPastedCellThatIsNotALengthKeepsItsColumn(self):
        self.table.setCurrentCell(0, 0)
        self._paste("20;abc;5\n")
        self.assertEqual(self._rows(), [(20, 0, 5)])

    def testEditsAreKeptWithLivePreviewOff(self):
        _widget("checkBoxUpdateView").setChecked(False)
        self.table.item(0, 1).setText("15")
        _widget("buttonAddRow").click()
        self.assertEqual(self._rows(), [(10, 15, 0), (20, 15, 0)])

    def testChoosingThePickedFaceAgainKeepsIt(self):
        top = next(
            "Face%d" % (i + 1)
            for i, f in enumerate(self.Doc.getObject("Pad").Shape.Faces)
            if abs(f.CenterOfMass.z - 10) < 1e-6
        )
        FreeCADGui.Control.activeTaskDialog().accept()
        QtWidgets.QApplication.processEvents()
        self.pattern.Plane = (self.Doc.getObject("Pad"), [top])
        self.pattern.MeasuredFrom = "Plane"
        FreeCADGui.ActiveDocument.setEdit(self.pattern)
        QtWidgets.QApplication.processEvents()
        combo = _widget("comboMeasuredFrom")
        index = combo.currentIndex()
        self.assertTrue(combo.itemText(index).endswith(top), combo.itemText(index))
        combo.activated.emit(index)
        self.assertEqual(list(self.pattern.Plane[1]), [top])

    def _reopenWithSheet(self, cells):
        FreeCADGui.Control.activeTaskDialog().accept()
        QtWidgets.QApplication.processEvents()
        sheet = self.Doc.addObject("Spreadsheet::Sheet", "Positions")
        for address, content in cells.items():
            sheet.set(address, content)
        self.Doc.recompute()
        FreeCADGui.ActiveDocument.setEdit(self.pattern)
        QtWidgets.QApplication.processEvents()
        self.table = _widget("tableRows")
        return sheet

    def _chooseRowsFrom(self, text):
        combo = _widget("comboRowsFrom")
        index = combo.findText(text)
        combo.setCurrentIndex(index)
        combo.activated.emit(index)

    def testChoosingASpreadsheetReadsItsLengths(self):
        sheet = self._reopenWithSheet({"A1": "X", "B2": "20 mm", "C2": "0 mm", "B3": "5 mm"})
        self.assertFalse(_widget("editCells").isVisibleTo(self.table.parentWidget()))
        self._chooseRowsFrom("Positions")
        self.assertEqual(self.pattern.Spreadsheet, sheet)
        self.assertEqual(self.pattern.Cells, "B2:C3")
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())
        self.assertEqual(self.table.rowCount(), 2)
        self.assertEqual(self.table.item(1, 0).text(), "5.00 mm")
        self.assertFalse(_widget("buttonAddRow").isVisibleTo(self.table.parentWidget()))

    def testEditedCellsAreKeptOnOK(self):
        self._reopenWithSheet({"A1": "20 mm", "A2": "30 mm"})
        self._chooseRowsFrom("Positions")
        edit = _widget("editCells")
        edit.setText("a2")
        edit.editingFinished.emit()
        self.assertEqual(self.table.rowCount(), 1)
        FreeCADGui.Control.activeTaskDialog().accept()
        QtWidgets.QApplication.processEvents()
        self.assertEqual(self.pattern.Cells, "A2")
        self.assertTrue(self.pattern.isValid(), self.pattern.getStatusString())

    def testChoosingTypedRowsAgainUnlinksTheSheet(self):
        self._reopenWithSheet({"A1": "20 mm"})
        self._chooseRowsFrom("Positions")
        self._chooseRowsFrom("Typed rows")
        self.assertIsNone(self.pattern.Spreadsheet)
        self.assertEqual(self._rows(), [(10, 0, 0)])
        self.assertTrue(_widget("buttonAddRow").isVisibleTo(self.table.parentWidget()))


class TestTablePatternCommand(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestTablePatternCommand", type="Part")
        pad = PartDesign.makeFeature(_square(self.Doc, "Plate", side=40), "Pad")
        self.Doc.recompute()
        self.body = PartDesign.findBodyOf(pad)
        self.hole = _square(self.Doc, "Hole", x0=3, y0=3, side=4)
        self.pocket = PartDesign.makeFeature(self.hole, "Pocket", body=self.body)
        self.Doc.recompute()

    def tearDown(self):
        if FreeCADGui.Control.activeDialog():
            FreeCADGui.Control.closeDialog()
        FreeCAD.closeDocument(self.Doc.Name)

    def _run(self, *selected):
        FreeCADGui.Selection.clearSelection()
        for obj in selected:
            FreeCADGui.Selection.addSelection(obj)
        FreeCADGui.runCommand("PartDesign_TablePattern")
        QtWidgets.QApplication.processEvents()
        return self.Doc.getObject("TablePattern")

    def testTheOriginalsOwnSketchIsNotAPlane(self):
        pattern = self._run(self.pocket, self.hole)
        self.assertEqual(pattern.MeasuredFrom, "Original")
        self.assertIsNone(pattern.Plane)

    def testASelectedSketchIsThePlane(self):
        positions = self.Doc.addObject("Sketcher::SketchObject", "Positions")
        self.Doc.recompute()
        pattern = self._run(self.pocket, positions)
        self.assertEqual(pattern.MeasuredFrom, "Plane")
        self.assertEqual(pattern.Plane[0], positions)
