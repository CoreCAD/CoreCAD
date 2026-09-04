# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 The CoreCAD contributors

"""Headless tests for what an import records about where it came from."""

import os
import tempfile
import unittest

import FreeCAD
import Import
import Part
import PartDesign


class ImportProvenanceCase(unittest.TestCase):
    """An import should leave a trail back to its source file."""

    def setUp(self):
        self.doc = FreeCAD.newDocument("ImportProvenance")
        self.dir = tempfile.mkdtemp(prefix="cc_import_")

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)
        for name in os.listdir(self.dir):
            os.remove(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def path(self, name):
        return os.path.join(self.dir, name)

    def testSinglePartRecordsItsSource(self):
        path = self.path("bracket.step")
        Part.makeBox(10, 20, 30).exportStep(path)

        Import.insert(path, self.doc.Name)

        self.assertEqual(len(self.doc.Objects), 1)
        imported = self.doc.Objects[0]
        self.assertEqual(imported.TypeId, "Import::Feature")
        self.assertEqual(imported.SourceFile, path)
        self.assertEqual(len(imported.SourceHash), 40)
        self.assertIn("mode", dict(imported.TranslatorSettings))

    def testAnImportedPartIsUpToDateWithoutRereading(self):
        path = self.path("uptodate.step")
        Part.makeBox(10, 20, 30).exportStep(path)

        Import.insert(path, self.doc.Name)
        imported = self.doc.Objects[0]

        # The importer just built this from the file it recorded, so stamping the
        # source must not leave the document asking to be recomputed.
        self.assertEqual(self.doc.recompute(), 0)
        self.assertIn("Up-to-date", imported.State)

    def writeAssembly(self, path, secondHeight):
        """Write a two-part assembly, the second part being the one that changes."""
        source = FreeCAD.newDocument("AssemblySource")
        try:
            one = source.addObject("Part::Feature", "One")
            one.Label = "PartA"
            one.Shape = Part.makeBox(10, 10, 10)
            two = source.addObject("Part::Feature", "Two")
            two.Label = "PartB"
            two.Shape = Part.makeBox(10, 10, secondHeight, FreeCAD.Vector(50, 0, 0))
            source.recompute()
            Import.export([one, two], path)
        finally:
            FreeCAD.closeDocument(source.Name)

    def testEachAssemblyPartRecordsWhichNodeOfTheFileItIs(self):
        path = self.path("assembly.step")
        self.writeAssembly(path, 10)

        Import.insert(path, self.doc.Name)

        parts = [o for o in self.doc.Objects if o.TypeId == "Import::Feature"]
        self.assertEqual(len(parts), 2)
        for part in parts:
            self.assertEqual(part.SourceFile, path)
            # One part among several has to say which node it is, or it would
            # re-read whatever happened to be first.
            self.assertNotEqual(part.SourceNode, "")
            self.assertNotEqual(part.SourceNodeName, "")
        self.assertEqual(len({p.SourceNode for p in parts}), 2)

    def testARevisedAssemblyRebuildsOnlyThePartThatChanged(self):
        path = self.path("revised.step")
        self.writeAssembly(path, 10)
        Import.insert(path, self.doc.Name)

        parts = {o.SourceNodeName: o for o in self.doc.Objects if o.TypeId == "Import::Feature"}
        self.assertEqual(sorted(parts), ["PartA", "PartB"])
        self.assertAlmostEqual(parts["PartB"].Shape.Volume, 1000.0, places=6)

        self.writeAssembly(path, 40)
        for part in parts.values():
            part.touch()
        self.doc.recompute()

        self.assertAlmostEqual(parts["PartA"].Shape.Volume, 1000.0, places=6)
        self.assertAlmostEqual(parts["PartB"].Shape.Volume, 4000.0, places=6)
        self.assertNotIn("Invalid", parts["PartB"].State)


class ImportBodyCase(unittest.TestCase):
    """Imported geometry belongs to a body rather than sitting loose in the document.

    The user never creates bodies: a body is the system's accounting of which connected
    solids exist, and 7.8 says imported geometry gets one like anything else -- "rather
    than appearing as a loose top-level object outside any Body". Being loose is what kept
    an import out of everything addressed to bodies, the overlap notice among them.

    A body needs the document's shared world frame, so these run in a Part document.
    """

    def setUp(self):
        self.doc = FreeCAD.newDocument("ImportBody", type="Part")
        # A fresh Part document arrives with its world frame waiting to be computed; settle
        # it here so a later count of what recomputed is about the import and nothing else.
        self.doc.recompute()
        self.dir = tempfile.mkdtemp(prefix="cc_import_body_")

    def tearDown(self):
        FreeCAD.closeDocument(self.doc.Name)
        for name in os.listdir(self.dir):
            os.remove(os.path.join(self.dir, name))
        os.rmdir(self.dir)

    def path(self, name):
        return os.path.join(self.dir, name)

    def bodies(self):
        return self.doc.findObjects("PartDesign::Body")

    def testASinglePartImportGetsABody(self):
        path = self.path("bracket.step")
        Part.makeBox(10, 20, 30).exportStep(path)

        Import.insert(path, self.doc.Name)

        bodies = self.bodies()
        self.assertEqual(len(bodies), 1)
        imported = self.doc.getObject(bodies[0].Tip.Name)
        self.assertEqual(imported.TypeId, "Import::Feature")
        # The body carries the geometry, so everything addressed to bodies reaches it.
        self.assertEqual(len(bodies[0].Shape.Solids), 1)
        self.assertAlmostEqual(bodies[0].Shape.Volume, 6000.0, places=6)

    def testTheBodyIsReadyWithoutASecondRecompute(self):
        path = self.path("settled.step")
        Part.makeBox(10, 10, 10).exportStep(path)

        Import.insert(path, self.doc.Name)

        # The body is born after the recompute that prompted it has finished, so it has to
        # bring itself up to date; otherwise the document is left asking to be recomputed
        # and the body shows nothing until something else is edited.
        self.assertEqual(self.doc.recompute(), 0)
        self.assertIn("Up-to-date", self.bodies()[0].State)

    def testTheBodyIsSpawnedOnlyOnce(self):
        path = self.path("once.step")
        Part.makeBox(10, 10, 10).exportStep(path)
        Import.insert(path, self.doc.Name)
        self.assertEqual(len(self.bodies()), 1)

        imported = self.bodies()[0].Tip
        imported.touch()
        self.doc.recompute()

        self.assertEqual(len(self.bodies()), 1)

    def testTwoImportsAreTwoBodies(self):
        path = self.path("twice.step")
        Part.makeBox(10, 10, 10).exportStep(path)
        Import.insert(path, self.doc.Name)
        Import.insert(path, self.doc.Name)

        self.assertEqual(len(self.bodies()), 2)
        self.assertEqual(len({b.Tip.Name for b in self.bodies()}), 2)

    def testAnAssemblysLeavesStayWithTheirInstanceTree(self):
        """The boundary, stated: an assembly's parts are presented by its link tree.

        Giving each leaf a body as well would put the same geometry in the document twice
        over. 7.8 has an assembly become one document per leaf body rather than one
        document holding a link tree, and until that is built there is nothing for a leaf
        to be the tip of.
        """
        path = self.path("assembly.step")
        source = FreeCAD.newDocument("BodyAssemblySource")
        try:
            one = source.addObject("Part::Feature", "One")
            one.Label = "PartA"
            one.Shape = Part.makeBox(10, 10, 10)
            two = source.addObject("Part::Feature", "Two")
            two.Label = "PartB"
            two.Shape = Part.makeBox(10, 10, 10, FreeCAD.Vector(50, 0, 0))
            source.recompute()
            Import.export([one, two], path)
        finally:
            FreeCAD.closeDocument(source.Name)

        Import.insert(path, self.doc.Name)

        self.assertEqual(len([o for o in self.doc.Objects if o.TypeId == "Import::Feature"]), 2)
        self.assertEqual(self.bodies(), [])
