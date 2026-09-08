# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/ExplodedViewStep.h", Namespace="Assembly")
class ExplodedViewStep(DocumentObject):
    """
    One move within an exploded view: a set of components and the displacement
    applied to them.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    def applyStep(self, com: object = None, size: float = 100.0, /) -> list:
        """
        Apply this move to the components it names, in place.

        Args:
        com: centre a radial move explodes away from. Defaults to the origin.
        size: overall assembly size a radial move is scaled against.

        Returns:
        A list of [start, end] vector pairs, one per component moved.
        """
        ...
