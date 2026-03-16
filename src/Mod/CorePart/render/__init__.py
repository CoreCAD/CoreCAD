# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

"""
CorePart render abstraction layer.

Exposes a renderer-agnostic interface so all proprietary UX features (section view,
interference analyser, face origin inspector, geometry metrics overlay, etc.) are
independent of the underlying rendering backend.

    Backend today:   CoinBackend  — Coin3D via pivy + FreeCAD Python view API
    Backend future:  WgpuBackend  — wgpu-py transparent Qt overlay (private repo)

Quick start:

    from CorePart.render import get_backend

    r = get_backend()
    oid = r.draw_line(start, end, colour=(1, 0, 0, 1), width=2.0)
    r.set_clipping_plane(placement)
    r.clear_overlays()

See :mod:`CorePart.render.interface` for the full operation contract.
See :mod:`CorePart.render.registry` for backend swapping.
"""

from .interface import Colour, OverlayId, RenderInterface
from .registry import get_backend, set_backend

__all__ = [
    "get_backend",
    "set_backend",
    "RenderInterface",
    "Colour",
    "OverlayId",
]
