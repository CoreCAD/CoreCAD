# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

# A script can ask which bodies overlap and acknowledge a pair, the same as the Check
# Interference dialog (Amendment 20, Clause 20.2 rule 6; Clause 20.1).

import os
import tempfile
import unittest

import FreeCAD
import PartDesign
from PartDesignTests.TestBodyEmergence import _square


def _uids(pairs):
    return {frozenset((a.Uid, b.Uid)) for a, b in pairs}


class TestOverlapAccess(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestOverlapAccess", type="Part")
        self.a = self._body("S1", x0=0)
        self.b = self._body("S2", x0=5)
        self.Doc.recompute()

    def _body(self, name, x0):
        pad = PartDesign.makeFeature(_square(self.Doc, name, x0=x0), "Pad")
        pad.Length = 10
        return PartDesign.findBodyOf(pad)

    def _pair(self):
        return {frozenset((self.a.Uid, self.b.Uid))}

    def testOverlapIsReported(self):
        self.assertEqual(_uids(PartDesign.overlappingPairs(self.Doc)), self._pair())

    def testSeparateBodiesAreNotReported(self):
        self._body("S3", x0=100)
        self.Doc.recompute()
        self.assertEqual(_uids(PartDesign.overlappingPairs(self.Doc)), self._pair())

    def testAcknowledgedPairIsLeftOutUnlessAsked(self):
        PartDesign.acknowledgeOverlap(self.a, self.b)
        self.assertEqual(PartDesign.overlappingPairs(self.Doc), [])
        self.assertEqual(_uids(PartDesign.overlappingPairs(self.Doc, True)), self._pair())

    def testAcknowledgementSurvivesReopen(self):
        PartDesign.acknowledgeOverlap(self.a, self.b)
        pair = self._pair()
        path = os.path.join(tempfile.mkdtemp(), "overlap.FCStd")
        self.Doc.saveAs(path)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(path)
        self.Doc.recompute()
        self.assertEqual(PartDesign.overlappingPairs(self.Doc), [])
        self.assertEqual(_uids(PartDesign.overlappingPairs(self.Doc, True)), pair)

    def testOnlyTwoDifferentBodiesCanBeAcknowledged(self):
        with self.assertRaises(TypeError):
            PartDesign.acknowledgeOverlap(self.a, self.a.Tip)
        with self.assertRaises(ValueError):
            PartDesign.acknowledgeOverlap(self.a, self.a)

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)
