# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from Base.Metadata import export
from App.GeoFeature import GeoFeature

@export(
    Twin="BodyBase",
    TwinPointer="BodyBase",
    Include="Mod/Part/App/BodyBase.h",
    Namespace="Part",
    FatherInclude="App/GeoFeaturePy.h",
    FatherNamespace="App",
)
class BodyBase(GeoFeature):
    """
    Base class of all Body objects

    Author: Juergen Riegel (FreeCAD@juergen-riegel.net)
    Licence: LGPL
    """

    ...
