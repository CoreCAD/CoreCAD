# SPDX-License-Identifier: LGPL-2.1-or-later
# /****************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth contributors                                  *
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

"""
Characterization tests for rigid sub-assembly (AssemblyLink) behaviour.

These pin the *observable meaning* of a rigid sub-assembly reference so that the
#38 migration (owned proxy graph -> resolve-through-reference) can be verified to
preserve behaviour. They deliberately assert what a user/consumer observes -- the
world placement of the referenced geometry, resolved through the link, and the
rigid "moves as one unit" property -- rather than the proxy mechanism, so they
remain valid before and after the internals are rewritten.

A rigid sub-assembly must resolve to:
  * its child parts kept at their child-solved relative poses (internal config
    frozen), and
  * the whole arrangement positioned by the instance Placement, so that the world
    placement of any child part == instancePlacement o childInternalPlacement.

Cross-document AssemblyLink requires both documents saved to disk (PropertyXLink
binds to the target's file path), so the fixtures persist to a temporary dir.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD as App
import AssemblyApp  # noqa: F401  (registers Assembly:: types)
import JointObject


def _msg(text, end="\n"):
    App.Console.PrintMessage(text + end)


class TestSubAssembly(unittest.TestCase):
    """Rigid sub-assembly (AssemblyLink) characterization."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="corecad_subasm_")
        self.docs = []
        _msg("  Temporary dir '{}'".format(self.tmp))

    def tearDown(self):
        for doc in reversed(self.docs):
            try:
                App.closeDocument(doc.Name)
            except Exception:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ----------------------------------------------------------

    def _new_doc(self, name):
        doc = App.newDocument(name)
        self.docs.append(doc)
        return doc

    def _build_child(self):
        """Child assembly: box1 grounded, box2 fixed to box1. Solved + saved.

        Returns (doc, assembly, box1, box2).
        """
        doc = self._new_doc("Child")
        asm = doc.addObject("Assembly::AssemblyObject", "Assembly")
        jg = asm.newObject("Assembly::JointGroup", "Joints")

        box1 = asm.newObject("Part::Box", "Box")
        box1.Length = box1.Width = box1.Height = 10
        box1.Placement = App.Placement(App.Vector(0, 0, 0), App.Rotation())

        box2 = asm.newObject("Part::Box", "Box")
        box2.Length = box2.Width = box2.Height = 10
        box2.Placement = App.Placement(App.Vector(30, 0, 0), App.Rotation())

        ground = jg.newObject("Assembly::GroundedJoint", "GroundedJoint")
        ground.ObjectToGround = box1

        joint = jg.newObject("Assembly::Joint", "fix")
        joint.JointType = JointObject.JointTypes[0]  # Fixed
        JointObject.setJointConnectors(joint, [])
        JointObject.setJointConnectors(
            joint, [[box1, ["Face6", "Vertex7"]], [box2, ["Face3", "Vertex1"]]]
        )
        doc.recompute()
        doc.saveAs(os.path.join(self.tmp, "Child.FCStd"))
        return doc, asm, box1, box2

    def _build_parent_with_link(self, child_asm, placement):
        """Parent assembly referencing child_asm as a rigid sub-assembly."""
        doc = self._new_doc("Parent")
        asm = doc.addObject("Assembly::AssemblyObject", "Assembly")
        asm.newObject("Assembly::JointGroup", "Joints")
        doc.saveAs(os.path.join(self.tmp, "Parent.FCStd"))

        link = doc.addObject("Assembly::AssemblyLink", "Sub")
        link.LinkedObject = child_asm
        link.Rigid = True
        asm.addObject(link)
        link.Placement = placement
        doc.recompute()
        return doc, asm, link

    # -- helpers -----------------------------------------------------------

    def _global_placement_through_link(self, link, child_obj):
        """World placement of child_obj resolved through the sub-assembly link."""
        return link.getSubObject(child_obj.Name + ".", retType=3)

    def _assert_placement(self, got, expected, msg):
        self.assertTrue(
            got.isSame(expected, 1e-6),
            "{}: got {} / {}, expected {} / {}".format(
                msg, got.Base, got.Rotation, expected.Base, expected.Rotation
            ),
        )

    # -- tests -------------------------------------------------------------

    def test_rigid_subassembly_resolves_through_placement(self):
        """Child geometry world placement == instancePlacement o childInternalPlacement."""
        _msg("  Test 'rigid sub-assembly resolves child geometry through instance placement'")
        _cdoc, casm, box1, box2 = self._build_child()

        instance = App.Placement(App.Vector(100, 0, 0), App.Rotation(App.Vector(0, 0, 1), 30))
        _pdoc, _pasm, link = self._build_parent_with_link(casm, instance)

        for child_box in (box1, box2):
            expected = instance.multiply(child_box.Placement)
            got = self._global_placement_through_link(link, child_box)
            self._assert_placement(
                got, expected, "child '{}' world placement".format(child_box.Name)
            )

    def test_rigid_subassembly_moves_as_one_unit(self):
        """Re-placing the instance carries every child part rigidly; internal pose frozen."""
        _msg("  Test 'rigid sub-assembly moves as one rigid unit'")
        _cdoc, casm, box1, box2 = self._build_child()

        # Internal relative pose child-solved (box2 relative to box1).
        internal_rel = box1.Placement.inverse().multiply(box2.Placement)

        first = App.Placement(App.Vector(0, 0, 0), App.Rotation())
        _pdoc, _pasm, link = self._build_parent_with_link(casm, first)

        # Move the instance and recompute.
        second = App.Placement(App.Vector(-40, 15, 5), App.Rotation(App.Vector(1, 0, 0), 90))
        link.Placement = second
        _pdoc.recompute()

        g1 = self._global_placement_through_link(link, box1)
        g2 = self._global_placement_through_link(link, box2)

        # Both parts followed the new instance placement.
        self._assert_placement(g1, second.multiply(box1.Placement), "box1 after move")
        self._assert_placement(g2, second.multiply(box2.Placement), "box2 after move")

        # Internal relative pose is unchanged (rigid == frozen internal config).
        got_rel = g1.inverse().multiply(g2)
        self._assert_placement(got_rel, internal_rel, "internal relative pose preserved")


if __name__ == "__main__":
    unittest.main()
