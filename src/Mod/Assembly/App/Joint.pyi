# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import constmethod, export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/Joint.h", Namespace="Assembly")
class Joint(DocumentObject):
    """
    A mate constraint between two assembly components.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    @constmethod
    def usesPreSolve(self) -> bool:
        """
        Whether this joint's kind is pre-positioned onto the mate before solving.
        """

    @constmethod
    def forbidsParallel(self) -> bool:
        """
        Whether the solver cannot handle this kind's two coordinate systems being parallel.
        """

    @constmethod
    def ignoresVertex(self) -> bool:
        """
        Whether reference-to-frame resolution ignores a picked vertex for this kind.
        """
