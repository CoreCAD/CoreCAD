# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

"""The appearance and material panel (UI_GUIDE §2.5): the selection decides the scope, each change
is one undo step, a material applies to whole parts only, and a script reaches the same code."""

import unittest

import FreeCAD as App
import FreeCADGui as Gui
import Part
import PartDesign
import PartGui
from PySide import QtWidgets

STEEL = "4b849c55-6b3a-4f75-a055-40c0d0324596"
TOP_FACE = 5  # Face6 of a pad on an XY sketch


def red():
    look = App.Material()
    look.DiffuseColor = (1.0, 0.0, 0.0, 0.0)
    return look


def isRed(look):
    return tuple(round(c, 3) for c in look.DiffuseColor[:3]) == (1.0, 0.0, 0.0)


class AppearancePanelTest(unittest.TestCase):

    def setUp(self):
        self.doc = App.newDocument("AppearancePanel", type="Part")
        self.doc.UndoMode = 1
        sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        corners = [
            App.Vector(0, 0, 0),
            App.Vector(20, 0, 0),
            App.Vector(20, 10, 0),
            App.Vector(0, 10, 0),
        ]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
        self.doc.recompute()
        self.pad = PartDesign.makeFeature(sketch, "Pad")
        self.pad.Length = 5
        self.doc.recompute()
        self.body = next(o for o in self.doc.Objects if o.TypeId == "PartDesign::Body")
        Gui.Selection.clearSelection()

    def tearDown(self):
        Gui.Selection.clearSelection()
        App.closeDocument(self.doc.Name)

    def _selectTopFace(self):
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.doc.Name, self.pad.Name, "Face%d" % (TOP_FACE + 1))

    def _selectPart(self, obj):
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.doc.Name, obj.Name)

    def _faceLooks(self):
        return list(self.pad.ViewObject.ShapeAppearance)

    def test_face_selection_colours_only_that_face(self):
        self._selectTopFace()
        PartGui.setAppearanceOfSelection(red())

        looks = self._faceLooks()
        self.assertEqual(len(looks), 6)
        self.assertTrue(isRed(looks[TOP_FACE]))
        self.assertEqual(sum(isRed(look) for look in looks), 1)

    def test_each_change_is_one_undo_step(self):
        self._selectTopFace()
        PartGui.setAppearanceOfSelection(red())
        self.doc.undo()

        self.assertFalse(any(isRed(look) for look in self._faceLooks()))

    def test_selecting_a_feature_paints_the_whole_part(self):
        self._selectPart(self.pad)
        PartGui.setAppearanceOfSelection(red())

        self.assertTrue(isRed(self.body.ViewObject.ShapeAppearance[0]))
        self.assertTrue(all(isRed(look) for look in self._faceLooks()))

    def test_reset_returns_a_face_to_its_material_look(self):
        self._selectPart(self.body)
        PartGui.setMaterialOfSelection(STEEL)
        steel = self._faceLooks()[0].DiffuseColor
        self._selectTopFace()
        PartGui.setAppearanceOfSelection(red())
        PartGui.resetAppearanceOfSelection()

        looks = self._faceLooks()
        self.assertFalse(isRed(looks[TOP_FACE]))
        self.assertEqual(looks[TOP_FACE].DiffuseColor, steel)

    def test_material_keeps_hand_coloured_faces(self):
        self._selectTopFace()
        PartGui.setAppearanceOfSelection(red())
        before = self._faceLooks()[0].DiffuseColor
        self._selectPart(self.body)
        PartGui.setMaterialOfSelection(STEEL)

        self.assertEqual(self.body.Material.UUID, STEEL)
        looks = self._faceLooks()
        self.assertTrue(isRed(looks[TOP_FACE]))
        self.assertNotEqual(looks[0].DiffuseColor, before)

    def test_material_refuses_faces(self):
        self._selectTopFace()
        with self.assertRaises(ValueError):
            PartGui.setMaterialOfSelection(STEEL)

    def test_panel_shows_only_what_applies(self):
        Gui.runCommand("Part_EditAppearance")
        dock = next(
            d
            for d in Gui.getMainWindow().findChildren(QtWidgets.QDockWidget)
            if d.objectName() == "Appearance"
        )
        groups = {g.title(): g for g in dock.findChildren(QtWidgets.QGroupBox)}

        def shown():
            QtWidgets.QApplication.processEvents()
            return {title for title, g in groups.items() if not g.isHidden()}

        try:
            Gui.Selection.clearSelection()
            self.assertEqual(shown(), set())
            self._selectPart(self.body)
            self.assertEqual(shown(), {"Made of", "Looks like"})
            self._selectTopFace()
            self.assertEqual(shown(), {"Looks like"})
        finally:
            dock.close()

    def test_old_tools_are_gone(self):
        commands = set(Gui.listCommands())
        self.assertIn("Part_EditAppearance", commands)
        for old in ("Std_SetMaterial", "Std_SetAppearance", "Part_ColorPerFace"):
            self.assertNotIn(old, commands)
