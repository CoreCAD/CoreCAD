# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

"""
Backend registry for the CorePart render layer.

Usage — consuming code (always renderer-agnostic):

    from CorePart.render import get_backend
    r = get_backend()
    r.highlight_faces(obj, [0, 2], (1.0, 0.2, 0.0, 0.8))

Usage — swapping to an alternative backend (e.g. from the proprietary private repo):

    from CorePart.render import set_backend
    from corecad_part.render.wgpu_backend import WgpuBackend
    set_backend(WgpuBackend())

The default backend is :class:`~CorePart.render.coin_backend.CoinBackend`, which routes
all rendering through FreeCAD's public Python view API and pivy.  It is instantiated
lazily on first call to :func:`get_backend`.
"""

from __future__ import annotations

from typing import Optional

_backend: Optional[object] = None


def get_backend() -> object:
    """Return the active renderer backend, creating the default (Coin3D) if needed."""
    global _backend
    if _backend is None:
        from .coin_backend import CoinBackend

        _backend = CoinBackend()
    return _backend


def set_backend(backend: object) -> None:
    """Replace the active renderer backend.

    The previous backend is discarded without cleanup — callers should call
    ``clear_highlights()`` and ``clear_overlays()`` on the old backend before
    switching if any overlays or highlights are active.
    """
    global _backend
    _backend = backend
