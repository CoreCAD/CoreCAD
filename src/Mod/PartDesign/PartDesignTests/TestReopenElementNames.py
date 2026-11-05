# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# #167: a reopened document gets its shapes back from the project cache, and their element
# names must mean what they meant before the save.

import os
import re
import tempfile
import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square


class TestReopenElementNames(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestReopenElementNames", type="Part")
        self.pad = PartDesign.makeFeature(_square(self.Doc, "S1"), "Pad")
        self.pad.Length = 10
        self.Doc.recompute()
        body = PartDesign.findBodyOf(self.pad)
        self.fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        self.fillet.Base = (self.pad, ["Edge1"])
        self.fillet.Radius = 1
        body.addFeature(self.fillet)
        self.Doc.recompute()
        self.assertTrue(self.fillet.isValid(), self.fillet.getStatusString())

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _histories(self):
        fillet = self.Doc.getObject("Fillet")
        return {
            face: [
                (source.Name, name, list(steps))
                for source, name, steps in fillet.getElementHistory(fillet.Name + "." + mapped)
            ]
            for mapped, face in fillet.Shape.ElementMap.items()
            if face.startswith("Face")
        }

    def _reopen(self):
        path = os.path.join(tempfile.mkdtemp(), "names.cpart")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)

    def testEveryFaceTracesTheSameHistoryAfterReopen(self):
        before = self._histories()
        self._reopen()
        self.assertEqual(self._histories(), before)

    def testEveryNumberInANameReadsTheSameStringAfterReopen(self):
        def strings(obj):
            numbers = {
                n for name in obj.Shape.ElementMap for n in re.findall(r"#([0-9a-f]+)", name)
            }
            table = obj.Shape.Hasher
            found = {n: table.getID(int(n, 16)) for n in numbers}
            return {n: sid.Data if sid else None for n, sid in found.items()}

        before = strings(self.Doc.getObject("Fillet"))
        self.assertTrue(before and None not in before.values())
        self._reopen()
        self.assertEqual(strings(self.Doc.getObject("Fillet")), before)

    def testReopenedShapesShareTheDocumentsStringTable(self):
        self._reopen()
        pad2 = PartDesign.makeFeature(_square(self.Doc, "S2", x0=30), "Pad")
        self.Doc.recompute()
        self.assertTrue(self.Doc.getObject("Fillet").Shape.Hasher.isSame(pad2.Shape.Hasher))

    def testTheFilletsEdgeIsFoundWithoutARebuild(self):
        edge = self.pad.Shape.getElementMappedName("Edge1")
        self._reopen()
        self.assertEqual(self.Doc.getObject("Pad").Shape.getElementIndexedName(edge), "Edge1")
        fillet = self.Doc.getObject("Fillet")
        fillet.enforceRecompute()
        self.Doc.recompute()
        self.assertTrue(fillet.isValid(), fillet.getStatusString())

    def _savedReference(self, path):
        with open(path, encoding="utf-8") as recipe:
            text = recipe.read()
        return text, re.search(r'<Target[^>]* sub="Edge1"(?: name="([^"]*)")?', text)

    def testASavedReferenceNamesItsEdgeInFull(self):
        # String-table numbers are handed out per session; one in a saved name would point at
        # whatever the next session gives that number.
        path = os.path.join(tempfile.mkdtemp(), "names.cpart")
        self.Doc.saveAs(path)
        _, reference = self._savedReference(path)
        self.assertIsNotNone(reference.group(1))
        self.assertNotRegex(reference.group(1), r"#[0-9a-f]")

    def testAReferenceFollowsItsNameWhenThePositionIsWrong(self):
        # Two branches that each add an edge can both store the same position for different
        # edges; after a merge only the name still says which edge was meant.
        meant = self.pad.Shape.getElement("Edge1").CenterOfMass
        volume = self.fillet.Shape.Volume
        path = os.path.join(tempfile.mkdtemp(), "names.cpart")
        self.Doc.saveAs(path)
        text, reference = self._savedReference(path)
        wrong = (
            text[: reference.start()]
            + reference.group(0).replace('sub="Edge1"', 'sub="Edge3"')
            + text[reference.end() :]
        )
        with open(path, "w", encoding="utf-8") as recipe:
            recipe.write(wrong)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)

        fillet = self.Doc.getObject("Fillet")
        fillet.enforceRecompute()
        self.Doc.recompute()
        base, subs = fillet.Base
        self.assertTrue(fillet.isValid(), fillet.getStatusString())
        self.assertLess((base.Shape.getElement(subs[0]).CenterOfMass - meant).Length, 1e-6)
        self.assertAlmostEqual(fillet.Shape.Volume, volume, places=6)

    def testAReferenceListThatCanCrossFilesFollowsItsName(self):
        meant = self.pad.Shape.getElement("Edge1").CenterOfMass
        self.fillet.addProperty("App::PropertyXLinkSubList", "Picked")
        self.fillet.Picked = [(self.pad, ["Edge1"])]
        path = os.path.join(tempfile.mkdtemp(), "names.cpart")
        self.Doc.saveAs(path)
        with open(path, encoding="utf-8") as recipe:
            text = recipe.read()
        picked = re.search(
            r'(<Property name="Picked".*?<Target[^>]* sub=")Edge1("(?: name="([^"]*)")?)',
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(picked.group(3))
        with open(path, "w", encoding="utf-8") as recipe:
            recipe.write(
                text[: picked.start()] + picked.expand(r"\1Edge3\2") + text[picked.end() :]
            )
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)

        (target, subs), *_ = self.Doc.getObject("Fillet").Picked
        self.assertLess((target.Shape.getElement(subs[0]).CenterOfMass - meant).Length, 1e-6)

    def testAReferenceListThatCanCrossFilesKeepsItsOrder(self):
        pads = [self.pad] + [
            PartDesign.makeFeature(_square(self.Doc, "S%d" % i, x0=30 * i), "Pad")
            for i in range(2, 6)
        ]
        self.Doc.recompute()
        stated = [p.Name for p in reversed(pads)]
        self.fillet.addProperty("App::PropertyXLinkList", "Sources")
        self.fillet.Sources = [self.Doc.getObject(n) for n in stated]
        self._reopen()
        self.assertEqual([o.Name for o in self.Doc.getObject("Fillet").Sources], stated)

    def testARebuildAfterReopenTagsNamesAsBefore(self):
        # Each name carries the integer id of the object that made it, so a reopened object must
        # have the id it was born with.
        def tags(obj):
            return {t for name in obj.Shape.ElementMap for t in re.findall(r":H([0-9a-f]+)", name)}

        before = tags(self.pad)
        self._reopen()
        pad = self.Doc.getObject("Pad")
        pad.enforceRecompute()
        self.Doc.recompute()
        self.assertEqual(tags(pad), before)
