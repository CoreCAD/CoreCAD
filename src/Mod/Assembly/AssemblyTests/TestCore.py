# SPDX-License-Identifier: LGPL-2.1-or-later
# /****************************************************************************
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
# ***************************************************************************/

import FreeCAD as App
import Part
import unittest

import AssemblyApp
import UtilsAssembly
import JointObject


def _msg(text, end="\n"):
    """Write messages to the console including the line ending."""
    App.Console.PrintMessage(text + end)


class TestCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """setUpClass()...
        This method is called upon instantiation of this test class.  Add code and objects here
        that are needed for the duration of the test() methods in this class.  In other words,
        set up the 'global' test environment here; use the `setUp()` method to set up a 'local'
        test environment.
        This method does not have access to the class `self` reference, but it
        is able to call static methods within this same class.
        """
        pass

    @classmethod
    def tearDownClass(cls):
        """tearDownClass()...
        This method is called prior to destruction of this test class.  Add code and objects here
        that cleanup the test environment after the test() methods in this class have been executed.
        This method does not have access to the class `self` reference.  This method
        is able to call static methods within this same class.
        """
        pass

    # Setup and tear down methods called before and after each unit test
    def setUp(self):
        """setUp()...
        This method is called prior to each `test()` method.  Add code and objects here
        that are needed for multiple `test()` methods.
        """
        doc_name = self.__class__.__name__
        if App.ActiveDocument:
            if App.ActiveDocument.Name != doc_name:
                App.newDocument(doc_name)
        else:
            App.newDocument(doc_name)
        App.setActiveDocument(doc_name)
        self.doc = App.ActiveDocument

        self.assembly = App.ActiveDocument.addObject("Assembly::AssemblyObject", "Assembly")
        if self.assembly:
            self.jointgroup = self.assembly.newObject("Assembly::JointGroup", "Joints")

        _msg("  Temporary document '{}'".format(self.doc.Name))

    def tearDown(self):
        """tearDown()...
        This method is called after each test() method. Add cleanup instructions here.
        Such cleanup instructions will likely undo those in the setUp() method.
        """
        App.closeDocument(self.doc.Name)

    def test_create_assembly(self):
        """Create an assembly."""
        operation = "Create Assembly Object"
        _msg("  Test '{}'".format(operation))
        self.assertTrue(self.assembly, "'{}' failed".format(operation))

    def test_create_jointGroup(self):
        """Create a joint group in an assembly."""
        operation = "Create JointGroup Object"
        _msg("  Test '{}'".format(operation))
        self.assertTrue(self.jointgroup, "'{}' failed".format(operation))

    def test_create_joint(self):
        """Create a joint in an assembly."""
        operation = "Create Joint Object"
        _msg("  Test '{}'".format(operation))

        joint = self.jointgroup.newObject("Assembly::Joint", "testJoint")
        self.assertTrue(joint, "'{}' failed (joint creation failed)".format(operation))
        joint.JointType = JointObject.JointTypes[0]
        JointObject.setJointConnectors(joint, [])

        self.assertTrue(hasattr(joint, "JointType"), "'{}' failed".format(operation))
        self.assertTrue(
            joint.isDerivedFrom("Assembly::Joint"),
            "'{}' failed: not a typed joint".format(operation),
        )

    def test_create_grounded_joint(self):
        """Create a grounded joint in an assembly."""
        operation = "Create Grounded Joint Object"
        _msg("  Test '{}'".format(operation))

        groundedjoint = self.jointgroup.newObject("Assembly::GroundedJoint", "testJoint")
        self.assertTrue(
            groundedjoint, "'{}' failed (grounded joint creation failed)".format(operation)
        )

        box = self.assembly.newObject("Part::Box", "Box")

        groundedjoint.ObjectToGround = box

        self.assertTrue(
            hasattr(groundedjoint, "ObjectToGround"),
            "'{}' failed: No attribute 'ObjectToGround'".format(operation),
        )
        self.assertTrue(
            groundedjoint.ObjectToGround == box,
            "'{}' failed: ObjectToGround not set correctly.".format(operation),
        )

    def test_toggle_grounded_joint(self):
        """test grounding and ungrounding a part, added because of github.com/freecad/freecad/issues/28440"""
        operation = "Toggle Grounded Joint"
        _msg("  Test '{}'".format(operation))

        box = self.assembly.newObject("Part::Box", "Box")

        # ground the part
        groundedjoint = self.jointgroup.newObject("Assembly::GroundedJoint", "GroundedJoint")
        groundedjoint.ObjectToGround = box
        self.doc.recompute()

        # verify grounded
        self.assertTrue(
            hasattr(groundedjoint, "ObjectToGround"),
            "'{}' failed: No attribute 'ObjectToGround'".format(operation),
        )
        self.assertEqual(
            groundedjoint.ObjectToGround,
            box,
            "'{}' failed: ObjectToGround not set correctly".format(operation),
        )

        # unground the part
        self.doc.removeObject(groundedjoint.Name)
        self.doc.recompute()

        # verify no grounded joints remain in this part
        for joint in self.jointgroup.Group:
            if hasattr(joint, "ObjectToGround"):
                self.assertNotEqual(
                    joint.ObjectToGround,
                    box,
                    "'{}' failed: part still grounded after toggle".format(operation),
                )

    def test_find_placement(self):
        """Test find placement of joint."""
        operation = "Find placement"
        _msg("  Test '{}'".format(operation))

        joint = self.jointgroup.newObject("Assembly::Joint", "testJoint")
        joint.JointType = JointObject.JointTypes[0]
        JointObject.setJointConnectors(joint, [])

        L = 2
        W = 3
        H = 7
        box = self.assembly.newObject("Part::Box", "Box")
        box.Length = L
        box.Width = W
        box.Height = H
        box.Placement = App.Placement(App.Vector(10, 20, 30), App.Rotation(15, 25, 35))

        # Step 0 : box with placement. No element selected
        ref = [self.assembly, [box.Name + ".", box.Name + "."]]
        plc = AssemblyApp.findPlacement(ref, joint.ignoresVertex()) * joint.Offset1
        targetPlc = App.Placement(App.Vector(), App.Rotation())
        self.assertTrue(plc.isSame(targetPlc, 1e-6), "'{}' failed - Step 0".format(operation))

        # Step 1 : box with placement. Face + Vertex
        ref = [self.assembly, [box.Name + ".Face6", box.Name + ".Vertex7"]]
        plc = AssemblyApp.findPlacement(ref, joint.ignoresVertex()) * joint.Offset1
        targetPlc = App.Placement(App.Vector(L, W, H), App.Rotation())
        self.assertTrue(plc.isSame(targetPlc, 1e-6), "'{}' failed - Step 1".format(operation))

        # Step 2 : box with placement. Edge + Vertex
        ref = [self.assembly, [box.Name + ".Edge8", box.Name + ".Vertex8"]]
        plc = AssemblyApp.findPlacement(ref, joint.ignoresVertex()) * joint.Offset1
        targetPlc = App.Placement(App.Vector(L, W, 0), App.Rotation(0, -90, 270))
        self.assertTrue(plc.isSame(targetPlc, 1e-6), "'{}' failed - Step 2".format(operation))

        # Step 3 : box with placement. Vertex
        ref = [self.assembly, [box.Name + ".Vertex3", box.Name + ".Vertex3"]]
        plc = AssemblyApp.findPlacement(ref, joint.ignoresVertex()) * joint.Offset1
        targetPlc = App.Placement(App.Vector(0, W, H), App.Rotation())
        _msg("  plc '{}'".format(plc))
        _msg("  targetPlc '{}'".format(targetPlc))
        self.assertTrue(plc.isSame(targetPlc, 1e-6), "'{}' failed - Step 3".format(operation))

        # Step 4 : box with placement. Face
        ref = [self.assembly, [box.Name + ".Face2", box.Name + ".Face2"]]
        plc = AssemblyApp.findPlacement(ref, joint.ignoresVertex()) * joint.Offset1
        targetPlc = App.Placement(App.Vector(L, W / 2, H / 2), App.Rotation(0, -90, 180))
        _msg("  plc '{}'".format(plc))
        _msg("  targetPlc '{}'".format(targetPlc))
        self.assertTrue(plc.isSame(targetPlc, 1e-6), "'{}' failed - Step 4".format(operation))

    def test_solve_assembly(self):
        """Test solving an assembly."""
        operation = "Solve assembly"
        _msg("  Test '{}'".format(operation))

        box = self.assembly.newObject("Part::Box", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        box.Placement = App.Placement(App.Vector(10, 20, 30), App.Rotation(15, 25, 35))

        box2 = self.assembly.newObject("Part::Box", "Box")
        box2.Length = 10
        box2.Width = 10
        box2.Height = 10
        box2.Placement = App.Placement(App.Vector(40, 50, 60), App.Rotation(45, 55, 65))

        ground = self.jointgroup.newObject("Assembly::GroundedJoint", "GroundedJoint")
        ground.ObjectToGround = box2

        joint = self.jointgroup.newObject("Assembly::Joint", "testJoint")
        joint.JointType = JointObject.JointTypes[0]
        JointObject.setJointConnectors(joint, [])

        refs = [
            [box2, ["Face6", "Vertex7"]],
            [box, ["Face6", "Vertex7"]],
        ]

        JointObject.setJointConnectors(joint, refs)

        self.assertTrue(box.Placement.isSame(box2.Placement, 1e-6), "'{}'".format(operation))

    def test_solve_part_inside_transformed_group(self):
        """Solver is group-aware: mate a part that lives inside a group carrying a transform.

        The MBD solver works in the world frame. A part directly in the assembly has an
        identity enclosing group, so its local Placement is its world pose and the solver
        never had to distinguish the two. A part inside a group that carries a non-identity
        transform (e.g. a nested flexible sub-assembly link) does not: its world pose is
        groupPlacement o localPlacement. The solver must seed such a part in the world frame
        and divide the group transform back out when it writes the local Placement -- otherwise
        the mate is solved against the wrong frame and the part lands at the wrong world pose.
        """
        operation = "Solve part inside a transformed group"
        _msg("  Test '{}'".format(operation))

        # Grounded anchor directly in the assembly (identity enclosing group).
        anchor = self.assembly.newObject("Part::Box", "Anchor")
        anchor.Length = anchor.Width = anchor.Height = 10
        anchor.Placement = App.Placement(App.Vector(50, 0, 0), App.Rotation())

        # A group carrying a non-identity transform, holding an inner part.
        group = self.doc.addObject("Assembly::AssemblyLink", "Grp")
        group.Rigid = False
        self.assembly.addObject(group)
        group_plc = App.Placement(App.Vector(0, 100, 0), App.Rotation(App.Vector(0, 0, 1), 90))
        group.Placement = group_plc

        inner = self.doc.addObject("Part::Box", "Inner")
        inner.Length = inner.Width = inner.Height = 10
        inner.Placement = App.Placement(App.Vector(5, 5, 5), App.Rotation())
        group.addObject(inner)
        self.doc.recompute()

        ground = self.jointgroup.newObject("Assembly::GroundedJoint", "GroundedJoint")
        ground.ObjectToGround = anchor

        joint = self.jointgroup.newObject("Assembly::Joint", "mate")
        joint.JointType = JointObject.JointTypes[0]  # Fixed
        JointObject.setJointConnectors(joint, [])
        refs = [[anchor, ["Face6", "Vertex7"]], [inner, ["Face6", "Vertex7"]]]
        JointObject.setJointConnectors(joint, refs)  # triggers the solve

        # The mate is satisfied in the world frame: inner's world pose coincides with the
        # grounded anchor's. (Pre-fix, inner was seeded from its local Placement while its
        # joint marker was global-relative, so it landed at the wrong world pose.)
        self.assertTrue(
            inner.getGlobalPlacement().isSame(anchor.getGlobalPlacement(), 1e-6),
            "inner world pose {} did not mate to anchor {}".format(
                inner.getGlobalPlacement(), anchor.getGlobalPlacement()
            ),
        )
        # The group itself did not move; only inner's local Placement absorbed the mate.
        self.assertTrue(
            group.Placement.isSame(group_plc, 1e-6),
            "group placement changed: {}".format(group.Placement),
        )
        # The stored local Placement is the world pose with the group transform divided out,
        # i.e. group o local reproduces the world pose (not local == world).
        composed = group.Placement.multiply(inner.Placement)
        self.assertTrue(
            composed.isSame(inner.getGlobalPlacement(), 1e-6),
            "group o local {} != world {}".format(composed, inner.getGlobalPlacement()),
        )
        self.assertFalse(
            inner.Placement.isSame(inner.getGlobalPlacement(), 1e-6),
            "local Placement should differ from world when the group carries a transform",
        )
