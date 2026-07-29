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

"""Self-tests for the scene-graph node-count helpers (scene_graph.py).

These build synthetic Coin graphs from nodes that are registered without a GUI
(SoCube / SoSeparator / SoSwitch), so the helper stays covered by the normal
headless ``FreeCADCmd -t`` lane -- no display or ViewProvider required. The
Gui-layer tests that actually consume these helpers against real render graphs
are gated on ``App.GuiUp`` elsewhere.
"""

import unittest

from AssemblyTests.scene_graph import (
    SceneGraphAssertions,
    find_nodes,
    node_count,
)

try:
    from pivy import coin  # type: ignore

    coin.SoDB.init()  # idempotent; needed before building nodes under FreeCADCmd
    _COIN_OK = True
except Exception:  # pragma: no cover - environment-dependent
    coin = None
    _COIN_OK = False


@unittest.skipUnless(_COIN_OK, "pivy.coin not available")
class TestSceneGraphHelpers(SceneGraphAssertions, unittest.TestCase):
    def test_counts_all_matches_not_just_first(self):
        """node_count returns every match in the subtree (interest ALL), not one."""
        root = coin.SoSeparator()
        for _ in range(3):
            root.addChild(coin.SoCube())
        self.assertEqual(node_count(root, "SoCube"), 3)

    def test_zero_when_type_absent(self):
        """A type that is present in the build but absent from the graph counts zero."""
        root = coin.SoSeparator()
        root.addChild(coin.SoSphere())
        self.assertEqual(node_count(root, "SoCube"), 0)

    def test_unregistered_type_raises(self):
        """An unknown Coin type name is a programming error, not a silent zero."""
        root = coin.SoSeparator()
        with self.assertRaises(ValueError):
            node_count(root, "SoNotARealCoinNode")

    def test_descends_into_switched_off_branches(self):
        """search_all=True finds geometry under an SoSwitch set to SO_SWITCH_NONE."""
        root = coin.SoSeparator()
        switch = coin.SoSwitch()
        switch.whichChild = coin.SO_SWITCH_NONE  # nothing traversed for rendering
        switch.addChild(coin.SoCube())
        root.addChild(switch)

        self.assertEqual(node_count(root, "SoCube", search_all=True), 1)
        # With search_all disabled the hidden branch is not entered.
        self.assertEqual(node_count(root, "SoCube", search_all=False), 0)

    def test_find_nodes_returns_the_actual_nodes(self):
        """find_nodes returns the matched node objects, tails of the found paths."""
        root = coin.SoSeparator()
        cube = coin.SoCube()
        root.addChild(cube)
        found = find_nodes(root, "SoCube")
        self.assertEqual(len(found), 1)
        self.assertIsInstance(found[0], coin.SoCube)
        self.assertEqual(found[0].getName(), cube.getName())

    def test_mixin_assertions(self):
        """The TestCase mixin asserts counts and reports the actual on mismatch."""
        root = coin.SoSeparator()
        root.addChild(coin.SoCube())
        root.addChild(coin.SoCube())

        self.assertNodeCount(root, "SoCube", 2)
        self.assertNoNodes(root, "SoSphere")

        with self.assertRaises(AssertionError) as ctx:
            self.assertNodeCount(root, "SoCube", 5)
        self.assertIn("found 2", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
