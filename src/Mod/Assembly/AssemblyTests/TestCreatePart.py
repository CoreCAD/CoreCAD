# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Assembly's "Insert New Part" seeds a fresh Part document and links its Body into the
# assembly (#69). The document is the container, so no container object may be made, and a
# Body only ever emerges from its first feature.

import unittest

import FreeCAD as App
import UtilsAssembly


class TestCreatePart(unittest.TestCase):
    def setUp(self):
        self.doc = App.newDocument("AssemblyTestCreatePart", type="Part")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testSeedsABodyWithASolid(self):
        body = UtilsAssembly.createPart(self.doc)
        self.assertTrue(body.isDerivedFrom("PartDesign::Body"))
        self.assertTrue(body.Shape.isValid())
        self.assertEqual(len(body.Shape.Solids), 1)

    def testMakesNoContainerObject(self):
        UtilsAssembly.createPart(self.doc)
        self.assertEqual([o for o in self.doc.Objects if o.TypeId == "App::Part"], [])
