# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2024 Werner Mayer <wmayer[at]users.sourceforge.net>     *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************

import unittest

import FreeCAD

""" Test transaction interaction """


class TestSketch(unittest.TestCase):
    def setUp(self):
        self.doc = FreeCAD.newDocument("PartDesignTestSketch", type="Part")
        self.doc.UndoMode = True

    def testIssue17553(self):
        self.doc.openTransaction("Create box")
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.commitTransaction()
        self.doc.recompute()

        self.doc.openTransaction("Create sketch")
        body = self.doc.addObject("PartDesign::Body", "Body")
        plane = self.doc.getObject("XY_Plane")
        self.doc.commitTransaction()

        self.doc.openTransaction("Rename object")
        box.Label = "Object"
        self.doc.commitTransaction()

        sketch = body.addFeature(body.Document.addObject("Sketcher::SketchObject", "Sketch"))
        sketch.AttachmentSupport = (plane, [""])
        sketch.MapMode = "FlatFace"
        self.doc.recompute()

        # De-ownership (marker model): a body records nothing, so a loose sketch that no
        # solid has consumed yet has no owner — nothing links back to it. It still links
        # out to its attachment plane.
        self.assertEqual(sketch.InList, [])
        self.assertEqual(sketch.OutList, [plane])
        sketch.AttachmentSupport == [(plane, ("",))]

        self.doc.undo()  # undo renaming
        self.doc.undo()  # undo body creation
        self.doc.undo()  # undo box creation

        self.doc.openTransaction("Remove sketch")
        self.doc.removeObject(sketch.Name)
        self.doc.commitTransaction()

        self.doc.undo()  # undo removal

        # The world frame belongs to the document, minted when the document was
        # created rather than by the body, so undoing the body creation no longer
        # takes the attachment plane away with it. The restored sketch therefore
        # keeps its attachment instead of being left dangling.
        self.assertEqual(sketch.InList, [])
        self.assertEqual(sketch.OutList, [plane])
        self.assertEqual(sketch.AttachmentSupport, [(plane, ("",))])

    def testDependency(self):
        self.doc.openTransaction("Create box")
        box = self.doc.addObject("Part::Box", "Box")
        self.doc.commitTransaction()
        self.doc.recompute()

        self.doc.openTransaction("Create sketch")
        body = self.doc.addObject("PartDesign::Body", "Body")
        plane = self.doc.getObject("XY_Plane")
        self.doc.commitTransaction()

        self.doc.openTransaction("Rename object")
        box.Label = "Object"
        self.doc.commitTransaction()

        sketch = body.addFeature(body.Document.addObject("Sketcher::SketchObject", "Sketch"))
        sketch.AttachmentSupport = (plane, [""])
        sketch.MapMode = "FlatFace"
        self.doc.recompute()

        sketch.OutList
        sketch.AttachmentSupport

        self.doc.undo()  # undo renaming
        self.doc.undo()  # undo body creation
        self.doc.undo()  # undo box creation

        self.doc.DependencyGraph

    def tearDown(self):
        FreeCAD.closeDocument("PartDesignTestSketch")
