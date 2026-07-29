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
assembly, and a nested source recurses -- each sub-assembly becoming its own
.cassembly linked into its parent by an Assembly::AssemblyLink.
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD as App
import Part

import Import
import AssemblyStepImport


def _enclosing_asm_link(obj):
    """The AssemblyLink whose group owns ``obj``, or None."""
    for p in obj.InList:
        if p.TypeId == "Assembly::AssemblyLink" and obj in p.Group:
            return p
    return None


def _global_placement(obj):
    """Compose ``obj``'s world frame by premultiplying every enclosing AssemblyLink placement.

    Mirrors the kernel's GeoFeatureGroup composition (what the renderer / getGlobalPlacement
    use). A flexible sub-assembly carries its instance transform on its AssemblyLink, not baked
    into the leaf proxies, so the world pose is the chain of group placements times the leaf's
    own -- and this holds at any nesting depth.
    """
    plc = obj.Placement
    parent = _enclosing_asm_link(obj)
    while parent is not None:
        plc = parent.Placement.multiply(plc)
        parent = _enclosing_asm_link(parent)
    return plc


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

    def _nested_step(self):
        """A two-level STEP: Outer holds a Base box and a placed Inner sub-assembly
        (which holds a single placed Pin cylinder). Built by exporting nested App::Parts
        purely to author the STEP hierarchy; the import under test creates no App::Part."""
        doc = App.newDocument("nestedsrc")
        outer = doc.addObject("App::Part", "Outer")
        box = doc.addObject("Part::Box", "Base")
        box.Length = box.Width = box.Height = 10
        inner = doc.addObject("App::Part", "Inner")
        inner.Placement = App.Placement(App.Vector(100, 0, 0), App.Rotation())
        pin = doc.addObject("Part::Cylinder", "Pin")
        pin.Radius = 3
        pin.Height = 15
        pin.Placement = App.Placement(App.Vector(0, 50, 0), App.Rotation())
        inner.addObject(pin)
        outer.addObject(box)
        outer.addObject(inner)
        doc.recompute()
        path = os.path.join(self.tmp, "nested.step")
        Import.export([outer], path)
        App.closeDocument(doc.Name)
        return path

    def _shared_leaf_step(self):
        """A single-level STEP that instances one part twice at distinct placements.

        Built from two App::Links to a single box so the exported STEP expresses one
        shared prototype with two instances -- the case #76 must not duplicate."""
        doc = App.newDocument("sharedsrc")
        outer = doc.addObject("App::Part", "Outer")
        box = doc.addObject("Part::Box", "Widget")
        box.Length = box.Width = box.Height = 10
        inst1 = doc.addObject("App::Link", "Inst1")
        inst1.LinkedObject = box
        inst2 = doc.addObject("App::Link", "Inst2")
        inst2.LinkedObject = box
        inst2.Placement = App.Placement(App.Vector(50, 0, 0), App.Rotation())
        outer.addObject(inst1)
        outer.addObject(inst2)
        doc.recompute()
        path = os.path.join(self.tmp, "shared.step")
        Import.export([outer], path)
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

    def test_nested_step_imports_recursively(self):
        """A nested STEP recurses: each sub-assembly becomes its own .cassembly,
        linked into its parent by an Assembly::AssemblyLink; leaves stay App::Link."""
        path = self._nested_step()

        assembly = AssemblyStepImport.importAssembly(path)

        # Top level: the Base leaf is an App::Link into a .cpart; the Inner sub-assembly
        # is an Assembly::AssemblyLink into a separate .cassembly. (A flexible sub-assembly
        # also surfaces proxy App::Links pointing into that .cassembly -- not leaves.)
        leaf_links = [
            o
            for o in assembly.Document.Objects
            if o.TypeId == "App::Link" and o.LinkedObject.Document.FileName.endswith(".cpart")
        ]
        sub_links = [o for o in assembly.Document.Objects if o.TypeId == "Assembly::AssemblyLink"]
        self.assertEqual([link.Label for link in leaf_links], ["Base"])
        self.assertEqual(len(sub_links), 1)

        sub = sub_links[0]
        self.assertIsNot(sub.LinkedObject.Document, assembly.Document)
        self.assertEqual(sub.LinkedObject.Document.DocumentType, "Assembly")
        self.assertTrue(sub.LinkedObject.Document.FileName.endswith(".cassembly"))
        self.assertEqual(sub.LinkedObject.TypeId, "Assembly::AssemblyObject")

        # Both levels solve, and no App::Part exists anywhere.
        self.assertEqual(assembly.solve(), 0)
        self.assertEqual(sub.LinkedObject.solve(), 0)
        for doc in App.listDocuments().values():
            parts = [o for o in doc.Objects if o.TypeId == "App::Part"]
            self.assertEqual(parts, [], f"App::Part leaked into {doc.Name}")

    def test_nested_sub_assembly_link_rigidity_follows_flag(self):
        """The sub-assembly link is flexible by default and rigid when asked."""
        path = self._nested_step()

        flexible = AssemblyStepImport.importAssembly(path, rigid=False)
        sub = [o for o in flexible.Document.Objects if o.TypeId == "Assembly::AssemblyLink"][0]
        self.assertFalse(sub.Rigid)
        self.assertEqual(flexible.solve(), 0)

        for doc in list(App.listDocuments().values()):
            App.closeDocument(doc.Name)
        shutil.rmtree(self.tmp, ignore_errors=True)
        os.makedirs(self.tmp, exist_ok=True)

        path = self._nested_step()
        rigid = AssemblyStepImport.importAssembly(path, rigid=True)
        sub = [o for o in rigid.Document.Objects if o.TypeId == "Assembly::AssemblyLink"][0]
        self.assertTrue(sub.Rigid)
        self.assertEqual(rigid.solve(), 0)

    def test_nested_world_placement_is_preserved(self):
        """A leaf deep inside a sub-assembly keeps its world pose through the nesting."""
        path = self._nested_step()

        assembly = AssemblyStepImport.importAssembly(path)

        sub = [o for o in assembly.Document.Objects if o.TypeId == "Assembly::AssemblyLink"][0]
        pin = [c for c in sub.Group if c.Label.startswith("Pin")][0]
        # A flexible sub-assembly carries its instance offset on the AssemblyLink itself; the
        # leaf proxy holds only the component's pose within its own sub-assembly. The rendered
        # world pose is therefore the group-composed frame (every enclosing AssemblyLink
        # placement premultiplied) applied to the body's own local geometry.
        body_local_center = pin.LinkedObject.LinkedObject.Shape.BoundBox.Center
        world_center = _global_placement(pin).multVec(body_local_center)

        # Pin (r3/h15) center local (0,0,7.5); placed (0,50,0) in Inner; Inner at
        # (100,0,0) in Outer -> world (100,50,7.5).
        self.assertLess((world_center - App.Vector(100, 50, 7.5)).Length, 1e-6)

    def test_shared_prototype_is_reused_not_duplicated(self):
        """One prototype instanced twice yields ONE leaf document, referenced twice (#76)."""
        path = self._shared_leaf_step()

        assembly = AssemblyStepImport.importAssembly(path)

        links = [o for o in assembly.Document.Objects if o.TypeId == "App::Link"]
        self.assertEqual(len(links), 2)

        # Both instances reference the SAME leaf object in the SAME single leaf document --
        # the shared prototype is not written twice.
        leaf_objects = {link.LinkedObject for link in links}
        self.assertEqual(len(leaf_objects), 1)
        leaf_docs = {link.LinkedObject.Document.Name for link in links}
        self.assertEqual(len(leaf_docs), 1)
        cpart_docs = [d for d in App.listDocuments().values() if d.FileName.endswith(".cpart")]
        self.assertEqual(len(cpart_docs), 1)

        # Each instance still carries its own placement: the shared geometry lands at two
        # distinct, correct world positions.
        centers = sorted(
            link.Placement.multVec(link.LinkedObject.Shape.BoundBox.Center).x for link in links
        )
        # Box center local x=5; second instance placed at x=50 -> world x centers 5 and 55.
        self.assertLess(abs(centers[0] - 5), 1e-6)
        self.assertLess(abs(centers[1] - 55), 1e-6)
        self.assertEqual(assembly.solve(), 0)

    def _shared_sub_step(self):
        """A two-level STEP that instances one sub-assembly twice at distinct placements.

        Outer holds two App::Links to a single Inner sub-assembly (a placed Pin), so the
        exported STEP expresses one shared sub-assembly prototype instanced twice -- the
        nested analogue of the #76 duplication case."""
        doc = App.newDocument("sharedsubsrc")
        outer = doc.addObject("App::Part", "Outer")
        inner = doc.addObject("App::Part", "Gadget")
        pin = doc.addObject("Part::Cylinder", "Pin")
        pin.Radius = 3
        pin.Height = 15
        pin.Placement = App.Placement(App.Vector(0, 50, 0), App.Rotation())
        inner.addObject(pin)
        g1 = doc.addObject("App::Link", "G1")
        g1.LinkedObject = inner
        g2 = doc.addObject("App::Link", "G2")
        g2.LinkedObject = inner
        g2.Placement = App.Placement(App.Vector(100, 0, 0), App.Rotation())
        outer.addObject(g1)
        outer.addObject(g2)
        doc.recompute()
        path = os.path.join(self.tmp, "sharedsub.step")
        Import.export([outer], path)
        App.closeDocument(doc.Name)
        return path

    def test_shared_sub_assembly_is_reused(self):
        """A sub-assembly instanced twice links ONE shared .cassembly twice, and its
        single leaf .cpart is written once, rather than duplicating either (#76)."""
        path = self._shared_sub_step()
        out_dir = os.path.join(self.tmp, "out")

        assembly = AssemblyStepImport.importAssembly(path, dest_dir=out_dir)

        sub_links = [o for o in assembly.Document.Objects if o.TypeId == "Assembly::AssemblyLink"]
        self.assertEqual(len(sub_links), 2)
        # Both instances link the SAME sub-assembly document, at distinct placements.
        sub_docs = {link.LinkedObject.Document.Name for link in sub_links}
        self.assertEqual(len(sub_docs), 1)
        self.assertNotEqual(sub_links[0].Placement.Base, sub_links[1].Placement.Base)
        # One .cassembly per distinct assembly (top + one shared sub) and one .cpart for
        # the single shared leaf -- no _2 duplicates on disk.
        cassembly_files = [f for f in os.listdir(out_dir) if f.endswith(".cassembly")]
        cpart_files = [f for f in os.listdir(out_dir) if f.endswith(".cpart")]
        self.assertEqual(len(cassembly_files), 2)
        self.assertEqual(len(cpart_files), 1)
        self.assertEqual(assembly.solve(), 0)

    def test_nested_reopens_from_disk(self):
        """Closing everything and reopening only the top .cassembly pulls the whole
        tree back from disk and still solves."""
        path = self._nested_step()

        assembly = AssemblyStepImport.importAssembly(path)
        top_file = assembly.Document.FileName
        for doc in list(App.listDocuments().values()):
            App.closeDocument(doc.Name)

        reopened = App.openDocument(top_file)
        top = [o for o in reopened.Objects if o.TypeId == "Assembly::AssemblyObject"][0]
        self.assertEqual(top.solve(), 0)

    def test_reopen_preserves_object_identity(self):
        """Every object's durable Uid survives the save/close/reopen round-trip.

        This is the git-for-CAD bedrock: identity is the stored Uid, not a name or
        position, so a diff across two loads of the same import must be empty. We assert
        the *set* of Uids across the whole imported tree is invariant -- matching by name
        or document would be fragile (unsaved sub-docs are lazily named 'Unnamed...' before
        the first save), but the Uid is exactly what must not change. A regression that
        re-mints ids on save or reopen would make the two sets differ."""
        path = self._nested_step()

        assembly = AssemblyStepImport.importAssembly(path)
        top_file = assembly.Document.FileName
        before = {o.Uid for doc in App.listDocuments().values() for o in doc.Objects}
        self.assertTrue(before, "import produced no objects to identify")

        for doc in list(App.listDocuments().values()):
            App.closeDocument(doc.Name)

        App.openDocument(top_file)
        after = {o.Uid for doc in App.listDocuments().values() for o in doc.Objects}

        self.assertEqual(
            before,
            after,
            "object Uids were not preserved across reopen -- git-for-CAD identity broke",
        )


if __name__ == "__main__":
    unittest.main()
