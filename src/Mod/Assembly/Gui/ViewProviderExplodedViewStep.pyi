# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from typing import Any

from Base.Metadata import export
from Gui.ViewProvider import ViewProvider

@export(
    Include="Mod/Assembly/Gui/ViewProviderExplodedViewStep.h",
    Namespace="AssemblyGui",
)
class ViewProviderExplodedViewStep(ViewProvider):
    """
    The view provider of one move within an exploded view.

    Author: Cruth contributors
    License: LGPL-2.1-or-later
    """

    def redrawLines(self, lines: Any, /) -> None:
        """
        Draw one dashed line per component moved, replacing whatever was drawn before.

        Args:
            lines: a sequence of (start, end) point pairs, as returned by
                ExplodedViewStep.applyStep().
        """
        ...
