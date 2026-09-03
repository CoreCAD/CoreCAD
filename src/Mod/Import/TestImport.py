# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 The CoreCAD contributors

"""Headless tests for what an import records about where it came from."""

import os
import tempfile
import unittest

import FreeCAD
import Import
import Part


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

    def testAssemblyPartsAreNotGivenASourceTheyCannotHonour(self):
        one = self.doc.addObject("Part::Feature", "One")
        one.Shape = Part.makeBox(10, 10, 10)
        two = self.doc.addObject("Part::Feature", "Two")
        two.Shape = Part.makeBox(10, 10, 10, FreeCAD.Vector(50, 0, 0))
        self.doc.recompute()

        path = self.path("assembly.step")
        Import.export([one, two], path)

        target = FreeCAD.newDocument("ImportedAssembly")
        try:
            Import.insert(path, target.Name)
            imported = [o for o in target.Objects if o.TypeId == "Import::Feature"]
            self.assertGreater(len(imported), 1)
            # Each part needs to say which node of the file it is before it can be
            # re-read; until then it must not claim to know its source.
            for obj in imported:
                self.assertEqual(obj.SourceFile, "")
        finally:
            FreeCAD.closeDocument(target.Name)
