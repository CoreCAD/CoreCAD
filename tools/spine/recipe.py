"""Git-for-CAD spine, stage 2 (crude): extract a sketch to an id-keyed recipe.

The point of the spine is to version, diff and merge the *recipe* (the authored
"source code") rather than the geometry (the regenerable "compiled binary"). For a
diff/merge to line two saved versions up, the recipe must key entities by a durable
identity, never by list position. Stage 1 made the per-geometry tag survive to disk;
this stage turns a sketch into a representation keyed by that tag.

Scope (deliberately narrow, per the rough-whole-first plan):
- One sketch, not the whole feature tree. We widen up into features later.
- Geometry is keyed by its durable tag (stable across save/load and reorder).
- Constraints reference geometry by *index* in the running model and carry no
  Python-exposed durable id of their own, so we translate their index-refs to
  geometry tags and identify each constraint by its content. Durable constraint
  identity is a later refinement (same bucket as clean id-inheritance on a split line).

Emergent geometry (faces/edges) is intentionally absent: its identity is derived every
regen, not stored, and belongs to a later, harder stage.

Usage:
    FreeCADCmd recipe.py <file.FCStd> [SketchName]   # prints the recipe as JSON
    # or import sketch_recipe(sk) / dump(recipe) from another spine tool.
"""

import json


# Round coordinates so a save/load round-trip or a harmless re-solve does not
# produce spurious diffs from floating-point noise.
_NDIGITS = 9


def _vec(v):
    return [round(v[0], _NDIGITS), round(v[1], _NDIGITS), round(v[2], _NDIGITS)]


def _geometry_def(geo):
    """Type-specific defining parameters for a piece of sketch geometry.

    Handles the common sketch primitives explicitly; falls back to the object's repr
    for anything exotic so the recipe is always total, never crashes on a new type.
    """
    t = geo.TypeId
    if t == "Part::GeomLineSegment":
        return {"StartPoint": _vec(geo.StartPoint), "EndPoint": _vec(geo.EndPoint)}
    if t == "Part::GeomPoint":
        return {"Point": [round(geo.X, _NDIGITS), round(geo.Y, _NDIGITS), round(geo.Z, _NDIGITS)]}
    if t == "Part::GeomCircle":
        return {"Center": _vec(geo.Center), "Radius": round(geo.Radius, _NDIGITS)}
    if t == "Part::GeomArcOfCircle":
        first, last = geo.FirstParameter, geo.LastParameter
        return {
            "Center": _vec(geo.Center),
            "Radius": round(geo.Radius, _NDIGITS),
            "Range": [round(first, _NDIGITS), round(last, _NDIGITS)],
        }
    if t == "Part::GeomEllipse":
        return {
            "Center": _vec(geo.Center),
            "MajorRadius": round(geo.MajorRadius, _NDIGITS),
            "MinorRadius": round(geo.MinorRadius, _NDIGITS),
        }
    # Fallback: exotic/unsupported type. Keep the recipe total.
    return {"repr": repr(geo)}


def _constraint_refs(con, tag_of):
    """Translate a constraint's index-based geometry refs into durable tag refs.

    Negative GeoIds are Sketcher pseudo-geometry (axes, origin) and external geometry;
    they are stable symbolic references, so we keep them as-is rather than tag them.
    """
    refs = []
    for geoid, pos in (
        (con.First, con.FirstPos),
        (con.Second, con.SecondPos),
        (con.Third, con.ThirdPos),
    ):
        if geoid is None or geoid == -2000:  # -2000 == Sketcher GeoUndef (unused slot)
            continue
        if geoid < 0:
            refs.append({"special": geoid, "pos": pos})
        else:
            refs.append({"geo": tag_of(geoid), "pos": pos})
    return refs


def sketch_recipe(sk):
    """Canonical, position-independent representation of one sketch.

    geometry: {tag -> {type, construction, def}}   (keyed by durable id)
    constraints: [ {type, refs (by tag), value} ]  (content-identified for now)
    """
    geo_list = sk.Geometry
    tag_of = lambda gid: geo_list[gid].Tag

    geometry = {}
    for gid, geo in enumerate(geo_list):
        geometry[geo.Tag] = {
            "type": geo.TypeId,
            "construction": bool(sk.getConstruction(gid)),
            "def": _geometry_def(geo),
        }

    constraints = []
    for con in sk.Constraints:
        entry = {"type": con.Type, "refs": _constraint_refs(con, tag_of)}
        # Value is only meaningful for dimensional constraints; include when nonzero-ish.
        if con.Type in ("Distance", "DistanceX", "DistanceY", "Radius", "Diameter",
                        "Angle", "SnellsLaw", "Weight"):
            entry["value"] = round(con.Value, _NDIGITS)
        constraints.append(entry)

    return {"kind": "sketch", "geometry": geometry, "constraints": constraints}


def dump(recipe):
    """Deterministic JSON: sorted keys so two versions diff cleanly."""
    return json.dumps(recipe, sort_keys=True, indent=2)


def _main():
    import sys
    import FreeCAD as App

    fcstd = next((a for a in sys.argv if a.endswith(".FCStd")), None)
    if not fcstd:
        print("usage: FreeCADCmd recipe.py <file.FCStd> [SketchName]")
        return
    want = next((a for a in sys.argv[1:]
                 if not a.endswith(".FCStd") and not a.endswith(".py")), None)

    doc = App.openDocument(fcstd)
    sketches = [o for o in doc.Objects if o.TypeId == "Sketcher::SketchObject"]
    if want:
        sketches = [o for o in sketches if o.Name == want or o.Label == want]
    if not sketches:
        print("no sketch found")
        return
    print(dump(sketch_recipe(sketches[0])))


# FreeCADCmd runs a script with __name__ set to the module basename, not "__main__",
# so guard on this file being the invoked script (argv[1]) instead. Importing this
# module from another spine tool leaves argv[1] as that tool, so _main won't fire.
import os as _os
import sys as _sys

if len(_sys.argv) > 1 and _os.path.basename(_sys.argv[1]) == "recipe.py":
    _main()
