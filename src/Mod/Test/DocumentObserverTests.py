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

import FreeCAD


class DocumentObserverCases(unittest.TestCase):
    class Observer:
        def __init__(self):
            self.clear()

        def clear(self):
            self.signal = []
            self.parameter = []
            self.parameter2 = []

        def slotCreatedDocument(self, doc):
            self.signal.append("DocCreated")
            self.parameter.append(doc)

        def slotDeletedDocument(self, doc):
            self.signal.append("DocDeleted")
            self.parameter.append(doc)

        def slotRelabelDocument(self, doc):
            self.signal.append("DocRelabled")
            self.parameter.append(doc)

        def slotActivateDocument(self, doc):
            self.signal.append("DocActivated")
            self.parameter.append(doc)

        def slotRecomputedDocument(self, doc):
            self.signal.append("DocRecomputed")
            self.parameter.append(doc)

        def slotUndoDocument(self, doc):
            self.signal.append("DocUndo")
            self.parameter.append(doc)

        def slotRedoDocument(self, doc):
            self.signal.append("DocRedo")
            self.parameter.append(doc)

        def slotOpenTransaction(self, doc, name):
            self.signal.append("DocOpenTransaction")
            self.parameter.append(doc)
            self.parameter2.append(name)

        def slotCommitTransaction(self, doc):
            self.signal.append("DocCommitTransaction")
            self.parameter.append(doc)

        def slotAbortTransaction(self, doc):
            self.signal.append("DocAbortTransaction")
            self.parameter.append(doc)

        def slotBeforeChangeDocument(self, doc, prop):
            self.signal.append("DocBeforeChange")
            self.parameter.append(doc)
            self.parameter2.append(prop)

        def slotChangedDocument(self, doc, prop):
            self.signal.append("DocChanged")
            self.parameter.append(doc)
            self.parameter2.append(prop)

        def slotCreatedObject(self, obj):
            self.signal.append("ObjCreated")
            self.parameter.append(obj)

        def slotDeletedObject(self, obj):
            self.signal.append("ObjDeleted")
            self.parameter.append(obj)

        def slotChangedObject(self, obj, prop):
            self.signal.append("ObjChanged")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotBeforeChangeObject(self, obj, prop):
            self.signal.append("ObjBeforeChange")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotRecomputedObject(self, obj):
            self.signal.append("ObjRecomputed")
            self.parameter.append(obj)

        def slotAppendDynamicProperty(self, obj, prop):
            self.signal.append("ObjAddDynProp")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotRemoveDynamicProperty(self, obj, prop):
            self.signal.append("ObjRemoveDynProp")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotChangePropertyEditor(self, obj, prop):
            self.signal.append("ObjChangePropEdit")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotStartSaveDocument(self, obj, name):
            self.signal.append("DocStartSave")
            self.parameter.append(obj)
            self.parameter2.append(name)

        def slotFinishSaveDocument(self, obj, name):
            self.signal.append("DocFinishSave")
            self.parameter.append(obj)
            self.parameter2.append(name)

        def slotBeforeAddingDynamicExtension(self, obj, extension):
            self.signal.append("ObjBeforeDynExt")
            self.parameter.append(obj)
            self.parameter2.append(extension)

        def slotAddedDynamicExtension(self, obj, extension):
            self.signal.append("ObjDynExt")
            self.parameter.append(obj)
            self.parameter2.append(extension)

    class GuiObserver:
        def __init__(self):
            self.clear()

        def clear(self):
            self.signal = []
            self.parameter = []
            self.parameter2 = []

        def slotCreatedDocument(self, doc):
            self.signal.append("DocCreated")
            self.parameter.append(doc)

        def slotDeletedDocument(self, doc):
            self.signal.append("DocDeleted")
            self.parameter.append(doc)

        def slotRelabelDocument(self, doc):
            self.signal.append("DocRelabled")
            self.parameter.append(doc)

        def slotRenameDocument(self, doc):
            self.signal.append("DocRenamed")
            self.parameter.append(doc)

        def slotActivateDocument(self, doc):
            self.signal.append("DocActivated")
            self.parameter.append(doc)

        def slotCreatedObject(self, obj):
            self.signal.append("ObjCreated")
            self.parameter.append(obj)

        def slotDeletedObject(self, obj):
            self.signal.append("ObjDeleted")
            self.parameter.append(obj)

        def slotChangedObject(self, obj, prop):
            self.signal.append("ObjChanged")
            self.parameter.append(obj)
            self.parameter2.append(prop)

        def slotInEdit(self, obj):
            self.signal.append("ObjInEdit")
            self.parameter.append(obj)

        def slotResetEdit(self, obj):
            self.signal.append("ObjResetEdit")
            self.parameter.append(obj)

    def assertQuiet(self, observer):
        self.assertTrue(not observer.signal and not observer.parameter and not observer.parameter2)

    def setUp(self):
        self.Obs = self.Observer()
        FreeCAD.addDocumentObserver(self.Obs)

    def testRemoveObserver(self):
        FreeCAD.removeDocumentObserver(self.Obs)
        self.Obs.clear()
        self.Doc1 = FreeCAD.newDocument("Observer")
        FreeCAD.closeDocument(self.Doc1.Name)
        self.assertEqual(len(self.Obs.signal), 0)
        self.assertEqual(len(self.Obs.parameter2), 0)
        self.assertEqual(len(self.Obs.signal), 0)
        FreeCAD.addDocumentObserver(self.Obs)

    def testSave(self):
        TempPath = tempfile.gettempdir()
        SaveName = TempPath + os.sep + "SaveRestoreTests.FCStd"
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Doc1.saveAs(SaveName)
        self.assertEqual(self.Obs.signal.pop(), "DocFinishSave")
        self.assertEqual(self.Obs.parameter2.pop(), self.Doc1.FileName)
        self.assertEqual(self.Obs.signal.pop(), "DocStartSave")
        self.assertEqual(self.Obs.parameter2.pop(), self.Doc1.FileName)
        FreeCAD.closeDocument(self.Doc1.Name)

    def testDocument(self):
        # in case another document already exists then the tests cannot
        # be done reliably
        if FreeCAD.GuiUp and FreeCAD.activeDocument():
            return

        self.Doc1 = self.newObservedDocument("Observer1")
        self.Doc2 = self.newObservedDocument("Observer2")
        self.activationSignals()
        self.transactionSignals()
        self.changeAndCloseSignals()

    def newObservedDocument(self, name):
        doc = FreeCAD.newDocument(name)
        if FreeCAD.GuiUp:
            self.assertEqual(self.Obs.signal.pop(0), "DocActivated")
            self.assertTrue(self.Obs.parameter.pop(0) is doc)
        self.assertEqual(self.Obs.signal.pop(0), "DocCreated")
        self.assertTrue(self.Obs.parameter.pop(0) is doc)
        self.assertEqual(self.Obs.signal.pop(0), "DocBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is doc)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is doc)
        self.assertEqual(self.Obs.parameter2.pop(0), "Label")
        self.assertEqual(self.Obs.signal.pop(0), "DocRelabled")
        self.assertTrue(self.Obs.parameter.pop(0) is doc)
        self.assertQuiet(self.Obs)
        return doc

    def activationSignals(self):
        FreeCAD.setActiveDocument("Observer1")
        self.assertEqual(self.Obs.signal.pop(), "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertQuiet(self.Obs)

        # undo/redo is not enabled in cmd line mode by default
        self.Doc2.UndoMode = 1

        # Must set Doc2 as active document before start transaction test. If not,
        # then a transaction will be auto created inside the active document if a
        # new transaction is triggered from a non active document
        FreeCAD.setActiveDocument("Observer2")
        self.assertEqual(self.Obs.signal.pop(), "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        self.assertQuiet(self.Obs)

    def transactionSignals(self):
        self.Doc2.openTransaction("test")
        # openTransaction() only sets up a pending transaction; the first change creates it
        self.Doc2.addObject("App::FeatureTest", "test")
        self.assertEqual(self.Obs.signal[0], "DocOpenTransaction")
        self.assertEqual(self.Obs.signal.count("DocOpenTransaction"), 1)
        self.assertTrue(self.Obs.parameter[0] is self.Doc2)
        self.assertEqual(self.Obs.parameter2[0], "test")
        self.Obs.clear()

        self.Doc2.commitTransaction()
        self.assertEqual(self.Obs.signal.pop(), "DocCommitTransaction")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        self.assertQuiet(self.Obs)

        self.Doc2.openTransaction("test2")
        self.Doc2.addObject("App::FeatureTest", "test")
        self.assertEqual(self.Obs.signal[0], "DocOpenTransaction")
        self.assertEqual(self.Obs.signal.count("DocOpenTransaction"), 1)
        self.assertTrue(self.Obs.parameter[0] is self.Doc2)
        self.assertEqual(self.Obs.parameter2[0], "test2")
        # there will be other signals because of the addObject()
        self.Obs.clear()

        self.Doc2.abortTransaction()
        self.assertEqual(self.Obs.signal.pop(), "DocAbortTransaction")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because of aborting the above addObject()
        self.Obs.clear()

        self.Doc2.undo()
        self.assertEqual(self.Obs.signal.pop(), "DocUndo")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because undoing the above addObject()
        self.Obs.clear()

        self.Doc2.redo()
        self.assertEqual(self.Obs.signal.pop(), "DocRedo")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        # there will be other signals because redoing the above addObject()
        self.Obs.clear()

    def changeAndCloseSignals(self):
        self.Doc1.Comment = "test comment"
        self.assertEqual(self.Obs.signal.pop(0), "DocBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Comment")
        self.assertEqual(self.Obs.signal.pop(0), "DocChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertEqual(self.Obs.parameter2.pop(0), "Comment")

        FreeCAD.closeDocument(self.Doc2.Name)
        self.assertEqual(self.Obs.signal.pop(), "DocDeleted")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc2)
        if FreeCAD.GuiUp and not FreeCAD.Gui.HasQtBug_129596:
            # only has document activated signal when running in GUI mode
            self.assertEqual(self.Obs.signal.pop(), "DocActivated")
            self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertQuiet(self.Obs)

        FreeCAD.closeDocument(self.Doc1.Name)
        self.assertEqual(self.Obs.signal.pop(), "DocDeleted")
        self.assertEqual(self.Obs.parameter.pop(), self.Doc1)
        self.assertQuiet(self.Obs)

    def testObject(self):
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Obs.clear()
        self.objectLifecycleSignals()
        self.dynamicPropertySignals()
        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()

    def objectLifecycleSignals(self):
        obj = self.Doc1.addObject("App::DocumentObject", "obj")
        self.assertTrue(self.Obs.signal.pop() == "ObjCreated")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        # there are multiple object change signals
        self.Obs.clear()

        obj.Label = "myobj"
        self.assertTrue(self.Obs.signal.pop(0) == "ObjBeforeChange")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "Label")
        self.assertTrue(self.Obs.signal.pop(0) == "ObjChanged")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "Label")
        self.assertQuiet(self.Obs)

        obj.enforceRecompute()
        obj.recompute()
        self.assertTrue(self.Obs.signal.pop(0) == "ObjRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertQuiet(self.Obs)

        obj.enforceRecompute()
        self.Doc1.recompute()
        self.assertTrue(self.Obs.signal.pop(0) == "ObjRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertTrue(self.Obs.signal.pop(0) == "DocRecomputed")
        self.assertTrue(self.Obs.parameter.pop(0) is self.Doc1)
        self.assertQuiet(self.Obs)

        FreeCAD.ActiveDocument.removeObject(obj.Name)
        self.assertTrue(self.Obs.signal.pop(0) == "ObjDeleted")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertQuiet(self.Obs)

    def dynamicPropertySignals(self):
        pyobj = self.Doc1.addObject("App::FeaturePython", "pyobj")
        self.Obs.clear()
        pyobj.addProperty("App::PropertyLength", "Prop", "Group", "test property")
        self.assertTrue(self.Obs.signal.pop() == "ObjAddDynProp")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)

        pyobj.setEditorMode("Prop", ["ReadOnly"])
        self.assertTrue(self.Obs.signal.pop() == "ObjChangePropEdit")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)

        pyobj.removeProperty("Prop")
        self.assertTrue(self.Obs.signal.pop() == "ObjRemoveDynProp")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)

        pyobj.addExtension("App::GroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjDynExt")
        self.assertTrue(self.Obs.parameter.pop() is pyobj)
        self.assertTrue(self.Obs.parameter2.pop() == "App::GroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop(0) == "ObjBeforeDynExt")
        self.assertTrue(self.Obs.parameter.pop(0) is pyobj)
        self.assertTrue(self.Obs.parameter2.pop(0) == "App::GroupExtensionPython")
        # a proxy property was changed, hence those events are also in the signal list
        self.Obs.clear()

    def testUndoDisabledDocument(self):

        # testing document level signals
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.Doc1.UndoMode = 0
        self.Obs.clear()

        self.Doc1.openTransaction("test")
        self.Doc1.commitTransaction()
        self.Doc1.undo()
        self.Doc1.redo()
        self.assertQuiet(self.Obs)

        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()

    def testGuiObserver(self):

        if not FreeCAD.GuiUp:
            return

        # in case another document already exists then the tests cannot
        # be done reliably
        if FreeCAD.activeDocument():
            return

        self.GuiObs = self.GuiObserver()
        FreeCAD.Gui.addDocumentObserver(self.GuiObs)
        self.guiDocumentSignals()
        obj = self.guiObjectCreated()
        self.viewObjectPropertySignals(obj)
        self.editSignals(obj)
        self.guiObjectRemoved(obj)

        FreeCAD.closeDocument(self.Doc1.Name)
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop() == "DocDeleted")
        self.assertTrue(self.GuiObs.parameter.pop() is self.GuiDoc1)
        self.assertQuiet(self.GuiObs)

        FreeCAD.Gui.removeDocumentObserver(self.GuiObs)
        self.GuiObs.clear()

    def guiDocumentSignals(self):
        self.Doc1 = FreeCAD.newDocument("Observer1")
        self.GuiDoc1 = FreeCAD.Gui.getDocument(self.Doc1.Name)
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocCreated")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocActivated")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocRelabled")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertQuiet(self.GuiObs)

        self.Doc1.Label = "test"
        self.assertTrue(self.Obs.signal.pop() == "DocRelabled")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        # not interested in the change signals
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop(0) == "DocRelabled")
        self.assertTrue(self.GuiObs.parameter.pop(0) is self.GuiDoc1)
        self.assertQuiet(self.GuiObs)

        FreeCAD.setActiveDocument(self.Doc1.Name)
        self.assertTrue(self.Obs.signal.pop() == "DocActivated")
        self.assertTrue(self.Obs.parameter.pop() is self.Doc1)
        self.assertQuiet(self.Obs)
        self.assertTrue(self.GuiObs.signal.pop() == "DocActivated")
        self.assertTrue(self.GuiObs.parameter.pop() is self.GuiDoc1)
        self.assertQuiet(self.GuiObs)

    def guiObjectCreated(self):
        obj = self.Doc1.addObject("App::FeaturePython", "obj")
        self.assertTrue(self.Obs.signal.pop() == "ObjCreated")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        # there are multiple object change signals
        self.Obs.clear()
        self.assertTrue(self.GuiObs.signal.pop() == "ObjCreated")
        self.assertTrue(self.GuiObs.parameter.pop() is obj.ViewObject)

        # There are object change signals, caused by sync of obj.Visibility. Same below.
        self.GuiObs.clear()
        return obj

    def viewObjectPropertySignals(self, obj):
        obj.ViewObject.Visibility = False
        self.assertTrue(self.Obs.signal.pop() == "ObjChanged")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        self.assertTrue(self.Obs.parameter2.pop() == "Visibility")
        self.assertTrue(self.Obs.signal.pop() == "ObjBeforeChange")
        self.assertTrue(self.Obs.parameter.pop() is obj)
        self.assertTrue(self.Obs.parameter2.pop() == "Visibility")
        self.assertQuiet(self.Obs)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjChanged")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertTrue(self.GuiObs.parameter2.pop(0) == "Visibility")
        self.assertQuiet(self.GuiObs)

        obj.ViewObject.addProperty("App::PropertyLength", "Prop", "Group", "test property")
        self.assertTrue(self.Obs.signal.pop() == "ObjAddDynProp")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)
        self.assertQuiet(self.GuiObs)

        obj.ViewObject.setEditorMode("Prop", ["ReadOnly"])
        self.assertTrue(self.Obs.signal.pop() == "ObjChangePropEdit")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)
        self.assertQuiet(self.GuiObs)

        obj.ViewObject.removeProperty("Prop")
        self.assertTrue(self.Obs.signal.pop() == "ObjRemoveDynProp")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Prop")
        self.assertQuiet(self.Obs)
        self.assertQuiet(self.GuiObs)

    def editSignals(self, obj):
        self.GuiDoc1.setEdit("obj", 0)
        self.assertQuiet(self.Obs)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjInEdit")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertQuiet(self.GuiObs)

        self.GuiDoc1.resetEdit()
        self.assertQuiet(self.Obs)
        self.assertTrue(self.GuiObs.signal.pop(0) == "ObjResetEdit")
        self.assertTrue(self.GuiObs.parameter.pop(0) is obj.ViewObject)
        self.assertQuiet(self.GuiObs)

    def guiObjectRemoved(self, obj):
        obj.ViewObject.addExtension("Gui::ViewProviderGroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjDynExt")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Gui::ViewProviderGroupExtensionPython")
        self.assertTrue(self.Obs.signal.pop() == "ObjBeforeDynExt")
        self.assertTrue(self.Obs.parameter.pop() is obj.ViewObject)
        self.assertTrue(self.Obs.parameter2.pop() == "Gui::ViewProviderGroupExtensionPython")
        # a proxy property was changed, hence those events are also in the signal list (but of GUI observer)
        self.GuiObs.clear()

        vo = obj.ViewObject
        FreeCAD.ActiveDocument.removeObject(obj.Name)
        self.assertTrue(self.Obs.signal.pop(0) == "ObjDeleted")
        self.assertTrue(self.Obs.parameter.pop(0) is obj)
        self.assertQuiet(self.Obs)
        self.assertTrue(self.GuiObs.signal.pop() == "ObjDeleted")
        self.assertTrue(self.GuiObs.parameter.pop() is vo)
        self.assertQuiet(self.GuiObs)

    def tearDown(self):
        FreeCAD.removeDocumentObserver(self.Obs)
        self.Obs.clear()
        self.Obs = None
