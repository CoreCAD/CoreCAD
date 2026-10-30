"""Git-for-CAD spine, stage 5 (crude): extract a whole part to an id-keyed recipe.

The point of the spine is to version, diff and merge the *recipe* (the authored
"source code") rather than the geometry (the regenerable "compiled binary"). For a
diff/merge to line two saved versions up, the recipe must key entities by a durable
identity, never by list position. Stage 1 made the per-geometry tag survive to disk;
stages 2-4 turned a single sketch into an id-keyed recipe/diff/merge. This stage widens
that up one level: the whole feature tree, keyed by each object's durable UUID (`.Uid`,
Amendment 3 Clause 3.1), with sketches nested as sub-recipes.

Structure (mirrors DESIGN_GIT_FOR_CAD.md sec.9 -- a part is a typed dependency graph):
- A part recipe is a flat map keyed by durable object UUID, plus an authored-order list.
- Each node carries: type (TypeId), name (a display handle only, never identity),
  params (authored scalar/value properties), refs (link properties resolved to the
  *target's UUID*, never its name -- sec.5 "match the authored grain by id"), and
  expressions (the formula layer, from ExpressionEngine).
- A sketch node additionally embeds the stage-2 sketch recipe for its interior grain,
  so the sketch-entity keystone plugs in unchanged.

What is deliberately *not* captured (honest crude-pass limits -- audited in the module
docstring at bottom against sec.9/sec.9.1):
- Geometry payloads (Shape, sketch Geometry, ConstraintList) -- that is the compiled
  binary; we version the recipe. Sketch interior comes back via the nested recipe.
- Emergent sub-shape identity: link sub-names ("Face6") are kernel-generated and
  unstable across regen (#10). We store them verbatim but they are NOT durable ids yet.
- Imported/baked geometry as stored content (sec.9.1); material as a reference.

Usage:
    FreeCADCmd recipe.py <file.FCStd>              # prints the whole-part recipe as JSON
    FreeCADCmd recipe.py <file.FCStd> sketch [Name]    # just one sketch (stage-2 mode)
    # or import part_recipe(doc) / sketch_recipe(sk) / dump(recipe) from a spine tool.
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


# --- Whole-part recipe (stage 5) -------------------------------------------------

# Property *types* that carry regenerable geometry or are captured by other means.
# These are the "compiled binary" (or a duplicate of something we store elsewhere) and
# must never land in the recipe -- note that a plain Shape is NOT flagged Output, so a
# status-only filter is insufficient; we blocklist by type.
_SKIP_PROP_TYPES = frozenset({
    "Part::PropertyPartShape",         # Shape, AddSubShape, InternalShape, ...
    "Part::PropertyGeometryList",      # sketch Geometry, ExternalGeo -> nested recipe
    "Part::PropertyFilletContour",
    "Sketcher::PropertyConstraintList",  # sketch Constraints -> nested recipe
    "App::PropertyExpressionEngine",   # captured via ExpressionEngine list instead
    "Materials::PropertyMaterial",     # heavy embedded material; belongs as a ref (TODO)
})

# Status strings that mark a property as computed output or non-persistent -- not
# authored input, so out of the recipe. (getPropertyStatus also returns bare integer
# bits with no name; those are ignored by simple string membership.)
_SKIP_STATUS = ("Output", "Transient")


def _is_link_type(ptype):
    """A property that wires one object to another (any Link/XLink variant)."""
    return "Link" in ptype


def _jsonable(value):
    """Total, deterministic conversion of a property value to JSON-friendly data.

    Handles the value types the recipe cares about explicitly; falls back to str() so
    the recipe never crashes on an exotic property (kept total, like _geometry_def).
    """
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        return round(value, _NDIGITS)
    # Base.Vector
    if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z") \
            and not hasattr(value, "Base"):
        return _vec(value)
    # Base.Placement (Base + Rotation)
    if hasattr(value, "Base") and hasattr(value, "Rotation"):
        rot = value.Rotation
        return {"pos": _vec(value.Base),
                "rot": [round(q, _NDIGITS) for q in rot.Q]}
    # Base.Quantity (Length, Angle, ...) exposes a numeric .Value
    if hasattr(value, "Value") and not hasattr(value, "__len__"):
        return round(float(value.Value), _NDIGITS)
    # Sequences (IntegerList, FloatList, tuples of numbers)
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    try:
        return round(float(value), _NDIGITS)
    except (TypeError, ValueError):
        return str(value)


def _link_targets(value):
    """Normalise any Link-property value to a list of {uid[, subs]} references.

    Resolves each linked object to the *target's durable UUID* (never its name). Link
    sub-names (faces/edges) are the emergent #10 layer -- stored verbatim, honestly not
    yet durable ids. Returns [] for an unset link so empty refs drop out of the recipe.
    """
    def _one(obj, subs=None):
        ref = {"uid": obj.Uid}
        subs = [s for s in (subs or []) if s]
        if subs:
            ref["subs"] = list(subs)
        return ref

    refs = []
    if value is None:
        return refs
    # PropertyLinkSub -> (obj, [subs]); PropertyLinkSubList -> [(obj, [subs]), ...]
    if isinstance(value, tuple) and len(value) == 2 and hasattr(value[0], "Uid"):
        refs.append(_one(value[0], value[1]))
    elif isinstance(value, (list, tuple)):
        for item in value:
            if isinstance(item, tuple) and len(item) == 2 and hasattr(item[0], "Uid"):
                refs.append(_one(item[0], item[1]))
            elif hasattr(item, "Uid"):
                refs.append(_one(item))
    elif hasattr(value, "Uid"):
        refs.append(_one(value))
    return refs


def _object_node(obj):
    """Recipe node for one document object: type, name, params, refs, expressions."""
    params, refs = {}, {}
    for name in obj.PropertiesList:
        if name == "Uid" or name.startswith("_"):
            continue
        status = obj.getPropertyStatus(name)
        if any(s in status for s in _SKIP_STATUS):
            continue
        ptype = obj.getTypeIdOfProperty(name)
        if ptype in _SKIP_PROP_TYPES:
            continue
        value = getattr(obj, name)
        if _is_link_type(ptype):
            targets = _link_targets(value)
            if targets:
                refs[name] = targets
        else:
            conv = _jsonable(value)
            if conv is not None and conv != "" and conv != []:
                params[name] = conv

    node = {"type": obj.TypeId, "name": obj.Name, "params": params, "refs": refs}
    exprs = list(obj.ExpressionEngine)
    if exprs:
        node["expressions"] = [[str(path), expr] for path, expr in exprs]
    if obj.TypeId == "Sketcher::SketchObject":
        node["sketch"] = sketch_recipe(obj)
    return node


def part_recipe(doc):
    """Canonical, position-independent recipe of a whole part (all its objects).

    features: {uid -> node}   keyed by durable object UUID (order-independent identity)
    order:    [uid, ...]      authored/topological order (data, not identity -- like
                              line order in a text file; reorders are a later refinement)
    """
    features = {obj.Uid: _object_node(obj) for obj in doc.Objects}
    order = [obj.Uid for obj in doc.Objects]
    return {"kind": "part", "features": features, "order": order}


def dump(recipe):
    """Deterministic JSON: sorted keys so two versions diff cleanly."""
    return json.dumps(recipe, sort_keys=True, indent=2)


def _main():
    import sys
    import FreeCAD as App

    fcstd = next((a for a in sys.argv if a.endswith(".FCStd")), None)
    if not fcstd:
        print("usage: FreeCADCmd recipe.py <file.FCStd> [sketch [Name]]")
        return
    doc = App.openDocument(fcstd)

    if "sketch" not in sys.argv:
        print(dump(part_recipe(doc)))
        return

    # Stage-2 mode: just one sketch.
    want = next((a for a in sys.argv[1:] if not a.endswith(".FCStd")
                 and not a.endswith(".py") and a != "sketch"), None)
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
