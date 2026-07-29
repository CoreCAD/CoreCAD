# SPDX-License-Identifier: LGPL-2.1-or-later
# /****************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth (Sean Barton)                                 *
#                                                                           *
#    This file is part of FreeCAD.                                          *
#                                                                           *
#    FreeCAD is free software: you can redistribute it and/or modify it     *
#    under the terms of the GNU Lesser General Public License as            *
#    published by the Free Software Foundation, either version 2.1 of the   *
#    License, or (at your option) any later version.                        *
#                                                                           *
#    FreeCAD is distributed in the hope that it will be useful, but         *
#    WITHOUT ANY WARRANTY; without even the implied warranty of             *
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
#    Lesser General Public License for more details.                        *
#                                                                           *
#    You should have received a copy of the GNU Lesser General Public       *
#    License along with FreeCAD. If not, see                                *
#    <https://www.gnu.org/licenses/>.                                       *
#                                                                           *
# ***************************************************************************/

"""Draw-layer regression for STEP-imported live assemblies (#77).

A flexible sub-assembly renders its leaves via its owned proxy children (#63);
its AssemblyLink view provider must contribute nothing through the link path. A
regression (#77) left a stale ``setChildren`` array on the link's LinkView after
the transient rigid pass during import, so every leaf inside a flexible
sub-assembly was drawn twice. That is invisible at the App layer -- the object
graph is identical either way -- so it can only be caught by counting draw nodes
in the built Coin scene, which is what this test does.

GUI-only: ViewProviders (and therefore any render graph) exist solely with a
running GUI, so this skips under the headless ``FreeCADCmd -t`` lane and runs
live or under a virtual display (e.g. xvfb).
"""

import os
import shutil
import tempfile
import unittest

import FreeCAD as App

import Import
import AssemblyStepImport

from AssemblyTests.scene_graph import SceneGraphAssertions


@unittest.skipIf(not App.GuiUp, "draw-layer regression needs a running GUI")
class TestStepAssemblyImportDraw(SceneGraphAssertions, unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="corecad_stepdraw_")

    def tearDown(self):
        for doc in list(App.listDocuments().values()):
            try:
                App.closeDocument(doc.Name)
            except Exception:
                pass
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _nested_step(self):
        """A two-level STEP: Outer holds a Base box and a placed Inner sub-assembly
        (holding one placed Pin). The import turns Inner into a flexible
        Assembly::AssemblyLink -- the case #77 double-drew."""
        doc = App.newDocument("drawsrc")
        outer = doc.addObject("App::Part", "Outer")
        box = doc.addObject("Part::Box", "Base")
        box.Length = box.Width = box.Height = 10
        inner = doc.addObject("App::Part", "Inner")
        inner.Placement = App.Placement(App.Vector(100, 0, 0), App.Rotation())
        pin = doc.addObject("Part::Cylinder", "Pin")
        pin.Radius = 3
        pin.Height = 15
        pin.Placement = App.Placement(App.Vector(0, 50, 0), App.Rotation())
        inner.addObject(pin)
        outer.addObject(box)
        outer.addObject(inner)
        doc.recompute()
        path = os.path.join(self.tmp, "nested.step")
        Import.export([outer], path)
        App.closeDocument(doc.Name)
        return path

    def test_flexible_sub_assembly_draws_its_leaves_once(self):
        """The flexible sub-assembly's render carries a single copy of its leaf
        geometry, not the doubled copy #77 produced.

        The count is the number of ``SoBrepFaceSet`` draw nodes under the flexible
        AssemblyLink's view-provider root. This is a structural count (one per
        solid per render layer -- independent of tessellation density), measured
        on both states of the fix:

            fixed  -> 8   (leaves drawn once, via the owned proxy children)
            #77    -> 12  (a stale setChildren array drew every leaf a second time)

        so a return of the bug moves the number and fails the test.
        """
        import FreeCADGui as Gui

        # Importing pivy.coin loads the SWIG-wrapped Coin library; without it,
        # accessing a view provider's RootNode raises "No SWIG wrapped library
        # loaded" (the coin node types are not registered yet).
        from pivy import coin  # noqa: F401

        path = self._nested_step()
        assembly = AssemblyStepImport.importAssembly(path)
        adoc = assembly.Document
        Gui.updateGui()  # force deferred ViewProvider construction

        sub = [o for o in adoc.Objects if o.TypeId == "Assembly::AssemblyLink"][0]
        self.assertFalse(sub.Rigid, "sub-assembly should import flexible by default")

        view = sub.ViewObject
        self.assertIsNotNone(view, "flexible AssemblyLink has no view provider")

        self.assertNodeCount(
            view.RootNode,
            "SoBrepFaceSet",
            8,
            msg=(
                "flexible sub-assembly leaf geometry is not drawn exactly once "
                "(8 expected; 12 means the #77 double-draw is back)"
            ),
        )

        # Document-level sanity: the extra copy also showed in the whole scene
        # (12 fixed / 16 with the bug). Only checkable when a 3D view with an
        # accessible scene graph exists -- under the bare test runner there may be
        # no live GL view (getSceneGraph raises), in which case the targeted VP
        # count above already carries the regression signal.
        gui_doc = Gui.getDocument(adoc)
        active_view = gui_doc.ActiveView if gui_doc is not None else None
        try:
            scene = active_view.getSceneGraph() if active_view is not None else None
        except Exception:
            scene = None
        if scene is not None:
            self.assertNodeCount(
                scene,
                "SoBrepFaceSet",
                12,
                msg="whole-scene leaf-geometry count changed (16 means #77 is back)",
            )


if __name__ == "__main__":
    unittest.main()
