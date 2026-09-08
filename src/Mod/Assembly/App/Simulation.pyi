# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/Simulation.h", Namespace="Assembly")
class Simulation(DocumentObject):
    """
    A kinematic simulation of an assembly: the integration settings, plus the
    motions that drive it.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    def getAssembly(self, /) -> object:
        """
        The assembly this simulation belongs to, or None if it belongs to none.
        """
        ...

    def getMotions(self, /) -> list:
        """
        The motions driving this simulation, in order.
        """
        ...
