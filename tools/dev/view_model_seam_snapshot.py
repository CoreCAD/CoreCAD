# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Cruth contributors
"""Characterization net for the view/model seam (DEOWNERSHIP_AUDIT Category 6).

`ViewProvider::claimChildren` is consumed to answer several different questions at
once. Three of them are model questions wearing display clothing:

  * is this a root item?          -> #68 (world frame under every body)
  * whose coordinate frame does   -> `handleChildren3D` re-parents the scene node
    this scene node inherit?         under the claimer's transform
  * what is this object's         -> `getElementPicked` derives the picked path
    selection path?                  from the child-root node -- this was all of #66

The last one is the dangerous one, because nothing in the test suite looks at it.
#38 shipped a base-class flip that silently changed what a click reports, and #66
then cost several sessions rediscovering it one GUI round-trip at a time.

This module captures those answers as a snapshot that can be diffed across a
change. It observes only -- it never edits the model.

Run it inside the GUI (it needs a 3D view):

    import view_model_seam_snapshot as vms
    vms.write_baseline("/path/to/baseline.json")   # before the change
    print(vms.compare("/path/to/baseline.json"))   # after the change
"""

import json
import os

import FreeCAD as App
import FreeCADGui as Gui
import Part
import PartDesign

SCENE = "seam_snapshot"

# Fractions of the viewport to fire picks at.
PICK_GRID = [(x / 8.0, y / 8.0) for x in range(2, 7) for y in range(2, 7)]

# An explicit camera, so the picks do not depend on whatever the view was doing
# beforehand. `viewAxonometric()` + `fitAll()` alone proved NOT reproducible --
# two consecutive runs disagreed about which faces were hit.
CAMERA = (
    "#Inventor V2.1 ascii\n"
    "OrthographicCamera {\n"
    "  viewportMapping ADJUST_CAMERA\n"
    "  position 60 -60 60\n"
    "  orientation 0.74 0.31 0.60  1.30\n"
    "  nearDistance 10\n"
    "  farDistance 250\n"
    "  aspectRatio 1\n"
    "  focalDistance 100\n"
    "  height 45\n"
    "}\n"
)


def _element_kind(subname):
    """`Pad.Face6` -> `Pad.Face`; `Face6` -> `Face`.

    Which *particular* face a grid pick lands on shifts with the camera and the
    viewport, so recording it makes the net flaky. The thing this net exists to
    protect is the SHAPE of the path -- how many components it has and what
    stands at the front -- because that is what a container flip changes
    (`Body` + `Pad.Face6` becoming `Pad` + `Face6`). Strip the index; keep the
    structure.
    """
    if not subname:
        return subname
    head, _, tail = subname.rpartition(".")
    kind = tail.rstrip("0123456789") or tail
    return "%s.%s" % (head, kind) if head else kind


def build_scene(name=SCENE):
    """A deterministic document exercising the cases the seam gets wrong.

    Two bodies so a shared/document-owned object can be seen claimed twice
    (the #68 shape), and one sketch consumed by both pads so the multi-consumer
    case (audit failure mode 2, confirmed live) is present rather than assumed.
    """
    if name in App.listDocuments():
        App.closeDocument(name)
    doc = App.newDocument(name, type=App.DocTypePart)

    sketch = doc.addObject("Sketcher::SketchObject", "SharedProfile")
    corners = [(0, 0, 0), (10, 0, 0), (10, 10, 0), (0, 10, 0)]
    for i in range(4):
        a = App.Vector(*corners[i])
        b = App.Vector(*corners[(i + 1) % 4])
        sketch.addGeometry(Part.LineSegment(a, b), False)
    doc.recompute()

    first = PartDesign.makeFeature(sketch, "Pad")
    first.Length = 5
    doc.recompute()

    # A second pad off the SAME sketch -> the sketch now has two consumers.
    second = PartDesign.makeFeature(sketch, "Pad")
    second.Length = 12
    doc.recompute()

    Gui.updateGui()
    return doc


def _claim_map(doc):
    """child name -> sorted list of the objects claiming it."""
    claimed_by = {}
    for obj in doc.Objects:
        vp = getattr(obj, "ViewObject", None)
        if vp is None:
            continue
        try:
            children = vp.claimChildren() or []
        except Exception:
            children = []
        for child in children:
            claimed_by.setdefault(child.Name, []).append(obj.Name)
    return {k: sorted(v) for k, v in sorted(claimed_by.items())}


def _scene_path_counts(doc):
    """How many paths reach each view provider's root node.

    `claimChildren3D` is not exposed to Python and screenshots have come back
    empty, so this is the reliable way to observe 3D parenting. One path means
    present and unduplicated; more than one means several parents for one node.
    """
    from pivy import coin

    graph = Gui.activeDocument().activeView().getSceneGraph()
    counts = {}
    for obj in doc.Objects:
        vp = getattr(obj, "ViewObject", None)
        if vp is None:
            continue
        try:
            node = vp.RootNode
        except Exception:
            continue
        action = coin.SoSearchAction()
        action.setNode(node)
        action.setInterest(coin.SoSearchAction.ALL)
        action.setSearchingAll(True)
        action.apply(graph)
        counts[obj.Name] = action.getPaths().getLength()
    return dict(sorted(counts.items()))


def _selection_paths(doc):
    """Distinct selection paths a click actually returns.

    This is the check #38 and #66 never had. It goes through the real pick
    machinery (`getElementPicked`), which derives the path from the child-root
    node -- so it sees the frame/selection coupling that unit tests miss.
    """
    view = Gui.activeDocument().activeView()
    view.setCamera(CAMERA)
    Gui.updateGui()

    width, height = view.getSize()
    seen = set()
    for fx, fy in PICK_GRID:
        pixel = (int(width * fx), int(height * fy))
        try:
            hits = view.getObjectsInfo(pixel) or []
        except Exception:
            continue
        for hit in hits:
            parent = hit.get("ParentObject")
            parent_name = parent.Name if parent is not None else None
            seen.add(
                (
                    hit.get("Document"),
                    parent_name,
                    hit.get("Object"),
                    _element_kind(hit.get("SubName")),
                )
            )
    return [
        {"document": d, "parent": p, "object": o, "subname": s}
        for d, p, o, s in sorted(seen, key=lambda t: tuple(str(x) for x in t))
    ]


def capture(doc=None):
    """Snapshot every answer the overloaded call is currently giving."""
    doc = doc or build_scene()
    claimed_by = _claim_map(doc)
    return {
        "objects": [{"name": o.Name, "type": o.TypeId} for o in doc.Objects],
        "root_items": [o.Name for o in doc.Objects if o.Name not in claimed_by],
        "claimed_by": claimed_by,
        "multi_claimed": {k: v for k, v in claimed_by.items() if len(v) > 1},
        "scene_path_counts": _scene_path_counts(doc),
        "selection_paths": _selection_paths(doc),
        "sub_objects": {
            o.Name: list(o.getSubObjects() or ()) for o in doc.Objects
        },
    }


def write_baseline(path, doc=None):
    snapshot = capture(doc)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(snapshot, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return snapshot


def compare(path, doc=None):
    """Diff the live application against a stored baseline.

    Returns a list of human-readable differences; empty means unchanged.
    """
    with open(path, encoding="utf-8") as handle:
        baseline = json.load(handle)
    current = capture(doc)

    differences = []
    for key in sorted(set(baseline) | set(current)):
        was, now = baseline.get(key), current.get(key)
        if was == now:
            continue
        differences.append("%s:" % key)
        if isinstance(was, dict) and isinstance(now, dict):
            for name in sorted(set(was) | set(now)):
                if was.get(name) != now.get(name):
                    differences.append(
                        "    %s: %r -> %r" % (name, was.get(name), now.get(name))
                    )
        else:
            differences.append("    was: %r" % (was,))
            differences.append("    now: %r" % (now,))
    return differences
