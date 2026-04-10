# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

"""Body-aware boolean commands for CorePart.

These commands operate on two selected PartDesign::Body objects and produce a
result that is immediately usable for further PartDesign modelling (sketches,
Pad, Pocket, etc.).

Implementation: uses Part::Cut/Fuse/Common (plain shape references, no
GeoFeatureGroup ownership constraints) wrapped in a new PartDesign::Body via
Body.BaseFeature. This avoids the GeoFeatureGroup exclusive-membership conflict
that prevents PartDesign::Boolean.Group from being set programmatically from
Python when the tool body is already inside an App::Part.

The result is a new Body whose shape is the boolean result. Both input bodies
are hidden. The new Body is placed in the same App::Part as the base body if
one exists.
"""

import FreeCAD as App
import FreeCADGui as Gui


class _BodyBooleanBase:
    """Shared implementation for CorePart body boolean commands."""

    # Subclasses must define:
    #   LABEL      — short display name (str)
    #   TOOLTIP    — descriptive tooltip (str)
    #   PIXMAP     — icon name (str)
    #   PART_TYPE  — FreeCAD object type string, e.g. "Part::Cut"

    def GetResources(self):
        return {
            "Pixmap": self.PIXMAP,
            "MenuText": self.LABEL,
            "ToolTip": self.TOOLTIP,
        }

    def IsActive(self):
        return self._selected_bodies() is not None

    def Activated(self):
        sel = self._selected_bodies()
        if sel is None:
            App.Console.PrintError(
                "Body Boolean: select exactly two PartDesign Bodies first.\n"
            )
            return
        base_body, tool_body = sel
        doc = base_body.Document
        doc.openTransaction(self.LABEL)
        try:
            # Part boolean — plain shape references, no GeoFeatureGroup ownership.
            bool_name = doc.getUniqueObjectName("BooleanResult")
            part_bool = doc.addObject(self.PART_TYPE, bool_name)
            self._configure(part_bool, base_body, tool_body)

            # Wrap the boolean result in a new Body so PartDesign workflow continues.
            body_name = doc.getUniqueObjectName("Body")
            new_body = doc.addObject("PartDesign::Body", body_name)
            new_body.BaseFeature = part_bool

            # Place both new objects in the same App::Part as the base body.
            container = self._find_part_container(base_body)
            if container:
                container.addObject(part_bool)
                container.addObject(new_body)

            # Hide the consumed input bodies.
            base_body.Visibility = False
            tool_body.Visibility = False

            doc.recompute()
            doc.commitTransaction()
        except Exception:
            doc.abortTransaction()
            raise

    def _configure(self, part_bool, base_body, tool_body):
        """Wire up the Part boolean feature. Override in subclasses if needed."""
        part_bool.Base = base_body
        part_bool.Tool = tool_body

    def _selected_bodies(self):
        """Return (base_body, tool_body) if exactly 2 Bodies are selected, else None."""
        bodies = [
            s.Object
            for s in Gui.Selection.getSelectionEx()
            if s.Object.isDerivedFrom("PartDesign::Body")
        ]
        if len(bodies) == 2:
            return bodies[0], bodies[1]
        return None

    def _find_part_container(self, obj):
        """Return the App::Part that directly contains obj, or None."""
        doc = obj.Document
        for candidate in doc.Objects:
            if not candidate.isDerivedFrom("App::Part"):
                continue
            try:
                if obj in candidate.Group:
                    return candidate
            except Exception:
                pass
        return None


class BodyUnion(_BodyBooleanBase):
    """Fuse two PartDesign Bodies into one."""

    LABEL = "Body Union"
    TOOLTIP = (
        "Fuse two Bodies into one.\n"
        "Both input Bodies are hidden; the result is a new Body\n"
        "that can be sketched on and modelled further."
    )
    PIXMAP = "Part_Fuse"
    PART_TYPE = "Part::Fuse"


class BodyCut(_BodyBooleanBase):
    """Subtract the second Body from the first."""

    LABEL = "Body Cut"
    TOOLTIP = (
        "Subtract the second selected Body from the first.\n"
        "Both input Bodies are hidden; the result is a new Body\n"
        "that can be sketched on and modelled further."
    )
    PIXMAP = "Part_Cut"
    PART_TYPE = "Part::Cut"


class BodyIntersect(_BodyBooleanBase):
    """Keep only the volume common to both Bodies."""

    LABEL = "Body Intersect"
    TOOLTIP = (
        "Keep only the volume common to both selected Bodies.\n"
        "Both input Bodies are hidden; the result is a new Body\n"
        "that can be sketched on and modelled further."
    )
    PIXMAP = "Part_Common"
    PART_TYPE = "Part::Common"
