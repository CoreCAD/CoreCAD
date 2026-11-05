# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #169: a pad must name its elements the same on its first build as on any later rebuild, or a
# fillet made after the first build loses its edge on the first edit.

import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square


class TestFirstBuildElementNames(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFirstBuildElementNames", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "S1"), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _names(self):
        return {element: name for name, element in self.pad.Shape.ElementMap.items()}

    def testARebuildKeepsEveryName(self):
        first = self._names()
        self.pad.Length = 20
        self.Doc.recompute()
        self.assertEqual(self._names(), first)

    def testAFilletSurvivesTheFirstEditOfItsPad(self):
        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (self.pad, ["Edge1"])
        fillet.Radius = 1
        PartDesign.findBodyOf(self.pad).addFeature(fillet)
        self.Doc.recompute()
        self.assertTrue(fillet.isValid(), fillet.getStatusString())
        self.pad.Length = 20
        self.Doc.recompute()
        self.assertTrue(fillet.isValid(), fillet.getStatusString())


if __name__ == "__main__":
    unittest.main()
