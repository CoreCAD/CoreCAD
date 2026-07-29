# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Locks the MultiBody pattern break-out contract (Cruth ARCHITECTURE §5.6 / §4.7).
#
# A "Whole shape" pattern with MultiBody emits one Body per instance (the multi-output
# reconciler). Break-out re-homes a selected instance into an independent BakedShape Body
# and records a skip so the pattern drops that instance and never silently re-merges it.
#
# Two contracts are guarded here:
#   * The skip keys on each instance's ORIGINAL ORDINAL (index in the transform sequence),
#     translated once from the selected Body's component-id where the ids are self-consistent.
#     An earlier design keyed it on the element-map component-id, which is context-dependent
#     and silently failed to match at execute time — especially on a second break-out. (#4)
#   * Break-out of the instance sitting at the origin position must NOT destroy the shared
#     world frame: the document-level Origin is owned by no Body, so the pattern's Direction
#     (a link to that Origin's X_Axis) survives and the survivors never collapse to zero. (#4)

import unittest

import FreeCAD


def _x_axis(doc):
    origin = next(o for o in doc.Objects if o.isDerivedFrom("App::Origin"))
    return next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == "X_Axis")


class TestBreakOut(unittest.TestCase):
    def setUp(self):
        # A CAD (Part-type) document mints the shared document-level Origin the pattern's
        # Direction anchors to.
        self.Doc = FreeCAD.newDocument("PartDesignTestBreakOut", type="Part")

    def _pattern(self, occurrences=4, length=90.0):
        """A MultiBody 'Whole shape' LinearPattern of a 10-cube along X, well separated so
        every instance is its own disjoint solid. Returns the pattern feature."""
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
        return lp

    def _instances(self, lp):
        """Emitted instance Bodies, left-to-right by x."""
        return sorted(
            [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip is lp],
            key=lambda b: b.Shape.Solids[0].CenterOfMass.x,
        )

    def _solid_x(self, lp):
        return sorted(round(s.CenterOfMass.x, 1) for s in lp.Shape.Solids)

    def testBreakOutDropsSelectedInstance(self):
        lp = self._pattern(occurrences=4, length=90.0)  # spacing 30 -> x = 5,35,65,95
        self.assertEqual(len(self._instances(lp)), 4)
        self.assertEqual(len(lp.Shape.Solids), 4)
        self.assertEqual(list(lp.SkipInstances), [])

        # Break out the second instance (kept-position 1 -> ordinal 1).
        target = self._instances(lp)[1]
        target_x = target.Shape.Solids[0].CenterOfMass.x
        newb = target.breakOutInstance()
        self.Doc.recompute()

        self.assertIsNotNone(newb)
        self.assertEqual(list(lp.SkipInstances), [1])
        self.assertEqual(len(lp.Shape.Solids), 3)
        # The broken-out Body is independent (frozen BakedShape, no pattern link) and in place.
        self.assertTrue(newb.Tip.isDerivedFrom("PartDesign::BakedShape"))
        self.assertAlmostEqual(newb.Shape.Volume, 1000.0, places=3)
        self.assertAlmostEqual(newb.Shape.Solids[0].CenterOfMass.x, target_x, places=3)
        self.assertNotIn(target, self.Doc.Objects)  # originating instance Body retired (§4.7)

    def testSecondBreakOutRemapsOrdinal(self):
        # The crux of the ordinal contract: a second break-out must step over the already
        # -skipped ordinal. Under the old component-id design this silently failed to match.
        lp = self._pattern(occurrences=4, length=90.0)
        self._instances(lp)[1].breakOutInstance()
        self.Doc.recompute()
        self.assertEqual(list(lp.SkipInstances), [1])

        # Now the second surviving instance is original ordinal 2.
        self._instances(lp)[1].breakOutInstance()
        self.Doc.recompute()
        self.assertEqual(sorted(lp.SkipInstances), [1, 2])
        self.assertEqual(len(lp.Shape.Solids), 2)

    def testBreakOutAtOriginPositionKeepsWorldFrame(self):
        # #4: breaking out the instance at the origin position must not delete the shared
        # world frame the pattern depends on, nor collapse the survivors onto x=0.
        lp = self._pattern(occurrences=3, length=120.0)  # x = 5, 65, 125
        xaxis = _x_axis(self.Doc)
        self.assertEqual(self._solid_x(lp), [5.0, 65.0, 125.0])

        self._instances(lp)[0].breakOutInstance()  # the x=5 (origin-position) instance
        self.Doc.recompute()

        self.assertIn(xaxis, self.Doc.Objects)  # world frame intact
        self.assertIsNotNone(lp.Direction)  # Direction not silently nulled
        self.assertEqual(self._solid_x(lp), [65.0, 125.0])  # survivors NOT collapsed to ~0
