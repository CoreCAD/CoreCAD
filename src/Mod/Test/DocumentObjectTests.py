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
import tempfile
import unittest
import xml.etree.ElementTree as ET

import FreeCAD
from FreeCAD import Base
from DocumentTestSupport import Proxy, MyFeature


class DocumentBasicCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("CreateTest")

    def saveAndRestore(self):
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        return self.Doc

    def testIssue18601(self):
        lnk = self.Doc.addObject("App::FeaturePython", "MyLink")
        obj = self.Doc.addObject("App::FeaturePython", "MyFeature")
        fea = MyFeature(obj)
        obj.propLink = [lnk]
        doc = self.saveAndRestore()
        FreeCAD.closeDocument(doc.Name)
        self.Doc = FreeCAD.newDocument("CreateTest")

    def testIssue24571(self):
        obj = self.Doc.addObject("App::FeatureTest", "Object")
        obj.ConstraintInt = (50, 0, 100, 1)
        obj.ConstraintFloat = (50.0, 0.0, 100.0, 1.0)
        self.Doc = self.saveAndRestore()
        obj = self.Doc.getObject("Object")
        # int
        obj.ConstraintInt = -1
        self.assertEqual(obj.ConstraintInt, 0)
        obj.ConstraintInt = 101
        self.assertEqual(obj.ConstraintInt, 100)
        # float
        obj.ConstraintFloat = -1.0
        self.assertEqual(obj.ConstraintFloat, 0.0)
        obj.ConstraintFloat = 101.0
        self.assertEqual(obj.ConstraintFloat, 100.0)

    def testAccessByNameOrID(self):
        obj = self.Doc.addObject("App::DocumentObject", "MyName")

        with self.assertRaises(TypeError):
            self.Doc.getObject([1])

        self.assertEqual(self.Doc.getObject(obj.Name), obj)
        self.assertEqual(self.Doc.getObject("Unknown"), None)
        self.assertEqual(self.Doc.getObject(obj.ID), obj)
        self.assertEqual(self.Doc.getObject(obj.ID + 1), None)

    def testCreateDestroy(self):
        self.assertTrue(FreeCAD.getDocument("CreateTest") is not None, "Creating Document failed")

    def testAddition(self):
        # No assertion: this is for running under a leak checker.
        self.Doc.openTransaction("Add")
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.undo()

    def testAddRemoveUndo(self):
        self.Doc.openTransaction("Add")
        obj = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.removeObject(obj.Name)
        self.Doc.undo()
        self.Doc.undo()

    def testNoRecompute(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.recompute()
        L1.TypeNoRecompute = 2
        execcount = L1.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 0)
        self.assertEqual(L1.ExecCount, execcount)

    def testNoRecomputeParent(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Child")
        L2 = self.Doc.addObject("App::FeatureTest", "Parent")
        L2.Source1 = L1
        self.Doc.recompute()
        L1.TypeNoRecompute = 2
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 1)
        self.assertEqual(L1.ExecCount, countChild)
        self.assertEqual(L2.ExecCount, countParent + 1)

        L1.touch("")
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 1)
        self.assertEqual(L1.ExecCount, countChild)
        self.assertEqual(L2.ExecCount, countParent + 1)

        L1.enforceRecompute()
        countChild = L1.ExecCount
        countParent = L2.ExecCount
        objectcount = self.Doc.recompute()
        self.assertEqual(objectcount, 2)
        self.assertEqual(L1.ExecCount, countChild + 1)
        self.assertEqual(L2.ExecCount, countParent + 1)

    def testAbortTransaction(self):
        self.Doc.openTransaction("Add")
        obj = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.abortTransaction()
        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.Doc.saveAs(SaveName)

    def testRemoval(self):
        # No assertion: this is for running under a leak checker.
        self.Doc.openTransaction("Add")
        L1 = self.Doc.addObject("App::FeatureTest", "Label")
        self.Doc.commitTransaction()
        self.Doc.openTransaction("Rem")
        L1 = self.Doc.removeObject("Label")
        self.Doc.commitTransaction()

    def testObjects(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        self.documentMembersAreReadable()
        self.Doc.recompute()
        self.featureTestDefaults(L1)
        self.propertyMetadata(L1)
        self.constraintsClamp(L1)
        self.enumProperty(L1)

        self.assertTrue(L1.Label == "Label_1", "Invalid object name")
        L1.Label = "Label_2"
        self.Doc.recompute()
        self.assertTrue(L1.Label == "Label_2", "Invalid object name")
        self.Doc.removeObject("Label_1")

    def documentMembersAreReadable(self):
        # call members to check for errors in ref counting
        self.Doc.ActiveObject
        self.Doc.Objects
        self.Doc.UndoMode
        self.Doc.UndoRedoMemSize
        self.Doc.UndoCount
        # UndoCount is read-only
        try:
            self.Doc.UndoCount = 3
        except Exception:
            FreeCAD.Console.PrintLog("   exception thrown, OK\n")
        else:
            self.fail("no exception thrown")
        self.Doc.RedoCount
        self.Doc.UndoNames
        self.Doc.RedoNames

    def featureTestDefaults(self, L1):
        self.assertTrue(L1.Integer == 4711)
        self.assertTrue(L1.Float - 47.11 < 0.001)
        self.assertTrue(L1.Bool == True)
        self.assertTrue(L1.String == "4711")
        self.assertTrue(float(L1.Angle) - 3.0 < 0.001)
        self.assertTrue(float(L1.Distance) - 47.11 < 0.001)

    def propertyMetadata(self, L1):
        self.assertTrue(not L1.getDocumentationOfProperty("Source1") == "")
        self.assertTrue(L1.getGroupOfProperty("Source1") == "Feature Test")
        self.assertTrue(L1.getTypeOfProperty("Source1") == [])
        self.assertTrue(L1.getEnumerationsOfProperty("Source1") is None)

    def constraintsClamp(self, L1):
        # test the constraint types ( both are constraint to percent range)
        self.assertTrue(L1.ConstraintInt == 5)
        self.assertTrue(L1.ConstraintFloat - 5.0 < 0.001)
        L1.ConstraintInt = 500
        L1.ConstraintFloat = 500.0
        self.assertTrue(L1.ConstraintInt == 100)
        self.assertTrue(L1.ConstraintFloat - 100.0 < 0.001)
        L1.ConstraintInt = -500
        L1.ConstraintFloat = -500.0
        self.assertTrue(L1.ConstraintInt == 0)
        self.assertTrue(L1.ConstraintFloat - 0.0 < 0.001)

    def enumProperty(self, L1):
        # in App::FeatureTest the current value is set to 4
        self.assertTrue(L1.Enum == "Four")
        L1.Enum = "Three"
        self.assertTrue(L1.Enum == "Three", "Different value to 'Three'")
        L1.Enum = 2
        self.assertTrue(L1.Enum == "Two", "Different value to 'Two'")
        try:
            L1.Enum = "SurelyNotInThere!"
        except Exception:
            FreeCAD.Console.PrintLog("   exception thrown, OK\n")
        else:
            self.fail("no exception thrown")
        self.assertTrue(
            sorted(L1.getEnumerationsOfProperty("Enum"))
            == sorted(["Zero", "One", "Two", "Three", "Four"])
        )

    def testEnum(self):
        enumeration_choices = ["one", "two"]
        obj = self.Doc.addObject("App::FeaturePython", "Label_2")
        obj.addProperty("App::PropertyEnumeration", "myEnumeration", "Enum", "mytest")
        with self.assertRaises(ValueError):
            obj.myEnumeration = enumeration_choices[0]

        obj.myEnumeration = enumeration_choices
        obj.myEnumeration = 0
        self.Doc.openTransaction("Modify enum")
        obj.myEnumeration = 1
        self.assertTrue(obj.myEnumeration, enumeration_choices[1])
        self.Doc.commitTransaction()
        self.Doc.undo()
        self.assertTrue(obj.myEnumeration, enumeration_choices[0])

    def testWrongTypes(self):
        with self.assertRaises(TypeError):
            self.Doc.addObject("App::DocumentObjectExtension")

        class Feature:
            pass

        with self.assertRaises(TypeError):
            self.Doc.addObject(type="App::DocumentObjectExtension", objProxy=Feature(), attach=True)

        ext = FreeCAD.Base.TypeId.fromName("App::DocumentObjectExtension")
        self.assertEqual(ext.createInstance(), None)

        obj = self.Doc.addObject("App::FeaturePython", "Object")
        with self.assertRaises(TypeError):
            obj.addProperty("App::DocumentObjectExtension", "Property")

        with self.assertRaises(TypeError):
            self.Doc.findObjects(Type="App::DocumentObjectExtension")

        e = FreeCAD.Base.TypeId.fromName("App::LinkExtensionPython")
        self.assertIsNone(e.createInstance())

        if FreeCAD.GuiUp:
            obj = self.Doc.addObject("App::DocumentObject", viewType="App::Extension")
            self.assertIsNone(obj.ViewObject)

    def testMem(self):
        self.Doc.MemSize

    def testDuplicateLinks(self):
        obj = self.Doc.addObject("App::FeatureTest", "obj")
        grp = self.Doc.addObject("App::DocumentObjectGroup", "group")
        grp.Group = [obj, obj]
        self.Doc.removeObject(obj.Name)
        self.assertListEqual(grp.Group, [])

    def testPlacementList(self):
        obj = self.Doc.addObject("App::FeaturePython", "Label")
        obj.addProperty("App::PropertyPlacementList", "PlmList")
        plm = FreeCAD.Placement()
        plm.Base = (1, 2, 3)
        plm.Rotation = (0, 0, 1, 0)
        obj.PlmList = [plm]
        cpy = self.Doc.copyObject(obj)
        self.assertListEqual(obj.PlmList, cpy.PlmList)

    def testRawAxis(self):
        obj = self.Doc.addObject("App::FeaturePython", "Label")
        obj.addProperty("App::PropertyPlacement", "Plm")
        obj.addProperty("App::PropertyRotation", "Rot")
        obj.Plm.Rotation.Axis = (1, 2, 3)
        obj.Rot.Axis = (3, 2, 1)

        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        obj = self.Doc.ActiveObject

        self.assertEqual(obj.Plm.Rotation.RawAxis.x, 1)
        self.assertEqual(obj.Plm.Rotation.RawAxis.y, 2)
        self.assertEqual(obj.Plm.Rotation.RawAxis.z, 3)

        self.assertEqual(obj.Rot.RawAxis.x, 3)
        self.assertEqual(obj.Rot.RawAxis.y, 2)
        self.assertEqual(obj.Rot.RawAxis.z, 1)

    def testAddRemove(self):
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        self.Doc.removeObject(L1.Name)
        try:
            L1.Name
        except Exception:
            self.assertTrue(True)
        else:
            self.assertTrue(False)
        del L1

        self.Doc.openTransaction("AddRemove")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        self.Doc.removeObject(L2.Name)
        self.Doc.commitTransaction()
        self.Doc.undo()
        try:
            L2.Name
        except Exception:
            self.assertTrue(True)
        else:
            self.assertTrue(False)
        del L2

    def testSubObject(self):
        obj = self.Doc.addObject("App::Origin", "Origin")
        self.Doc.recompute()
        self.subObjectPlacements(obj)
        self.subObjectTuple(obj)
        self.subObjectsByOutListName()

    def assertTurnsTo(self, matrix, local, world):
        self.assertEqual(matrix.multVec(local).getAngle(world), 0.0)

    def subObjectPlacements(self, obj):
        x, y, z = FreeCAD.Vector(1, 0, 0), FreeCAD.Vector(0, 1, 0), FreeCAD.Vector(0, 0, 1)
        self.assertTurnsTo(obj.getSubObject("X_Axis", retType=2)[1], x, x)
        self.assertTurnsTo(obj.getSubObject("Y_Axis", retType=2)[1], x, y)
        self.assertTurnsTo(obj.getSubObject("Z_Axis", retType=2)[1], x, z)
        self.assertTurnsTo(obj.getSubObject("XY_Plane", retType=2)[1], z, z)
        self.assertTurnsTo(obj.getSubObject("XZ_Plane", retType=2)[1], z, FreeCAD.Vector(0, -1, 0))
        self.assertTurnsTo(obj.getSubObject("YZ_Plane", retType=2)[1], z, x)
        self.assertTurnsTo(obj.getSubObject("YZ_Plane", retType=3), z, x)
        self.assertTurnsTo(obj.getSubObject("YZ_Plane", retType=4), z, x)

    def subObjectTuple(self, obj):
        self.assertEqual(
            obj.getSubObject(("XY_Plane", "YZ_Plane"), retType=4)[0],
            obj.getSubObject("XY_Plane", retType=4),
        )
        self.assertEqual(
            obj.getSubObject(("XY_Plane", "YZ_Plane"), retType=4)[1],
            obj.getSubObject("YZ_Plane", retType=4),
        )

    def subObjectsByOutListName(self):
        obj2 = self.Doc.addObject("App::Origin", "Origin2")
        self.Doc.recompute()

        # Use the names of the origin's out-list
        for i in obj2.OutList:
            self.assertEqual(obj2.getSubObject(i.Name, retType=1).Name, i.Name)
        # Add a '.' to the names
        for i in obj2.OutList:
            self.assertEqual(obj2.getSubObject(i.Name + ".", retType=1).Name, i.Name)

    def testExtensions(self):
        # we try to create a normal python object and add an extension to it
        obj = self.Doc.addObject("App::DocumentObject", "Extension_1")
        grp = self.Doc.addObject("App::DocumentObject", "Extension_2")
        # we should have all methods we need to handle extensions
        try:
            self.assertTrue(not grp.hasExtension("App::GroupExtensionPython"))
            grp.addExtension("App::GroupExtensionPython")
            self.assertTrue(grp.hasExtension("App::GroupExtension"))
            self.assertTrue(grp.hasExtension("App::GroupExtensionPython"))
            grp.addObject(obj)
            self.assertTrue(len(grp.Group) == 1)
            self.assertTrue(grp.Group[0] == obj)
        except Exception:
            self.assertTrue(False)

        # test if the method override works
        class SpecialGroup:
            def allowObject(self, ext, obj):
                return False

        callback = SpecialGroup()
        grp2 = self.Doc.addObject("App::FeaturePython", "Extension_3")
        grp2.addExtension("App::GroupExtensionPython")
        grp2.Proxy = callback

        try:
            self.assertTrue(grp2.hasExtension("App::GroupExtension"))
            grp2.addObject(obj)
            self.assertTrue(len(grp2.Group) == 0)
        except Exception:
            self.assertTrue(False)

        self.Doc.removeObject(grp.Name)
        self.Doc.removeObject(grp2.Name)
        self.Doc.removeObject(obj.Name)
        del obj
        del grp
        del grp2

    def testExtensionBug0002785(self):
        class MyExtension:
            def __init__(self, obj):
                obj.addExtension("App::GroupExtensionPython")

        obj = self.Doc.addObject("App::DocumentObject", "myObj")
        MyExtension(obj)
        self.assertTrue(obj.hasExtension("App::GroupExtension"))
        self.assertTrue(obj.hasExtension("App::GroupExtensionPython"))
        self.Doc.removeObject(obj.Name)
        del obj

    def testExtensionGroup(self):
        obj = self.Doc.addObject("App::DocumentObject", "Obj")
        grp = self.Doc.addObject("App::FeaturePython", "Extension_2")
        grp.addExtension("App::GroupExtensionPython")
        grp.Group = [obj]
        self.assertTrue(obj in grp.Group)

    def testExtensionBugViewProvider(self):
        class Layer:
            def __init__(self, obj):
                obj.addExtension("App::GroupExtensionPython")

        class LayerViewProvider:
            def __init__(self, obj):
                obj.addExtension("Gui::ViewProviderGroupExtensionPython")
                obj.Proxy = self

        obj = self.Doc.addObject("App::FeaturePython", "Layer")
        Layer(obj)
        self.assertTrue(obj.hasExtension("App::GroupExtension"))

        if FreeCAD.GuiUp:
            LayerViewProvider(obj.ViewObject)
            self.assertTrue(obj.ViewObject.hasExtension("Gui::ViewProviderGroupExtension"))
            self.assertTrue(obj.ViewObject.hasExtension("Gui::ViewProviderGroupExtensionPython"))

        self.Doc.removeObject(obj.Name)
        del obj

    def testHasSelection(self):
        if FreeCAD.GuiUp:
            import FreeCADGui

            self.assertFalse(FreeCADGui.Selection.hasSelection("", 1))

    def testPropertyLink_Issue2902Part1(self):
        o1 = self.Doc.addObject("App::FeatureTest", "test1")
        o2 = self.Doc.addObject("App::FeatureTest", "test2")
        o3 = self.Doc.addObject("App::FeatureTest", "test3")

        o1.Link = o2
        self.assertEqual(o1.Link, o2)
        o1.Link = o3
        self.assertEqual(o1.Link, o3)
        o2.Placement = FreeCAD.Placement()
        self.assertEqual(o1.Link, o3)

    def testProp_NonePropertyLink(self):
        obj1 = self.Doc.addObject("App::FeaturePython", "Obj1")
        obj2 = self.Doc.addObject("App::FeaturePython", "Obj2")
        obj1.addProperty(
            "App::PropertyLink",
            "Link",
            "Base",
            "Link to another feature",
            FreeCAD.PropertyType.Prop_None,
            False,
            False,
        )
        obj1.Link = obj2
        self.assertEqual(obj1.MustExecute, True)

    def testProp_OutputPropertyLink(self):
        obj1 = self.Doc.addObject("App::FeaturePython", "Obj1")
        obj2 = self.Doc.addObject("App::FeaturePython", "Obj2")
        obj1.addProperty(
            "App::PropertyLink",
            "Link",
            "Base",
            "Link to another feature",
            FreeCAD.PropertyType.Prop_Output,
            False,
            False,
        )
        obj1.Link = obj2
        self.assertEqual(obj1.MustExecute, False)

    def testAttributeOfDynamicProperty(self):
        obj = self.Doc.addObject("App::FeaturePython", "Obj")
        # Prop_NoPersist is the enum with the highest value
        max_value = FreeCAD.PropertyType.Prop_NoPersist
        list_of_types = []
        for i in range(0, max_value + 1):
            obj.addProperty("App::PropertyString", "String" + str(i), "", "", i)
            list_of_types.append(obj.getTypeOfProperty("String" + str(i)))

        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)

        obj = self.Doc.ActiveObject
        for i in range(0, max_value):
            types = obj.getTypeOfProperty("String" + str(i))
            self.assertEqual(list_of_types[i], types)

        # A property with flag Prop_NoPersist won't be saved to the file
        with self.assertRaises(AttributeError):
            obj.getTypeOfProperty("String" + str(max_value))

    def testNotification_Issue2902Part2(self):
        o = self.Doc.addObject("App::FeatureTest", "test")

        plm = o.Placement
        o.Placement = FreeCAD.Placement()
        plm.Base.x = 5
        self.assertEqual(o.Placement.Base.x, 0)
        o.Placement.Base.x = 5
        self.assertEqual(o.Placement.Base.x, 5)

    def testNotification_Issue2996(self):
        if not FreeCAD.GuiUp:
            return

        class ViewProvider:
            def __init__(self, vobj):
                vobj.Proxy = self

            def attach(self, vobj):
                self.ViewObject = vobj
                self.Object = vobj.Object

            def claimChildren(self):
                children = [self.Object.Link]
                return children

        obj = self.Doc.addObject("App::FeaturePython", "Sketch")
        obj.addProperty("App::PropertyLink", "Link")
        ViewProvider(obj.ViewObject)

        ext = self.Doc.addObject("App::FeatureTest", "Extrude")
        ext.Link = obj

        sli = self.Doc.addObject("App::FeaturePython", "Slice")
        sli.addProperty("App::PropertyLink", "Link").Link = ext
        ViewProvider(sli.ViewObject)

        com = self.Doc.addObject("App::FeaturePython", "CompoundFilter")
        com.addProperty("App::PropertyLink", "Link").Link = sli
        ViewProvider(com.ViewObject)

        ext.Label = "test"

        self.assertEqual(ext.Link, obj)
        self.assertNotEqual(ext.Link, sli)

    def testIssue4823(self):
        # Removing an origin must not crash the tree view in GUI mode.
        obj = self.Doc.addObject("App::Origin")
        self.Doc.removeObject(obj.Name)

    def testSamePropertyOfLinkAndLinkedObject(self):
        test = self.Doc.addObject("App::FeaturePython", "Python")
        link = self.Doc.addObject("App::Link", "Link")
        test.addProperty("App::PropertyFloat", "Test")
        link.addProperty("App::PropertyFloat", "Test")
        link.LinkedObject = test
        SaveName = tempfile.gettempdir() + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("CreateTest")
        self.Doc = FreeCAD.open(SaveName)
        self.assertIn("Test", self.Doc.Python.PropertiesList)
        self.assertIn("Test", self.Doc.Link.PropertiesList)

    def testNoProxy(self):
        test = self.Doc.addObject("App::DocumentObject", "Object")
        test.addProperty("App::PropertyPythonObject", "Dictionary")
        test.Dictionary = {"Stored data": [3, 5, 7]}

        doc = self.saveAndRestore()
        obj = doc.Object

        self.assertEqual(obj.Dictionary, {"Stored data": [3, 5, 7]})

    def testWithProxy(self):
        test = self.Doc.addObject("App::FeaturePython", "Python")
        proxy = Proxy(test)
        proxy.Dictionary["Stored data"] = [3, 5, 7]

        doc = self.saveAndRestore()
        obj = doc.Python.Proxy

        self.assertEqual(obj.Dictionary, {"Stored data": [3, 5, 7]})

    def testContent(self):
        test = self.Doc.addObject("App::FeaturePython", "Python")
        types = Base.TypeId.getAllDerivedFrom("App::Property")
        for type in types:
            try:
                test.addProperty(type.Name, type.Name.replace(":", "_"))
                print("Add property type: {}".format(type.Name))
            except Exception as e:
                pass
        root = ET.fromstring(test.Content)
        self.assertEqual(root.tag, "Properties")

    def testValidateXml(self):
        self.Doc.openTransaction("Add")
        obj = self.Doc.addObject("App::FeatureTest", "Label")
        obj.Label = "abc\x01ef"
        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "CreateTest.FCStd"
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument(self.Doc.Name)
        self.Doc = FreeCAD.open(SaveName)
        self.assertEqual(self.Doc.ActiveObject.Label, "abc_ef")

    def tearDown(self):
        FreeCAD.closeDocument("CreateTest")


# class must be defined in global scope to allow it to be reloaded on document open


class DocumentPropertyCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PropertyTests")
        self.Obj = self.Doc.addObject("App::FeaturePython", "Test")

    def testDescent(self):
        props = self.Obj.supportedProperties()
        for i in props:
            self.Obj.addProperty(i, i.replace(":", "_"))
        tempPath = tempfile.gettempdir()
        tempFile = tempPath + os.sep + "PropertyTests.FCStd"
        self.Doc.saveAs(tempFile)
        FreeCAD.closeDocument("PropertyTests")
        self.Doc = FreeCAD.open(tempFile)

    def testRemoveProperty(self):
        prop = "Something"
        self.Obj.addProperty("App::PropertyFloat", prop)
        self.Obj.Something = 0.01
        self.Doc.recompute()
        self.Doc.openTransaction("modify and remove property")
        self.Obj.Something = 0.00
        self.Obj.removeProperty(prop)
        self.Obj.recompute()
        self.Doc.abortTransaction()

    def testRemovePropertyExpression(self):
        p1 = self.Doc.addObject("App::FeaturePython", "params1")
        p2 = self.Doc.addObject("App::FeaturePython", "params2")
        p1.addProperty("App::PropertyFloat", "a")
        p1.a = 42
        p2.addProperty("App::PropertyFloat", "b")
        p2.setExpression("b", "params1.a")
        self.Doc.recompute()
        p2.removeProperty("b")
        p1.touch()
        self.Doc.recompute()
        self.assertTrue(not p2 in p1.InList)

    def testRemovePropertyOnChange(self):
        class Feature:
            def __init__(self, fp):
                fp.Proxy = self
                fp.addProperty("App::PropertyString", "Test")

            def onBeforeChange(self, fp, prop):
                if prop == "Test":
                    fp.removeProperty("Test")

            def onChanged(self, fp, prop):
                getattr(fp, prop)

        obj = self.Doc.addObject("App::FeaturePython")
        fea = Feature(obj)
        obj.Test = "test"

    def tearDown(self):
        FreeCAD.closeDocument("PropertyTests")


class DocumentAutoCreatedCases(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("TestDoc")

    def tearDown(self):
        for doc_name in FreeCAD.listDocuments().keys():
            FreeCAD.closeDocument(doc_name)

    def test_set_get_auto_created(self):
        self.doc.setAutoCreated(True)
        self.assertTrue(self.doc.isAutoCreated(), "autoCreated flag should be True")

        self.doc.setAutoCreated(False)
        self.assertFalse(self.doc.isAutoCreated(), "autoCreated flag should be False")

    def test_auto_created_document_closes_on_opening_existing_document(self):
        self.doc.setAutoCreated(True)
        self.assertEqual(len(self.doc.Objects), 0)
        saved_doc = FreeCAD.newDocument("SavedDoc")
        file_path = tempfile.gettempdir() + os.sep + "SavedDoc.FCStd"
        saved_doc.saveAs(file_path)
        FreeCAD.closeDocument("SavedDoc")
        FreeCAD.setActiveDocument("TestDoc")
        FreeCAD.open(file_path)
        if self.doc.isAutoCreated() and len(self.doc.Objects) == 0:
            FreeCAD.closeDocument("TestDoc")
        self.assertNotIn("TestDoc", FreeCAD.listDocuments())

    def test_manual_document_does_not_close_on_opening_existing_document(self):
        self.assertFalse(self.doc.isAutoCreated())
        self.assertEqual(len(self.doc.Objects), 0)
        saved_doc = FreeCAD.newDocument("SavedDoc")
        file_path = tempfile.gettempdir() + os.sep + "SavedDoc.FCStd"
        saved_doc.saveAs(file_path)
        FreeCAD.closeDocument("SavedDoc")
        FreeCAD.setActiveDocument("TestDoc")
        FreeCAD.open(file_path)
        if self.doc.isAutoCreated() and len(self.doc.Objects) == 0:
            FreeCAD.closeDocument("TestDoc")
        self.assertIn("TestDoc", FreeCAD.listDocuments())
        self.assertIn("SavedDoc", FreeCAD.listDocuments())
