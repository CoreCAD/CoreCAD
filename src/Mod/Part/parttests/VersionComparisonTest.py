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
        App.createProject(self._dir, "Model")
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


class HistoryCommitTest(unittest.TestCase):
    """Committing from the History panel, and the offer of a project where there is none."""

    def setUp(self):
        self._dir = tempfile.mkdtemp()
        self._path = os.path.join(self._dir, "part.cpart")
        self._doc = App.newDocument("HistoryCommit", type="Part")
        self._box = self._doc.addObject("Part::Box", "Box")
        self._box.Length = 10
        self._doc.recompute()
        self._doc.saveAs(self._path)
        App.setActiveDocument(self._doc.Name)

    def tearDown(self):
        App.closeDocument(self._doc.Name)
        shutil.rmtree(self._dir, ignore_errors=True)

    def _open_panel(self):
        Gui.runCommand("Std_ShowHistory")
        self._settle()

    def _settle(self, rounds=3):
        from PySide import QtCore, QtWidgets

        app = QtWidgets.QApplication.instance()
        for _ in range(rounds):
            app.processEvents()
            QtCore.QThread.msleep(200)
            app.processEvents()

    def _widget(self, kind, name):
        from PySide import QtWidgets

        found = Gui.getMainWindow().findChild(getattr(QtWidgets, kind), name)
        self.assertIsNotNone(found, name)
        return found

    def _offer(self, name):
        from PySide import QtWidgets

        return Gui.getMainWindow().findChild(QtWidgets.QPushButton, name)

    def _in_dialog(self, act, name=None):
        """Run `act` on the next modal dialog (named `name`, when given) once it is showing; the
        dialog blocks the caller."""
        from PySide import QtCore, QtWidgets

        self._dialog_failure = None
        tries = [50]

        def run():
            dialog = QtWidgets.QApplication.activeModalWidget()
            if dialog is None or (name and dialog.objectName() != name):
                tries[0] -= 1
                if tries[0] > 0:
                    QtCore.QTimer.singleShot(100, run)
                else:
                    self._dialog_failure = AssertionError("no %s dialog appeared" % name)
                    if dialog is not None:
                        dialog.reject()
                return
            try:
                act(dialog)
            except Exception as failure:  # reported after the dialog has closed
                self._dialog_failure = failure
                dialog.reject()

        QtCore.QTimer.singleShot(100, run)

    def _button(self, dialog, text):
        from PySide import QtWidgets

        for button in dialog.findChildren(QtWidgets.QPushButton):
            if button.text() == text:
                return button
        self.fail("no %s button" % text)

    def _versions(self):
        return self._widget("QTreeWidget", "HistoryVersions")

    def _commit(self, message):
        self._widget("QPlainTextEdit", "HistoryCommitMessage").setPlainText(message)
        self._widget("QPushButton", "HistoryCommitButton").click()
        self._settle()

    def _start_and_commit_first(self):
        App.createProject(self._dir, "Part")
        App.commitFile(self._path, "Ten")

    def test_a_part_not_in_a_project_is_offered_one_and_commits_from_the_panel(self):
        from PySide import QtWidgets

        # History alone is not a project: the part's versions are not shown until it is in one.
        App.startHistory(self._dir)
        self._open_panel()
        self.assertTrue(self._offer("HistoryNewProjectButton").isVisible())
        self.assertTrue(self._offer("HistoryAddToProjectButton").isVisible())
        self.assertFalse(self._widget("QWidget", "HistoryCommitButton").isVisible())
        os.makedirs(os.path.join(self._dir, "Taken"))
        with open(os.path.join(self._dir, "Taken", "part.cpart"), "w") as taken:
            taken.write("another part")
        seen = {}

        def create(dialog):
            name = dialog.findChild(QtWidgets.QLineEdit, "ProjectName")
            statement = dialog.findChild(QtWidgets.QLabel, "ProjectStatement")
            problem = dialog.findChild(QtWidgets.QLabel, "ProjectProblem")
            create_button = self._button(dialog, "Create project")
            seen["empty"] = create_button.isEnabled()
            name.setText("Taken")
            seen["taken"] = (create_button.isEnabled(), problem.text())
            name.setText("Gearbox")
            seen["statement"] = statement.text()
            create_button.click()

        self._in_dialog(create)
        self._offer("HistoryNewProjectButton").click()
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

        self.assertFalse(seen["empty"], "a project could be made without a name")
        self.assertFalse(seen["taken"][0], "the move could overwrite another part")
        self.assertIn("part.cpart", seen["taken"][1])
        moved = os.path.join(self._dir, "Gearbox", "part.cpart")
        self.assertEqual(
            seen["statement"], "part.cpart will be moved to %s." % os.path.dirname(moved)
        )
        self.assertFalse(os.path.exists(self._path), "the part was copied, not moved")
        self.assertEqual(os.path.realpath(self._doc.FileName), os.path.realpath(moved))
        self.assertTrue(App.projectFileOf(moved).endswith("Gearbox.cproject"))
        self.assertFalse(self._offer("HistoryNewProjectButton").isVisible())
        self.assertTrue(self._widget("QPushButton", "HistoryCommitButton").isVisible())
        self._commit("Start the part")

        versions = App.fileVersions(moved)
        self.assertEqual([v["summary"] for v in versions], ["Start the part"])
        self.assertEqual(self._versions().topLevelItemCount(), 2)
        self.assertFalse(self._widget("QPushButton", "HistoryCommitButton").isVisible())

    def test_add_to_project_moves_the_part_into_a_chosen_project(self):
        project = App.createProject(os.path.join(self._dir, "Gearbox"), "Gearbox")
        self._open_panel()
        seen = {}

        def choose(dialog):
            dialog.findChild(QtWidgets.QLineEdit, "fileNameEdit").setText(project)
            self._in_dialog(confirm, "AddToProjectDialog")
            dialog.accept()

        def confirm(dialog):
            seen["statement"] = dialog.findChild(QtWidgets.QLabel, "ProjectStatement").text()
            self._button(dialog, "Move to project").click()

        from PySide import QtWidgets

        self._in_dialog(choose)
        self._offer("HistoryAddToProjectButton").click()
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

        moved = os.path.join(self._dir, "Gearbox", "part.cpart")
        self.assertIn("in the project Gearbox", seen["statement"])
        self.assertFalse(os.path.exists(self._path))
        self.assertTrue(os.path.exists(moved))
        self.assertEqual(
            os.path.realpath(App.projectFileOf(self._doc.FileName)), os.path.realpath(project)
        )
        self.assertTrue(self._widget("QPushButton", "HistoryCommitButton").isVisible())

    def test_new_project_from_the_file_menu_moves_nothing(self):
        from PySide import QtWidgets

        where = os.path.join(self._dir, "Empty")
        seen = {}

        def create(dialog):
            dialog.findChild(QtWidgets.QLineEdit, "ProjectName").setText("Empty")
            folder = dialog.findChild(QtWidgets.QLineEdit, "ProjectFolder")
            folder.setText(where)
            folder.textEdited.emit(where)
            seen["statement"] = dialog.findChild(QtWidgets.QLabel, "ProjectStatement").text()
            self._button(dialog, "Create project").click()

        self._in_dialog(create)
        Gui.runCommand("Std_NewProject")
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

        self.assertEqual(seen["statement"], "", "a project with no part said something would move")
        self.assertTrue(os.path.exists(os.path.join(where, "Empty.cproject")))
        self.assertTrue(os.path.exists(self._path), "the open part was moved")

        self._start_and_commit_first()
        self._box.Length = 40
        self._doc.recompute()
        self._open_panel()

        self._commit("Forty")

        self.assertFalse(Gui.getDocument(self._doc.Name).Modified)
        committed = App.openFileVersion(self._path, "HEAD")
        try:
            self.assertEqual(committed.getObject("Box").Length, 40)
        finally:
            App.closeDocument(committed.Name)
        self.assertEqual(self._widget("QPlainTextEdit", "HistoryCommitMessage").toPlainText(), "")

    def test_commit_waits_for_a_message(self):
        self._start_and_commit_first()
        self._box.Length = 40
        self._doc.recompute()
        self._open_panel()
        message = self._widget("QPlainTextEdit", "HistoryCommitMessage")
        button = self._widget("QPushButton", "HistoryCommitButton")

        message.setPlainText("  \n ")
        self.assertFalse(button.isEnabled())
        message.setPlainText("Forty")
        self.assertTrue(button.isEnabled())

    def test_nothing_to_commit_and_older_versions_offer_no_commit(self):
        self._start_and_commit_first()
        self._open_panel()
        row = self._versions().topLevelItem(1)
        self.assertEqual(
            (row.text(0), row.text(1)), ("Ten", "just now"), "the row names its author"
        )
        author = App.fileVersions(self._path)[0]["author"]
        self.assertTrue(row.toolTip(0).startswith(author + ", "), row.toolTip(0))
        button = self._widget("QPushButton", "HistoryCommitButton")
        self.assertFalse(button.isVisible(), "offered to commit with nothing changed")

        self._box.Length = 40
        self._doc.recompute()
        self._settle()
        self.assertTrue(button.isVisible())

        versions = self._versions()
        versions.setCurrentItem(versions.topLevelItem(1))
        self._settle()
        self.assertFalse(button.isVisible(), "offered to commit while an older version is chosen")
        versions.setCurrentItem(versions.topLevelItem(0))
        self._settle()


class ProjectPanelTest(unittest.TestCase):
    """The Project panel (UI_GUIDE §2.2.2): the open project's documents in two headed groups,
    outside ones held at a version and updated only by Update."""

    _settle = HistoryCommitTest._settle
    _widget = HistoryCommitTest._widget
    _in_dialog = HistoryCommitTest._in_dialog
    _button = HistoryCommitTest._button

    def setUp(self):
        HistoryCommitTest.setUp(self)
        self._project = App.createProject(os.path.join(self._dir, "Gearbox"), "Gearbox")
        self._library = os.path.join(self._dir, "Library")
        os.makedirs(self._library)
        self._bolt_path = os.path.join(self._library, "Bolt.cpart")
        self._bolt = App.newDocument("Bolt", type="Part")
        self._bolt.addObject("Part::Box", "Head")
        self._bolt.recompute()
        self._bolt.saveAs(self._bolt_path)
        App.startHistory(self._library)
        self._first = App.commitFile(self._bolt_path, "Bolt")
        App.setActiveDocument(self._doc.Name)

    def tearDown(self):
        if "Bolt" in App.listDocuments():
            App.closeDocument("Bolt")
        HistoryCommitTest.tearDown(self)

    def _list(self):
        return self._widget("QTreeWidget", "ProjectDocuments")

    def _rows(self):
        """Each heading with the rows under it, as the panel shows them."""
        found = []
        tree = self._list()
        for i in range(tree.topLevelItemCount()):
            group = tree.topLevelItem(i)
            found.append(
                (
                    group.text(0),
                    [group.child(j).text(0) for j in range(group.childCount())],
                )
            )
        return found

    def _dock_title(self):
        from PySide import QtWidgets

        return self._list().parent().parent().windowTitle()

    def _choose_file(self, path, then=None):
        from PySide import QtWidgets

        def choose(dialog):
            dialog.findChild(QtWidgets.QLineEdit, "fileNameEdit").setText(path)
            if then:
                then()
            dialog.accept()

        self._in_dialog(choose)

    def _open_project(self, project):
        self._choose_file(project)
        Gui.runCommand("Std_OpenProject")
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

    def test_a_new_project_opens_in_the_panel_empty(self):
        from PySide import QtWidgets

        def create(dialog):
            dialog.findChild(QtWidgets.QLineEdit, "ProjectName").setText("Empty")
            folder = dialog.findChild(QtWidgets.QLineEdit, "ProjectFolder")
            folder.setText(os.path.join(self._dir, "Empty"))
            folder.textEdited.emit(folder.text())
            self._button(dialog, "Create project").click()

        self._in_dialog(create)
        Gui.runCommand("Std_NewProject")
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

        self.assertEqual(self._dock_title(), "Project")
        self.assertEqual(self._model_tree()[0][0], "Empty")
        self.assertEqual(self._rows(), [("This project has no documents yet.", [])])

    def test_the_panel_is_a_tab_beside_model(self):
        from PySide import QtCore, QtWidgets

        self._open_project(self._project)

        window = Gui.getMainWindow()
        dock = self._list().parent().parent()
        model = window.findChild(QtWidgets.QDockWidget, "Model")
        model.show()
        self._settle()
        self.assertEqual(window.dockWidgetArea(dock), QtCore.Qt.LeftDockWidgetArea)
        self.assertIn(model, window.tabifiedDockWidgets(dock))

    def test_the_panel_lists_documents_in_the_project_and_from_outside(self):
        self._doc.saveAs(os.path.join(self._dir, "Gearbox", "Housing.cpart"))
        App.addOutsideDocument(self._project, self._bolt_path)

        self._open_project(self._project)

        self.assertEqual(self._dock_title(), "Project")
        self.assertEqual(
            self._rows(),
            [
                ("In this project (1)", ["Housing.cpart"]),
                (
                    "From outside (1)",
                    ["Bolt.cpart\n%s · Bolt · just now" % os.path.join("..", "Library")],
                ),
            ],
        )

    def _model_tree(self):
        """Each top-level row of the model tree, with the rows under a project row."""
        from PySide import QtWidgets

        for tree in Gui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
            if tree.columnCount() == 4:
                rows = []
                for i in range(tree.topLevelItemCount()):
                    item = tree.topLevelItem(i)
                    if item.type() == 1003:
                        rows.append(
                            (
                                item.text(0),
                                [item.child(j).text(0) for j in range(item.childCount())],
                            )
                        )
                    else:
                        rows.append(item.text(0))
                return rows
        self.fail("model tree not found")

    def _model_tree_item(self, text):
        from PySide import QtCore, QtWidgets

        for tree in Gui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
            if tree.columnCount() == 4:
                found = tree.findItems(text, QtCore.Qt.MatchExactly | QtCore.Qt.MatchRecursive)
                if found:
                    return found[0]
        self.fail("%s not in the model tree" % text)

    def test_the_model_tree_groups_the_documents_the_project_lists(self):
        housing = os.path.join(self._dir, "Gearbox", "Housing.cpart")
        self._doc.saveAs(housing)
        App.addOutsideDocument(self._project, self._bolt_path)
        loose = App.newDocument("Loose", type="Part")
        loose.saveAs(os.path.join(self._dir, "Loose.cpart"))
        try:
            self._open_project(self._project)

            self.assertEqual(
                self._model_tree(),
                [
                    ("Gearbox", [self._doc.Label, "Bolt (external)"]),
                    loose.Label,
                ],
            )
            self.assertEqual(
                self._model_tree_item("Bolt (external)").toolTip(0),
                "From Library\n" + self._bolt_path,
            )
        finally:
            App.closeDocument(loose.Name)

    def test_a_newer_version_is_offered_and_taken_only_by_update(self):
        from PySide import QtWidgets

        App.addOutsideDocument(self._project, self._bolt_path)
        self._bolt.addObject("Part::Box", "Thread")
        self._bolt.recompute()
        self._bolt.save()
        second = App.commitFile(self._bolt_path, "Thread")

        self._open_project(self._project)

        row = self._rows()[1][1][0]
        self.assertTrue(row.endswith("Bolt · just now · newer version available"), row)
        self.assertEqual(App.outsideDocuments(self._project)[0]["version"], self._first)
        update = self._list().findChild(QtWidgets.QPushButton, "ProjectUpdate")
        self.assertIsNotNone(update, "no Update offered")
        update.click()
        self._settle()

        self.assertEqual(App.outsideDocuments(self._project)[0]["version"], second)
        self.assertTrue(self._rows()[1][1][0].endswith("Thread · just now"), self._rows())
        from PySide import QtCore

        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        self.assertIsNone(
            self._list().findChild(QtWidgets.QPushButton, "ProjectUpdate"), "Update still offered"
        )

    def test_add_from_outside_lists_a_document_where_it_is(self):
        self._open_project(self._project)

        self._choose_file(self._bolt_path)
        self._widget("QPushButton", "ProjectAddFromOutside").click()
        self._settle()
        if self._dialog_failure:
            raise self._dialog_failure

        self.assertTrue(os.path.exists(self._bolt_path), "listing a document moved it")
        self.assertEqual(self._rows()[1][0], "From outside (1)")
        self.assertEqual(App.outsideDocuments(self._project)[0]["version"], self._first)

        self._choose_file(self._bolt_path)
        self._widget("QPushButton", "ProjectAddFromOutside").click()
        self._settle()
        problem = self._widget("QLabel", "ProjectProblem")
        self.assertTrue(problem.isVisible())
        self.assertEqual(
            problem.text(), "Bolt.cpart was not added. The project lists this document already."
        )

    def test_opening_another_project_replaces_the_one_shown(self):
        other = App.createProject(os.path.join(self._dir, "Motor"), "Motor")
        self._open_project(self._project)
        self._open_project(other)

        self.assertEqual(self._model_tree()[0][0], "Motor")

    def test_double_click_opens_a_document(self):
        App.closeDocument("Bolt")
        App.addOutsideDocument(self._project, self._bolt_path)
        self._open_project(self._project)

        row = self._list().topLevelItem(1).child(0)
        self._list().itemDoubleClicked.emit(row, 0)
        self._settle()

        opened = [d for d in App.listDocuments().values() if d.FileName == self._bolt_path]
        self.assertEqual(len(opened), 1, "the document was not opened")
        App.closeDocument(opened[0].Name)
