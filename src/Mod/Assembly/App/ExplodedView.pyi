# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

from __future__ import annotations

from typing import Any

from Base.Metadata import constmethod, export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/ExplodedView.h", Namespace="Assembly")
class ExplodedView(DocumentObject):
    """
    A saved explosion of an assembly: an ordered group of moves that displace
    components away from each other so the parts can be seen separately.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    @constmethod
    def getAssembly(self) -> Any:
        """
        Return the assembly this view explodes, or None if it belongs to none.
        """
        ...

    def applyMoves(self) -> list:
        """
        Apply every move to the assembly, in order.

        Returns:
        A list of [start, end] vector pairs, one per component moved, marking the
        line each component travelled along.
        """
        ...

    def explodeTemporarily(self) -> None:
        """
        Remember the assembly's placements, then explode it in place.

        Paired with restoreAssembly(). Calling it twice without restoring keeps the
        first snapshot, so the assembly always returns to where the model actually was.
        """
        ...

    def restoreAssembly(self) -> None:
        """
        Put every component back where it was before explodeTemporarily().
        """
        ...

    def saveAssemblyAndExplode(self) -> Any:
        """
        Explode the assembly and return a compound of the explosion lines, or None
        if no component travelled.
        """
        ...

    @constmethod
    def getExplodedShape(self) -> Any:
        """
        Return a compound of the assembly as it would look exploded: every visible
        component at its exploded placement, plus the explosion lines.

        The document is not touched, which is what lets a drawing consume an exploded
        view without disturbing the model. Returns None if there is nothing to show.
        """
        ...
