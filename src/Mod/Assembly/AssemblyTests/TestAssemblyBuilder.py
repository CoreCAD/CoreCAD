# SPDX-License-Identifier: LGPL-2.1-or-later
# /****************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth (Sean Barton)                                 *
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

"""Headless assembly-construction primitives (UtilsAssembly.createAssembly /
addComponent / groundComponent).

These are the App-layer path that both the Insert Component command and the STEP
importer will stand on. The tests assert what a consumer observes: a typed
assembly document is minted, components are cross-document references (never owned
geometry -- the content-scope door forbids that), and a grounded arrangement is a
genuinely solvable live assembly, not just nodes in a tree.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD as App

import UtilsAssembly


class TestAssemblyBuilder(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="corecad_asmbuild_")
        self.docs = []

    def tearDown(self):
        for doc in reversed(self.docs):
            try:
                App.closeDocument(doc.Name)
            except Exception:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ----------------------------------------------------------

    def _leaf_part_doc(self, name="Leaf"):
        """A standalone document holding a single linkable solid.

        Saved to disk because a cross-document link references the target by file
        path -- an unsaved linked document cannot be referenced.
        """
        doc = App.newDocument(name)
        self.docs.append(doc)
        box = doc.addObject("Part::Box", "Box")
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        doc.saveAs(os.path.join(self.tmp, name + ".FCStd"))
        return doc, box

    def _make_assembly(self, name="Assembly"):
        assembly = UtilsAssembly.createAssembly(name)
        self.docs.append(assembly.Document)
        # Saved to disk: a cross-document link is a file-to-file reference, so the
        # owner (assembly) document needs a path before it can hold a component.
        assembly.Document.saveAs(os.path.join(self.tmp, name + ".cassembly"))
        return assembly

    # -- tests -------------------------------------------------------------

    def test_creates_typed_assembly_document(self):
        """createAssembly mints a typed Assembly document with a JointGroup."""
        assembly = self._make_assembly()

        self.assertEqual(assembly.Document.DocumentType, "Assembly")
        self.assertEqual(assembly.TypeId, "Assembly::AssemblyObject")
        self.assertEqual(UtilsAssembly.getJointGroup(assembly).TypeId, "Assembly::JointGroup")

    def test_component_is_a_cross_document_reference(self):
        """addComponent inserts an App::Link that references another document."""
        _leaf_doc, box = self._leaf_part_doc()
        assembly = self._make_assembly()
        placement = App.Placement(App.Vector(100, 0, 0), App.Rotation(App.Vector(0, 0, 1), 30))

        link = UtilsAssembly.addComponent(assembly, box, placement, label="Widget")

        self.assertEqual(link.TypeId, "App::Link")
        self.assertIs(link.LinkedObject, box)
        self.assertEqual(link.Label, "Widget")
        self.assertTrue(link.Placement.isSame(placement, 1e-9))
        # The reference lives in the assembly; the geometry lives elsewhere.
        self.assertIs(link.Document, assembly.Document)
        self.assertIsNot(box.Document, assembly.Document)

    def test_typed_assembly_refuses_owned_geometry(self):
        """The content-scope door forbids owning part geometry in an assembly file."""
        assembly = self._make_assembly()
        with self.assertRaises(Exception):
            assembly.Document.addObject("Part::Box", "Box")

    def test_grounded_component_is_a_solvable_assembly(self):
        """A grounded component yields an assembly the solver accepts (returns 0)."""
        _leaf_doc, box = self._leaf_part_doc()
        assembly = self._make_assembly()
        link = UtilsAssembly.addComponent(assembly, box, App.Placement())

        ground = UtilsAssembly.groundComponent(assembly, link)
        self.assertEqual(ground.TypeId, "Assembly::GroundedJoint")
        self.assertIs(ground.ObjectToGround, link)

        assembly.Document.recompute()
        self.assertTrue(assembly.isPartGrounded(link))
        self.assertEqual(assembly.solve(), 0)
