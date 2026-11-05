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

import os
import math
import tempfile
import unittest

import FreeCAD


class DocumentRecomputeCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("RecomputeTests")
        self.L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        self.L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        self.L3 = self.Doc.addObject("App::FeatureTest", "Label_3")

    def testDescent(self):
        FreeCAD.Console.PrintLog("def testDescent(self):Testcase not implemented\n")
        self.L1.Link = self.L2
        self.L2.Link = self.L3

    def testRecompute(self):
        L1, L2, L3, L4, L5, L6, L7, L8 = self.buildRecomputeGraph()
        self.assertTopologicalOrder(L1, L2, L3, L5)
        self.recomputeOnlyWhatIsTouched(L1, L2, L3, L4, L5, L6)
        for obj in (L1, L2, L3, L4, L5, L6, L7, L8):
            self.Doc.removeObject(obj.Name)

    def buildRecomputeGraph(self):
        # sequence to test recompute behaviour
        #       L1---\    L7
        #      /  \   \    |
        #    L2   L3   \  L8
        #   /  \ /  \  /
        #  L4   L5   L6

        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        L4 = self.Doc.addObject("App::FeatureTest", "Label_4")
        L5 = self.Doc.addObject("App::FeatureTest", "Label_5")
        L6 = self.Doc.addObject("App::FeatureTest", "Label_6")
        L7 = self.Doc.addObject("App::FeatureTest", "Label_7")
        L8 = self.Doc.addObject("App::FeatureTest", "Label_8")
        L1.LinkList = [L2, L3, L6]
        L2.Link = L4
        L2.LinkList = [L5]
        L3.LinkList = [L5, L6]
        L7.Link = L8  # make second root

        self.assertTrue(L7 in self.Doc.RootObjects)
        self.assertTrue(L1 in self.Doc.RootObjects)
        return L1, L2, L3, L4, L5, L6, L7, L8

    def assertTopologicalOrder(self, L1, L2, L3, L5):
        self.assertTrue(len(self.Doc.Objects) == len(self.Doc.TopologicalSortedObjects))

        seqDic = {}
        i = 0
        for obj in self.Doc.TopologicalSortedObjects:
            seqDic[obj] = i
            print(obj)
            i += 1

        self.assertTrue(seqDic[L2] > seqDic[L1])
        self.assertTrue(seqDic[L3] > seqDic[L1])
        self.assertTrue(seqDic[L5] > seqDic[L2])
        self.assertTrue(seqDic[L5] > seqDic[L3])
        self.assertTrue(seqDic[L5] > seqDic[L1])

    def recomputeOnlyWhatIsTouched(self, L1, L2, L3, L4, L5, L6):
        objs = (L1, L2, L3, L4, L5, L6)
        self.assertExecCounts(objs, (0, 0, 0, 0, 0, 0))
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertExecCounts(objs, (1, 1, 1, 0, 0, 0))
        L5.enforceRecompute()
        self.assertExecCounts(objs, (1, 1, 1, 0, 0, 0))
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertExecCounts(objs, (2, 2, 2, 0, 1, 0))
        L4.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 3)
        self.assertExecCounts(objs, (3, 3, 2, 1, 1, 0))
        L5.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 4)
        self.assertExecCounts(objs, (4, 4, 3, 1, 2, 0))
        L6.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 3)
        self.assertExecCounts(objs, (5, 4, 4, 1, 2, 1))
        L2.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 2)
        self.assertExecCounts(objs, (6, 5, 4, 1, 2, 1))
        L1.enforceRecompute()
        self.assertTrue(self.Doc.recompute() == 1)
        self.assertExecCounts(objs, (7, 5, 4, 1, 2, 1))

    def assertExecCounts(self, objs, counts):
        self.assertTrue(counts == tuple(obj.ExecCount for obj in objs))

    def tearDown(self):
        FreeCAD.closeDocument("RecomputeTests")


class DocumentExpressionCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument()

    def assertAlmostEqual(self, v1, v2):
        if math.fabs(v2 - v1) > 1e-12:
            self.assertEqual(v1, v2)

    def testExpression(self):
        self.Obj1 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj2 = self.Doc.addObject("App::FeatureTest", "Test")
        # set the object twice to test that the backlinks are removed when overwriting the expression
        self.Obj2.setExpression(
            "Placement.Rotation.Angle", "%s.Placement.Rotation.Angle" % self.Obj1.Name
        )
        self.Obj2.setExpression(
            "Placement.Rotation.Angle", "%s.Placement.Rotation.Angle" % self.Obj1.Name
        )
        self.Obj1.Placement = FreeCAD.Placement(
            FreeCAD.Vector(0, 0, 0), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), 10)
        )
        self.Doc.recompute()
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )

        # clear the expression
        self.Obj2.setExpression("Placement.Rotation.Angle", None)
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )
        self.Doc.recompute()
        self.assertAlmostEqual(
            self.Obj1.Placement.Rotation.Angle, self.Obj2.Placement.Rotation.Angle
        )
        # touch the objects to perform a recompute
        self.Obj1.Placement = self.Obj1.Placement
        self.Obj2.Placement = self.Obj2.Placement
        # must not raise a topological error
        self.assertEqual(self.Doc.recompute(), 2)

        self.Obj3 = self.Doc.addObject("App::FeatureTest", "Test")
        self.Obj3.setExpression("Float", "2*(5%3)")
        self.Doc.recompute()
        self.assertEqual(self.Obj3.Float, 4)
        self.assertEqual(self.Obj3.evalExpression(self.Obj3.ExpressionEngine[0][1]), 4)

    def testIssue4649(self):
        class Cls:
            def __init__(self, obj):
                self.MonitorChanges = False
                obj.Proxy = self
                obj.addProperty("App::PropertyFloat", "propA", "group")
                obj.addProperty("App::PropertyFloat", "propB", "group")
                self.MonitorChanges = True
                obj.setExpression("propB", "6*9")

            def onChanged(self, obj, prop):
                print("onChanged", self, obj, prop)
                if self.MonitorChanges and prop == "propA":
                    print("Removing expression...")
                    obj.setExpression("propB", None)

        obj = self.Doc.addObject("App::DocumentObjectGroupPython", "Obj")
        Cls(obj)
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("Expression")
        obj.setExpression("propA", "42")
        self.Doc.recompute()
        self.Doc.commitTransaction()
        self.assertTrue(("propB", None) in obj.ExpressionEngine)
        self.assertTrue(("propA", "42") in obj.ExpressionEngine)

        self.Doc.undo()
        self.assertFalse(("propB", None) in obj.ExpressionEngine)
        self.assertFalse(("propA", "42") in obj.ExpressionEngine)

        self.Doc.redo()
        self.assertTrue(("propB", None) in obj.ExpressionEngine)
        self.assertTrue(("propA", "42") in obj.ExpressionEngine)

        self.Doc.recompute()
        obj.ExpressionEngine

        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "ExpressionTests.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.openDocument(SaveName)

    def testCyclicDependencyOnPlacement(self):
        obj = self.Doc.addObject("App::FeaturePython", "Python")
        obj.addProperty("App::PropertyPlacement", "Placement")
        obj.setExpression(".Placement.Base.x", ".Placement.Base.y + 10mm")
        with self.assertRaises(RuntimeError):
            obj.setExpression(".Placement.Base.y", ".Placement.Base.x + 10mm")

    def tearDown(self):
        FreeCAD.closeDocument(self.Doc.Name)


class FeatureTestColumn(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestColumn")
        self.obj = doc.addObject("App::FeatureTestColumn", "Column")

    def testEmpty(self):
        value = self.obj.Value
        self.obj.Column = ""
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testA(self):
        self.obj.Column = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 0)

    def testZ(self):
        self.obj.Column = "Z"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 25)

    def testAA(self):
        self.obj.Column = "AA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 26)

    def testAB(self):
        self.obj.Column = "AB"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 27)

    def testAZ(self):
        self.obj.Column = "AZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 51)

    def testBA(self):
        self.obj.Column = "BA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 52)

    def testCB(self):
        self.obj.Column = "CB"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 79)

    def testZA(self):
        self.obj.Column = "ZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 676)

    def testZZ(self):
        self.obj.Column = "ZZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 701)

    def testAAA(self):
        self.obj.Column = "AAA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 702)

    def testAAZ(self):
        self.obj.Column = "AAZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 727)

    def testCBA(self):
        self.obj.Column = "CBA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 2080)

    def testAZA(self):
        self.obj.Column = "AZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 1352)

    def testZZA(self):
        self.obj.Column = "ZZA"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 18252)

    def testZZZ(self):
        self.obj.Column = "ZZZ"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 18277)

    def testALL(self):
        self.obj.Column = "ALL"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 999)

    def testAb(self):
        value = self.obj.Value
        self.obj.Column = "Ab"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testABCD(self):
        value = self.obj.Value
        self.obj.Column = "ABCD"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def testEmptySilent(self):
        self.obj.Column = ""
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def testAbSilent(self):
        self.obj.Column = "Ab"
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def testABCDSilent(self):
        self.obj.Column = "ABCD"
        self.obj.Silent = True
        self.assertTrue(self.obj.recompute())
        self.assertEqual(self.obj.Value, -1)

    def tearDown(self):
        FreeCAD.closeDocument("TestColumn")


class FeatureTestRow(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestRow")
        self.obj = doc.addObject("App::FeatureTestRow", "Row")

    def testEmpty(self):
        self.obj.Silent = True
        self.obj.Row = ""
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def testA(self):
        self.obj.Silent = True
        self.obj.Row = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def testException(self):
        value = self.obj.Value
        self.obj.Row = "A"
        self.assertFalse(self.obj.recompute())
        self.assertEqual(self.obj.Value, value)

    def test0(self):
        self.obj.Silent = True
        self.obj.Row = "0"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def test1(self):
        self.obj.Silent = True
        self.obj.Row = "1"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 0)

    def test16384(self):
        self.obj.Silent = True
        self.obj.Row = "16384"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, 16383)

    def test16385(self):
        self.obj.Silent = True
        self.obj.Row = "16385"
        self.obj.recompute()
        self.assertEqual(self.obj.Value, -1)

    def tearDown(self):
        FreeCAD.closeDocument("TestRow")


class FeatureTestAbsAddress(unittest.TestCase):
    def setUp(self):
        doc = FreeCAD.newDocument("TestAbsAddress")
        self.obj = doc.addObject("App::FeatureTestAbsAddress", "Cell")

    def testAbsoluteA12(self):
        self.obj.Address = "$A$12"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteA13(self):
        self.obj.Address = "A$13"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteAA13(self):
        self.obj.Address = "AA$13"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteZZ12(self):
        self.obj.Address = "$ZZ$12"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, True)

    def testAbsoluteABC1(self):
        self.obj.Address = "$ABC1"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testAbsoluteABC2(self):
        self.obj.Address = "ABC$2"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testRelative(self):
        self.obj.Address = "A1"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testInvalid(self):
        self.obj.Address = "A"
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def testEmpty(self):
        self.obj.Address = ""
        self.obj.recompute()
        self.assertEqual(self.obj.Valid, False)

    def tearDown(self):
        FreeCAD.closeDocument("TestAbsAddress")


class FeatureTestAttribute(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("TestAttribute")
        self.doc.UndoMode = 0

    def testValidAttribute(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        obj.Attribute = "Name"
        self.doc.recompute()
        self.assertIn("Up-to-date", obj.State)

    def testInvalidAttribute(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        obj.Attribute = "Name123"
        self.doc.recompute()
        self.assertIn("Invalid", obj.State)
        self.assertIn("Touched", obj.State)

    def testRemoval(self):
        obj = self.doc.addObject("App::FeatureTestAttribute", "Attribute")
        obj.Object = obj
        self.assertEqual(self.doc.removeObject("Attribute"), None)

    def tearDown(self):
        FreeCAD.closeDocument("TestAttribute")
