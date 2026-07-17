# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Locks PartDesign.makeFeature — the scriptable body-emergence path (Cruth §4.6/§8.5).
# A Body EMERGES from the first solid feature; scripts must never addObject a Body up
# front. makeFeature composes the GUI-shared primitives (resolveBaseBody + spawnBody +
# Body.addFeature) so a script and a click produce identical structure.

import unittest

import FreeCAD
import Part
import Sketcher
import PartDesign
from FreeCAD import Vector


def _square(doc, name, x0=0.0, y0=0.0, side=10.0):
    """A closed, coincident-constrained square sketch, born free (no Body)."""
    sk = doc.addObject("Sketcher::SketchObject", name)
    pts = [(x0, y0), (x0 + side, y0), (x0 + side, y0 + side), (x0, y0 + side)]
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sk.addGeometry(Part.LineSegment(Vector(a[0], a[1], 0), Vector(b[0], b[1], 0)), False)
    for i in range(4):
        sk.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))
    doc.recompute()
    return sk


class TestBodyEmergence(unittest.TestCase):
    def setUp(self):
        # A CAD (Part-type) document mints the shared world frame makeFeature needs.
        self.Doc = FreeCAD.newDocument("PartDesignTestBodyEmergence", type="Part")

    def _bodies(self):
        return [o for o in self.Doc.Objects if o.TypeId == "PartDesign::Body"]

    def testSpawnsBodyFromFeature(self):
        # A born-free sketch reaches no Body: makeFeature must spawn one and the
        # solid must emerge from the feature.
        sk = _square(self.Doc, "S1")
        self.assertIsNone(PartDesign.resolveBaseBody(sk))  # nothing to extend yet

        pad = PartDesign.makeFeature(sk, "Pad")
        pad.Length = 5
        self.Doc.recompute()

        body = PartDesign.findBodyOf(pad)
        self.assertIsNotNone(body)
        self.assertEqual(body.Tip, pad)
        self.assertEqual(len(self._bodies()), 1)
        self.assertAlmostEqual(body.Shape.Volume, 500.0, places=3)  # 10 * 10 * 5

    def testIndependentSketchesSpawnDistinctBodies(self):
        # A second born-free sketch reaches no Body either, so it must start its
        # own Body — not silently fold into the first.
        pad1 = PartDesign.makeFeature(_square(self.Doc, "S1"), "Pad")
        pad2 = PartDesign.makeFeature(_square(self.Doc, "S2", x0=50), "Pad")
        self.Doc.recompute()

        b1 = PartDesign.findBodyOf(pad1)
        b2 = PartDesign.findBodyOf(pad2)
        self.assertIsNotNone(b1)
        self.assertIsNotNone(b2)
        self.assertNotEqual(b1, b2)
        self.assertEqual(len(self._bodies()), 2)

    def testExplicitBodyExtends(self):
        # Passing body= force-extends that Body instead of spawning: the new
        # feature joins its pipeline and becomes the Tip.
        pad = PartDesign.makeFeature(_square(self.Doc, "S1"), "Pad")
        pad.Length = 5
        self.Doc.recompute()
        body = PartDesign.findBodyOf(pad)

        pocket = PartDesign.makeFeature(
            _square(self.Doc, "S2", x0=2, y0=2, side=4), "Pocket", body=body
        )
        pocket.Length = 2
        self.Doc.recompute()

        self.assertEqual(PartDesign.findBodyOf(pocket), body)
        self.assertEqual(body.Tip, pocket)
        self.assertEqual(len(self._bodies()), 1)  # no new Body spawned

    def testFullTypeNameAccepted(self):
        # Both the short ('Pad') and fully-qualified ('PartDesign::Pad') forms work.
        pad = PartDesign.makeFeature(_square(self.Doc, "S1"), "PartDesign::Pad")
        self.assertTrue(pad.isDerivedFrom("PartDesign::Pad"))
        self.assertIsNotNone(PartDesign.findBodyOf(pad))

    def testRequiresCadDocument(self):
        # Without a world frame there is no coordinate system to spawn a Body onto;
        # makeFeature must fail loudly rather than mint one off the Body.
        plain = FreeCAD.newDocument("PartDesignTestNonCad")  # untyped: no world frame
        try:
            sk = plain.addObject("Sketcher::SketchObject", "S1")
            sk.addGeometry(Part.LineSegment(Vector(0, 0, 0), Vector(10, 0, 0)), False)
            plain.recompute()
            with self.assertRaises(Exception):
                PartDesign.makeFeature(sk, "Pad")
        finally:
            FreeCAD.closeDocument(plain.Name)

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)


if __name__ == "__main__":
    unittest.main()
