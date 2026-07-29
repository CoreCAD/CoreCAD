# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Locks the Cruth §4.6/§4.7 body lifecycle when a Tip feature is deleted directly via
# Document.removeObject (a script / MCP path that bypasses Body.removeFeature). Without the
# retire-or-retreat hook the Bodies tipped by the deleted feature survive as zombies (Tip
# nulled to None, no resolvable component) — CoreCAD/CoreCAD#2.
#
# The honest end-state matches the GUI feature-delete path exactly:
#   * A multi-output Tip (a MultiBody pattern) that HAS a base retreats every emitted Body
#     onto that base; the reconciler folds them into the single Body the base accounts for.
#     No zombie survives, and the base is NOT left as a feature without a Body.
#   * A single-output Tip that extends a chain retreats its one Body onto the base.
#   * A root Tip with no base has nothing to fall back on, so its Body retires (§4.6).

import unittest

import FreeCAD


def _x_axis(doc):
    origin = next(o for o in doc.Objects if o.isDerivedFrom("App::Origin"))
    return next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == "X_Axis")


def _bodies(doc):
    return [o for o in doc.Objects if o.isDerivedFrom("PartDesign::Body")]


class TestMultiOutputDelete(unittest.TestCase):
    def setUp(self):
        # A CAD (Part-type) document mints the shared document-level Origin the pattern anchors to.
        self.Doc = FreeCAD.newDocument("PartDesignTestMultiOutputDelete", type="Part")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)

    def _multibody_pattern(self, occurrences=4, length=90.0):
        """A MultiBody 'Whole shape' LinearPattern of a 10-cube along X, well separated so every
        instance is its own disjoint solid: emits one Body per instance. Returns (base, pattern)."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()

        lp = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        lp.TransformMode = "Whole shape"
        lp.MultiBody = True
        lp.Direction = (_x_axis(self.Doc), [""])
        lp.Length = length
        lp.Occurrences = occurrences
        body.addFeature(lp)
        self.Doc.recompute()
        return box, lp

    def testDeleteMultiOutputTipRetreatsToBase(self):
        box, lp = self._multibody_pattern(occurrences=4)
        self.assertEqual(len(_bodies(self.Doc)), 4)

        # Raw removeObject — the script / MCP path that bypasses Body.removeFeature.
        self.Doc.removeObject(lp.Name)
        self.Doc.recompute()

        # Converges to the single Body the base accounts for; no zombie, no orphaned feature.
        survivors = _bodies(self.Doc)
        self.assertEqual(len(survivors), 1)
        self.assertIs(survivors[0].Tip, box)
        self.assertEqual(len(survivors[0].Shape.Solids), 1)
        self.assertNotIn("Touched", survivors[0].State)
        self.assertNotIn("Invalid", survivors[0].State)
        self.assertIn(box, self.Doc.Objects)  # the base is NOT left bodiless

    def testDeleteSingleOutputTipRetreatsToBase(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()

        box2 = self.Doc.addObject("PartDesign::AdditiveBox", "Box2")
        body.addFeature(box2)
        box2.Length = 20.0
        box2.Width = box2.Height = 10.0
        box2.BaseFeature = box
        self.Doc.recompute()
        self.assertIs(body.Tip, box2)

        self.Doc.removeObject(box2.Name)
        self.Doc.recompute()

        survivors = _bodies(self.Doc)
        self.assertEqual(len(survivors), 1)
        self.assertIs(survivors[0].Tip, box)
        self.assertNotIn("Invalid", survivors[0].State)

    def testDeleteRootTipRetiresBody(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()
        self.assertEqual(len(_bodies(self.Doc)), 1)

        # The root feature has no base to retreat onto — its Body has nothing left to account
        # for and retires (§4.6). No zombie survives.
        self.Doc.removeObject(box.Name)
        self.Doc.recompute()
        self.assertEqual(len(_bodies(self.Doc)), 0)


if __name__ == "__main__":
    unittest.main()
