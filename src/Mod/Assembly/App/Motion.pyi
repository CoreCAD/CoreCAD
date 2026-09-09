# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors

from __future__ import annotations

from Base.Metadata import export

from App.DocumentObject import DocumentObject

@export(Include="Mod/Assembly/App/Motion.h", Namespace="Assembly")
class Motion(DocumentObject):
    """
    A driver applied to one joint for the duration of a simulation: a formula
    giving that joint's angle or displacement as a function of time.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    def getSimulation(self, /) -> object:
        """
        The simulation this motion belongs to, or None if it belongs to none.
        """
        ...

    def getAssembly(self, /) -> object:
        """
        The assembly this motion belongs to, or None if it belongs to none.
        """
        ...
