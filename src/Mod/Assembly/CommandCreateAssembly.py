# SPDX-License-Identifier: LGPL-2.1-or-later
# /**************************************************************************
#                                                                           *
#    Copyright (c) 2023 Ondsel <development@ondsel.com>                     *
#                                                                           *
#    This file is part of FreeCAD.                                          *
#                                                                           *
#    FreeCAD is free software: you can redistribute it and/or modify it     *
#    under the terms of the GNU Lesser General Public License as            *
#    published by the Free Software Foundation, either version 2.1 of the   *
#    License, or (at your option) any later version.                        *
#                                                                           *
#    FreeCAD is distributed in the hope that it will be useful, but         *
#    WITHOUT ANY WARRANTY; without even the implied warranty of             *
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
#    Lesser General Public License for more details.                        *
#                                                                           *
#    You should have received a copy of the GNU Lesser General Public       *
#    License along with FreeCAD. If not, see                                *
#    <https://www.gnu.org/licenses/>.                                       *
#                                                                           *
# **************************************************************************/

import FreeCAD as App

from PySide.QtCore import QT_TRANSLATE_NOOP

if App.GuiUp:
    import FreeCADGui as Gui
    from PySide import QtCore, QtGui, QtWidgets

import UtilsAssembly

translate = App.Qt.translate

__title__ = "Assembly Command Create Assembly"
__author__ = "Ondsel"
__url__ = "https://www.freecad.org"


class CommandCreateAssembly:
    def __init__(self):
        pass

    def GetResources(self):
        return {
            "Pixmap": "Geoassembly",
            "MenuText": QT_TRANSLATE_NOOP("Assembly_CreateAssembly", "New Assembly"),
            "Accel": "A",
            "ToolTip": QT_TRANSLATE_NOOP(
                "Assembly_CreateAssembly",
                "Creates a new assembly in its own document. Each assembly is its own file; add one assembly to another as a reference with Insert Link.",
            ),
            "CmdType": "ForEdit",
        }

    def IsActive(self):
        return not Gui.Control.activeDialog()

    def Activated(self):
        # An assembly always lives in its own document (one assembly per file). Invoking
        # this while editing another assembly makes a separate, independent one — it is
        # not nested. A sub-assembly enters another assembly only as a cross-document
        # reference, via Insert Link.
        Gui.addModule("UtilsAssembly")
        # An assembly document is typed at creation (the single door, per §7.1 / Amendment 9).
        # Typing it activates the content-scope admission door (Amendment 8): the document then
        # refuses part geometry from any path, so a component can only be a cross-document
        # reference, never owned geometry in the assembly file.
        App.newDocument(type=App.DocTypeAssembly)
        Gui.ActiveDocument.openCommand("New assembly")
        commands = (
            'assembly = App.ActiveDocument.addObject("Assembly::AssemblyObject", "Assembly")\n'
            'assembly.newObject("Assembly::JointGroup", "Joints")'
        )

        Gui.doCommand(commands)
        Gui.doCommandGui("Gui.ActiveDocument.setEdit(assembly)")

        Gui.ActiveDocument.commitCommand()


class ActivateAssemblyTaskPanel:
    """A basic TaskPanel to select an assembly to activate."""

    def __init__(self, assemblies):
        self.assemblies = assemblies
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle(translate("Assembly_ActivateAssembly", "Activate Assembly"))

        layout = QtWidgets.QVBoxLayout(self.form)
        label = QtWidgets.QLabel(
            translate("Assembly_ActivateAssembly", "Select an assembly to activate:")
        )
        self.combo = QtWidgets.QComboBox()

        for asm in self.assemblies:
            # Store the user-friendly Label for display, and the internal Name for activation
            self.combo.addItem(asm.Label, asm.Name)

        layout.addWidget(label)
        layout.addWidget(self.combo)

    def accept(self):
        """Called when the user clicks OK."""
        selected_name = self.combo.currentData()
        if selected_name:
            Gui.doCommand(f"Gui.ActiveDocument.setEdit('{selected_name}')")
        return True

    def reject(self):
        """Called when the user clicks Cancel or closes the panel."""
        return True


class CommandActivateAssembly:
    def __init__(self):
        self.task_panel = None

    def GetResources(self):
        return {
            "Pixmap": "Assembly_ActivateAssembly",
            "MenuText": QT_TRANSLATE_NOOP("Assembly_ActivateAssembly", "Activate Assembly"),
            "ToolTip": QT_TRANSLATE_NOOP(
                "Assembly_ActivateAssembly", "Sets an assembly as the active one for editing."
            ),
            "CmdType": "ForEdit",
        }

    def IsActive(self):
        if Gui.Control.activeDialog() or App.ActiveDocument is None:
            return False

        # Command is only active if no assembly is currently active
        if UtilsAssembly.activeAssembly() is not None:
            return False

        # And if there is at least one assembly in the document to activate
        for obj in App.ActiveDocument.Objects:
            if obj.isDerivedFrom("Assembly::AssemblyObject"):
                return True

        return False

    def Activated(self):
        doc = App.ActiveDocument
        assemblies = [o for o in doc.Objects if o.isDerivedFrom("Assembly::AssemblyObject")]

        if len(assemblies) == 1:
            # If there's only one, activate it directly without showing a dialog
            Gui.doCommand(f"Gui.ActiveDocument.setEdit('{assemblies[0].Name}')")
        elif len(assemblies) > 1:
            # If there are multiple, show a task panel to let the user choose
            self.task_panel = ActivateAssemblyTaskPanel(assemblies)
            Gui.Control.showDialog(self.task_panel)


if App.GuiUp:
    Gui.addCommand("Assembly_CreateAssembly", CommandCreateAssembly())
    Gui.addCommand("Assembly_ActivateAssembly", CommandActivateAssembly())
