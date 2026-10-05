# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

"""Compare in 3D (UI_GUIDE §2.2.1): its own tab, shared material faint, added blue, removed red,
and the model it was asked about left as it was."""

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
        # By name: a comparison replaced by a later one is already closed.
        for name in self._open:
            if name in App.listDocuments():
                App.closeDocument(name)

    def _box(self, doc, x, length):
        box = doc.addObject("Part::Box", "Box")
        box.Length = length
        box.Placement.Base = App.Vector(x, 0, 0)
        doc.recompute()
        return box

    def _show(self, title):
        shown = PartGui.showVersionComparison(self._before, self._after, title)
        self._open.append(shown.Name)
        return shown

    def _colour(self, shown, name):
        view = Gui.getDocument(shown.Name).getObject(name)
        return view.ShapeAppearance[0].DiffuseColor[:3]

    def test_lengthened_box_shows_shared_and_added(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 0, 40)

        shown = self._show("Compare — Longer")

        layers = {obj.Name: obj.Shape.Volume for obj in shown.Objects}
        self.assertEqual(set(layers), {"Shared", "Added"})
        self.assertAlmostEqual(layers["Shared"], 10 * 10 * 10)
        self.assertAlmostEqual(layers["Added"], 30 * 10 * 10)
        for got, want in zip(self._colour(shown, "Added"), ADDED):
            self.assertAlmostEqual(got, want, places=2)
        self.assertEqual(Gui.getDocument(shown.Name).getObject("Shared").Transparency, 75)

    def test_moved_box_shows_removed_in_red(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 4, 10)

        shown = self._show("Compare — Moved")

        self.assertIn("Removed", [obj.Name for obj in shown.Objects])
        for got, want in zip(self._colour(shown, "Removed"), REMOVED):
            self.assertAlmostEqual(got, want, places=2)

    def test_the_model_is_left_as_it_was(self):
        box = self._box(self._after, 0, 40)
        self._box(self._before, 0, 10)
        before_objects = len(self._after.Objects)
        visible = box.ViewObject.Visibility

        self._show("Compare — Untouched")

        self.assertEqual(len(self._after.Objects), before_objects)
        self.assertEqual(box.ViewObject.Visibility, visible)
        self.assertEqual(self._open.count(self._after.Name), 1)

    def test_comparing_the_same_entry_again_replaces_its_tab(self):
        self._box(self._before, 0, 10)
        self._box(self._after, 0, 40)

        self._show("Compare — Again")
        self._show("Compare — Again")

        labels = [doc.Label for doc in App.listDocuments().values()]
        self.assertEqual(labels.count("Compare — Again"), 1)
