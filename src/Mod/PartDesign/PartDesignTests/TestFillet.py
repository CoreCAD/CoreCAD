# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************

from __future__ import division
from math import pi
import unittest

import FreeCAD


class TestFillet(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFillet", type="Part")

    def testFilletCubeToSphere(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Body.addFeature(self.Box)
        self.Box.Length = 10.00
        self.Box.Width = 10.00
        self.Box.Height = 10.00
        self.Doc.recompute()
        self.Fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        self.Fillet.Base = (self.Box, ["Face" + str(i + 1) for i in range(6)])
        self.Fillet.Radius = 4.999999
        self.Body.addFeature(self.Fillet)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        # test UseAllEdges property
        self.Fillet.UseAllEdges = True
        self.Fillet.Base = (self.Box, [""])  # no subobjects, should still work
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        self.Fillet.Base = (self.Box, ["Face50"])  # non-existent face, topo naming resilience
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        self.Fillet.UseAllEdges = False
        self.Fillet.Base = (self.Box, ["Face1"])
        self.Doc.recompute()
        self.assertNotAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)

    def _box_with_notch(self):
        """A 20x20x10 box with a notch cut through its top-front edge at x 5..10."""
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = 20.0
        box.Height = 10.0
        body.addFeature(box)
        notch = self.Doc.addObject("PartDesign::SubtractiveBox", "Notch")
        notch.Length = notch.Height = 5.0
        notch.Width = 4.0
        notch.Placement = FreeCAD.Placement(FreeCAD.Vector(5, -1, 6), FreeCAD.Rotation())
        body.addFeature(notch)
        self.Doc.recompute()
        return body, box, notch

    @staticmethod
    def _edge(shape, xmin, xmax, ymin, ymax, zmin, zmax):
        want = (xmin, xmax, ymin, ymax, zmin, zmax)
        for index, edge in enumerate(shape.Edges, 1):
            bb = edge.BoundBox
            got = (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax)
            if all(abs(a - b) < 1e-6 for a, b in zip(got, want)):
                return "Edge%d" % index
        raise AssertionError("no edge with bounds %s" % (want,))

    def _fillet_then_delete_notch(self, pick):
        """Fillets the notch edges `pick` returns, deletes the notch, returns (box, fillet)."""
        body, box, notch = self._box_with_notch()
        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (notch, pick(notch.Shape))
        fillet.Radius = 0.5
        body.addFeature(fillet)
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        body.removeFeature(notch)
        self.Doc.removeObject(notch.Name)
        self.Doc.recompute()
        return box, fillet

    def testDeletedFeatureNeverTouchedTheEdge(self):
        box, fillet = self._fillet_then_delete_notch(
            lambda s: [self._edge(s, 20, 20, 20, 20, 0, 10)]
        )
        self.assertIs(fillet.Base[0], box)
        self.assertEqual(list(fillet.Base[1]), [self._edge(box.Shape, 20, 20, 20, 20, 0, 10)])
        self.assertTrue(fillet.isValid())

    def testEdgeTheDeletedFeatureCreatedFails(self):
        box, fillet = self._fillet_then_delete_notch(lambda s: [self._edge(s, 5, 5, 0, 0, 6, 10)])
        self.assertIsNone(fillet.Base)
        self.assertFalse(fillet.isValid())

    def testEdgeTheDeletedFeatureTrimmedFollowsBackToTheWholeEdge(self):
        box, fillet = self._fillet_then_delete_notch(lambda s: [self._edge(s, 0, 5, 0, 0, 10, 10)])
        self.assertIs(fillet.Base[0], box)
        self.assertEqual(list(fillet.Base[1]), [self._edge(box.Shape, 0, 20, 0, 0, 10, 10)])
        self.assertTrue(fillet.isValid())

    def testBothTrimmedPiecesFollowBackToOneEdge(self):
        box, fillet = self._fillet_then_delete_notch(
            lambda s: [self._edge(s, 0, 5, 0, 0, 10, 10), self._edge(s, 10, 20, 0, 0, 10, 10)]
        )
        self.assertIs(fillet.Base[0], box)
        self.assertEqual(list(fillet.Base[1]), [self._edge(box.Shape, 0, 20, 0, 0, 10, 10)])
        self.assertTrue(fillet.isValid())

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("PartDesignTestFillet")
        # print ("omit closing document for debugging")
