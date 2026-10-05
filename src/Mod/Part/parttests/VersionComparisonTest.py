# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

"""How History draws a version in the model view (UI_GUIDE §2.2.1): the version's material, what it
added blue, what it removed red and see-through, or the version plainly; the model left as it was.
"""

import os
import shutil
import subprocess
import tempfile
import unittest

import FreeCAD as App
import FreeCADGui as Gui
import PartGui

ADDED = (0x0F / 255.0, 0x6C / 255.0, 0xBD / 255.0)
REMOVED = (0xC5 / 255.0, 0x0F / 255.0, 0x1F / 255.0)


class VersionComparisonTest(unittest.TestCase):

    def setUp(self):
        self._before = App.newDocument("CompareBefore")
        self._after = App.newDocument("CompareAfter")
        self._open = [self._before.Name, self._after.Name]

    def tearDown(self):
        for name in self._open:
            if name in App.listDocuments():
                App.closeDocument(name)

    def _box(self, doc, x, length):
        box = doc.addObject("Part::Box", "Box")
        box.Length = length
        box.Placement.Base = App.Vector(x, 0, 0)
        doc.recompute()
        return box

    def _layers(self, show_changes=True):
        layers = PartGui.versionLayers(self._before, self._after, show_changes)
        self._open.append(layers.Name)
        return layers

    def _view(self, layers, name):
        return Gui.getDocument(layers.Name).getObject(name)

    def test_lengthened_box_shows_kept_and_added(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 0, 40)

        layers = self._layers()

        volumes = {obj.Name: obj.Shape.Volume for obj in layers.Objects}
        self.assertEqual(set(volumes), {"Kept", "Added"})
        self.assertAlmostEqual(volumes["Kept"], 10 * 10 * 10)
        self.assertAlmostEqual(volumes["Added"], 30 * 10 * 10)
        added = self._view(layers, "Added").ShapeAppearance[0].DiffuseColor[:3]
        for got, want in zip(added, ADDED):
            self.assertAlmostEqual(got, want, places=2)
        self.assertEqual(self._view(layers, "Kept").Transparency, 0)

    def test_moved_box_shows_removed_in_see_through_red(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 4, 10)

        layers = self._layers()

        removed = self._view(layers, "Removed")
        for got, want in zip(removed.ShapeAppearance[0].DiffuseColor[:3], REMOVED):
            self.assertAlmostEqual(got, want, places=2)
        self.assertGreater(removed.Transparency, 0)

    def test_without_changes_the_version_is_drawn_plainly(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 0, 40)

        layers = self._layers(show_changes=False)

        self.assertEqual([obj.Name for obj in layers.Objects], ["Version"])
        self.assertAlmostEqual(layers.Objects[0].Shape.Volume, 40 * 10 * 10)

    def test_the_layers_have_no_tab_and_the_model_is_left_as_it_was(self):
        box = self._box(self._after, 0, 40)
        self._box(self._before, 0, 10)
        objects = len(self._after.Objects)
        visible = box.ViewObject.Visibility

        layers = self._layers()

        self.assertEqual(Gui.getDocument(layers.Name).mdiViewsOfType("Gui::View3DInventor"), [])
        self.assertEqual(len(self._after.Objects), objects)
        self.assertEqual(box.ViewObject.Visibility, visible)


@unittest.skipIf(shutil.which("git") is None, "History reads versions from git")
class HistoryModelViewTest(unittest.TestCase):
    """Choosing a version in the History panel shows it in the model view, locked."""

    def setUp(self):
        self._dir = tempfile.mkdtemp()
        self._git("init", "-q")
        self._git("config", "user.email", "test@example.com")
        self._git("config", "user.name", "Test")
        self._doc = App.newDocument("HistoryModel", type="Part")
        self._box = self._doc.addObject("Part::Box", "Box")
        path = os.path.join(self._dir, "part.cpart")
        for length in (10, 40):
            self._box.Length = length
            self._doc.recompute()
            self._doc.saveAs(path)
            self._git("add", "-A")
            self._git("commit", "-qm", "Length %d" % length)
        App.setActiveDocument(self._doc.Name)
        Gui.runCommand("Std_ShowHistory")
        self._settle()
        self._versions = self._panelList(rows=3)

    def tearDown(self):
        self._versions.setCurrentItem(self._versions.topLevelItem(0))
        self._settle()
        App.closeDocument(self._doc.Name)
        shutil.rmtree(self._dir, ignore_errors=True)

    def _git(self, *args):
        subprocess.run(["git", "-C", self._dir, *args], check=True, capture_output=True)

    def _settle(self, rounds=3):
        from PySide import QtCore, QtWidgets

        app = QtWidgets.QApplication.instance()
        for _ in range(rounds):
            app.processEvents()
            QtCore.QThread.msleep(200)
            app.processEvents()

    def _panelList(self, rows):
        from PySide import QtWidgets

        for tree in Gui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
            if tree.topLevelItemCount() == rows and tree.columnCount() == 2:
                return tree
        self.fail("History panel's version list not found")

    def _bars(self):
        from PySide import QtWidgets

        return [
            bar
            for bar in Gui.getMainWindow().findChildren(QtWidgets.QFrame, "PastVersionBar")
            if bar.isVisible()
        ]

    def _choose(self, row):
        self._versions.setCurrentItem(self._versions.topLevelItem(row))
        self._settle()

    def _editable(self):
        """Whether a command that changes the part is available (Std_Group: always, otherwise)."""
        Gui.Command.update()
        actions = Gui.Command.get("Std_Group").getAction()
        self.assertTrue(actions)
        return actions[0].isEnabled()

    def test_choosing_an_older_version_shows_a_bar_and_locks_editing(self):
        self.assertTrue(self._editable())

        self._choose(2)

        self.assertEqual(len(self._bars()), 1)
        self.assertFalse(self._editable())
        self.assertEqual(self._box.Length, 40)

    def test_the_bar_is_tinted_and_clear_of_the_navigation_cube(self):
        """Default settings put the navigation cube in the top-right corner of the view."""
        self._choose(2)

        bar = self._bars()[0]
        view = bar.parentWidget()
        cube = App.ParamGet("User parameter:BaseApp/Preferences/NaviCube").GetInt("CubeSize", 132)
        self.assertLessEqual(bar.geometry().right(), view.width() - cube)
        painted = bar.grab().toImage().pixelColor(2, 2)
        self.assertNotEqual(painted, view.palette().window().color())

    def test_choosing_not_yet_committed_returns_to_now(self):
        self._choose(2)
        self._choose(0)

        self.assertEqual(self._bars(), [])
        self.assertTrue(self._editable())

    def test_a_change_to_the_part_returns_to_now(self):
        self._choose(2)

        self._box.Width = 25
        self.assertEqual(self._bars(), [], "the old version stayed on screen after a change")
        self._settle()

        self.assertEqual(self._versions.currentItem(), self._versions.topLevelItem(0))
        self.assertTrue(self._editable())
