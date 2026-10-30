# ***************************************************************************
# *   Copyright (c) 2003 Juergen Riegel <juergen.riegel@web.de>             *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful,            *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with FreeCAD; if not, write to the Free Software        *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************/

import unittest

import FreeCAD
from DocumentTestSupport import _placedGroup


class DocumentGroupCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("GroupTests")

    def testGroup(self):
        G1 = self.groupMembership()
        self.Doc.UndoMode = 1
        self.removalOrdersUndo(G1)
        self.removeSeveralThenUndo(G1)
        self.Doc.UndoMode = 0

        self.Doc.removeObject("Group")
        self.Doc.removeObject("Label_2")
        self.Doc.removeObject("Label_3")

    def groupMembership(self):
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        G1 = self.Doc.addObject("App::DocumentObjectGroup", "Group")
        G1.addObject(L2)
        self.assertTrue(G1.hasObject(L2))

        # Adding the group to itself must fail
        try:
            G1.addObject(G1)
        except Exception:
            FreeCAD.Console.PrintLog("Cannot add group to itself, OK\n")
        else:
            self.fail("Adding the group to itself must not be possible")
        return G1

    def removalOrdersUndo(self, G1):
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first group and then the object
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Group")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first object and then the group in two transactions
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.Doc.commitTransaction()
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

        # Remove first object and then the group in one transaction
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_2") is not None)

    def removeSeveralThenUndo(self, G1):
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        G1.addObject(L3)
        self.Doc.openTransaction("Remove")
        self.Doc.removeObject("Label_2")
        self.assertTrue(G1.getObject("Label_2") is None)
        self.Doc.removeObject("Label_3")
        self.assertTrue(G1.getObject("Label_3") is None)
        self.Doc.removeObject("Group")
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(G1.getObject("Label_3") is not None)
        self.assertTrue(G1.getObject("Label_2") is not None)

    def testGroupAndGeoFeatureGroup(self):
        obj1, grp1, grp2 = self.oneGroupAtATime()
        prt1, prt2 = self.groupInsideGeoFeatureGroup(obj1, grp2)
        self.oneGeoFeatureGroupAtATime(obj1, grp1, grp2, prt1, prt2)
        self.crossLinksBetweenGeoFeatureGroups(prt1, prt2)
        self.cyclicGrouping(prt1, prt2)

    def oneGroupAtATime(self):
        # an object can only be in one group at once, that must be enforced
        obj1 = self.Doc.addObject("App::FeatureTest", "obj1")
        grp1 = self.Doc.addObject("App::DocumentObjectGroup", "Group1")
        grp2 = self.Doc.addObject("App::DocumentObjectGroup", "Group2")
        grp1.addObject(obj1)
        self.assertTrue(obj1.getParentGroup() == grp1)
        self.assertTrue(obj1.getParentGeoFeatureGroup() is None)
        self.assertTrue(grp1.hasObject(obj1))
        grp2.addObject(obj1)
        self.assertTrue(grp1.hasObject(obj1) == False)
        self.assertTrue(grp2.hasObject(obj1))
        return obj1, grp1, grp2

    def groupInsideGeoFeatureGroup(self, obj1, grp2):
        # an object is allowed to be in a group and a geofeaturegroup
        prt1 = _placedGroup(self.Doc, "Part1")
        prt2 = _placedGroup(self.Doc, "Part2")

        prt1.addObject(grp2)
        self.assertTrue(grp2.getParentGeoFeatureGroup() == prt1)
        self.assertTrue(grp2.getParentGroup() is None)
        self.assertTrue(grp2.hasObject(obj1))
        self.assertTrue(prt1.hasObject(grp2))
        self.assertTrue(prt1.hasObject(obj1))
        return prt1, prt2

    def oneGeoFeatureGroupAtATime(self, obj1, grp1, grp2, prt1, prt2):
        # it is not allowed to be in 2 geofeaturegroups
        prt2.addObject(grp2)
        self.assertTrue(grp2.hasObject(obj1))
        self.assertTrue(prt1.hasObject(grp2) == False)
        self.assertTrue(prt1.hasObject(obj1) == False)
        self.assertTrue(prt2.hasObject(grp2))
        self.assertTrue(prt2.hasObject(obj1))
        try:
            grp = prt1.Group
            grp.append(obj1)
            prt1.Group = grp
        except Exception:
            grp.remove(obj1)
            self.assertTrue(prt1.Group == grp)
        else:
            self.fail("No exception thrown when object is in multiple Groups")

        # it is not allowed to be in 2 Groups
        prt2.addObject(grp1)
        grp = grp1.Group
        grp.append(obj1)
        try:
            grp1.Group = grp
        except Exception:
            pass
        else:
            self.fail("No exception thrown when object is in multiple Groups")

    def crossLinksBetweenGeoFeatureGroups(self, prt1, prt2):
        # cross linking between GeoFeatureGroups is not allowed
        self.Doc.recompute()
        box = self.Doc.addObject("App::FeatureTest", "Box")
        cyl = self.Doc.addObject("App::FeatureTest", "Cylinder")
        fus = self.Doc.addObject("App::FeatureTest", "Fusion")
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")
        fus.LinkList = (
            []
        )  # remove all links as addObject would otherwise transfer all linked objects
        prt1.addObject(cyl)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        # should be Invalid: links may not cross geo-feature groups; not enforced yet
        fus.LinkList = []
        prt1.addObject(box)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        # should be Invalid: links may not cross geo-feature groups; not enforced yet
        fus.LinkList = []
        prt1.addObject(fus)
        fus.LinkList = [cyl, box]
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")
        prt2.addObject(box)  # this time addObject should move all dependencies to the new part
        self.Doc.recompute()
        self.assertTrue(fus.State[0] == "Up-to-date")

    def cyclicGrouping(self, prt1, prt2):
        # grouping must survive cyclic links without crashing
        prt1.addObject(prt2)
        grp = prt2.Group
        grp.append(prt1)
        prt2.Group = grp
        self.Doc.recompute()
        prt2.Group = []
        try:
            prt2.Group = [prt2]
        except Exception:
            pass
        else:
            self.fail("Exception is expected")

        self.Doc.recompute()

    def testContainerChainGroupInPart(self):
        # ContainerChain must not raise when a plain group is nested inside a GeoFeatureGroup
        from Show.Containers import ContainerChain

        part = _placedGroup(self.Doc, "Part")
        group = self.Doc.addObject("App::DocumentObjectGroup", "Group")
        obj = self.Doc.addObject("App::FeatureTest", "Obj")
        part.addObject(group)
        group.addObject(obj)
        self.Doc.recompute()

        chain = ContainerChain(obj)
        objects_in_chain = [c for c in chain if not c.isDerivedFrom("App::Document")]
        self.assertIn(part, objects_in_chain)
        self.assertIn(group, objects_in_chain)

    def testIssue0003150Part2(self):
        self.box = self.Doc.addObject("App::FeatureTest")
        self.cyl = self.Doc.addObject("App::FeatureTest")
        self.sph = self.Doc.addObject("App::FeatureTest")

        self.fus1 = self.Doc.addObject("App::FeatureTest")
        self.fus2 = self.Doc.addObject("App::FeatureTest")

        self.fus1.LinkList = [self.box, self.cyl]
        self.fus2.LinkList = [self.sph, self.cyl]

        self.prt = _placedGroup(self.Doc, "Part")
        self.prt.addObject(self.fus1)
        self.assertTrue(len(self.prt.Group) == 5)
        self.assertTrue(self.fus2.getParentGeoFeatureGroup() == self.prt)
        self.assertTrue(self.prt.hasObject(self.sph))

        self.prt.removeObject(self.fus1)
        self.assertTrue(len(self.prt.Group) == 0)

    def tearDown(self):
        FreeCAD.closeDocument("GroupTests")
