# SPDX-License-Identifier: LGPL-2.1-or-later

# **************************************************************************
#   Copyright (c) 2024 Werner Mayer <wmayer[at]users.sourceforge.net>     *
#                                                                         *
#   This file is part of FreeCAD.                                         *
#                                                                         *
#   FreeCAD is free software: you can redistribute it and/or modify it    *
#   under the terms of the GNU Lesser General Public License as           *
#   published by the Free Software Foundation, either version 2.1 of the  *
#   License, or (at your option) any later version.                       *
#                                                                         *
#   FreeCAD is distributed in the hope that it will be useful, but        *
#   WITHOUT ANY WARRANTY; without even the implied warranty of            *
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
#   Lesser General Public License for more details.                       *
#                                                                         *
#   You should have received a copy of the GNU Lesser General Public      *
#   License along with FreeCAD. If not, see                               *
#   <https://www.gnu.org/licenses/>.                                      *
#                                                                         *
# **************************************************************************

import os
import tempfile
import unittest
import FreeCAD as App
import Part
import ImportGui
from pivy import coin


def _dxf_tag(code, value):
    return f"{code}\n{value}\n"


def writeBlockDxf(path):
    """Write the smallest DXF that reaches the block-composition path.

    No sample file in the repository contains a single INSERT, so nothing
    otherwise exercises blocks at all. This one has a leaf block, an outer block
    that nests it, instances of both, and a polyline with a bulge (which is what
    makes the importer build a polyline out of separate line and arc objects).
    The rotated-and-scaled instance is the case that matters: an instance left at
    the origin lands correctly even when its transform is dropped.
    """
    t = _dxf_tag

    def line(x1, y1, x2, y2):
        return (
            t(0, "LINE")
            + t(8, "0")
            + t(10, x1)
            + t(20, y1)
            + t(30, 0.0)
            + t(11, x2)
            + t(21, y2)
            + t(31, 0.0)
        )

    def circle(x, y, r):
        return t(0, "CIRCLE") + t(8, "0") + t(10, x) + t(20, y) + t(30, 0.0) + t(40, r)

    def insert(name, x, y, rot=0.0, s=1.0):
        return (
            t(0, "INSERT")
            + t(8, "0")
            + t(2, name)
            + t(10, x)
            + t(20, y)
            + t(30, 0.0)
            + t(41, s)
            + t(42, s)
            + t(43, s)
            + t(50, rot)
        )

    def block(name, body):
        return (
            t(0, "BLOCK")
            + t(8, "0")
            + t(2, name)
            + t(70, 0)
            + t(10, 0.0)
            + t(20, 0.0)
            + t(30, 0.0)
            + t(3, name)
            + body
            + t(0, "ENDBLK")
            + t(8, "0")
        )

    out = t(0, "SECTION") + t(2, "HEADER") + t(9, "$ACADVER") + t(1, "AC1009") + t(0, "ENDSEC")
    out += t(0, "SECTION") + t(2, "BLOCKS")
    out += block("INNER", line(0.0, 0.0, 10.0, 0.0) + circle(10.0, 0.0, 2.0))
    out += block(
        "OUTER",
        line(0.0, 0.0, 0.0, 20.0) + line(0.0, 20.0, 30.0, 20.0) + insert("INNER", 5.0, 5.0, 30.0),
    )
    out += t(0, "ENDSEC")
    out += t(0, "SECTION") + t(2, "ENTITIES")
    # A red polyline whose second span is an arc -> built from separate objects.
    out += t(0, "LWPOLYLINE") + t(8, "0") + t(62, 1) + t(90, 3) + t(70, 0)
    out += t(10, 200.0) + t(20, 0.0) + t(10, 220.0) + t(20, 0.0) + t(42, 0.5)
    out += t(10, 240.0) + t(20, 10.0)
    out += insert("OUTER", 0.0, 0.0)
    out += insert("OUTER", 100.0, 50.0, rot=45.0, s=2.0)
    out += t(0, "ENDSEC") + t(0, "EOF")

    with open(path, "w") as handle:
        handle.write(out)


class DxfBlockImportTest(unittest.TestCase):
    """A DXF block is a container, and the styling reaches everything it builds."""

    def setUp(self):
        self.fileName = os.path.join(tempfile.gettempdir(), "BlockImportTest.dxf")
        writeBlockDxf(self.fileName)
        self.params = App.ParamGet("User parameter:BaseApp/Preferences/Mod/Draft")
        self.savedMode = self.params.GetInt("DxfImportMode", 2)
        self.savedLayers = self.params.GetBool("dxfUseDraftVisGroups", True)
        self.savedColors = self.params.GetBool("dxfGetOriginalColors", True)
        # EditablePrimitives: the mode whose blocks and polylines stay separate objects.
        self.params.SetInt("DxfImportMode", 1)
        self.params.SetBool("dxfUseDraftVisGroups", False)
        self.params.SetBool("dxfGetOriginalColors", True)
        self.doc = App.newDocument()
        ImportGui.readDXF(self.fileName, self.doc.Name)
        self.doc.recompute()

    def tearDown(self):
        self.params.SetInt("DxfImportMode", self.savedMode)
        self.params.SetBool("dxfUseDraftVisGroups", self.savedLayers)
        self.params.SetBool("dxfGetOriginalColors", self.savedColors)
        App.closeDocument(self.doc.Name)
        if os.path.exists(self.fileName):
            os.remove(self.fileName)

    def _named(self, prefix):
        return [obj for obj in self.doc.Objects if obj.Name.startswith(prefix)]

    def testBlockDefinitionsAreContainersNotGeometry(self):
        """A block gathers the objects an insert points at; it stores no shape.

        Asking a link group for a shape still answers -- it builds one from its
        children on request -- so the thing to check is that it holds no shape
        property of its own, which is the second copy a compound would store.
        """
        definitions = self._named("BLOCK_")
        self.assertEqual(len(definitions), 2)
        for definition in definitions:
            self.assertEqual(definition.TypeId, "App::LinkGroup")
            self.assertNotIn(
                "Shape",
                definition.PropertiesList,
                f"{definition.Name} stores a copy of its children's geometry",
            )
            self.assertTrue(definition.ElementList)

        outer = self.doc.getObject("BLOCK_OUTER")
        # The nested block is referenced through a link, not copied in.
        nested = [obj for obj in outer.ElementList if obj.TypeId == "App::Link"]
        self.assertEqual(len(nested), 1)
        self.assertEqual(nested[0].LinkedObject.Name, "BLOCK_INNER")

    def testAnInstanceCarriesItsOwnTransform(self):
        """Each insert is a link at its own place; the rotated, scaled one included."""
        instances = [
            obj
            for obj in self._named("Link_OUTER")
            if obj.LinkedObject is not None and obj.Visibility
        ]
        self.assertEqual(len(instances), 2)

        boxes = {}
        for instance in instances:
            shape = Part.getShape(instance, "", needSubElement=False, transform=True, retType=0)
            # Two lines of OUTER plus the line and circle of the nested INNER.
            self.assertEqual(len(shape.Edges), 4, f"{instance.Name} lost the nested block")
            boxes[round(instance.Placement.Base.x)] = shape.BoundBox

        atOrigin = boxes[0]
        self.assertAlmostEqual(atOrigin.XMin, 0.0, places=4)
        self.assertAlmostEqual(atOrigin.YMin, 0.0, places=4)
        self.assertAlmostEqual(atOrigin.XMax, 30.0, places=4)
        self.assertAlmostEqual(atOrigin.YMax, 20.0, places=4)

        # Placed at (100, 50), turned 45 degrees and doubled in size.
        moved = boxes[100]
        self.assertAlmostEqual(moved.XMin, 71.7157, places=3)
        self.assertAlmostEqual(moved.YMin, 50.0, places=3)
        self.assertAlmostEqual(moved.XMax, 114.1421, places=3)
        self.assertAlmostEqual(moved.YMax, 120.7107, places=3)

    def testStylingReachesAPolylineBuiltFromSeveralObjects(self):
        """The object holding a polyline's parts takes the file's colour too.

        It is a compound, which is a shape source but not a placed feature; a
        style dispatch keyed on the placed type skips it and leaves it at the
        default while its children come out correctly coloured.
        """
        polylines = self._named("Polyline")
        self.assertEqual(len(polylines), 1)
        polyline = polylines[0]
        self.assertTrue(polyline.Links, "the polyline should hold its parts")

        red = (1.0, 0.0, 0.0)
        self.assertEqual(tuple(polyline.ViewObject.LineColor)[:3], red)
        for part in polyline.Links:
            self.assertEqual(tuple(part.ViewObject.LineColor)[:3], red)


class ExportImportTest(unittest.TestCase):
    def setUp(self):
        TempPath = tempfile.gettempdir()
        self.fileName = TempPath + os.sep + "ColorPerFaceTest.step"
        self.doc = App.newDocument()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testSaveLoadStepFile(self):
        """
        Create a STEP file with color per face
        """
        part = self.doc.addObject("App::Part", "Part")
        box = part.newObject("Part::Box", "Box")
        self.doc.recompute()

        box.ViewObject.DiffuseColor = [
            (1.0, 0.0, 0.0, 1.0),
            (1.0, 0.0, 0.0, 1.0),
            (1.0, 0.0, 0.0, 1.0),
            (1.0, 0.0, 0.0, 1.0),
            (1.0, 1.0, 0.0, 1.0),
            (1.0, 1.0, 0.0, 1.0),
        ]

        ImportGui.export([part], self.fileName)

        self.doc.clearDocument()
        ImportGui.insert(name=self.fileName, docName=self.doc.Name, merge=False)

        part_features = list(filter(lambda x: x.isDerivedFrom("Part::Feature"), self.doc.Objects))
        self.assertEqual(len(part_features), 1)
        feature = part_features[0]

        self.assertEqual(len(feature.ViewObject.DiffuseColor), 6)
        self.assertEqual(feature.ViewObject.DiffuseColor[0], (1.0, 0.0, 0.0, 1.0))
        self.assertEqual(feature.ViewObject.DiffuseColor[1], (1.0, 0.0, 0.0, 1.0))
        self.assertEqual(feature.ViewObject.DiffuseColor[2], (1.0, 0.0, 0.0, 1.0))
        self.assertEqual(feature.ViewObject.DiffuseColor[3], (1.0, 0.0, 0.0, 1.0))
        self.assertEqual(feature.ViewObject.DiffuseColor[4], (1.0, 1.0, 0.0, 1.0))
        self.assertEqual(feature.ViewObject.DiffuseColor[5], (1.0, 1.0, 0.0, 1.0))

        sa = coin.SoSearchAction()
        sa.setType(coin.SoMaterialBinding.getClassTypeId())
        # We need an easier way to access nodes of a display mode
        sa.setInterest(coin.SoSearchAction.ALL)
        sa.apply(feature.ViewObject.RootNode)
        paths = sa.getPaths()

        bind = paths.get(1).getTail()
        self.assertEqual(bind.value.getValue(), bind.PER_PART)

        sa = coin.SoSearchAction()
        sa.setType(coin.SoMaterial.getClassTypeId())
        # We need an easier way to access nodes of a display mode
        sa.setInterest(coin.SoSearchAction.ALL)
        sa.apply(feature.ViewObject.RootNode)
        paths = sa.getPaths()

        mat = paths.get(1).getTail()
        self.assertEqual(mat.diffuseColor.getNum(), 6)
