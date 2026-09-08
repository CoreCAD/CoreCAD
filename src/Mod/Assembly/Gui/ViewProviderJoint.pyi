# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from typing import Any

from Base.Metadata import export
from Base.Placement import Placement
from Gui.ViewProvider import ViewProvider

@export(
    Include="Mod/Assembly/Gui/ViewProviderJoint.h",
    Namespace="AssemblyGui",
)
class ViewProviderJoint(ViewProvider):
    """
    The view provider of a joint: it draws the joint frames on the components.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    def redrawMarkers(self) -> None:
        """
        Redraw both joint frames from the joint's references and placements.

        Needed after something has moved a component: a frame's position depends on
        where the component is, not only on what the joint stores.
        """
        ...

    def showPreviewJcs(self, placement: Placement, ref: Any, /) -> None:
        """
        Show the preview frame while a joint is being placed.

        Args:
            placement: the frame, expressed in the reference's own frame.
            ref: the reference it is measured against, as [object, [subelement]].
        """
        ...

    def hidePreviewJcs(self) -> None:
        """Hide the preview frame."""
        ...

    def setPickableState(self, state: bool, /) -> None:
        """Make the joint frames selectable (True) or unselectable (False) in the 3D view."""
        ...
