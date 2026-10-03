# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (c) 2026 Cruth contributors

import os
import shutil
import tempfile
import unittest

import FreeCAD


class DocumentDuplicateCases(unittest.TestCase):
    """A copy of an open document is never given a new identity without being asked (§7.2)."""

    def setUp(self):
        self.Dir = tempfile.mkdtemp()
        self.OriginalPath = os.path.join(self.Dir, "original.FCStd")
        self.CopyPath = os.path.join(self.Dir, "copy.FCStd")
        doc = FreeCAD.newDocument("DuplicateOriginal")
        self.ObjectUid = doc.addObject("App::FeatureTest", "Feature").Uid
        doc.saveAs(self.OriginalPath)
        FreeCAD.closeDocument(doc.Name)
        shutil.copyfile(self.OriginalPath, self.CopyPath)
        self.Original = FreeCAD.openDocument(self.OriginalPath)
        self.Uid = self.Original.Uid

    def openDocumentNames(self):
        return set(FreeCAD.listDocuments())

    def testCopyIsRefusedWithoutAnAnswer(self):
        before = self.openDocumentNames()
        with self.assertRaises(RuntimeError) as raised:
            FreeCAD.openDocument(self.CopyPath)
        self.assertIn("already open", str(raised.exception))
        self.assertEqual(self.openDocumentNames(), before)
        self.assertEqual(self.Original.Uid, self.Uid)

    def testSamePartKeepsTheIdentity(self):
        copy = FreeCAD.openDocument(self.CopyPath, duplicate="same")
        self.assertEqual(copy.Uid, self.Uid)
        self.assertEqual(self.Original.Uid, self.Uid)
        self.assertTrue(copy.StatesWhatItsFileStates)

    def testNewPartGetsFreshIdentityAndObjectsKeepTheirs(self):
        copy = FreeCAD.openDocument(self.CopyPath, duplicate="new")
        self.assertNotEqual(copy.Uid, self.Uid)
        self.assertEqual(self.Original.Uid, self.Uid)
        self.assertEqual(copy.getObject("Feature").Uid, self.ObjectUid)
        self.assertFalse(copy.StatesWhatItsFileStates, "the fresh identity is not in the file yet")

    def testNewPartIdentityHoldsAfterSaving(self):
        copy = FreeCAD.openDocument(self.CopyPath, duplicate="new")
        fresh = copy.Uid
        copy.save()
        FreeCAD.closeDocument(copy.Name)
        reopened = FreeCAD.openDocument(self.CopyPath)
        self.assertEqual(reopened.Uid, fresh)

    def testAnswerIsIgnoredWhenNothingClashes(self):
        FreeCAD.closeDocument(self.Original.Name)
        reopened = FreeCAD.openDocument(self.OriginalPath, duplicate="new")
        self.assertEqual(reopened.Uid, self.Uid)

    def testUnknownAnswerIsRejected(self):
        with self.assertRaises(ValueError):
            FreeCAD.openDocument(self.CopyPath, duplicate="maybe")

    def tearDown(self):
        for name in list(FreeCAD.listDocuments()):
            if name.startswith(("original", "copy", "DuplicateOriginal")):
                FreeCAD.closeDocument(name)
