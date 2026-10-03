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


class SaveRestoreSpecialGroup:
    def __init__(self, obj):
        obj.addExtension("App::GroupExtensionPython")
        obj.Proxy = self

    def allowObject(self, ext, obj):
        return False


# class must be defined in global scope to allow it to be reloaded on document open


class SaveRestoreSpecialGroupViewProvider:
    def __init__(self, obj):
        obj.addExtension("Gui::ViewProviderGroupExtensionPython")
        obj.Proxy = self

    def testFunction(self):
        pass


class DocumentSaveRestoreCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("SaveRestoreTests")
        L1 = self.Doc.addObject("App::FeatureTest", "Label_1")
        L2 = self.Doc.addObject("App::FeatureTest", "Label_2")
        L3 = self.Doc.addObject("App::FeatureTest", "Label_3")
        self.TempPath = tempfile.gettempdir()
        FreeCAD.Console.PrintLog("  Using temp path: " + self.TempPath + "\n")

    def testSaveAndRestore(self):
        SaveName = self.TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.assertTrue(self.Doc.Label_1.TypeTransient == 4711)
        self.Doc.Label_1.TypeTransient = 4712
        # setup Linking
        self.Doc.Label_1.Link = self.Doc.Label_2
        self.Doc.Label_2.Link = self.Doc.Label_3
        self.Doc.Label_1.LinkSub = (self.Doc.Label_2, ["Sub1", "Sub2"])
        self.Doc.Label_2.LinkSub = (self.Doc.Label_3, ["Sub3", "Sub4"])
        self.Doc.saveAs(SaveName)
        FreeCAD.closeDocument("SaveRestoreTests")
        self.Doc = FreeCAD.open(SaveName)
        self.assertTrue(self.Doc.Label_1.Integer == 4711)
        self.assertTrue(self.Doc.Label_2.Integer == 4711)
        # test Linkage
        self.assertTrue(self.Doc.Label_1.Link == self.Doc.Label_2)
        self.assertTrue(self.Doc.Label_2.Link == self.Doc.Label_3)
        self.assertTrue(self.Doc.Label_1.LinkSub == (self.Doc.Label_2, ["Sub1", "Sub2"]))
        self.assertTrue(self.Doc.Label_2.LinkSub == (self.Doc.Label_3, ["Sub3", "Sub4"]))
        # do NOT save transient properties
        self.assertTrue(self.Doc.Label_1.TypeTransient == 4711)
        self.assertTrue(self.Doc == FreeCAD.getDocument(self.Doc.Name))

    def testRestore(self):
        Doc = FreeCAD.newDocument("RestoreTests")
        Doc.addObject("App::FeatureTest", "Label_1")
        FileName = self.TempPath + os.sep + "Test2.FCStd"
        Doc.saveAs(FileName)
        # restore must first clear the current content
        Doc.restore()
        self.assertTrue(len(Doc.Objects) == 1)
        FreeCAD.closeDocument("RestoreTests")

    def testActiveDocument(self):
        Second = FreeCAD.newDocument("Active")
        FreeCAD.closeDocument("Active")
        try:
            # There might be no active document anymore
            # This also checks for dangling pointers
            Active = FreeCAD.activeDocument()
            # Second is still a valid object
            self.assertTrue(Second != Active)
        except Exception:
            # Okay, no document open
            self.assertTrue(True)

    def testExtensionSaveRestore(self):
        SaveName = self.TempPath + os.sep + "SaveRestoreExtensions.FCStd"
        Doc = FreeCAD.newDocument("SaveRestoreExtensions")
        # we try to create a normal python object and add an extension to it
        obj = Doc.addObject("App::DocumentObject", "Obj")
        grp1 = Doc.addObject("App::DocumentObject", "Extension_1")
        grp2 = Doc.addObject("App::FeaturePython", "Extension_2")

        grp1.addExtension("App::GroupExtensionPython")
        SaveRestoreSpecialGroup(grp2)
        if FreeCAD.GuiUp:
            SaveRestoreSpecialGroupViewProvider(grp2.ViewObject)
        grp2.Group = [obj]

        Doc.saveAs(SaveName)
        FreeCAD.closeDocument("SaveRestoreExtensions")
        Doc = FreeCAD.open(SaveName)

        self.assertTrue(Doc.Extension_1.hasExtension("App::GroupExtension"))
        self.assertTrue(Doc.Extension_2.hasExtension("App::GroupExtension"))
        self.assertTrue(Doc.Extension_2.Group[0] is Doc.Obj)
        self.assertTrue(hasattr(Doc.Extension_2.Proxy, "allowObject"))

        if FreeCAD.GuiUp:
            self.assertTrue(
                Doc.Extension_2.ViewObject.hasExtension("Gui::ViewProviderGroupExtensionPython")
            )
            self.assertTrue(hasattr(Doc.Extension_2.ViewObject.Proxy, "testFunction"))

        FreeCAD.closeDocument("SaveRestoreExtensions")

    def testPersistenceContentDump(self):
        # test smallest level... property
        self.Doc.Label_1.Vector = (1, 2, 3)
        dump = self.Doc.Label_1.dumpPropertyContent("Vector", Compression=9)
        self.Doc.Label_2.restorePropertyContent("Vector", dump)
        self.assertEqual(self.Doc.Label_1.Vector, self.Doc.Label_2.Vector)

        # next higher: object
        self.Doc.Label_1.Distance = 12
        self.Doc.Label_1.String = "test"
        dump = self.Doc.Label_1.dumpContent()
        self.Doc.Label_3.restoreContent(dump)
        self.assertEqual(self.Doc.Label_1.Distance, self.Doc.Label_3.Distance)
        self.assertEqual(self.Doc.Label_1.String, self.Doc.Label_3.String)

        # highest level: document
        dump = self.Doc.dumpContent(9)
        Doc = FreeCAD.newDocument("DumpTest")
        Doc.restoreContent(dump)
        self.assertEqual(len(self.Doc.Objects), len(Doc.Objects))
        self.assertEqual(self.Doc.Label_1.Distance, Doc.Label_1.Distance)
        self.assertEqual(self.Doc.Label_1.String, Doc.Label_1.String)
        self.assertEqual(self.Doc.Label_1.Vector, Doc.Label_1.Vector)
        FreeCAD.closeDocument("DumpTest")

    def tearDown(self):
        FreeCAD.closeDocument("SaveRestoreTests")


class DocumentRecoveryCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("RecoveryTests")
        self.Obj = self.Doc.addObject("App::FeatureTest", "RecoveryObject")
        self.savedFileName = None

    def tearDown(self):
        if FreeCAD.getDocument("RecoveryTests") is not None:
            FreeCAD.closeDocument("RecoveryTests")
        if self.savedFileName and os.path.exists(self.savedFileName):
            os.remove(self.savedFileName)

    def testSnapshotIsWrittenAsTheDocumentItself(self):
        """A snapshot can become the record -- recovery binds it to the original document's
        path -- so it is written in the record's own form, by the record's own writer."""
        self.assertTrue(self.Doc.canWriteRecoverySnapshot())
        self.assertTrue(FreeCAD.writeRecoverySnapshotToTransientDir(self.Doc))

        metadata = os.path.join(self.Doc.TransientDir, "fc_recovery_file.xml")
        snapshot = os.path.join(self.Doc.TransientDir, "fc_recovery_file.cpart")

        self.assertTrue(os.path.isfile(metadata))
        self.assertTrue(os.path.isfile(snapshot))

        root = ET.parse(metadata).getroot()
        self.assertEqual(root.tag, "AutoRecovery")

        with open(snapshot) as written:
            recipe = written.read()
        self.assertIn("<Recipe", recipe)
        self.assertIn('name="RecoveryObject"', recipe)

    def testRecoveryMetadataEscapesXml(self):
        self.Doc.Label = 'Recovery <Label> & "Name"'
        self.savedFileName = os.path.join(tempfile.gettempdir(), "Recovery&Name.FCStd")
        self.Doc.saveAs(self.savedFileName)

        self.assertTrue(FreeCAD.writeRecoverySnapshotToTransientDir(self.Doc))

        metadata = os.path.join(self.Doc.TransientDir, "fc_recovery_file.xml")
        root = ET.parse(metadata).getroot()

        self.assertEqual(root.findtext("Label"), self.Doc.Label)
        self.assertEqual(root.findtext("FileName"), self.savedFileName)

    def testRejectRecoverySnapshotDuringTransaction(self):
        self.Doc.openTransaction("RecoveryWrite")
        try:
            self.assertFalse(self.Doc.canWriteRecoverySnapshot())
            with self.assertRaises(RuntimeError):
                FreeCAD.writeRecoverySnapshotToTransientDir(self.Doc)
        finally:
            self.Doc.abortTransaction()


class DocumentPlatformCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PlatformTests")
        self.Doc.addObject("App::FeatureTest", "Test")
        self.TempPath = tempfile.gettempdir()
        self.DocName = self.TempPath + os.sep + "PlatformTests.FCStd"

    def testFloatList(self):
        self.Doc.Test.FloatList = [-0.05, 2.5, 5.2]

        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(abs(self.Doc.Test.FloatList[0] + 0.05) < 0.01)
        self.assertTrue(abs(self.Doc.Test.FloatList[1] - 2.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.FloatList[2] - 5.2) < 0.01)

    def testColorList(self):
        self.Doc.Test.ColourList = [(1.0, 0.5, 0.0), (0.0, 0.5, 1.0)]

        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(abs(self.Doc.Test.ColourList[0][0] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][1] - 0.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][2] - 0.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[0][3] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][0] - 0.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][1] - 0.5) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][2] - 1.0) < 0.01)
        self.assertTrue(abs(self.Doc.Test.ColourList[1][3] - 1.0) < 0.01)

    def testVectorList(self):
        self.Doc.Test.VectorList = [(-0.05, 2.5, 5.2), (-0.05, 2.5, 5.2)]

        self.Doc.saveAs(self.DocName)
        FreeCAD.closeDocument("PlatformTests")
        self.Doc = FreeCAD.open(self.DocName)

        self.assertTrue(len(self.Doc.Test.VectorList) == 2)

    def testPoints(self):
        try:
            self.Doc.addObject("Points::Feature", "Points")

            self.Doc.saveAs(self.DocName)
            FreeCAD.closeDocument("PlatformTests")
            self.Doc = FreeCAD.open(self.DocName)

            self.assertTrue(self.Doc.Points.Points.count() == 0)
        except Exception:
            pass

    def tearDown(self):
        FreeCAD.closeDocument("PlatformTests")


class DocumentFileIncludeCases(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("FileIncludeTests")
        self.Doc.UndoMode = 1

    def testApplyFiles(self):
        self.Doc.openTransaction("Transaction0")
        self.L1 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject1")
        self.assertTrue(self.L1.File == "")
        self.Filename = self.L1.File

        self.Doc.openTransaction("Transaction1")
        self.TempPath = tempfile.gettempdir()
        # creating a file in the Transient directory of the document
        file = open(self.Doc.getTempFileName("test"), "w")
        file.write("test No1")
        file.close()
        self.L1.File = (file.name, "Test.txt")
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        file = open(self.TempPath + "/testNest.txt", "w")
        file.write("test No2")
        file.close()
        self.Doc.openTransaction("Transaction2")
        self.L1.File = file.name
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()
        self.Doc.undo()
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        self.Doc.undo()
        self.assertTrue(self.L1.File == "")
        self.Doc.redo()
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        self.Doc.redo()
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file = open(self.L1.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()
        # Save restore test
        FileName = self.TempPath + "/FileIncludeTests.fcstd"
        self.Doc.saveAs(FileName)
        FreeCAD.closeDocument("FileIncludeTests")
        self.Doc = FreeCAD.open(self.TempPath + "/FileIncludeTests.fcstd")
        # check if the file is still there
        self.L1 = self.Doc.getObject("FileObject1")
        file = open(self.L1.File, "r")
        res = file.read()
        FreeCAD.Console.PrintLog(res + "\n")
        self.assertTrue(res == "test No2")
        self.assertTrue(self.L1.File.split("/")[-1] == "Test.txt")
        file.close()

        # two files with the same base name must not overwrite each other
        L2 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject2")
        L3 = self.Doc.addObject("App::DocumentObjectFileIncluded", "FileObject3")

        # creating two files in the Transient directory of the document
        file1 = open(self.Doc.getTempFileName("test"), "w")
        file1.write("test No1")
        file1.close()
        file2 = open(self.Doc.getTempFileName("test"), "w")
        file2.write("test No2")
        file2.close()

        # applying the file with the same base name
        L2.File = (file1.name, "Test.txt")
        L3.File = (file2.name, "Test.txt")

        file = open(L2.File, "r")
        self.assertTrue(file.read() == "test No1")
        file.close()
        file = open(L3.File, "r")
        self.assertTrue(file.read() == "test No2")
        file.close()

        # create a second document, copy a file and close the document
        # the test is about to put the file to the correct transient dir
        doc2 = FreeCAD.newDocument("Doc2")
        L4 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L5 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L6 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject")
        L4.File = (L3.File, "Test.txt")
        L5.File = L3.File
        L6.File = L3.File
        FreeCAD.closeDocument("FileIncludeTests")
        self.Doc = FreeCAD.open(self.TempPath + "/FileIncludeTests.fcstd")
        self.assertTrue(os.path.exists(L4.File))
        self.assertTrue(os.path.exists(L5.File))
        self.assertTrue(os.path.exists(L6.File))
        self.assertTrue(L5.File != L6.File)
        # copy file from L5 which is in the same directory
        L7 = doc2.addObject("App::DocumentObjectFileIncluded", "FileObject3")
        L7.File = (L5.File, "Copy.txt")
        self.assertTrue(os.path.exists(L7.File))
        FreeCAD.closeDocument("Doc2")

    def tearDown(self):
        FreeCAD.closeDocument("FileIncludeTests")
