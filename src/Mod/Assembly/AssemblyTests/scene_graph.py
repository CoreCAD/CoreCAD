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

"""Reusable scene-graph (Coin/Inventor) inspection helpers for Gui-layer tests.

Some defects are invisible at the App layer and only show up in the built render
graph -- e.g. a flexible sub-assembly that draws its leaf geometry twice (#77).
Catching those means counting draw nodes under a ViewProvider's root, which is
what these helpers do: they wrap ``SoSearchAction`` so a test can ask "how many
``SoBrepFaceSet`` nodes hang under this root?" in one line, and offer a
``unittest`` mixin with a node-count assertion that reports the actual count on
failure.

``pivy.coin`` is imported lazily so that importing this module never fails on a
build without pivy; callers that need Coin get a clear skip/error at call time.
"""


def _coin():
    """Return the ``pivy.coin`` module, or raise a helpful error if unavailable."""
    try:
        from pivy import coin  # type: ignore
    except Exception as exc:  # pragma: no cover - environment-dependent
        raise RuntimeError("pivy.coin is not available in this build") from exc
    return coin


def find_nodes(root, type_name, *, search_all=True):
    """Every Coin node of ``type_name`` reachable from ``root``.

    Returns the matched node objects (tails of the found paths). ``search_all``
    descends through hidden ``SoSwitch`` branches too, so the result reflects the
    graph that was built, not only what is currently switched on -- important when
    asserting that geometry exists (or does *not*) regardless of visibility state.
    """
    coin = _coin()
    sotype = coin.SoType.fromName(type_name)
    if sotype.isBad():
        raise ValueError(f"Coin type not registered: {type_name!r}")

    action = coin.SoSearchAction()
    action.setType(sotype)
    action.setInterest(coin.SoSearchAction.ALL)
    action.setSearchingAll(search_all)
    action.apply(root)

    paths = action.getPaths()
    return [paths[i].getTail() for i in range(paths.getLength())]


def node_count(root, type_name, *, search_all=True):
    """Number of Coin nodes of ``type_name`` reachable from ``root``.

    Thin count-only wrapper over :func:`find_nodes` for the common case where a
    test only cares how many draw nodes of a kind are present.
    """
    coin = _coin()
    sotype = coin.SoType.fromName(type_name)
    if sotype.isBad():
        raise ValueError(f"Coin type not registered: {type_name!r}")

    action = coin.SoSearchAction()
    action.setType(sotype)
    action.setInterest(coin.SoSearchAction.ALL)
    action.setSearchingAll(search_all)
    action.apply(root)
    return action.getPaths().getLength()


class SceneGraphAssertions:
    """``unittest.TestCase`` mixin adding scene-graph node-count assertions.

    Mix into a test case alongside ``unittest.TestCase`` to get assertions that
    report the actual node count on failure, which is what you want when a render
    regression changes a count you expected to be stable.
    """

    def nodeCount(self, root, type_name, *, search_all=True):
        return node_count(root, type_name, search_all=search_all)

    def assertNodeCount(self, root, type_name, expected, *, search_all=True, msg=None):
        actual = node_count(root, type_name, search_all=search_all)
        default = f"expected {expected} {type_name} node(s) under root, found {actual}"
        self.assertEqual(actual, expected, msg or default)

    def assertNoNodes(self, root, type_name, *, search_all=True, msg=None):
        self.assertNodeCount(root, type_name, 0, search_all=search_all, msg=msg)
