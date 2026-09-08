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

import re
import os
import FreeCAD as App


from PySide.QtCore import QT_TRANSLATE_NOOP

if App.GuiUp:
    import FreeCADGui as Gui
    from PySide import QtCore, QtGui, QtWidgets
    from PySide.QtWidgets import QPushButton, QMenu

import AssemblyApp
import UtilsAssembly
import Preferences

__title__ = "Assembly Command Create Exploded View"
__author__ = "Ondsel"
__url__ = "https://www.freecad.org"


class CommandCreateView:
    def __init__(self):
        pass

    def GetResources(self):
        return {
            "Pixmap": "Assembly_ExplodedView",
            "MenuText": QT_TRANSLATE_NOOP("Assembly_CreateView", "Exploded View"),
            "Accel": "E",
            "ToolTip": QT_TRANSLATE_NOOP(
                "Assembly_CreateView",
                "Creates an exploded view of the current assembly",
            ),
            "CmdType": "ForEdit",
        }

    def IsActive(self):
        return (
            UtilsAssembly.isAssemblyCommandActive()
            and UtilsAssembly.assembly_has_at_least_n_parts(2)
        )

    def Activated(self):
        assembly = UtilsAssembly.activeAssembly()
        if not assembly:
            return

        Gui.addModule("CommandCreateView")  # NOLINT
        Gui.doCommand("panel = CommandCreateView.TaskAssemblyCreateView()")
        self.panel = Gui.doCommandEval("panel")
        Gui.doCommandGui("dialog = Gui.Control.showDialog(panel)")
        dialog = Gui.doCommandEval("dialog")
        if dialog is not None:
            dialog.setAutoCloseOnDeletedDocument(True)
            dialog.setDocumentName(App.ActiveDocument.Name)


######### Exploded View Object ###########
def redrawStepLines(step, positions):
    """Ask a move's view provider to draw the lines its components travelled along.

    The move used to reach into its own view provider from inside applyStep. A typed
    object must not depend on the view layer, so it returns the lines instead and the
    panel hands them over.
    """
    if step.ViewObject is None:
        return

    step.ViewObject.redrawLines(positions)


def editExplodedView(viewObj):
    """Open the exploded-view panel on a view. Called by its view provider."""
    task = Gui.Control.activeTaskDialog()
    if task:
        task.reject()

    assembly = viewObj.getAssembly()
    if assembly is None:
        return

    if UtilsAssembly.activeAssembly() != assembly:
        Gui.ActiveDocument.setEdit(assembly)

    panel = TaskAssemblyCreateView(viewObj)
    dialog = Gui.Control.showDialog(panel)
    if dialog is not None:
        dialog.setAutoCloseOnDeletedDocument(True)
        dialog.setDocumentName(App.ActiveDocument.Name)


class ExplodedViewStepsObserver:
    """Notices moves being added to or removed from an exploded view.

    The former Python proxy called the panel back from its own onChanged. A typed
    object holds no reference to a dialog, so the panel watches the document instead,
    which also catches an undo the proxy never saw.
    """

    def __init__(self, viewObj, callback):
        self.viewObj = viewObj
        self.callback = callback

    def slotChangedObject(self, obj, prop):
        if obj == self.viewObj and prop == "Group":
            self.callback()


class ExplodedViewSelGate:
    def __init__(self, assembly, viewObj):
        self.assembly = assembly
        self.viewObj = viewObj

    def allow(self, doc, obj, sub):
        comp, new_sub = UtilsAssembly.getComponentReference(self.assembly, obj, sub)
        if comp:
            # Objects within the assembly.
            return True

        if obj in self.viewObj.Group:
            # Enable selection of steps object
            return True

        return False


######### Create Exploded View Task ###########
class TaskAssemblyCreateView(QtCore.QObject):
    def __init__(self, viewObj=None):
        super().__init__()

        self.form = Gui.PySideUic.loadUi(":/panels/TaskAssemblyCreateView.ui")
        self.form.stepList.installEventFilter(self)
        self.form.stepList.itemClicked.connect(self.onItemClicked)

        view = Gui.activeDocument().activeView()

        self.assembly = UtilsAssembly.activeAssembly()
        self.assembly.ViewObject.EnableMovement = False
        self.com, self.size = UtilsAssembly.getComAndSize(self.assembly)
        self.asmDragger = self.assembly.ViewObject.getDragger()
        self.cbFin = view.addDraggerCallback(
            self.asmDragger, "addFinishCallback", self.draggerFinished
        )
        self.cbMov = view.addDraggerCallback(
            self.asmDragger, "addMotionCallback", self.draggerMoved
        )

        Gui.Selection.clearSelection()

        self.form.btnAlignDragger.setMenu(QMenu(self.form.btnAlignDragger))
        actionAlignTo = self.form.btnAlignDragger.menu().addAction("Align to...")
        actionAlignToCenter = self.form.btnAlignDragger.menu().addAction("Align to part center")
        actionAlignToOrigin = self.form.btnAlignDragger.menu().addAction("Align to part origin")

        # Connect actions to the respective functions
        actionAlignTo.triggered.connect(self.onAlignTo)
        actionAlignToCenter.triggered.connect(self.onAlignToCenter)
        actionAlignToOrigin.triggered.connect(self.onAlignToPartOrigin)

        self.form.btnAlignDragger.setEnabled(False)
        self.form.btnAlignDragger.setText("Select a part")
        self.form.btnRadialExplosion.clicked.connect(self.onRadialClicked)

        pref = Preferences.preferences()
        self.form.CheckBox_PartsAsSingleSolid.setChecked(pref.GetBool("PartsAsSingleSolid", True))

        self.initialPlcs = UtilsAssembly.saveAssemblyPartsPlacements(self.assembly)

        if viewObj:
            Gui.ActiveDocument.openCommand("Edit Exploded View")

            self.viewObj = viewObj
            for move in self.viewObj.Group:
                move.Visibility = True
            self.onMovesChanged()

        else:
            Gui.ActiveDocument.openCommand("Create Exploded View")
            self.createExplodedViewObject()

        Gui.Selection.addSelectionGate(
            ExplodedViewSelGate(self.assembly, self.viewObj), Gui.Selection.ResolveMode.NoResolve
        )
        Gui.Selection.addObserver(self, Gui.Selection.ResolveMode.NoResolve)

        self.stepsObserver = ExplodedViewStepsObserver(self.viewObj, self.onMovesChanged)
        App.addDocumentObserver(self.stepsObserver)
        self.callbackMove = view.addEventCallback("SoLocation2Event", self.moveMouse)
        self.callbackClick = view.addEventCallback("SoMouseButtonEvent", self.clickMouse)
        self.callbackKey = view.addEventCallback("SoKeyboardEvent", self.KeyboardEvent)

        self.selectingFeature = False
        self.form.LabelAlignDragger.setVisible(False)
        self.presel_ref = None

        self.blockSetDragger = False
        self.blockDraggerMove = True
        self.currentStep = None
        self.radialExplosion = False

        self.viewObj.purgeTouched()

    def accept(self):
        self.deactivate()
        UtilsAssembly.restoreAssemblyPartsPlacements(self.assembly, self.initialPlcs)
        for move in self.viewObj.Group:
            move.Visibility = False
        commands = ""
        for move in self.viewObj.Group:
            more = UtilsAssembly.generatePropertySettings(move)
            commands = commands + more
        Gui.doCommand(commands[:-1])  # Don't use the last \n
        Gui.ActiveDocument.commitCommand()

        self.viewObj.purgeTouched()

        return True

    def reject(self):
        self.deactivate()
        Gui.ActiveDocument.abortCommand()
        App.activeDocument().recompute()
        return True

    def deactivate(self):
        pref = Preferences.preferences()
        pref.SetBool("PartsAsSingleSolid", self.form.CheckBox_PartsAsSingleSolid.isChecked())

        view = Gui.activeDocument().activeView()
        view.removeDraggerCallback(self.asmDragger, "addFinishCallback", self.cbFin)
        view.removeDraggerCallback(self.asmDragger, "addMotionCallback", self.cbMov)

        self.assembly.ViewObject.DraggerVisibility = False
        self.assembly.ViewObject.EnableMovement = True

        Gui.Selection.removeSelectionGate()
        Gui.Selection.removeObserver(self)
        Gui.Selection.clearSelection()

        App.removeDocumentObserver(self.stepsObserver)
        view.removeEventCallback("SoLocation2Event", self.callbackMove)
        view.removeEventCallback("SoMouseButtonEvent", self.callbackClick)
        view.removeEventCallback("SoKeyboardEvent", self.callbackKey)

        if Gui.Control.activeDialog():
            Gui.Control.closeDialog()

    def setDragger(self):
        if self.blockSetDragger:
            return

        self.dismissCurrentStep()
        self.selectedRefs = []
        self.selectedObjs = []
        self.selectedObjsInitPlc = []
        selection = Gui.Selection.getSelectionEx("*", 0)
        if not selection:
            self.enableDragger(False)
            return
        for sel in selection:
            # If you select 2 solids (bodies for example) within an assembly.
            # There'll be a single sel but 2 SubElementNames.

            if not sel.SubElementNames:
                # no subnames, so its a root assembly itself that is selected.
                Gui.Selection.removeSelection(sel.Object)
                continue

            for sub_name in sel.SubElementNames:
                moving_part, new_sub = UtilsAssembly.getComponentReference(
                    self.assembly, sel.Object, sub_name
                )
                if not moving_part:
                    continue

                ref = [moving_part, [new_sub]]
                obj = UtilsAssembly.getObject(ref)
                element_name = UtilsAssembly.getElementName(sub_name)

                # Only objects within the assembly, not the assembly and not elements.
                if obj is None or moving_part is None or obj == self.assembly or element_name != "":
                    Gui.Selection.removeSelection(sel.Object, sub_name)
                    continue

                partAsSolid = self.form.CheckBox_PartsAsSingleSolid.isChecked()
                if partAsSolid:
                    obj = moving_part

                # truncate the sub name at obj.Name
                if partAsSolid:
                    # We handle both cases separately because with external files there
                    # can be several times the same name. For containing part we are sure it's
                    # the first instance, for the object we are sure it's the last.
                    ref[1][0] = UtilsAssembly.truncateSubAtLast(ref[1][0], obj.Name)
                else:
                    ref[1][0] = UtilsAssembly.truncateSubAtFirst(ref[1][0], obj.Name)

                if not obj in self.selectedObjs and hasattr(obj, "Placement"):
                    ref = [sel.Object, [sub_name]]
                    self.selectedRefs.append(ref)
                    self.selectedObjs.append(obj)
                    self.selectedObjsInitPlc.append(App.Placement(obj.Placement))

        if len(self.selectedObjs) != 0:
            self.enableDragger(True)
            self.onAlignToCenter()

        else:
            self.enableDragger(False)

    def enableDragger(self, val):
        self.assembly.ViewObject.DraggerVisibility = val
        self.form.btnAlignDragger.setEnabled(val)
        if val:
            self.form.btnAlignDragger.setText("Align dragger to...")
        else:
            self.form.btnAlignDragger.setText("Select a part")

    def onMovesChanged(self):
        # First reset positions
        UtilsAssembly.restoreAssemblyPartsPlacements(self.assembly, self.initialPlcs)

        for move in self.viewObj.Group:
            redrawStepLines(move, move.applyStep(self.com, self.size))

        self.form.stepList.clear()
        for move in self.viewObj.Group:
            self.form.stepList.addItem(move.Name)

    def onItemClicked(self, item):
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(self.viewObj.Document.Name, item.text(), "")
        # we give back the focus to the item as addSelection gave the focus to the 3dview
        self.form.stepList.setCurrentItem(item)

    def onRadialClicked(self):
        self.dismissCurrentStep()

        # Add to selection all the movable parts
        partsAsSolid = self.form.CheckBox_PartsAsSingleSolid.isChecked()
        assemblyParts = UtilsAssembly.getMovablePartsWithin(self.assembly, partsAsSolid)
        self.blockSetDragger = True
        for part in assemblyParts:
            Gui.Selection.addSelection(part, "")
        self.blockSetDragger = False
        self.setDragger()

        self.radialExplosion = True

    def onAlignTo(self):
        self.alignMode = "Custom"
        self.selectingFeature = True
        # We use greedy selection to prevent that clicking again on the solid
        # clears selection before trying to select the whole assembly
        Gui.Selection.setSelectionStyle(Gui.Selection.SelectionStyle.GreedySelection)
        self.enableDragger(False)
        self.form.LabelAlignDragger.setVisible(True)

    def endSelectionMode(self):
        self.selectingFeature = False
        self.enableDragger(True)
        Gui.Selection.setSelectionStyle(Gui.Selection.SelectionStyle.NormalSelection)
        self.form.LabelAlignDragger.setVisible(False)

    def onAlignToCenter(self):
        self.alignMode = "Center"
        self.setDraggerObjectPlc()

    def onAlignToPartOrigin(self):
        self.alignMode = "PartOrigin"
        self.setDraggerObjectPlc()

    def findDraggerInitialPlc(self):
        if len(self.selectedObjs) == 0:
            return

        if self.alignMode == "Custom":
            self.initialDraggerPlc = App.Placement(self.assembly.ViewObject.DraggerPlacement)
        else:
            plc = UtilsAssembly.getGlobalPlacement(self.selectedRefs[0], self.selectedObjs[0])
            self.initialDraggerPlc = App.Placement(plc)
            if self.alignMode == "Center":
                self.initialDraggerPlc.Base = UtilsAssembly.getCenterOfBoundingBox(
                    self.selectedObjs, self.selectedRefs
                )

    def setDraggerObjectPlc(self):
        self.findDraggerInitialPlc()

        self.blockDraggerMove = True
        self.assembly.ViewObject.DraggerPlacement = self.initialDraggerPlc
        self.blockDraggerMove = False

    def createExplodedViewObject(self):

        Gui.addModule("UtilsAssembly")
        commands = (
            f'assembly = App.ActiveDocument.getObject("{self.assembly.Name}")\n'
            "view_group = UtilsAssembly.getViewGroup(assembly)\n"
            'viewObj = view_group.newObject("Assembly::ExplodedView", "Exploded View")'
        )
        Gui.doCommand(commands)
        self.viewObj = Gui.doCommandEval("viewObj")

    def createExplodedStepObject(self):
        moveType = "Normal"
        if self.radialExplosion:
            self.radialExplosion = False
            moveType = "Radial"

        # The move is created ON the exploded view, which is the only thing that owns
        # it. It used to be created on the assembly and then also listed in the view's
        # group, putting one object in two groups at once -- something the group rule
        # forbids, and which only went unnoticed because the Python group extension
        # never ran the check.
        commands = (
            f'viewObj = App.ActiveDocument.getObject("{self.viewObj.Name}")\n'
            'currentStep = viewObj.newObject("Assembly::ExplodedViewStep", "Move")'
        )
        Gui.doCommand(commands)
        self.currentStep = Gui.doCommandEval("currentStep")

        self.currentStep.MoveType = moveType
        self.currentStep.MovementTransform = App.Placement()

        # Note: the rootObj of all our refs must be the same since all the
        # objects are within assembly. So we put all the sub in a single ref.
        listOfSubs = []
        for ref in self.selectedRefs:
            listOfSubs.append(ref[1][0])
        self.currentStep.References = [self.selectedRefs[0][0], listOfSubs]

    def dismissCurrentStep(self):
        if self.currentStep is None:
            return

        for obj, init_plc in zip(self.selectedObjs, self.selectedObjsInitPlc):
            obj.Placement = init_plc

        Gui.doCommand(f'App.ActiveDocument.removeObject("{self.currentStep.Name}")')
        self.currentStep = None

        Gui.Selection.clearSelection()

    def draggerMoved(self, event):
        if self.blockDraggerMove:
            return

        if self.currentStep is None:
            self.createExplodedStepObject()

        # reset the objects position to their position before the current move.
        for obj, init_plc in zip(self.selectedObjs, self.selectedObjsInitPlc):
            obj.Placement = init_plc

        # we update the move Placement.
        draggerPlc = self.assembly.ViewObject.DraggerPlacement
        self.currentStep.MovementTransform = draggerPlc * self.initialDraggerPlc.inverse()

        # Apply the move
        redrawStepLines(self.currentStep, self.currentStep.applyStep(self.com, self.size))

    def draggerFinished(self, event):
        isRadial = self.currentStep.MoveType == "Radial"
        self.currentStep = None

        if isRadial:
            Gui.Selection.clearSelection()
            return

        # Reset the initial placements
        self.findDraggerInitialPlc()

        for i, obj in enumerate(self.selectedObjs):
            self.selectedObjsInitPlc[i] = App.Placement(obj.Placement)

    def moveMouse(self, info):
        if not self.selectingFeature:
            return

        view = Gui.activeDocument().activeView()
        cursor_info = view.getObjectInfo(view.getCursorPos())

        if not cursor_info or not self.presel_ref:
            self.assembly.ViewObject.DraggerVisibility = False
            return

        ref = self.presel_ref
        element_name = UtilsAssembly.getElementName(ref[1][0])

        if element_name == "":
            vertex_name = ""
        else:
            newPos = App.Vector(cursor_info["x"], cursor_info["y"], cursor_info["z"])
            vertex_name = UtilsAssembly.findElementClosestVertex(self.assembly, ref, newPos)

        ref = UtilsAssembly.addVertexToReference(ref, vertex_name)

        plc = AssemblyApp.findPlacement(ref)
        global_plc = UtilsAssembly.getGlobalPlacement(ref)
        plc = global_plc * plc

        self.blockDraggerMove = True
        self.assembly.ViewObject.DraggerPlacement = plc
        self.blockDraggerMove = False
        self.assembly.ViewObject.DraggerVisibility = True

    def clickMouse(self, info):
        if info["Button"] == "BUTTON2" and info["State"] == "DOWN":
            if self.selectingFeature:
                self.endSelectionMode()

    # 3D view keyboard handler
    def KeyboardEvent(self, info):
        if info["State"] == "UP" and info["Key"] == "ESCAPE":
            if self.currentStep is None:
                self.reject()
            else:
                if self.selectingFeature:
                    self.endSelectionMode()
                else:
                    self.dismissCurrentStep()

    # Taskbox keyboard event handler
    def eventFilter(self, watched, event):
        if self.form is not None and watched == self.form.stepList:
            if event.type() == QtCore.QEvent.ShortcutOverride:
                if event.key() == QtCore.Qt.Key_Delete:
                    event.accept()
                    return True  # Indicate that the event has been handled
                return False

            elif event.type() == QtCore.QEvent.KeyPress:
                if event.key() == QtCore.Qt.Key_Delete:
                    selected_indexes = self.form.stepList.selectedIndexes()
                    sorted_indexes = sorted(selected_indexes, key=lambda x: x.row(), reverse=True)
                    for index in sorted_indexes:
                        row = index.row()
                        if row < len(self.viewObj.Group):
                            move = self.viewObj.Group[row]
                            # First remove the link from the viewObj
                            self.viewObj.Group.remove(move)
                            # Delete the object
                            move.Document.removeObject(move.Name)

                    return True  # Consume the event

        return super().eventFilter(watched, event)

    # selectionObserver stuff
    def addSelection(self, doc_name, obj_name, sub_name, mousePos):
        if self.selectingFeature:
            Gui.Selection.removeSelection(doc_name, obj_name, sub_name)
            return

        else:
            rootObj = App.getDocument(doc_name).getObject(obj_name)
            moving_part, new_sub = UtilsAssembly.getComponentReference(
                self.assembly, rootObj, sub_name
            )
            ref = [moving_part, [new_sub]]
            obj = UtilsAssembly.getObject(ref)

            if obj is None or moving_part is None:
                return

            if self.form.CheckBox_PartsAsSingleSolid.isChecked():
                part = moving_part
            else:
                part = obj

            element_name = UtilsAssembly.getElementName(sub_name)

            if element_name != "":
                # When selecting, we do not want to select an element, but only the containing part.
                Gui.Selection.removeSelection(doc_name, obj_name, sub_name)
                if Gui.Selection.isSelected(part, ""):
                    Gui.Selection.removeSelection(part, "")
                else:
                    Gui.Selection.addSelection(part, "")
            else:
                self.setDragger()
                pass

    def removeSelection(self, doc_name, obj_name, sub_name, mousePos=None):
        if self.selectingFeature:
            self.endSelectionMode()
            self.findDraggerInitialPlc()
            return

        element_name = UtilsAssembly.getElementName(sub_name)
        if element_name == "":
            self.setDragger()
            pass

    def setPreselection(self, doc_name, obj_name, sub_name):
        if not self.selectingFeature or not sub_name:
            self.presel_ref = None
            return

        self.presel_ref = [App.getDocument(doc_name).getObject(obj_name), [sub_name]]

    def clearSelection(self, doc_name):
        self.form.stepList.clearSelection()
        self.setDragger()


if App.GuiUp:
    Gui.addCommand("Assembly_CreateView", CommandCreateView())
