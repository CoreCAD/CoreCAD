# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

"""
Coin3D / FreeCAD implementation of :class:`RenderInterface`.

All rendering is routed through FreeCAD's public Python view API and pivy
(the Coin3D Python binding that ships with FreeCAD).  No C++ modifications
are required, and no LGPL2+ source files are touched.

When a wgpu backend is ready it can be activated via:
    from CorePart.render import registry
    from corecad_part.render.wgpu_backend import WgpuBackend
    registry.set_backend(WgpuBackend())
"""

from __future__ import annotations

import uuid
from typing import Dict, List, Optional, Tuple

import FreeCAD as App
import FreeCADGui as Gui

Colour = Tuple[float, float, float, float]
OverlayId = str


class CoinBackend:
    """RenderInterface backed by Coin3D (via pivy) and the FreeCAD Python view API."""

    def __init__(self) -> None:
        # id(obj) → (obj_ref, original_DiffuseColor)
        self._saved_colours: Dict[int, Tuple[App.DocumentObject, list]] = {}
        # overlay_id → coin.SoSeparator
        self._overlays: Dict[OverlayId, object] = {}
        # SoAnnotation injected into the active view's scene graph for overlays
        self._overlay_root = None
        # SoSeparator inserted BEFORE SoClipPlane for cap geometry
        self._cap_root = None

    # ------------------------------------------------------------------ #
    # Internal helpers                                                     #
    # ------------------------------------------------------------------ #

    def _active_view(self):
        """Return the active 3D view, or None if unavailable."""
        try:
            doc = Gui.ActiveDocument
            return doc.ActiveView if doc is not None else None
        except Exception:
            return None

    def _ensure_overlay_root(self) -> bool:
        """Lazily create and attach our overlay SoAnnotation to the scene graph.

        SoAnnotation renders on top of all scene geometry (like a HUD layer),
        so overlays are always visible regardless of depth.
        Returns True if the overlay root is ready to use.
        """
        if self._overlay_root is not None:
            return True
        view = self._active_view()
        if view is None:
            return False
        try:
            from pivy import coin

            sg = view.getSceneGraph()
            ann = coin.SoAnnotation()
            self._overlay_root = ann
            sg.addChild(ann)
            return True
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: could not attach overlay root: {}\n".format(exc)
            )
            return False

    # ------------------------------------------------------------------ #
    # Face highlighting                                                    #
    # ------------------------------------------------------------------ #

    def highlight_faces(
        self,
        obj: App.DocumentObject,
        face_indices: List[int],
        colour: Colour,
    ) -> None:
        vp = getattr(obj, "ViewObject", None)
        if vp is None:
            return
        shape = getattr(obj, "Shape", None)
        if shape is None:
            return
        n_faces = len(shape.Faces)
        if n_faces == 0:
            return

        # Save original colours the first time we touch this object
        key = id(obj)
        if key not in self._saved_colours:
            self._saved_colours[key] = (obj, list(vp.DiffuseColor))

        # Build a per-face colour list of the correct length
        current = list(vp.DiffuseColor)
        if len(current) == 1:
            current = current * n_faces
        elif len(current) != n_faces:
            base = tuple(vp.ShapeColor) + (1.0,)
            current = [base] * n_faces

        r, g, b, a = colour
        for i in face_indices:
            if 0 <= i < n_faces:
                current[i] = (r, g, b, a)

        vp.DiffuseColor = current

    def clear_highlights(self, obj: Optional[App.DocumentObject] = None) -> None:
        if obj is not None:
            entry = self._saved_colours.pop(id(obj), None)
            if entry is not None:
                _, original = entry
                vp = getattr(obj, "ViewObject", None)
                if vp is not None:
                    try:
                        vp.DiffuseColor = original
                    except Exception:
                        pass
        else:
            for _, (stored_obj, original) in list(self._saved_colours.items()):
                vp = getattr(stored_obj, "ViewObject", None)
                if vp is not None:
                    try:
                        vp.DiffuseColor = original
                    except Exception:
                        pass
            self._saved_colours.clear()

    # ------------------------------------------------------------------ #
    # Clipping plane                                                       #
    # ------------------------------------------------------------------ #

    def set_clipping_plane(
        self,
        placement: App.Placement,
        enabled: bool = True,
    ) -> None:
        view = self._active_view()
        if view is None:
            return
        try:
            if not enabled:
                view.toggleClippingPlane(0)
                return
            # toggleClippingPlane returns early without updating the plane if one
            # is already active.  Remove it first, then re-add at the new placement.
            if view.hasClippingPlane():
                view.toggleClippingPlane(0)
            view.toggleClippingPlane(1, pla=placement)
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: set_clipping_plane failed: {}\n".format(exc)
            )

    def clear_clipping_plane(self) -> None:
        view = self._active_view()
        if view is None:
            return
        try:
            view.toggleClippingPlane(0)
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: clear_clipping_plane failed: {}\n".format(exc)
            )

    # ------------------------------------------------------------------ #
    # Scene overlays                                                       #
    # ------------------------------------------------------------------ #

    def draw_line(
        self,
        start: App.Vector,
        end: App.Vector,
        colour: Colour,
        width: float = 1.0,
    ) -> OverlayId:
        if not self._ensure_overlay_root():
            return ""
        try:
            from pivy import coin

            r, g, b, a = colour
            sep = coin.SoSeparator()

            mat = coin.SoMaterial()
            mat.diffuseColor.setValue(r, g, b)
            mat.transparency.setValue(1.0 - a)
            sep.addChild(mat)

            ds = coin.SoDrawStyle()
            ds.lineWidth = width
            sep.addChild(ds)

            coords = coin.SoCoordinate3()
            coords.point.set1Value(0, start.x, start.y, start.z)
            coords.point.set1Value(1, end.x, end.y, end.z)
            sep.addChild(coords)

            ls = coin.SoLineSet()
            ls.numVertices.setValue(2)
            sep.addChild(ls)

            overlay_id = str(uuid.uuid4())
            self._overlay_root.addChild(sep)
            self._overlays[overlay_id] = sep
            return overlay_id
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: draw_line failed: {}\n".format(exc)
            )
            return ""

    def draw_annotation(
        self,
        position: App.Vector,
        text: str,
        colour: Colour,
    ) -> OverlayId:
        if not self._ensure_overlay_root():
            return ""
        try:
            from pivy import coin

            r, g, b, a = colour
            sep = coin.SoSeparator()

            mat = coin.SoMaterial()
            mat.diffuseColor.setValue(r, g, b)
            mat.transparency.setValue(1.0 - a)
            sep.addChild(mat)

            xf = coin.SoTranslation()
            xf.translation.setValue(position.x, position.y, position.z)
            sep.addChild(xf)

            # SoText2 is screen-aligned — always readable regardless of view angle
            label = coin.SoText2()
            label.string.setValue(text)
            sep.addChild(label)

            overlay_id = str(uuid.uuid4())
            self._overlay_root.addChild(sep)
            self._overlays[overlay_id] = sep
            return overlay_id
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: draw_annotation failed: {}\n".format(exc)
            )
            return ""

    def _build_mesh_sep(self, points, triangles, colour, normal=None):
        """Build a Coin3D SoSeparator containing a shaded filled triangle mesh."""
        from pivy import coin

        r, g, b, a = colour
        sep = coin.SoSeparator()

        # Two-sided lighting — cap visible regardless of view direction
        hints = coin.SoShapeHints()
        hints.shapeType = coin.SoShapeHints.UNKNOWN_SHAPE_TYPE
        hints.vertexOrdering = coin.SoShapeHints.UNKNOWN_ORDERING
        sep.addChild(hints)

        mat = coin.SoMaterial()
        mat.diffuseColor.setValue(r, g, b)
        mat.transparency.setValue(1.0 - a)
        sep.addChild(mat)

        if normal is not None:
            nb = coin.SoNormalBinding()
            nb.value = coin.SoNormalBinding.OVERALL
            sep.addChild(nb)
            nrm = coin.SoNormal()
            nrm.vector.set1Value(0, normal[0], normal[1], normal[2])
            sep.addChild(nrm)

        coords = coin.SoCoordinate3()
        for i, (x, y, z) in enumerate(points):
            coords.point.set1Value(i, x, y, z)
        sep.addChild(coords)

        ifs = coin.SoIndexedFaceSet()
        flat = []
        for tri in triangles:
            flat += [int(tri[0]), int(tri[1]), int(tri[2]), -1]
        ifs.coordIndex.setValues(0, len(flat), flat)
        sep.addChild(ifs)

        return sep

    def draw_mesh(
        self,
        points,
        triangles,
        colour,
        normal=None,
    ):
        """Draw a filled triangle mesh as a scene overlay."""
        if not self._ensure_overlay_root():
            return ""
        try:
            from pivy import coin

            sep = self._build_mesh_sep(points, triangles, colour, normal)
            overlay_id = str(uuid.uuid4())
            self._overlay_root.addChild(sep)
            self._overlays[overlay_id] = sep
            return overlay_id
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: draw_mesh failed: {}\n".format(exc)
            )
            return ""

    def update_cap_meshes(self, meshes) -> None:
        """Replace all cap geometry, inserted before the active SoClipPlane."""
        view = self._active_view()
        if view is None:
            return
        try:
            from pivy import coin

            sg = view.getSceneGraph()

            # Remove old cap root from the scene graph
            if self._cap_root is not None:
                idx = sg.findChild(self._cap_root)
                if idx >= 0:
                    sg.removeChild(self._cap_root)
                self._cap_root = None

            if not meshes:
                return

            cap_root = coin.SoSeparator()
            for (points, triangles, colour, normal) in meshes:
                cap_root.addChild(self._build_mesh_sep(points, triangles, colour, normal))

            # Insert before the SoClipPlane so the cap is not clipped
            clip_idx = -1
            for i in range(sg.getNumChildren()):
                try:
                    if isinstance(sg.getChild(i), coin.SoClipPlane):
                        clip_idx = i
                        break
                except Exception:
                    pass

            if clip_idx >= 0:
                sg.insertChild(cap_root, clip_idx)
            else:
                sg.addChild(cap_root)

            self._cap_root = cap_root
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: update_cap_meshes failed: {}\n".format(exc)
            )

    def clear_cap_meshes(self) -> None:
        """Remove all cap geometry from the scene graph."""
        if self._cap_root is None:
            return
        view = self._active_view()
        if view is None:
            return
        try:
            sg = view.getSceneGraph()
            idx = sg.findChild(self._cap_root)
            if idx >= 0:
                sg.removeChild(self._cap_root)
            self._cap_root = None
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: clear_cap_meshes failed: {}\n".format(exc)
            )

    def draw_bounding_box(
        self,
        bbox: App.BoundBox,
        colour: Colour,
        width: float = 1.0,
    ) -> OverlayId:
        """Draw the 12 edges of an axis-aligned bounding box."""
        if not self._ensure_overlay_root():
            return ""
        try:
            from pivy import coin

            r, g, b, a = colour

            # 8 corners in a consistent winding order
            xn, xx = bbox.XMin, bbox.XMax
            yn, yx = bbox.YMin, bbox.YMax
            zn, zx = bbox.ZMin, bbox.ZMax
            corners = [
                (xn, yn, zn), (xx, yn, zn), (xx, yx, zn), (xn, yx, zn),  # bottom
                (xn, yn, zx), (xx, yn, zx), (xx, yx, zx), (xn, yx, zx),  # top
            ]
            # 12 edges as (start_idx, end_idx) pairs
            edges = [
                (0, 1), (1, 2), (2, 3), (3, 0),  # bottom face
                (4, 5), (5, 6), (6, 7), (7, 4),  # top face
                (0, 4), (1, 5), (2, 6), (3, 7),  # verticals
            ]

            sep = coin.SoSeparator()

            mat = coin.SoMaterial()
            mat.diffuseColor.setValue(r, g, b)
            mat.transparency.setValue(1.0 - a)
            sep.addChild(mat)

            ds = coin.SoDrawStyle()
            ds.lineWidth = width
            sep.addChild(ds)

            coords = coin.SoCoordinate3()
            for i, (cx, cy, cz) in enumerate(corners):
                coords.point.set1Value(i, cx, cy, cz)
            sep.addChild(coords)

            ils = coin.SoIndexedLineSet()
            flat: list = []
            for a_idx, b_idx in edges:
                flat += [a_idx, b_idx, -1]
            ils.coordIndex.setValues(0, len(flat), flat)
            sep.addChild(ils)

            overlay_id = str(uuid.uuid4())
            self._overlay_root.addChild(sep)
            self._overlays[overlay_id] = sep
            return overlay_id
        except Exception as exc:
            App.Console.PrintWarning(
                "CorePart.render: draw_bounding_box failed: {}\n".format(exc)
            )
            return ""

    def remove_overlay(self, overlay_id: OverlayId) -> None:
        node = self._overlays.pop(overlay_id, None)
        if node is not None and self._overlay_root is not None:
            try:
                self._overlay_root.removeChild(node)
            except Exception:
                pass

    def clear_overlays(self) -> None:
        if self._overlay_root is not None:
            try:
                self._overlay_root.removeAllChildren()
            except Exception:
                pass
        self._overlays.clear()
