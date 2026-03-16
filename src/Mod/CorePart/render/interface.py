# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

"""
CorePart Render Interface — renderer-agnostic operations for CorePart UX features.

All proprietary features (section view, interference analyser, face origin inspector,
geometry metrics overlay, etc.) call this interface only.  The active backend is
swapped via :func:`registry.set_backend` without touching any UX code:

    Coin3D today   →  ``CoinBackend``   (routes through FreeCAD Python view API + pivy)
    wgpu tomorrow  →  ``WgpuBackend``   (private repo, transparent Qt overlay)

Design principle: every method here is called at **interaction frequency** (on user
events such as mouse clicks or parameter changes) — never inside the render loop.
The actual GPU draw calls remain in C++/Coin3D regardless of which backend is active.
The abstraction therefore adds no measurable overhead.

Swapping the backend (future wgpu migration):
    from CorePart.render import registry
    from corecad_part.render.wgpu_backend import WgpuBackend
    registry.set_backend(WgpuBackend())

All methods are silent no-ops when no document or 3D view is active.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, List, Optional, Protocol, Tuple

if TYPE_CHECKING:
    import FreeCAD as App

# RGBA colour, each channel 0.0–1.0
Colour = Tuple[float, float, float, float]

# Opaque handle returned by draw_* methods; pass to remove_overlay() to delete
OverlayId = str


class RenderInterface(Protocol):
    """Renderer-agnostic drawing operations for CorePart UX overlays.

    Implement this protocol to add a new renderer backend (e.g. wgpu).
    The Coin3D implementation lives in :mod:`CorePart.render.coin_backend`.
    """

    # ------------------------------------------------------------------ #
    # Face highlighting                                                    #
    # ------------------------------------------------------------------ #

    def highlight_faces(
        self,
        obj: "App.DocumentObject",
        face_indices: List[int],
        colour: Colour,
    ) -> None:
        """Colour specific faces on *obj* without modifying document geometry.

        *face_indices* are 0-based indices into ``obj.Shape.Faces``.
        Original colours are saved so they can be restored by :meth:`clear_highlights`.
        Calling this again on the same object replaces any previous highlight.
        """
        ...

    def clear_highlights(self, obj: Optional["App.DocumentObject"] = None) -> None:
        """Restore original face colours.

        If *obj* is given, restore only that object.
        If *obj* is ``None``, restore all previously highlighted objects.
        """
        ...

    # ------------------------------------------------------------------ #
    # Clipping plane                                                       #
    # ------------------------------------------------------------------ #

    def set_clipping_plane(
        self,
        placement: "App.Placement",
        enabled: bool = True,
    ) -> None:
        """Place a display-only clipping plane at *placement*.

        No geometry is created or modified.  The clip normal is the +Z axis of
        *placement*.  Replaces any previously active clipping plane.
        """
        ...

    def clear_clipping_plane(self) -> None:
        """Remove the clipping plane if one is active."""
        ...

    # ------------------------------------------------------------------ #
    # Scene overlays                                                       #
    # ------------------------------------------------------------------ #

    def draw_line(
        self,
        start: "App.Vector",
        end: "App.Vector",
        colour: Colour,
        width: float = 1.0,
    ) -> OverlayId:
        """Draw a 3D world-space line segment.  Returns an overlay ID."""
        ...

    def draw_annotation(
        self,
        position: "App.Vector",
        text: str,
        colour: Colour,
    ) -> OverlayId:
        """Draw a screen-aligned text label anchored at a 3D world-space point.
        Returns an overlay ID."""
        ...

    def draw_bounding_box(
        self,
        bbox: "App.BoundBox",
        colour: Colour,
        width: float = 1.0,
    ) -> OverlayId:
        """Draw a wireframe axis-aligned bounding box.  Returns an overlay ID."""
        ...

    def remove_overlay(self, overlay_id: OverlayId) -> None:
        """Remove a specific overlay by the ID returned from a draw_* method."""
        ...

    def clear_overlays(self) -> None:
        """Remove all scene overlays (lines, annotations, bounding boxes)."""
        ...
