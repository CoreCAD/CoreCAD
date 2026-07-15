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

import math

import FreeCAD as App
import Part

from PySide import QtCore
from PySide.QtCore import QT_TRANSLATE_NOOP
from collections.abc import Sequence

if App.GuiUp:
    import FreeCADGui as Gui
    from PySide import QtGui, QtWidgets

__title__ = "Assembly Joint object"
__author__ = "Ondsel"
__url__ = "https://www.freecad.org"

from pivy import coin
import AssemblyApp
import UtilsAssembly
import Preferences

from SoSwitchMarker import SoSwitchMarker

translate = App.Qt.translate

TranslatedJointTypes = [
    translate("Assembly", "Fixed"),
    translate("Assembly", "Revolute"),
    translate("Assembly", "Cylindrical"),
    translate("Assembly", "Slider"),
    translate("Assembly", "Ball"),
    translate("Assembly", "Distance"),
    translate("Assembly", "Parallel"),
    translate("Assembly", "Perpendicular"),
    translate("Assembly", "Angle"),
    translate("Assembly", "RackPinion"),
    translate("Assembly", "Screw"),
    translate("Assembly", "Gears"),
    translate("Assembly", "Belt"),
]

JointTypes = [
    "Fixed",
    "Revolute",
    "Cylindrical",
    "Slider",
    "Ball",
    "Distance",
    "Parallel",
    "Perpendicular",
    "Angle",
    "RackPinion",
    "Screw",
    "Gears",
    "Belt",
]

JointUsingAngle = [
    "Angle",
]

JointUsingDistance = [
    "Distance",
    "RackPinion",
    "Screw",
    "Gears",
    "Belt",
]

JointUsingDistance2 = [
    "Gears",
    "Belt",
]

JointNoNegativeDistance = [
    "Gears",
    "Belt",
]

JointUsingOffset = [
    "Fixed",
    "Revolute",
]

JointUsingRotation = [
    "Fixed",
    "Slider",
]

JointUsingReverse = [
    "Fixed",
    "Revolute",
    "Cylindrical",
    "Slider",
    "Distance",
    "Parallel",
]

JointUsingLimitLength = [
    "Cylindrical",
    "Slider",
]

JointUsingLimitAngle = [
    "Revolute",
    "Cylindrical",
]

# The per-kind behaviour capabilities (pre-solve, parallel handling, vertex
# handling) now live on the typed C++ Assembly::Joint (see JointKind): the joint
# answers joint.usesPreSolve() / joint.forbidsParallel() / joint.ignoresVertex()
# directly, replacing the former JointUsingPreSolve / JointParallelForbidden
# membership lists. The remaining JointUsing* lists below still drive the Python
# task dialog and move to JointKind when the dialog is ported.


def solveIfAllowed(assembly, storePrev=False):
    if assembly.Type == "Assembly" and Preferences.preferences().GetBool(
        "SolveInJointCreation", True
    ):
        assembly.solve(storePrev)


def getContext(obj):
    """Fetch the context of an object."""
    context = []
    current = obj

    while current:
        # Add the object's Label at the beginning or the Name if label is empty
        context.insert(0, current.Label if current.Label else current.Name)
        # Get the immediate parent object
        parents = getattr(current, "InList", [])
        current = parents[0] if parents else None
    return ".".join(context)


# The joint object consists of 2 JCS (joint coordinate systems) and a Joint Type.
# A JCS is a placement that is computed (unless it is detached) from references (PropertyXLinkSubHidden) that links to :
# - An object: this can be any Part::Feature solid. Or a PartDesign Body. Or a App::Link to those.
# - An element name: This can be either a face, an edge, a vertex or empty. Empty means that the Object placement will be used
# - A vertex name: For faces and edges, we need to specify which vertex of said face/edge to use
# Both element names hold the full path to the object.
# From these a placement is computed. It is relative to the Object.


# ---------------------------------------------------------------------------
# Interactive joint tooling. These orchestrate reference wiring and pre-solve
# part positioning for the create/edit dialog. They live at module scope (not on
# the Python Joint proxy) so they survive the Stage 5 flip to the bare typed
# Assembly::Joint and stay reachable headless. Transient pre-solve undo state is
# held on the active task panel (activeTask); it only matters while editing.
# ---------------------------------------------------------------------------


def setJointConnectors(joint, refs):
    # refs is a list of [obj, [subelements]] references picked in the dialog.
    assembly = UtilsAssembly.getAssembly(joint)
    isAssembly = assembly.Type == "Assembly"

    if len(refs) >= 1:
        joint.Reference1 = refs[0]
    else:
        joint.Reference1 = None
        joint.Placement1 = App.Placement()

    if len(refs) >= 2:
        joint.Reference2 = refs[1]
    else:
        joint.Reference2 = None
        joint.Placement2 = App.Placement()

    # References changed: refresh Placement1/2 from the new references before any
    # pre-solve reads them (this recompute used to be driven by the proxy onChanged).
    joint.recompute()

    if len(refs) >= 2:
        ensureUnconnectedIsSecondRef(joint)

        if joint.usesPreSolve():
            preSolve(joint)
        elif joint.forbidsParallel():
            preventParallel(joint)

        if isAssembly:
            solveIfAllowed(assembly, True)
        else:
            joint.updateJCSPlacements()

    else:
        if isAssembly:
            assembly.undoSolve()
        undoPreSolve(joint)


def relabelForJointType(joint):
    # Replace the previous joint-type word in the (translated) label with the new
    # one, so a "Fixed" joint renamed to "Revolute" tracks its label.
    newType = joint.JointType
    tr_new_type = TranslatedJointTypes[JointTypes.index(newType)]
    for i, old_type_name in enumerate(JointTypes):
        if old_type_name == newType:
            continue
        tr_old_type = TranslatedJointTypes[i]
        if tr_old_type in joint.Label:
            joint.Label = joint.Label.replace(tr_old_type, tr_new_type)
            break


def flipOnePart(joint):
    matchJCS(joint, False, True)


def preSolve(joint, savePlc=True):
    # Put the part in the correct position to avoid a wrong placement by the solve.
    # We don't want to match the JCS perfectly, only in the current closest
    # direction (either matched or flipped).
    matchJCS(joint, savePlc)


def matchJCS(joint, savePlc=True, reverse=False):
    assembly = UtilsAssembly.getAssembly(joint)
    sameDir = UtilsAssembly.areJcsSameDir(joint)
    if reverse:
        sameDir = not sameDir

    part1 = UtilsAssembly.getMovingPart(joint.Reference1)
    part2 = UtilsAssembly.getMovingPart(joint.Reference2)

    if not part1 or not part2:
        return False

    isAssembly = assembly.Type == "Assembly"
    if isAssembly:
        joint.Suppressed = True
        part1Connected = assembly.isPartConnected(part1)
        part2Connected = assembly.isPartConnected(part2)
        joint.Suppressed = False
    else:
        part1Connected = True
        part2Connected = False

    moving_part = None
    moving_part_ref = None
    fixed_part_ref = None
    moving_placement = None
    fixed_placement = None

    if not part1Connected and part1:
        moving_part = part1
        moving_part_ref = joint.Reference1
        fixed_part_ref = joint.Reference2
        moving_placement = joint.Placement1
        fixed_placement = joint.Placement2
    elif not part2Connected and part2:
        moving_part = part2
        moving_part_ref = joint.Reference2
        fixed_part_ref = joint.Reference1
        moving_placement = joint.Placement2
        fixed_placement = joint.Placement1
    else:
        # Both parts are constrained, or something is invalid. Nothing to pre-solve.
        return False

    parts_to_move = [moving_part]
    if isAssembly:
        parts_to_move = parts_to_move + assembly.getDownstreamParts(moving_part, joint)

    if savePlc and activeTask is not None:
        activeTask.partsMovedByPresolved = {p: p.Placement for p in parts_to_move}

    moving_part_global_jcs = UtilsAssembly.getJcsGlobalPlc(moving_placement, moving_part_ref)
    fixed_part_global_jcs = UtilsAssembly.getJcsGlobalPlc(fixed_placement, fixed_part_ref)

    if not sameDir:
        moving_part_global_jcs = UtilsAssembly.flipPlacement(moving_part_global_jcs)

    transform_plc = fixed_part_global_jcs * moving_part_global_jcs.inverse()

    for part in parts_to_move:
        part.Placement = transform_plc * part.Placement

    return True


def undoPreSolve(joint):
    moved = getattr(activeTask, "partsMovedByPresolved", None) if activeTask else None
    if moved:
        for part, plc in moved.items():
            if part and hasattr(part, "Placement"):
                part.Placement = plc
        activeTask.partsMovedByPresolved = {}

        if joint.ViewObject:
            joint.ViewObject.Proxy.redrawJointPlacements(joint)


def preventParallel(joint):
    # Angle and perpendicular joints in the solver cannot handle the situation
    # where both JCS are parallel.
    parallel = UtilsAssembly.areJcsZParallel(joint)
    if not parallel:
        return

    assembly = UtilsAssembly.getAssembly(joint)

    part1 = UtilsAssembly.getMovingPart(joint.Reference1)
    part2 = UtilsAssembly.getMovingPart(joint.Reference2)

    isAssembly = assembly.Type == "Assembly"
    if isAssembly:
        part1ConnectedByJoint = assembly.isJointConnectingPartToGround(joint, "Reference1")
        part2ConnectedByJoint = assembly.isJointConnectingPartToGround(joint, "Reference2")
    else:
        part1ConnectedByJoint = False
        part2ConnectedByJoint = True

    if part2ConnectedByJoint:
        # Get the global JCS placement to find a suitable rotation axis (its own X-axis)
        globalJcsPlc = UtilsAssembly.getJcsGlobalPlc(joint.Placement2, joint.Reference2)
        # Transform the local X-axis vector (1,0,0) into the global coordinate system
        rotation_axis = globalJcsPlc.Rotation.multVec(App.Vector(1, 0, 0))

        part2.Placement = UtilsAssembly.applyRotationToPlacementAlongAxis(
            part2.Placement, 10, rotation_axis
        )

    elif part1ConnectedByJoint:
        # Get the global JCS placement to find a suitable rotation axis (its own X-axis)
        globalJcsPlc = UtilsAssembly.getJcsGlobalPlc(joint.Placement1, joint.Reference1)
        # Transform the local X-axis vector (1,0,0) into the global coordinate system
        rotation_axis = globalJcsPlc.Rotation.multVec(App.Vector(1, 0, 0))

        part1.Placement = UtilsAssembly.applyRotationToPlacementAlongAxis(
            part1.Placement, 10, rotation_axis
        )


def ensureUnconnectedIsSecondRef(joint):
    # Several joints do not solve properly if the part connected to ground is not
    # the first. See github.com/FreeCAD/FreeCAD/issues/29355. This swaps the
    # references if possible to avoid those issues.
    assembly = UtilsAssembly.getAssembly(joint)
    if not assembly or assembly.Type != "Assembly":
        return

    part1 = UtilsAssembly.getMovingPart(joint.Reference1)
    part2 = UtilsAssembly.getMovingPart(joint.Reference2)

    if not part1 or not part2:
        return

    # Temporarily suppress the joint to avoid evaluating it as a valid connection
    suppressed_backup = joint.Suppressed
    joint.Suppressed = True
    part1Connected = assembly.isPartConnected(part1)
    part2Connected = assembly.isPartConnected(part2)
    joint.Suppressed = suppressed_backup

    # If only part1 is unconnected and part2 is connected, swap references and related properties
    if not part1Connected and part2Connected:
        ref1 = joint.Reference1
        joint.Reference1 = joint.Reference2
        joint.Reference2 = ref1

        plc1 = joint.Placement1
        joint.Placement1 = joint.Placement2
        joint.Placement2 = plc1

        off1 = joint.Offset1
        joint.Offset1 = joint.Offset2
        joint.Offset2 = off1

        det1 = joint.Detach1
        joint.Detach1 = joint.Detach2
        joint.Detach2 = det1

        if activeTask and activeTask.joint == joint:
            activeTask.updateTaskboxFromJoint()


class ViewProviderJoint:
    def __init__(self, vobj):
        """Set this object to the proxy object of the actual view provider"""

        vobj.Proxy = self

        vobj.addExtension("Gui::ViewProviderSuppressibleExtensionPython")

    def attach(self, vobj):
        """Setup the scene sub-graph of the view provider, this method is mandatory"""
        self.app_obj = vobj.Object

        self.switch_JCS1 = SoSwitchMarker(vobj)
        self.switch_JCS2 = SoSwitchMarker(vobj)
        self.switch_JCS_preview = SoSwitchMarker(vobj)

        self.display_mode = coin.SoType.fromName("SoFCSelection").createInstance()
        self.display_mode.addChild(self.switch_JCS1)
        self.display_mode.addChild(self.switch_JCS2)
        self.display_mode.addChild(self.switch_JCS_preview)
        vobj.addDisplayMode(self.display_mode, "Wireframe")

    def updateData(self, joint, prop):
        """If a property of the handled feature has changed we have the chance to handle this here"""
        if prop == "Placement1" and hasattr(joint, "Reference1"):
            self.redrawJointPlacement(self.switch_JCS1, joint.Placement1, joint.Reference1)

        if prop == "Placement2" and hasattr(joint, "Reference2"):
            self.redrawJointPlacement(self.switch_JCS2, joint.Placement2, joint.Reference2)

    def redrawJointPlacements(self, joint):
        if not hasattr(joint, "Reference1") or not hasattr(joint, "Reference2"):
            return

        self.redrawJointPlacement(self.switch_JCS1, joint.Placement1, joint.Reference1)
        self.redrawJointPlacement(self.switch_JCS2, joint.Placement2, joint.Reference2)

    def redrawJointPlacement(self, jcs, plc, ref):
        if ref:
            jcs.whichChild = coin.SO_SWITCH_ALL
            self.setJCSPosition(jcs, plc, ref)
        else:
            jcs.whichChild = coin.SO_SWITCH_NONE

    def showPreviewJCS(self, visible, placement=None, ref=None):
        if visible:
            self.switch_JCS_preview.whichChild = coin.SO_SWITCH_ALL
            self.setJCSPosition(self.switch_JCS_preview, placement, ref)
        else:
            self.switch_JCS_preview.whichChild = coin.SO_SWITCH_NONE

    def setJCSPosition(self, jcs, plc, ref):
        assembly = UtilsAssembly.getAssembly(self.app_obj)
        if assembly and ref and plc:
            asm_global_plc = assembly.getGlobalPlacement()
            if asm_global_plc != App.Placement():
                global_plc = UtilsAssembly.getJcsGlobalPlc(plc, ref)
                plc = asm_global_plc.inverse() * global_plc
                ref = None
        jcs.set_marker_placement(plc, ref)

    def setPickableState(self, state: bool):
        """Set JCS selectable or unselectable in 3D view"""
        self.switch_JCS1.setPickableState(state)
        self.switch_JCS2.setPickableState(state)
        self.switch_JCS_preview.setPickableState(state)

    def getDisplayModes(self, obj):
        """Return a list of display modes."""
        modes = []
        modes.append("Wireframe")
        return modes

    def getDefaultDisplayMode(self):
        """Return the name of the default display mode. It must be defined in getDisplayModes."""
        return "Wireframe"

    def onChanged(self, vp, prop):
        """Here we can do something when a single property got changed"""
        # App.Console.PrintMessage("Change property: " + str(prop) + "\n")
        if prop == "color_X_axis" or prop == "color_Y_axis" or prop == "color_Z_axis":
            self.switch_JCS1.onChanged(vp, prop)
            self.switch_JCS2.onChanged(vp, prop)
            self.switch_JCS_preview.onChanged(vp, prop)

    def getIcon(self):
        if self.app_obj.JointType == "Fixed":
            return ":/icons/Assembly_CreateJointFixed.svg"
        elif self.app_obj.JointType == "Revolute":
            return ":/icons/Assembly_CreateJointRevolute.svg"
        elif self.app_obj.JointType == "Cylindrical":
            return ":/icons/Assembly_CreateJointCylindrical.svg"
        elif self.app_obj.JointType == "Slider":
            return ":/icons/Assembly_CreateJointSlider.svg"
        elif self.app_obj.JointType == "Ball":
            return ":/icons/Assembly_CreateJointBall.svg"
        elif self.app_obj.JointType == "Distance":
            return ":/icons/Assembly_CreateJointDistance.svg"
        elif self.app_obj.JointType == "Parallel":
            return ":/icons/Assembly_CreateJointParallel.svg"
        elif self.app_obj.JointType == "Perpendicular":
            return ":/icons/Assembly_CreateJointPerpendicular.svg"
        elif self.app_obj.JointType == "Angle":
            return ":/icons/Assembly_CreateJointAngle.svg"
        elif self.app_obj.JointType == "RackPinion":
            return ":/icons/Assembly_CreateJointRackPinion.svg"
        elif self.app_obj.JointType == "Screw":
            return ":/icons/Assembly_CreateJointScrew.svg"
        elif self.app_obj.JointType == "Gears":
            return ":/icons/Assembly_CreateJointGears.svg"
        elif self.app_obj.JointType == "Belt":
            return ":/icons/Assembly_CreateJointPulleys.svg"

        return ":/icons/Assembly_CreateJoint.svg"

    def getOverlayIcons(self):
        """
        Return a dictionary of overlay icons.
        Keys are positions from Gui.IconPosition.
        Values are the icon resource names.
        """

        overlays = {}

        assembly = UtilsAssembly.getAssembly(self.app_obj)
        # Assuming Reference1 corresponds to the first part link
        if hasattr(self.app_obj, "Reference1"):
            part = UtilsAssembly.getMovingPart(self.app_obj.Reference1)
            if part is not None and not assembly.isPartConnected(part):
                overlays[Gui.IconPosition.BottomLeft] = "Part_Detached"

        return overlays

    def dumps(self):
        """When saving the document this object gets stored using Python's json module.\
                Since we have some un-serializable parts here -- the Coin stuff -- we must define this method\
                to return a tuple of all serializable objects or None."""
        return None

    def loads(self, state):
        """When restoring the serialized object from document we have the chance to set some internals here.\
                Since no data were serialized nothing needs to be done here."""
        return None

    def doubleClicked(self, vobj):
        App.ActiveDocument.abortTransaction()  # Close the auto-transaction

        task = Gui.Control.activeTaskDialog()
        if task:
            task.reject()

        assembly = UtilsAssembly.getAssembly(vobj.Object)

        if assembly is None:
            return False

        if UtilsAssembly.activeAssembly() != assembly:
            vobj.Document.setEdit(assembly)

        panel = TaskAssemblyCreateJoint(0, vobj.Object)
        dialog = Gui.Control.showDialog(panel)
        if dialog is not None:
            dialog.setAutoCloseOnTransactionChange(True)
            dialog.setAutoCloseOnDeletedDocument(True)
            dialog.setDocumentName(App.ActiveDocument.Name)

        return True

    def canDelete(self, _obj):
        return True


################ Grounded Joint object #################


class ViewProviderGroundedJoint:
    def __init__(self, obj):
        """Set this object to the proxy object of the actual view provider"""
        obj.Proxy = self

    def attach(self, vobj):
        """Setup the scene sub-graph of the view provider, this method is mandatory"""
        app_obj = vobj.Object
        if app_obj is None:
            return
        groundedObj = app_obj.ObjectToGround
        if groundedObj is None:
            return

        self.scaleFactor = 3.0

        lockpadColorInt = Preferences.preferences().GetUnsigned("AssemblyConstraints", 0xCC333300)
        self.lockpadColor = coin.SoBaseColor()
        self.lockpadColor.rgb.setValue(UtilsAssembly.color_from_unsigned(lockpadColorInt))

        self.app_obj = vobj.Object
        app_doc = self.app_obj.Document
        self.gui_doc = Gui.getDocument(app_doc)

        # Create transformation (position and orientation)
        self.transform = coin.SoTransform()
        self.set_lock_position(groundedObj)

        # Create the 2D components of the lockpad: a square and two arcs
        self.square = self.create_square()

        # Creating the arcs (approximated with line segments)
        self.arc = self.create_arc(0, 4, 4, 0, 180)

        self.pick = coin.SoPickStyle()
        self.pick.style.setValue(coin.SoPickStyle.SHAPE_ON_TOP)

        # Assemble the parts into a scenegraph
        self.lockpadSeparator = coin.SoSeparator()
        self.lockpadSeparator.addChild(self.lockpadColor)
        self.lockpadSeparator.addChild(self.square)
        self.lockpadSeparator.addChild(self.arc)

        # Use SoVRMLBillboard to make sure the lockpad always faces the camera
        self.billboard = coin.SoVRMLBillboard()
        self.billboard.addChild(self.lockpadSeparator)

        self.scale = coin.SoType.fromName("SoShapeScale").createInstance()
        self.scale.setPart("shape", self.billboard)
        self.scale.scaleFactor = self.scaleFactor

        self.transformSeparator = coin.SoSeparator()
        self.transformSeparator.addChild(self.transform)
        self.transformSeparator.addChild(self.pick)
        self.transformSeparator.addChild(self.scale)

        # Attach the scenegraph to the view provider
        vobj.addDisplayMode(self.transformSeparator, "Wireframe")

    def create_square(self):
        coords = [
            (-5, -4, 0),
            (5, -4, 0),
            (5, 4, 0),
            (-5, 4, 0),
        ]
        vertices = coin.SoCoordinate3()
        vertices.point.setValues(0, 4, coords)

        squareFace = coin.SoFaceSet()
        squareFace.numVertices.setValue(4)

        square = coin.SoAnnotation()
        square.addChild(vertices)
        square.addChild(squareFace)

        return square

    def create_arc(self, centerX, centerY, radius, startAngle, endAngle):
        coords = []
        for angle in range(
            startAngle, endAngle + 1, 5
        ):  # Increment can be adjusted for smoother arcs
            rad = math.radians(angle)
            x = centerX + math.cos(rad) * radius
            y = centerY + math.sin(rad) * radius
            coords.append((x, y, 0))

        radius = radius * 0.7
        for angle in range(endAngle + 1, startAngle - 1, -5):  # Step backward
            rad = math.radians(angle)
            x = centerX + math.cos(rad) * radius
            y = centerY + math.sin(rad) * radius
            coords.append((x, y, 0))

        vertices = coin.SoCoordinate3()
        vertices.point.setValues(0, len(coords), coords)

        shapeHints = coin.SoShapeHints()
        shapeHints.faceType = coin.SoShapeHints.UNKNOWN_FACE_TYPE

        line = coin.SoFaceSet()
        line.numVertices.setValue(len(coords))

        arc = coin.SoAnnotation()
        arc.addChild(shapeHints)
        arc.addChild(vertices)
        arc.addChild(line)

        return arc

    def set_lock_position(self, groundedObj):
        bBox = groundedObj.ViewObject.getBoundingBox()
        if bBox.isValid():
            pos = bBox.Center
        else:
            pos = groundedObj.Placement.Base

        self.transform.translation.setValue(pos.x, pos.y, pos.z)

    def updateData(self, fp, prop):
        """If a property of the handled feature has changed we have the chance to handle this here"""
        # fp is the handled feature, prop is the name of the property that has changed

        if prop == "Placement" and fp.ObjectToGround:
            self.set_lock_position(fp.ObjectToGround)

    def getDisplayModes(self, obj):
        """Return a list of display modes."""
        modes = ["Wireframe"]
        return modes

    def getDefaultDisplayMode(self):
        """Return the name of the default display mode. It must be defined in getDisplayModes."""
        return "Wireframe"

    def onChanged(self, vp, prop):
        """Here we can do something when a single property got changed"""
        # App.Console.PrintMessage("Change property: " + str(prop) + "\n")
        pass

    def getIcon(self):
        return ":/icons/Assembly_ToggleGrounded.svg"

    def dumps(self):
        """When saving the document this object gets stored using Python's json module.\
                Since we have some un-serializable parts here -- the Coin stuff -- we must define this method\
                to return a tuple of all serializable objects or None."""
        return None

    def loads(self, state):
        """When restoring the serialized object from document we have the chance to set some internals here.\
                Since no data were serialized nothing needs to be done here."""
        return None

    def canDelete(self, _obj):
        return True


class MakeJointSelGate:
    def __init__(self, taskbox, assembly):
        self.taskbox = taskbox
        self.assembly = assembly

    def allow(self, doc, obj, sub):
        if not sub:
            return False

        objs_names, element_name = UtilsAssembly.getObjsNamesAndElement(obj.Name, sub)

        if self.assembly.Name not in objs_names:
            # Only objects within the assembly.
            return False

        ref = [obj, [sub]]
        sel_obj = UtilsAssembly.getObject(ref)

        if UtilsAssembly.isLink(sel_obj):
            linked = sel_obj.getLinkedObject()
            if linked == sel_obj:
                return True  # We accept empty links
            sel_obj = linked

        if sel_obj.isDerivedFrom("Part::Feature") or sel_obj.isDerivedFrom("App::Part"):
            return True

        if sel_obj.isDerivedFrom("App::LocalCoordinateSystem") or sel_obj.isDerivedFrom(
            "App::DatumElement"
        ):
            datum = sel_obj
            if datum.isDerivedFrom("App::DatumElement"):
                parent = datum.getParent()
                if parent.isDerivedFrom("App::LocalCoordinateSystem"):
                    datum = parent

            if self.assembly.hasObject(datum) and hasattr(datum, "MapMode"):
                # accept only datum that are not attached
                return datum.MapMode == "Deactivated"

            return True

        return False


activeTask = None


class TaskAssemblyCreateJoint(QtCore.QObject):
    def __init__(self, jointTypeIndex, jointObj=None, subclass=False):
        super().__init__()

        global activeTask
        activeTask = self
        self.blockOffsetRotation = False
        # Parts moved by the pre-solve, kept so a reference change can undo them.
        self.partsMovedByPresolved = {}

        self.assembly = UtilsAssembly.activeAssembly()
        if not self.assembly:
            self.assembly = UtilsAssembly.activePart()
            self.activeType = "Part"
        else:
            self.activeType = "Assembly"
            self.assembly.ensureIdentityPlacements()

        self.doc = self.assembly.Document
        self.gui_doc = Gui.getDocument(self.doc)

        self.view = self.gui_doc.activeView()

        if not self.assembly or not self.view or not self.doc:
            return

        if self.activeType == "Assembly":
            self.assembly.ViewObject.MoveOnlyPreselected = True
            self.assembly.ViewObject.MoveInCommand = False

        # Create a top-level container widget for subclasses of TaskAssemblyCreateJoint
        self.form = QtWidgets.QWidget()

        # Load the joint creation UI and parent it to `self.form`
        self.jForm = Gui.PySideUic.loadUi(":/panels/TaskAssemblyCreateJoint.ui", self.form)

        # Create a layout for `self.form` and add `self.jForm` to it
        layout = QtWidgets.QVBoxLayout(self.form)
        if not subclass:
            layout.setContentsMargins(0, 0, 0, 0)
            layout.setSpacing(0)
        layout.addWidget(self.jForm)

        self.isolate_modes = ["Transparent", "Wireframe", "Hidden", "Disabled"]
        self.jForm.isolateType.addItems(
            [translate("Assembly", mode) for mode in self.isolate_modes]
        )
        self.jForm.isolateType.currentIndexChanged.connect(self.updateIsolation)

        if self.activeType == "Part":
            self.jForm.setWindowTitle("Match parts")
            self.jForm.jointType.hide()
            self.jForm.isolateType.hide()

        self.jForm.jointType.addItems(TranslatedJointTypes)

        self.jForm.jointType.setCurrentIndex(jointTypeIndex)
        self.jType = JointTypes[self.jForm.jointType.currentIndex()]
        self.jForm.jointType.currentIndexChanged.connect(self.onJointTypeChanged)

        if jointObj:
            Gui.Selection.clearSelection()
            self.creating = False
            self.joint = jointObj
            self.jointName = jointObj.Label
            Gui.ActiveDocument.openCommand("Edit " + self.jointName + " Joint")

            self.updateTaskboxFromJoint()
            self.visibilityBackup = self.joint.Visibility
            self.joint.Visibility = True

        else:
            self.creating = True
            self.jointName = self.jForm.jointType.currentText().replace(" ", "")
            if self.activeType == "Part":
                Gui.ActiveDocument.openCommand("Transform")
            else:
                Gui.ActiveDocument.openCommand("Create " + self.jointName + " Joint")

            self.refs = []
            self.presel_ref = None

            self.createJointObject()
            self.visibilityBackup = False

        self.jForm.angleSpinbox.valueChanged.connect(self.onAngleChanged)
        self.jForm.distanceSpinbox.valueChanged.connect(self.onDistanceChanged)
        self.jForm.distanceSpinbox2.valueChanged.connect(self.onDistance2Changed)
        self.jForm.offsetSpinbox.valueChanged.connect(self.onOffsetChanged)
        self.jForm.rotationSpinbox.valueChanged.connect(self.onRotationChanged)
        bind = Gui.ExpressionBinding(self.jForm.angleSpinbox).bind(self.joint, "Angle")
        bind = Gui.ExpressionBinding(self.jForm.distanceSpinbox).bind(self.joint, "Distance")
        bind = Gui.ExpressionBinding(self.jForm.distanceSpinbox2).bind(self.joint, "Distance2")
        bind = Gui.ExpressionBinding(self.jForm.offsetSpinbox).bind(self.joint, "Offset2.Base.z")
        bind = Gui.ExpressionBinding(self.jForm.rotationSpinbox).bind(
            self.joint, "Offset2.Rotation.Yaw"
        )

        self.jForm.reverseRotCheckbox.setChecked(self.jType == "Gears")
        self.jForm.reverseRotCheckbox.stateChanged.connect(self.reverseRotToggled)

        self.jForm.advancedOffsetCheckbox.stateChanged.connect(self.advancedOffsetToggled)

        self.jForm.offset1Button.clicked.connect(self.onOffset1Clicked)
        self.jForm.offset2Button.clicked.connect(self.onOffset2Clicked)
        self.jForm.PushButtonReverse.clicked.connect(self.onReverseClicked)

        self.jForm.limitCheckbox1.stateChanged.connect(self.adaptUi)
        self.jForm.limitCheckbox2.stateChanged.connect(self.adaptUi)
        self.jForm.limitCheckbox3.stateChanged.connect(self.adaptUi)
        self.jForm.limitCheckbox4.stateChanged.connect(self.adaptUi)

        self.jForm.limitLenMinSpinbox.valueChanged.connect(self.onLimitLenMinChanged)
        self.jForm.limitLenMaxSpinbox.valueChanged.connect(self.onLimitLenMaxChanged)
        self.jForm.limitRotMinSpinbox.valueChanged.connect(self.onLimitRotMinChanged)
        self.jForm.limitRotMaxSpinbox.valueChanged.connect(self.onLimitRotMaxChanged)
        bind = Gui.ExpressionBinding(self.jForm.limitLenMinSpinbox).bind(self.joint, "LengthMin")
        bind = Gui.ExpressionBinding(self.jForm.limitLenMaxSpinbox).bind(self.joint, "LengthMax")
        bind = Gui.ExpressionBinding(self.jForm.limitRotMinSpinbox).bind(self.joint, "AngleMin")
        bind = Gui.ExpressionBinding(self.jForm.limitRotMaxSpinbox).bind(self.joint, "AngleMax")

        self.adaptUi()

        if self.creating:
            # This has to be after adaptUi so that properties default values are adapted
            # if needed. For instance for gears adaptUi will prevent radii from being 0
            # before handleInitialSelection tries to solve.
            self.handleInitialSelection()

        UtilsAssembly.setJointsPickableState(self.doc, False)

        Gui.Selection.addSelectionGate(
            MakeJointSelGate(self, self.assembly), Gui.Selection.ResolveMode.NoResolve
        )
        Gui.Selection.addObserver(self, Gui.Selection.ResolveMode.NoResolve)
        Gui.Selection.setSelectionStyle(Gui.Selection.SelectionStyle.GreedySelection)

        self.callbackMove = self.view.addEventCallback("SoLocation2Event", self.moveMouse)
        self.callbackKey = self.view.addEventCallback("SoKeyboardEvent", self.KeyboardEvent)

        self.jForm.featureList.installEventFilter(self)

        self.createDeleteAction()

        self.addition_rejected = False

    def accept(self):
        if len(self.refs) != 2:
            App.Console.PrintWarning(
                translate("Assembly", "Select 2 elements from 2 separate parts")
            )
            return False

        self.deactivate()

        if self.activeType == "Assembly":
            self.joint.Visibility = self.visibilityBackup
        else:
            self.joint.Document.removeObject(self.joint.Name)

        cmds = UtilsAssembly.generatePropertySettings(self.joint)
        Gui.doCommand(cmds)

        self.assembly.recompute(True)

        Gui.ActiveDocument.commitCommand()
        return True

    def reject(self):
        self.deactivate()
        Gui.ActiveDocument.abortCommand()
        self.assembly.recompute(True)
        return True

    def autoClosedOnTransactionChange(self):
        self.reject()

    def autoClosedOnDeletedDocument(self):
        global activeTask
        activeTask = None
        Gui.Selection.removeSelectionGate()
        Gui.Selection.removeObserver(self)
        Gui.Selection.setSelectionStyle(Gui.Selection.SelectionStyle.NormalSelection)
        App.ActiveDocument.abortTransaction()

    def deactivate(self):
        global activeTask
        activeTask = None

        if self.activeType == "Assembly":
            self.assembly.clearUndo()
            self.assembly.ViewObject.MoveOnlyPreselected = False
            self.assembly.ViewObject.MoveInCommand = True

        Gui.Selection.removeSelectionGate()
        Gui.Selection.removeObserver(self)
        Gui.Selection.setSelectionStyle(Gui.Selection.SelectionStyle.NormalSelection)
        Gui.Selection.clearSelection()
        self.view.removeEventCallback("SoLocation2Event", self.callbackMove)
        self.view.removeEventCallback("SoKeyboardEvent", self.callbackKey)
        UtilsAssembly.setJointsPickableState(self.doc, True)
        if Gui.Control.activeDialog():
            Gui.Control.closeDialog()

    def handleInitialSelection(self):
        selection = Gui.Selection.getSelectionEx("*", 0)
        if not selection:
            return
        for sel in selection:
            # If you select 2 solids (bodies for example) within an assembly.
            # There'll be a single sel but 2 SubElementNames.

            if not sel.SubElementNames:
                # no subnames, so its a root assembly itself that is selected.
                Gui.Selection.removeSelection(sel.Object)
                continue

            for sub_name in sel.SubElementNames:
                # We add sub_name twice because the joints references have element name + vertex name
                # and in the case of initial selection, both are the same.

                moving_part, new_sub = UtilsAssembly.getComponentReference(
                    self.assembly, sel.Object, sub_name
                )
                if not moving_part:
                    break

                # Construct the reference using the Component as the root
                ref = [moving_part, [new_sub, new_sub]]

                # Only objects within the assembly.
                if moving_part is None:
                    Gui.Selection.removeSelection(sel.Object, sub_name)
                    continue

                if len(self.refs) == 1 and moving_part == self.getMovingPart(self.refs[0]):
                    # do not select several feature of the same object.
                    self.refs.clear()
                    Gui.Selection.clearSelection()
                    return

                self.refs.append(ref)

        # do not accept initial selection if we don't have 2 selected features
        if len(self.refs) != 2:
            self.refs.clear()
            Gui.Selection.clearSelection()
        else:
            self.updateJoint()

    def createJointObject(self):
        type_index = self.jForm.jointType.currentIndex()

        if self.activeType == "Part":
            self.joint = self.assembly.newObject("Assembly::Joint", "Temporary joint")
        else:
            joint_group = UtilsAssembly.getJointGroup(self.assembly)
            self.joint = joint_group.newObject("Assembly::Joint", "Joint")
            self.joint.Label = self.jointName
            joint_group.purgeTouched()
            self.assembly.purgeTouched()

        # The typed Assembly::Joint owns its properties, content scope and the
        # SuppressibleExtension; creation only picks the initial kind and clears
        # the connectors (formerly the Python Joint.__init__).
        self.joint.JointType = JointTypes[type_index]
        setJointConnectors(self.joint, [])
        ViewProviderJoint(self.joint.ViewObject)
        self.joint.purgeTouched()

    def onJointTypeChanged(self, index):
        self.jType = JointTypes[self.jForm.jointType.currentIndex()]
        self.joint.JointType = self.jType
        relabelForJointType(self.joint)
        self.adaptUi()

    def refsAreSet(self):
        joint = self.joint
        return (
            hasattr(joint, "Reference1")
            and hasattr(joint, "Reference2")
            and joint.Reference1 is not None
            and joint.Reference2 is not None
        )

    def solveAfterOffsetChange(self):
        # Re-solve after an offset edit (previously driven by the proxy onChanged
        # reacting to Offset1/Offset2).
        if not self.refsAreSet():
            return

        joint = self.joint
        joint.updateJCSPlacements()
        presolved = joint.usesPreSolve() and preSolve(joint, False)
        assembly = UtilsAssembly.getAssembly(joint)
        if assembly.Type == "Assembly" and not presolved:
            solveIfAllowed(assembly)
        else:
            joint.updateJCSPlacements()

    def onAngleChanged(self, quantity):
        self.joint.Angle = self.jForm.angleSpinbox.property("rawValue")
        if self.joint.JointType == "Angle" and self.refsAreSet():
            if self.joint.Angle != 0.0:
                preventParallel(self.joint)
            solveIfAllowed(UtilsAssembly.getAssembly(self.joint))

    def onDistanceChanged(self, quantity):
        self.joint.Distance = self.jForm.distanceSpinbox.property("rawValue")
        if self.joint.JointType == "Distance" and self.refsAreSet():
            solveIfAllowed(UtilsAssembly.getAssembly(self.joint))

    def onDistance2Changed(self, quantity):
        self.joint.Distance2 = self.jForm.distanceSpinbox2.property("rawValue")

    def onOffsetChanged(self, quantity):
        if self.blockOffsetRotation:
            return

        self.joint.Offset2.Base = App.Vector(0, 0, self.jForm.offsetSpinbox.property("rawValue"))
        self.solveAfterOffsetChange()

    def onRotationChanged(self, quantity):
        if self.blockOffsetRotation:
            return

        yaw = self.jForm.rotationSpinbox.property("rawValue")
        ypr = self.joint.Offset2.Rotation.getYawPitchRoll()
        self.joint.Offset2.Rotation.setYawPitchRoll(yaw, ypr[1], ypr[2])
        self.solveAfterOffsetChange()

    def onLimitLenMinChanged(self, quantity):
        if self.jForm.limitCheckbox1.isChecked():
            self.joint.LengthMin = self.jForm.limitLenMinSpinbox.property("rawValue")

    def onLimitLenMaxChanged(self, quantity):
        if self.jForm.limitCheckbox2.isChecked():
            self.joint.LengthMax = self.jForm.limitLenMaxSpinbox.property("rawValue")

    def onLimitRotMinChanged(self, quantity):
        if self.jForm.limitCheckbox3.isChecked():
            self.joint.AngleMin = self.jForm.limitRotMinSpinbox.property("rawValue")

    def onLimitRotMaxChanged(self, quantity):
        if self.jForm.limitCheckbox4.isChecked():
            self.joint.AngleMax = self.jForm.limitRotMaxSpinbox.property("rawValue")

    def onReverseClicked(self):
        flipOnePart(self.joint)

    def reverseRotToggled(self, val):
        if val:
            self.jForm.jointType.setCurrentIndex(JointTypes.index("Gears"))
        else:
            self.jForm.jointType.setCurrentIndex(JointTypes.index("Belt"))

    def adaptUi(self):
        jType = self.jType

        needAngle = jType in JointUsingAngle
        self.jForm.angleLabel.setVisible(needAngle)
        self.jForm.angleSpinbox.setVisible(needAngle)

        needDistance = jType in JointUsingDistance
        self.jForm.distanceLabel.setVisible(needDistance)
        self.jForm.distanceSpinbox.setVisible(needDistance)
        if needDistance:
            if jType == "Distance":
                self.jForm.distanceLabel.setText(translate("Assembly", "Distance"))
            elif jType == "Gears" or jType == "Belt":
                self.jForm.distanceLabel.setText(translate("Assembly", "Radius 1"))
            elif jType == "Screw":
                self.jForm.distanceLabel.setText(translate("Assembly", "Thread pitch"))
            else:
                self.jForm.distanceLabel.setText(translate("Assembly", "Pitch radius"))

        needDistance2 = jType in JointUsingDistance2
        self.jForm.distanceLabel2.setVisible(needDistance2)
        self.jForm.distanceSpinbox2.setVisible(needDistance2)
        self.jForm.reverseRotCheckbox.setVisible(needDistance2)

        if jType in JointNoNegativeDistance:
            # Setting minimum to 0.01 to prevent 0 and negative values
            self.jForm.distanceSpinbox.setProperty("minimum", 1e-7)
            if self.jForm.distanceSpinbox.property("rawValue") == 0.0:
                self.jForm.distanceSpinbox.setProperty("rawValue", 1.0)

            if jType == "Gears" or jType == "Belt":
                self.jForm.distanceSpinbox2.setProperty("minimum", 1e-7)
                if self.jForm.distanceSpinbox2.property("rawValue") == 0.0:
                    self.jForm.distanceSpinbox2.setProperty("rawValue", 1.0)
        else:
            self.jForm.distanceSpinbox.setProperty("minimum", float("-inf"))
            self.jForm.distanceSpinbox2.setProperty("minimum", float("-inf"))

        advancedOffset = self.jForm.advancedOffsetCheckbox.isChecked()
        needOffset = jType in JointUsingOffset
        needRotation = jType in JointUsingRotation
        self.jForm.offset1Label.setVisible(advancedOffset)
        self.jForm.offset2Label.setVisible(advancedOffset)
        self.jForm.offset1Button.setVisible(advancedOffset)
        self.jForm.offset2Button.setVisible(advancedOffset)
        self.jForm.offsetLabel.setVisible(not advancedOffset and needOffset)
        self.jForm.offsetSpinbox.setVisible(not advancedOffset and needOffset)
        self.jForm.rotationLabel.setVisible(not advancedOffset and needRotation)
        self.jForm.rotationSpinbox.setVisible(not advancedOffset and needRotation)

        self.jForm.PushButtonReverse.setVisible(jType in JointUsingReverse)

        needLengthLimits = jType in JointUsingLimitLength
        needAngleLimits = jType in JointUsingLimitAngle
        needLimits = needLengthLimits or needAngleLimits
        self.jForm.groupBox_limits.setVisible(needLimits)

        if needLimits:
            self.joint.EnableLengthMin = self.jForm.limitCheckbox1.isChecked()
            self.joint.EnableLengthMax = self.jForm.limitCheckbox2.isChecked()
            self.joint.EnableAngleMin = self.jForm.limitCheckbox3.isChecked()
            self.joint.EnableAngleMax = self.jForm.limitCheckbox4.isChecked()

            self.jForm.limitCheckbox1.setVisible(needLengthLimits)
            self.jForm.limitCheckbox2.setVisible(needLengthLimits)
            self.jForm.limitLenMinSpinbox.setVisible(needLengthLimits)
            self.jForm.limitLenMaxSpinbox.setVisible(needLengthLimits)

            self.jForm.limitCheckbox3.setVisible(needAngleLimits)
            self.jForm.limitCheckbox4.setVisible(needAngleLimits)
            self.jForm.limitRotMinSpinbox.setVisible(needAngleLimits)
            self.jForm.limitRotMaxSpinbox.setVisible(needAngleLimits)

            if needLengthLimits:
                self.jForm.limitLenMinSpinbox.setEnabled(self.joint.EnableLengthMin)
                self.jForm.limitLenMaxSpinbox.setEnabled(self.joint.EnableLengthMax)
                self.onLimitLenMinChanged(0)  # dummy value
                self.onLimitLenMaxChanged(0)

            if needAngleLimits:
                self.jForm.limitRotMinSpinbox.setEnabled(self.joint.EnableAngleMin)
                self.jForm.limitRotMaxSpinbox.setEnabled(self.joint.EnableAngleMax)
                self.onLimitRotMinChanged(0)
                self.onLimitRotMaxChanged(0)

        self.updateOffsetWidgets()

    def updateOffsetWidgets(self):
        # Makes sure the values in both the simplified and advanced tabs are sync.
        pos = self.joint.Offset1.Base
        self.jForm.offset1Button.setText(f"({pos.x}, {pos.y}, {pos.z})")

        pos = self.joint.Offset2.Base
        self.jForm.offset2Button.setText(f"({pos.x}, {pos.y}, {pos.z})")

        self.blockOffsetRotation = True
        self.jForm.offsetSpinbox.setProperty("rawValue", pos.z)
        self.jForm.rotationSpinbox.setProperty(
            "rawValue", self.joint.Offset2.Rotation.getYawPitchRoll()[0]
        )
        self.blockOffsetRotation = False

    def advancedOffsetToggled(self, on):
        self.adaptUi()
        self.updateOffsetWidgets()

    def onOffset1Clicked(self):
        UtilsAssembly.openEditingPlacementDialog(self.joint, "Offset1")
        self.solveAfterOffsetChange()
        self.updateOffsetWidgets()

    def onOffset2Clicked(self):
        UtilsAssembly.openEditingPlacementDialog(self.joint, "Offset2")
        self.solveAfterOffsetChange()
        self.updateOffsetWidgets()

    def updateIsolation(self):
        """Isolates the two selected components or clears isolation."""

        if self.activeType != "Assembly":
            return

        isolate_mode = self.jForm.isolateType.currentIndex()

        assembly_vobj = self.assembly.ViewObject

        # If "Disabled" is selected, clear any active isolation and stop.
        if isolate_mode == 3:
            assembly_vobj.clearIsolate()
            return

        if len(self.refs) == 2:
            try:
                # Use a set to handle cases where both refs point to the same object
                parts_to_isolate = {
                    self.getMovingPart(self.refs[0]),
                    self.getMovingPart(self.refs[1]),
                }
                assembly_vobj.isolateComponents(list(parts_to_isolate), isolate_mode)
            except Exception as e:
                App.Console.PrintWarning(f"Could not update isolation: {e}\n")
                assembly_vobj.clearIsolate()
        else:
            assembly_vobj.clearIsolate()

    def updateTaskboxFromJoint(self):
        self.refs = []
        self.presel_ref = None

        ref1 = self.joint.Reference1
        ref2 = self.joint.Reference2

        if UtilsAssembly.isRefValid(ref1, 2):
            self.refs.append(ref1)
            sub1 = UtilsAssembly.addTipNameToSub(ref1)
            Gui.Selection.addSelection(ref1[0].Document.Name, ref1[0].Name, sub1)

        if UtilsAssembly.isRefValid(ref2, 2):
            self.refs.append(ref2)
            sub2 = UtilsAssembly.addTipNameToSub(ref2)
            Gui.Selection.addSelection(ref2[0].Document.Name, ref2[0].Name, sub2)

        self.jForm.angleSpinbox.setProperty("rawValue", self.joint.Angle.Value)
        self.jForm.distanceSpinbox.setProperty("rawValue", self.joint.Distance.Value)
        self.jForm.distanceSpinbox2.setProperty("rawValue", self.joint.Distance2.Value)
        self.jForm.offsetSpinbox.setProperty("rawValue", self.joint.Offset2.Base.z)
        self.jForm.rotationSpinbox.setProperty(
            "rawValue", self.joint.Offset2.Rotation.getYawPitchRoll()[0]
        )

        self.jForm.limitCheckbox1.setChecked(self.joint.EnableLengthMin)
        self.jForm.limitCheckbox2.setChecked(self.joint.EnableLengthMax)
        self.jForm.limitCheckbox3.setChecked(self.joint.EnableAngleMin)
        self.jForm.limitCheckbox4.setChecked(self.joint.EnableAngleMax)
        self.jForm.limitLenMinSpinbox.setProperty("rawValue", self.joint.LengthMin.Value)
        self.jForm.limitLenMaxSpinbox.setProperty("rawValue", self.joint.LengthMax.Value)
        self.jForm.limitRotMinSpinbox.setProperty("rawValue", self.joint.AngleMin.Value)
        self.jForm.limitRotMaxSpinbox.setProperty("rawValue", self.joint.AngleMax.Value)

        self.jForm.jointType.setCurrentIndex(JointTypes.index(self.joint.JointType))
        self.updateJointList()
        self.updateIsolation()

    def updateJoint(self):
        # First we build the listwidget
        self.updateJointList()

        # Then we pass the new list to the joint object
        setJointConnectors(self.joint, self.refs)

        self.updateIsolation()

    def updateJointList(self):
        self.jForm.featureList.clear()
        simplified_names = []
        for ref in self.refs:

            sname = UtilsAssembly.getObject(ref).Label

            element_name = UtilsAssembly.getElementName(ref[1][0])
            if element_name != "":
                sname = sname + "." + element_name
            simplified_names.append(sname)
        self.jForm.featureList.addItems(simplified_names)

    def updateLimits(self):
        needLengthLimits = self.jType in JointUsingLimitLength
        needAngleLimits = self.jType in JointUsingLimitAngle
        if needLengthLimits:
            distance = UtilsAssembly.getJointDistance(self.joint)
            if (
                not self.jForm.limitCheckbox1.isChecked()
                and self.jForm.limitLenMinSpinbox.property("expression") == ""
            ):
                self.jForm.limitLenMinSpinbox.setProperty("rawValue", distance)
            if (
                not self.jForm.limitCheckbox2.isChecked()
                and self.jForm.limitLenMaxSpinbox.property("expression") == ""
            ):
                self.jForm.limitLenMaxSpinbox.setProperty("rawValue", distance)

        if needAngleLimits:
            angle = UtilsAssembly.getJointXYAngle(self.joint) / math.pi * 180
            if (
                not self.jForm.limitCheckbox3.isChecked()
                and self.jForm.limitRotMinSpinbox.property("expression") == ""
            ):
                self.jForm.limitRotMinSpinbox.setProperty("rawValue", angle)
            if (
                not self.jForm.limitCheckbox4.isChecked()
                and self.jForm.limitRotMaxSpinbox.property("expression") == ""
            ):
                self.jForm.limitRotMaxSpinbox.setProperty("rawValue", angle)

    def moveMouse(self, info):
        if len(self.refs) >= 2 or (
            len(self.refs) == 1
            and (
                not self.presel_ref
                or self.getMovingPart(self.refs[0]) == self.getMovingPart(self.presel_ref)
            )
        ):
            self.joint.ViewObject.Proxy.showPreviewJCS(False)
            if len(self.refs) >= 2:
                self.updateLimits()
            return

        cursor_pos = self.view.getCursorPos()
        cursor_info = self.view.getObjectInfo(cursor_pos)
        # cursor_info example  {'x': 41.515, 'y': 7.449, 'z': 16.861, 'ParentObject': <Part object>, 'SubName': 'Body002.Pad.Face5', 'Document': 'part3', 'Object': 'Pad', 'Component': 'Face5'}

        if (
            not cursor_info
            or not self.presel_ref
            # or cursor_info["SubName"] != self.presel_ref["sub_name"]
            # Removed because they are not equal when hovering a line endpoints.
            # But we don't actually need to test because if there's no preselection then not cursor is None
        ):
            self.joint.ViewObject.Proxy.showPreviewJCS(False)
            return

        ref = self.presel_ref

        # newPos = self.view.getPoint(*info["Position"]) is not OK: it's not pos on the object but on the focal plane
        newPos = App.Vector(cursor_info["x"], cursor_info["y"], cursor_info["z"])
        vertex_name = UtilsAssembly.findElementClosestVertex(ref, newPos)

        ref = UtilsAssembly.addVertexToReference(ref, vertex_name)

        # Preview uses an arbitrary hover reference (not the joint's stored ones),
        # so it computes the frame directly rather than via joint.updateJCSPlacements.
        placement = AssemblyApp.findPlacement(ref, self.joint.ignoresVertex())
        placement = placement * self.joint.Offset1
        self.joint.ViewObject.Proxy.showPreviewJCS(True, placement, ref)
        self.previewJCSVisible = True

    # 3D view keyboard handler
    def KeyboardEvent(self, info):
        if info["State"] == "UP" and info["Key"] == "ESCAPE":
            self.reject()

        if info["State"] == "UP" and info["Key"] == "RETURN":
            self.accept()

    def _removeSelectedItems(self, selected_indexes):
        for index in selected_indexes:
            row = index.row()
            if row < len(self.refs):
                ref = self.refs[row]
                sub = UtilsAssembly.addTipNameToSub(ref)
                Gui.Selection.removeSelection(ref[0], sub)
            else:
                print(f"Row {row} is out of bounds for refs (length: {len(self.refs)})")

    def eventFilter(self, watched, event):
        if self.jForm is not None and watched == self.jForm.featureList:
            if event.type() == QtCore.QEvent.ShortcutOverride:
                if (
                    hasattr(self, "deleteAction")
                    and self.deleteAction.shortcut().matches(event.key())
                    != QtGui.QKeySequence.NoMatch
                ):
                    event.accept()
                    return True
                return False

            elif event.type() == QtCore.QEvent.KeyPress:
                if (
                    hasattr(self, "deleteAction")
                    and self.deleteAction.shortcut().matches(event.key())
                    != QtGui.QKeySequence.NoMatch
                ):
                    self.deleteAction.trigger()
                    return True  # Consume the event

        return super().eventFilter(watched, event)

    def createDeleteAction(self):
        """Create delete action with shortcut"""
        try:
            delete_sequence = Gui.QtTools.deleteKeySequence()
        except AttributeError:
            # fallback to standard key if there is no sequence defined
            delete_sequence = QtGui.QKeySequence(QtCore.Qt.Key_Delete)

        self.deleteAction = QtGui.QAction("Remove", self.jForm)
        self.deleteAction.setShortcut(delete_sequence)

        self.deleteAction.setIcon(
            QtWidgets.QApplication.style().standardIcon(QtWidgets.QStyle.SP_DialogCancelButton)
        )

        self.deleteAction.setShortcutVisibleInContextMenu(True)

        self.jForm.featureList.addAction(self.deleteAction)
        self.jForm.featureList.setContextMenuPolicy(QtCore.Qt.ActionsContextMenu)

        self.deleteAction.triggered.connect(self.deleteSelectedItems)

    def deleteSelectedItems(self):
        """Delete selected items from the feature list - same logic as Delete key in ev filter"""
        selected_indexes = self.jForm.featureList.selectedIndexes()
        self._removeSelectedItems(selected_indexes)

    def getMovingPart(self, ref):
        return UtilsAssembly.getMovingPart(ref)

    # selectionObserver stuff
    def addSelection(self, doc_name, obj_name, sub_name, mousePos):
        rootObj = App.getDocument(doc_name).getObject(obj_name)

        # We do not need the full TNP string like :"Part.Body.Pad.;#a:1;:G0;XTR;:Hc94:8,F.Face6"
        # instead we need : "Part.Body.Pad.Face6"
        resolved = rootObj.resolveSubElement(sub_name, True)
        sub_name = resolved[2]

        sub_name = UtilsAssembly.fixBodyExtraFeatureInSub(doc_name, sub_name)

        comp, new_sub = UtilsAssembly.getComponentReference(self.assembly, rootObj, sub_name)
        if not comp:
            # Selection was not valid (not inside assembly or logic failed)
            Gui.Selection.removeSelection(doc_name, obj_name, sub_name)
            return

        # Construct the reference using the Component as the root
        ref = [comp, [new_sub]]

        moving_part = self.getMovingPart(ref)

        # Check if the addition is acceptable (we are not doing this in selection gate to let user move objects)
        acceptable = True
        if len(self.refs) >= 2:
            # No more than 2 elements can be selected for basic joints.
            acceptable = False

        for reference in self.refs:
            sel_moving_part = self.getMovingPart(reference)
            if sel_moving_part == moving_part:
                # Can't join a solid to itself. So the user need to select 2 different parts.
                acceptable = False

        if not acceptable:
            self.addition_rejected = True
            Gui.Selection.removeSelection(doc_name, obj_name, sub_name)
            return

        # Selection is acceptable so add it

        mousePos = App.Vector(mousePos[0], mousePos[1], mousePos[2])
        vertex_name = UtilsAssembly.findElementClosestVertex(ref, mousePos)

        # add the vertex name to the reference
        ref = UtilsAssembly.addVertexToReference(ref, vertex_name)

        self.refs.append(ref)
        self.updateJoint()

        # We hide the preview JCS if we just added to the selection
        self.joint.ViewObject.Proxy.showPreviewJCS(False)

    def removeSelection(self, doc_name, obj_name, sub_name, mousePos=None):
        if self.addition_rejected:
            self.addition_rejected = False
            return

        rootObj = App.getDocument(doc_name).getObject(obj_name)

        # Apply the same processing as in addSelection to ensure consistent comparison
        resolved = rootObj.resolveSubElement(sub_name, True)
        sub_name = resolved[2]

        sub_name = UtilsAssembly.fixBodyExtraFeatureInSub(doc_name, sub_name)

        comp, new_sub = UtilsAssembly.getComponentReference(self.assembly, rootObj, sub_name)
        if not comp:
            return

        for reference in self.refs[:]:
            ref_obj = reference[0]
            ref_element_name = reference[1][0] if len(reference[1]) > 0 else ""

            # match both object and processed element name for precise identification
            if ref_obj == comp and ref_element_name == new_sub:
                self.refs.remove(reference)
                break
        else:
            print("No matching ref found for removal!")

        self.updateJoint()

    def setPreselection(self, doc_name, obj_name, sub_name):
        if not sub_name:
            self.presel_ref = None
            return

        sub_name = UtilsAssembly.fixBodyExtraFeatureInSub(doc_name, sub_name)

        rootObj = App.getDocument(doc_name).getObject(obj_name)

        comp, new_sub = UtilsAssembly.getComponentReference(self.assembly, rootObj, sub_name)
        if not comp:
            return

        self.presel_ref = [comp, [new_sub]]

    def clearSelection(self, doc_name):
        self.refs.clear()
        self.updateJoint()
