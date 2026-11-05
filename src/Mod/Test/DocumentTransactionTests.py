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


class UndoRedoCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("UndoTest")
        self.Doc.UndoMode = 0
        self.Doc.addObject("App::FeatureTest", "Base")
        self.Doc.addObject("App::FeatureTest", "Del")
        self.Doc.getObject("Del").Integer = 2

    def assertStacks(self, undo, redo):
        self.assertEqual(self.Doc.UndoNames, undo)
        self.assertEqual(self.Doc.UndoCount, len(undo))
        self.assertEqual(self.Doc.RedoNames, redo)
        self.assertEqual(self.Doc.RedoCount, len(redo))

    def testUndoProperties(self):
        self.Doc.UndoMode = 1

        # first transaction
        self.Doc.openTransaction("Transaction1")
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.getObject("test1").Integer = 1
        self.Doc.getObject("test1").String = "test1"
        self.Doc.getObject("test1").Float = 1.0
        self.Doc.getObject("test1").Bool = 1

        # second transaction
        self.Doc.openTransaction("Transaction2")
        self.Doc.getObject("test1").Integer = 2
        self.Doc.getObject("test1").String = "test2"
        self.Doc.getObject("test1").Float = 2.0
        self.Doc.getObject("test1").Bool = 0

        self.Doc.UndoMode = 0

    def testUndoClear(self):
        self.Doc.UndoMode = 1
        self.assertStacks([], [])

        self.Doc.openTransaction("Transaction1")
        # becomes the active object
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.commitTransaction()
        # removes the active object
        self.Doc.undo()
        self.assertEqual(self.Doc.ActiveObject, None)
        # deletes the active object
        self.Doc.clearUndos()
        self.assertEqual(self.Doc.ActiveObject, None)

    def testUndo(self):
        self.Doc.UndoMode = 1
        self.assertStacks([], [])
        self.openFourTransactions()
        self.undoAllFour()
        self.redoTwoThenUndoOne()
        self.newTransactionsDropTheRedos()
        self.Doc.UndoMode = 0
        self.assertStacks([], [])

    def openFourTransactions(self):
        self.Doc.openTransaction("Transaction1")
        self.Doc.addObject("App::FeatureTest", "test1")
        self.Doc.getObject("test1").Integer = 1
        self.Doc.getObject("Del").Integer = 1
        self.Doc.removeObject("Del")
        self.assertStacks(["Transaction1"], [])

        self.Doc.openTransaction("Transaction2")
        # no change, so no transaction
        self.assertStacks(["Transaction1"], [])

        self.Doc.getObject("test1").Integer = 2
        self.assertStacks(["Transaction2", "Transaction1"], [])

        self.Doc.abortTransaction()
        self.assertStacks(["Transaction1"], [])
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)

        self.Doc.openTransaction("Transaction2")
        self.Doc.getObject("test1").Integer = 2
        self.assertStacks(["Transaction2", "Transaction1"], [])

        self.Doc.openTransaction("Transaction3")
        self.Doc.getObject("test1").Integer = 3
        self.assertStacks(["Transaction3", "Transaction2", "Transaction1"], [])

        self.Doc.openTransaction("Transaction4")
        self.Doc.getObject("test1").Integer = 4
        self.assertStacks(["Transaction4", "Transaction3", "Transaction2", "Transaction1"], [])

    def undoAllFour(self):
        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 3)
        self.assertStacks(["Transaction3", "Transaction2", "Transaction1"], ["Transaction4"])

        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 2)
        self.assertStacks(["Transaction2", "Transaction1"], ["Transaction3", "Transaction4"])

        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertStacks(["Transaction1"], ["Transaction2", "Transaction3", "Transaction4"])

        self.Doc.undo()
        self.assertTrue(self.Doc.getObject("test1") is None)
        self.assertTrue(self.Doc.getObject("Del").Integer == 2)
        self.assertStacks([], ["Transaction1", "Transaction2", "Transaction3", "Transaction4"])

    def redoTwoThenUndoOne(self):
        self.Doc.redo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertStacks(["Transaction1"], ["Transaction2", "Transaction3", "Transaction4"])

        self.Doc.redo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 2)
        self.assertStacks(["Transaction2", "Transaction1"], ["Transaction3", "Transaction4"])

        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 1)
        self.assertStacks(["Transaction1"], ["Transaction2", "Transaction3", "Transaction4"])

    def newTransactionsDropTheRedos(self):
        self.Doc.openTransaction("Transaction8")
        self.Doc.getObject("test1").Integer = 8
        self.assertStacks(["Transaction8", "Transaction1"], [])
        self.Doc.abortTransaction()
        self.assertStacks(["Transaction1"], [])

        self.Doc.openTransaction("Transaction8")
        self.Doc.getObject("test1").Integer = 8
        self.assertStacks(["Transaction8", "Transaction1"], [])

        self.Doc.openTransaction("Transaction9")
        self.Doc.getObject("test1").Integer = 9
        self.assertStacks(["Transaction9", "Transaction8", "Transaction1"], [])
        self.Doc.commitTransaction()
        self.assertStacks(["Transaction9", "Transaction8", "Transaction1"], [])
        self.assertEqual(self.Doc.getObject("test1").Integer, 9)

        self.Doc.undo()
        self.assertEqual(self.Doc.getObject("test1").Integer, 8)
        self.assertStacks(["Transaction8", "Transaction1"], ["Transaction9"])

    def testUndoInList(self):

        self.Doc.UndoMode = 1

        self.Doc.openTransaction("Box")
        self.Box = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Cylinder")
        self.Cylinder = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse1 = self.Doc.addObject("App::FeatureTest", "Fuse")
        self.Fuse1.LinkList = [self.Box, self.Cylinder]
        self.Doc.commitTransaction()

        self.Doc.undo()
        self.assertTrue(len(self.Box.InList) == 0)
        self.assertTrue(len(self.Cylinder.InList) == 0)

        self.Doc.redo()
        self.assertTrue(len(self.Box.InList) == 1)
        self.assertTrue(self.Box.InList[0] == self.Doc.Fuse)
        self.assertTrue(len(self.Cylinder.InList) == 1)
        self.assertTrue(self.Cylinder.InList[0] == self.Doc.Fuse)

    def testUndoIssue0003150Part1(self):

        self.Doc.UndoMode = 1

        self.Doc.openTransaction("Box")
        self.Box = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Cylinder")
        self.Cylinder = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse1 = self.Doc.addObject("App::FeatureTest")
        self.Fuse1.LinkList = [self.Box, self.Cylinder]
        self.Doc.commitTransaction()
        self.Doc.recompute()

        self.Doc.openTransaction("Sphere")
        self.Sphere = self.Doc.addObject("App::FeatureTest")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Fuse")
        self.Fuse2 = self.Doc.addObject("App::FeatureTest")
        self.Fuse2.LinkList = [self.Fuse1, self.Sphere]
        self.Doc.commitTransaction()
        self.Doc.recompute()

        self.Doc.openTransaction("Part")
        self.Part = _placedGroup(self.Doc, "Part")
        self.Doc.commitTransaction()

        self.Doc.openTransaction("Drag")
        self.Part.addObject(self.Fuse2)
        self.Doc.commitTransaction()

        # 3 undos show the problem of failing recompute
        self.Doc.undo()
        self.Doc.undo()
        self.Doc.undo()
        self.assertTrue(self.Doc.recompute() >= 0)

    def tearDown(self):
        FreeCAD.closeDocument("UndoTest")


class DocumentBacklinks(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("BackLinks")

    def testIssue0003323(self):
        self.Doc.UndoMode = 1
        self.Doc.openTransaction("Create object")
        obj1 = self.Doc.addObject("App::FeatureTest", "Test1")
        obj2 = self.Doc.addObject("App::FeatureTest", "Test2")
        obj2.Link = obj1
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.Doc.openTransaction("Create object")

    def tearDown(self):
        FreeCAD.closeDocument("BackLinks")


# Test if actions done on two documents are undone together
class MultiDocumentUndo(unittest.TestCase):
    def setUp(self):
        self.Doc1 = FreeCAD.newDocument("Doc1")
        self.Doc2 = FreeCAD.newDocument("Doc2")
        self.Doc1.UndoMode = 1
        self.Doc2.UndoMode = 1

    def testAddObjects(self):
        self.Doc1.openTransaction("transact1")
        self.Doc2.openTransaction("transact2")

        obj1 = self.Doc1.addObject("App::DocumentObject", "Obj1Name")
        obj2 = self.Doc2.addObject("App::DocumentObject", "Obj2Name")

        self.assertNotEqual(self.Doc1.getBookedTransactionID(), self.Doc2.getBookedTransactionID())

        self.Doc1.commitTransaction()
        self.Doc2.commitTransaction()

        with self.assertRaises(TypeError):
            self.Doc1.getObject([1])
            self.Doc2.getObject([1])

        self.assertEqual(self.Doc1.getObject("Obj1Name"), obj1)
        self.assertEqual(self.Doc2.getObject("Obj2Name"), obj2)

        self.Doc1.undo()
        self.assertEqual(self.Doc1.getObject("Obj1Name"), None)
        self.assertEqual(self.Doc2.getObject("Obj2Name"), obj2)

        self.Doc2.undo()
        self.assertEqual(self.Doc1.getObject("Obj1Name"), None)
        self.assertEqual(self.Doc2.getObject("Obj2Name"), None)

        self.Doc1.redo()
        self.assertEqual(self.Doc1.getObject("Obj1Name"), obj1)
        self.assertEqual(self.Doc2.getObject("Obj2Name"), None)

    def tearDown(self):
        FreeCAD.closeDocument("Doc1")
        FreeCAD.closeDocument("Doc2")
