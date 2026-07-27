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

"""Characterization of the STEP -> live-assembly translator (AssemblyStepImport).

The reader (Import.readAssemblyStructure) mints no document objects, and the
translator turns its output into standalone .cpart leaves plus a .cassembly that
links them. These tests assert the observable contract: no App::Part is ever
created, each component's world pose is preserved, the result is a solvable live
assembly, and a nested source is refused rather than silently flattened.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD as App
import Part

import Import
import AssemblyStepImport


class TestStepAssemblyImport(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="corecad_stepasm_")

    def tearDown(self):
        for doc in list(App.listDocuments().values()):
            try:
                App.closeDocument(doc.Name)
            except Exception:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- fixtures ----------------------------------------------------------

    def _flat_step(self):
        """A single-level STEP assembly: a box at the origin and a placed cylinder."""
        doc = App.newDocument("flatsrc")
        box = doc.addObject("Part::Box", "B1")
        box.Length = box.Width = box.Height = 10
        cyl = doc.addObject("Part::Cylinder", "B2")
        cyl.Radius = 5
        cyl.Height = 20
        cyl.Placement = App.Placement(App.Vector(50, 0, 0), App.Rotation(App.Vector(0, 1, 0), 90))
        doc.recompute()
        path = os.path.join(self.tmp, "flat.step")
        Import.export([box, cyl], path)
        App.closeDocument(doc.Name)
        return path

    # -- tests -------------------------------------------------------------

    def test_reader_mints_no_document_objects(self):
        """readAssemblyStructure returns geometry+placement without touching a doc."""
        path = self._flat_step()
        before = set(App.listDocuments().keys())

        structure = Import.readAssemblyStructure(path)

        self.assertEqual(set(App.listDocuments().keys()), before)
        self.assertEqual(len(structure["components"]), 2)
        for comp in structure["components"]:
            self.assertFalse(comp["is_assembly"])
            self.assertIsInstance(comp["shape"], Part.Shape)

    def test_imports_flat_step_as_live_assembly(self):
        """A flat STEP becomes leaf .cpart files linked by a solvable .cassembly."""
        path = self._flat_step()

        assembly = AssemblyStepImport.importAssembly(path)

        # A typed assembly document, saved as .cassembly.
        self.assertEqual(assembly.Document.DocumentType, "Assembly")
        self.assertTrue(assembly.Document.FileName.endswith(".cassembly"))

        # Two components, each a cross-document App::Link into its own saved leaf.
        links = [o for o in assembly.Document.Objects if o.TypeId == "App::Link"]
        self.assertEqual(len(links), 2)
        for link in links:
            self.assertIsNot(link.LinkedObject.Document, assembly.Document)
            self.assertTrue(link.LinkedObject.Document.FileName.endswith(".cpart"))

        # It solves as a live assembly.
        self.assertEqual(assembly.solve(), 0)

    def test_no_app_part_is_ever_created(self):
        """The whole point: live import produces zero App::Part across all documents."""
        path = self._flat_step()

        AssemblyStepImport.importAssembly(path)

        for doc in App.listDocuments().values():
            parts = [o for o in doc.Objects if o.TypeId == "App::Part"]
            self.assertEqual(parts, [], f"App::Part leaked into {doc.Name}")

    def test_world_placement_is_preserved(self):
        """Each component's world position survives the local-shape + link split."""
        path = self._flat_step()

        assembly = AssemblyStepImport.importAssembly(path)

        centers = {}
        for link in [o for o in assembly.Document.Objects if o.TypeId == "App::Link"]:
            local_center = link.LinkedObject.Shape.BoundBox.Center
            centers[link.Label] = link.Placement.multVec(local_center)

        # Box spans 0..10 -> center (5,5,5); cylinder r5/h20 placed at (50,0,0)
        # rotated 90 about Y -> world center (60,0,0).
        self.assertLess((centers["B1"] - App.Vector(5, 5, 5)).Length, 1e-6)
        self.assertLess((centers["B2"] - App.Vector(60, 0, 0)).Length, 1e-6)

    def test_nested_assembly_is_refused(self):
        """A nested STEP is refused, not silently flattened (single-level only)."""
        nested = "data/tests/Step/as1-ac-214_small.stp"
        if not os.path.exists(nested):
            self.skipTest("nested STEP fixture not available")
        with self.assertRaises(NotImplementedError):
            AssemblyStepImport.importAssembly(nested, dest_dir=self.tmp)


if __name__ == "__main__":
    unittest.main()
