# SPDX-License-Identifier: LGPL-2.1-or-later
# /**************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth contributors                                  *
#                                                                           *
#    This file is part of the Cruth CAD development system, a fork of       *
#    FreeCAD.                                                               *
#                                                                           *
#    Cruth is free software: you can redistribute it and/or modify it       *
#    under the terms of the GNU Lesser General Public License as            *
#    published by the Free Software Foundation, either version 2.1 of the   *
#    License, or (at your option) any later version.                        *
#                                                                           *
#    Cruth is distributed in the hope that it will be useful, but           *
#    WITHOUT ANY WARRANTY; without even the implied warranty of             *
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
#    Lesser General Public License for more details.                        *
#                                                                           *
#    You should have received a copy of the GNU Lesser General Public       *
#    License along with Cruth. If not, see                                  *
#    <https://www.gnu.org/licenses/>.                                       *
#                                                                           *
# **************************************************************************/

"""The exploded-view and simulation objects are shown by typed C++ view providers.

They used to be Python proxies attached by the creation command, which meant an
object's appearance existed only if that command had run: a document opened without
the command's module reaching it showed nothing, and C++ could not ask a view
provider anything without going through an opaque Proxy.

GUI-only: view providers (and therefore any render graph) exist solely with a
running GUI, so this skips under the headless ``FreeCADCmd -t`` lane and runs live
or under a virtual display (e.g. xvfb).
"""

import unittest

import FreeCAD as App

# Registers the Coin SWIG types: without it, reading a view provider's RootNode
# raises "No SWIG wrapped library loaded".
try:
    from pivy import coin  # noqa: F401
except ImportError:
    coin = None

import UtilsAssembly

from AssemblyTests.scene_graph import SceneGraphAssertions


@unittest.skipIf(not App.GuiUp, "view providers need a running GUI")
class TestAssemblyViewProviders(SceneGraphAssertions, unittest.TestCase):
    def setUp(self):
        import FreeCADGui as Gui

        self.doc = App.newDocument("asmvptest")
        self.assembly = self.doc.addObject("Assembly::AssemblyObject", "Assembly")

        viewGroup = UtilsAssembly.getViewGroup(self.assembly)
        self.view = viewGroup.newObject("Assembly::ExplodedView", "ExplodedView")
        self.step = self.view.newObject("Assembly::ExplodedViewStep", "Move")

        simGroup = UtilsAssembly.getSimulationGroup(self.assembly)
        self.simulation = simGroup.newObject("Assembly::Simulation", "Simulation")
        self.motion = self.simulation.newObject("Assembly::Motion", "Motion")

        self.doc.recompute()
        # View provider construction is deferred to the event loop.
        Gui.updateGui()

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    # -- the view providers are typed -------------------------------------

    def testEachObjectHasItsOwnTypedViewProvider(self):
        self.assertEqual(self.view.ViewObject.TypeId, "AssemblyGui::ViewProviderExplodedView")
        self.assertEqual(self.step.ViewObject.TypeId, "AssemblyGui::ViewProviderExplodedViewStep")
        self.assertEqual(self.simulation.ViewObject.TypeId, "AssemblyGui::ViewProviderSimulation")
        self.assertEqual(self.motion.ViewObject.TypeId, "AssemblyGui::ViewProviderMotion")

    def testAViewProviderNeedsNoPythonProxy(self):
        """The meaning of these objects is no longer parked in a Proxy attribute."""
        for obj in (self.view, self.step, self.simulation, self.motion):
            self.assertFalse(
                hasattr(obj.ViewObject, "Proxy"),
                f"{obj.Name} still carries a Python view provider proxy",
            )

    # -- what the tree shows ----------------------------------------------

    def testAnExplodedViewShowsItsMoves(self):
        self.assertEqual(self.view.ViewObject.claimChildren(), [self.step])

    def testASimulationShowsItsMotions(self):
        self.assertEqual(self.simulation.ViewObject.claimChildren(), [self.motion])

    # -- what a move draws -------------------------------------------------

    def testAMoveDrawsOneLinePerComponentItMoved(self):
        lines = [
            (App.Vector(0, 0, 0), App.Vector(10, 0, 0)),
            (App.Vector(0, 5, 0), App.Vector(10, 5, 0)),
        ]
        self.step.ViewObject.redrawLines(lines)

        self.assertNodeCount(self.step.ViewObject.RootNode, "SoLineSet", 2)

    def testRedrawingReplacesTheLinesRatherThanAddingToThem(self):
        """A move dragged twice must not leave the first explosion drawn."""
        two = [
            (App.Vector(0, 0, 0), App.Vector(10, 0, 0)),
            (App.Vector(0, 5, 0), App.Vector(10, 5, 0)),
        ]
        self.step.ViewObject.redrawLines(two)
        self.step.ViewObject.redrawLines([(App.Vector(0, 0, 0), App.Vector(20, 0, 0))])

        self.assertNodeCount(self.step.ViewObject.RootNode, "SoLineSet", 1)

    def testAMoveThatMovedNothingDrawsNothing(self):
        self.step.ViewObject.redrawLines([(App.Vector(0, 0, 0), App.Vector(10, 0, 0))])
        self.step.ViewObject.redrawLines([])

        self.assertNoNodes(self.step.ViewObject.RootNode, "SoLineSet")
