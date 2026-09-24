# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# Locks the SECOND of a pattern's two merge decisions (Cruth ARCHITECTURE §5.5 / §8.5, #34).
#
# A pattern answers two independent questions, and only one of them is its own:
#   * Does the result join the Body the pattern was added to? That is the ordinary §8.5
#     Merge Result gesture, asked of every solid feature alike and answered by the
#     BaseFeature chain. Not this file's subject.
#   * Do the COPIES join EACH OTHER? That is MultiBody, and it is what is guarded here.
#
# Until now MultiBody was read only in "Whole shape" mode and ignored in the ordinary
# Features mode, so a feature pattern whose copies overlapped fused them into one lump with
# no way to ask for anything else — the silent collapse of #34 with no second option. The
# copies are now held back from the fuse and emitted beside the support, which gives one
# Body per copy through the multi-output reconciler, exactly as §5.5 describes.
#
# The support is not one of the copies: the untransformed original was spliced into the
# chain before the pattern existed and cannot be peeled back out, so N copies come back as
# the support plus N-1 loose solids.

import unittest

import FreeCAD


def _origin_feature(doc, role):
    origin = next(o for o in doc.Objects if o.isDerivedFrom("App::Origin"))
    return next(f for f in origin.OriginFeatures if getattr(f, "Role", "") == role)


def _x_axis(doc):
    return _origin_feature(doc, "X_Axis")


class TestKeepCopiesApart(unittest.TestCase):
    def setUp(self):
        # A CAD (Part-type) document mints the shared document-level Origin the pattern's
        # Direction anchors to.
        self.Doc = FreeCAD.newDocument("PartDesignTestKeepCopiesApart", type="Part")

    def _pattern(self, keep_apart, length, occurrences=4):
        """A Features-mode LinearPattern of a 10-cube along X. length=15 puts the copies
        1.7 apart so they run into each other; length=90 separates them completely."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()

        lp = self.Doc.addObject("PartDesign::LinearPattern", "LinearPattern")
        lp.Originals = [box]
        lp.Direction = (_x_axis(self.Doc), [""])
        lp.Length = length
        lp.Occurrences = occurrences
        lp.Refine = False
        lp.MultiBody = keep_apart
        body.addFeature(lp)
        self.Doc.recompute()
        return lp

    def _emitted(self, lp):
        return [o for o in self.Doc.Objects if o.isDerivedFrom("PartDesign::Body") and o.Tip is lp]

    # --- the gap this change closes -------------------------------------------------

    def testOverlappingCopiesStayApartWhenAsked(self):
        # The case #34 was raised for: four copies that run into each other. Asked to keep
        # them apart, the pattern must still produce four pieces, interpenetrating or not.
        lp = self._pattern(keep_apart=True, length=15.0)
        self.assertEqual(len(lp.Shape.Solids), 4)
        self.assertEqual(len(self._emitted(lp)), 4)

    def testOverlappingCopiesFuseWhenNotAsked(self):
        # The default is unchanged: §8.5 makes merging the default for a feature whose
        # inputs trace back to a Body, and a pattern's do.
        lp = self._pattern(keep_apart=False, length=15.0)
        self.assertEqual(len(lp.Shape.Solids), 1)

    def testSeparatedCopiesAreUnaffectedByTheChoice(self):
        # Copies that never touched come back as four pieces either way — the choice only
        # bites where the geometry would otherwise have merged them.
        for keep_apart in (False, True):
            with self.subTest(keep_apart=keep_apart):
                self.setUp()
                lp = self._pattern(keep_apart=keep_apart, length=90.0)
                self.assertEqual(len(lp.Shape.Solids), 4)

    # --- the counts the dialog reads ------------------------------------------------

    def testCollapseIsRecordedNotJustLogged(self):
        # The shortfall has to be READABLE, not only watchable in a console line: this is
        # what lets the dialog offer the choice at the moment it becomes real.
        lp = self._pattern(keep_apart=False, length=15.0)
        self.assertEqual(lp.InstancesRequested, 4)
        self.assertEqual(lp.InstancePieces, 1)

    def testNoCollapseIsRecordedAsSuch(self):
        # "They all stayed apart" is an answer too. Equal counts, not zeros — zeros mean
        # the question never arose.
        lp = self._pattern(keep_apart=False, length=90.0)
        self.assertEqual(lp.InstancesRequested, 4)
        self.assertEqual(lp.InstancePieces, 4)

    def testTheQuestionDoesNotAriseWhenCopiesAreKeptApart(self):
        # Nothing was fused, so nothing collapsed and there is nothing to report.
        lp = self._pattern(keep_apart=True, length=15.0)
        self.assertEqual(lp.InstancesRequested, 0)
        self.assertEqual(lp.InstancePieces, 0)

    def testCountsDoNotSurviveTheConditionThatProducedThem(self):
        # A stale pair would have the dialog offering a choice about an overlap that is no
        # longer there. Spread the copies and the recorded answer must follow.
        lp = self._pattern(keep_apart=False, length=15.0)
        self.assertEqual(lp.InstancePieces, 1)
        lp.Length = 90.0
        self.Doc.recompute()
        self.assertEqual(lp.InstancePieces, 4)

    # --- copies that touch rather than overlap (#126) --------------------------------

    def testCopiesTouchingFaceToFaceAreRecordedAsJoined(self):
        # Pitch equal to the part's own width: the copies share no volume, but the fuse welds
        # them at the shared faces all the same. Four asked for, one came back.
        lp = self._pattern(keep_apart=False, length=30.0)
        self.assertEqual(len(lp.Shape.Solids), 1)
        self.assertEqual(lp.InstancesRequested, 4)
        self.assertEqual(lp.InstancePieces, 1)

    def testTouchingCopiesStayApartWhenAsked(self):
        # The choice the notice points at has to work for this case too.
        lp = self._pattern(keep_apart=True, length=30.0)
        self.assertEqual(len(lp.Shape.Solids), 4)

    def testMirrorAcrossTheFaceItSitsOnIsRecordedAsJoined(self):
        # The case #126 was found with. A mirror is one more pattern and gets the same rule.
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addFeature(box)
        box.Length = box.Width = box.Height = 10.0
        self.Doc.recompute()

        mirrored = self.Doc.addObject("PartDesign::Mirrored", "Mirrored")
        mirrored.Originals = [box]
        mirrored.MirrorPlane = (_origin_feature(self.Doc, "XY_Plane"), [""])
        mirrored.Refine = False
        body.addFeature(mirrored)
        self.Doc.recompute()

        self.assertEqual(len(mirrored.Shape.Solids), 1)
        self.assertEqual(mirrored.InstancesRequested, 2)
        self.assertEqual(mirrored.InstancePieces, 1)

    # --- §5.6 break-out reaches this path too ---------------------------------------

    def testBrokenOutInstanceIsDroppedFromAFeaturePattern(self):
        # Keeping the copies apart makes each one a Body, which makes break-out reachable
        # in Features mode for the first time. The skip-list has to be honoured here or a
        # detached instance would silently come back on the next recompute (§5.6).
        lp = self._pattern(keep_apart=True, length=90.0)
        instances = sorted(self._emitted(lp), key=lambda b: b.Shape.Solids[0].CenterOfMass.x)
        self.assertEqual(len(instances), 4)

        # The first emitted Body is the support (it holds the untransformed original), so
        # break out a later one: those are the transformed copies the skip-list addresses.
        detached = instances[2].breakOutInstance()
        self.Doc.recompute()

        self.assertIsNotNone(detached)
        self.assertEqual(len(lp.Shape.Solids), 3)

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)


if __name__ == "__main__":
    unittest.main()
