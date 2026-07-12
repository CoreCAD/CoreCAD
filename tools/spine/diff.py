"""Git-for-CAD spine, stage 5 (crude): diff two whole-part recipes by id.

Given two saved versions of a part, line their objects up *by durable UUID* (never by
position) and report what changed: features added / removed / changed. A changed feature
is diffed field-by-field (params, refs, expressions); a changed sketch additionally
recurses into the stage-3 sketch diff for its interior geometry and constraints. This is
the deliberately-dumb structural diff -- no face matching, no cleverness. It is the first
place the spine visibly behaves like git.

- Objects are keyed by durable UUID (Amendment 3, Clause 3.1), so add/remove/change is an
  exact, order-independent set comparison. A reordered-but-unedited feature is *unchanged*
  here -- which is the whole point of keying by id, not position.
- References resolve to target UUIDs, so a downstream feature whose upstream was merely
  renamed shows no change -- name is a display handle, not identity.
- Sub-shape link sub-names ("Face6") are compared verbatim; they are the emergent #10
  layer and not yet durable, so a regen that reshuffles them can show false ref changes.
- Sketch interiors keep the stage-3 limits (constraints content-identified, etc.).

Usage:
    FreeCADCmd diff.py <old.FCStd> <new.FCStd>              # whole-part diff
    FreeCADCmd diff.py <old.FCStd> <new.FCStd> sketch [Name]     # one sketch (stage-3)
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import sketch_recipe, part_recipe  # noqa: E402  (path set above)


def _con_sig(con):
    """Canonical, comparable signature for a content-identified constraint."""
    return json.dumps(con, sort_keys=True)


def _multiset_diff(list_a, list_b, sig):
    """Return (only_in_a, only_in_b) treating the lists as multisets keyed by sig."""
    from collections import Counter

    count_a = Counter(sig(x) for x in list_a)
    count_b = Counter(sig(x) for x in list_b)
    by_sig_a = {sig(x): x for x in list_a}
    by_sig_b = {sig(x): x for x in list_b}

    only_a, only_b = [], []
    for s in count_a - count_b:
        only_a.extend([by_sig_a[s]] * (count_a[s] - count_b.get(s, 0)))
    for s in count_b - count_a:
        only_b.extend([by_sig_b[s]] * (count_b[s] - count_a.get(s, 0)))
    return only_a, only_b


def _geometry_field_changes(old, new):
    """Per-field differences between two geometry entries of the same tag."""
    changes = {}
    if old["type"] != new["type"]:
        changes["type"] = [old["type"], new["type"]]
    if old["construction"] != new["construction"]:
        changes["construction"] = [old["construction"], new["construction"]]
    old_def, new_def = old.get("def", {}), new.get("def", {})
    for key in sorted(set(old_def) | set(new_def)):
        if old_def.get(key) != new_def.get(key):
            changes[f"def.{key}"] = [old_def.get(key), new_def.get(key)]
    return changes


def diff_recipes(rec_a, rec_b):
    """Structural, id-keyed diff of two sketch recipes. Pure -- no FreeCAD needed."""
    geo_a, geo_b = rec_a["geometry"], rec_b["geometry"]
    tags_a, tags_b = set(geo_a), set(geo_b)

    added = {t: geo_b[t] for t in sorted(tags_b - tags_a)}
    removed = {t: geo_a[t] for t in sorted(tags_a - tags_b)}
    changed = {}
    for t in sorted(tags_a & tags_b):
        field_changes = _geometry_field_changes(geo_a[t], geo_b[t])
        if field_changes:
            changed[t] = field_changes

    con_removed, con_added = _multiset_diff(
        rec_a["constraints"], rec_b["constraints"], _con_sig
    )

    return {
        "geometry": {"added": added, "removed": removed, "changed": changed},
        "constraints": {"added": con_added, "removed": con_removed},
    }


def _dict_field_changes(old, new):
    """Per-key [old, new] differences between two flat dicts (params or refs)."""
    changes = {}
    for key in sorted(set(old) | set(new)):
        if old.get(key) != new.get(key):
            changes[key] = [old.get(key), new.get(key)]
    return changes


def _node_changes(old, new):
    """Field-by-field diff of one feature node. {} if identical.

    For a sketch, the interior (geometry/constraints) is diffed with the stage-3 sketch
    diff and attached under "sketch"; a change there counts as a change to the node.
    """
    changes = {}
    if old["type"] != new["type"]:
        changes["type"] = [old["type"], new["type"]]
    param_changes = _dict_field_changes(old.get("params", {}), new.get("params", {}))
    if param_changes:
        changes["params"] = param_changes
    ref_changes = _dict_field_changes(old.get("refs", {}), new.get("refs", {}))
    if ref_changes:
        changes["refs"] = ref_changes
    if old.get("expressions", []) != new.get("expressions", []):
        changes["expressions"] = [old.get("expressions", []), new.get("expressions", [])]
    if "sketch" in old and "sketch" in new:
        sk = diff_recipes(old["sketch"], new["sketch"])
        g, c = sk["geometry"], sk["constraints"]
        if g["added"] or g["removed"] or g["changed"] or c["added"] or c["removed"]:
            changes["sketch"] = sk
    return changes


def diff_parts(rec_a, rec_b):
    """Structural, id-keyed diff of two whole-part recipes. Pure -- no FreeCAD needed."""
    feat_a, feat_b = rec_a["features"], rec_b["features"]
    ids_a, ids_b = set(feat_a), set(feat_b)

    added = {i: feat_b[i] for i in sorted(ids_b - ids_a)}
    removed = {i: feat_a[i] for i in sorted(ids_a - ids_b)}
    changed = {}
    for i in sorted(ids_a & ids_b):
        node_changes = _node_changes(feat_a[i], feat_b[i])
        if node_changes:
            changed[i] = node_changes

    reordered = rec_a.get("order", []) != rec_b.get("order", [])
    return {"added": added, "removed": removed, "changed": changed,
            "reordered": reordered}


def _short(tag):
    return tag[:8]


def _con_str(con):
    refs = ", ".join(
        f"{r['geo'][:8]}:{r['pos']}" if "geo" in r else f"axis{r['special']}:{r['pos']}"
        for r in con["refs"]
    )
    val = f" = {con['value']}" if "value" in con else ""
    return f"{con['type']}({refs}){val}"


def format_diff(diff, name_a, name_b):
    """Human-readable rendering of a structural diff."""
    lines = [f"sketch diff: {name_a} -> {name_b}", "geometry:"]
    g = diff["geometry"]
    if not (g["added"] or g["removed"] or g["changed"]):
        lines.append("  (no change)")
    for t, geo in g["added"].items():
        lines.append(f"  + {_short(t)} {geo['type'].split('::')[-1]}")
    for t, geo in g["removed"].items():
        lines.append(f"  - {_short(t)} {geo['type'].split('::')[-1]}")
    for t, changes in g["changed"].items():
        lines.append(f"  ~ {_short(t)}")
        for field, (old, new) in changes.items():
            lines.append(f"      {field}: {old} -> {new}")

    lines.append("constraints:")
    c = diff["constraints"]
    if not (c["added"] or c["removed"]):
        lines.append("  (no change)")
    for con in c["removed"]:
        lines.append(f"  - {_con_str(con)}")
    for con in c["added"]:
        lines.append(f"  + {_con_str(con)}")
    return "\n".join(lines)


def format_part_diff(diff, name_a, name_b):
    """Human-readable rendering of a whole-part structural diff."""
    lines = [f"part diff: {name_a} -> {name_b}"]
    if not (diff["added"] or diff["removed"] or diff["changed"] or diff["reordered"]):
        lines.append("  (no change)")
        return "\n".join(lines)

    for i, node in diff["added"].items():
        lines.append(f"  + {_short(i)} {node['name']} [{node['type'].split('::')[-1]}]")
    for i, node in diff["removed"].items():
        lines.append(f"  - {_short(i)} {node['name']} [{node['type'].split('::')[-1]}]")
    for i, changes in diff["changed"].items():
        lines.append(f"  ~ {_short(i)}")
        for field in ("type", "params", "refs", "expressions"):
            if field not in changes:
                continue
            if field in ("params", "refs"):
                for key, (old, new) in changes[field].items():
                    lines.append(f"      {field}.{key}: {old} -> {new}")
            else:
                lines.append(f"      {field}: {changes[field][0]} -> {changes[field][1]}")
        if "sketch" in changes:
            for sub in format_diff(changes["sketch"], "", "").splitlines()[1:]:
                lines.append(f"    {sub}")
    if diff["reordered"]:
        lines.append("  (feature order changed)")
    return "\n".join(lines)


def _main():
    import FreeCAD as App

    files = [a for a in sys.argv if a.endswith(".FCStd")]
    if len(files) < 2:
        print("usage: FreeCADCmd diff.py <old.FCStd> <new.FCStd> [sketch [Name]]")
        return

    doc_a = App.openDocument(files[0])
    doc_b = App.openDocument(files[1])

    if "sketch" not in sys.argv:
        diff = diff_parts(part_recipe(doc_a), part_recipe(doc_b))
        print(format_part_diff(diff, os.path.basename(files[0]), os.path.basename(files[1])))
        return

    want = next((a for a in sys.argv[1:] if not a.endswith(".FCStd")
                 and not a.endswith(".py") and a != "sketch"), None)
    sk_a = _first_sketch(doc_a, want)
    sk_b = _first_sketch(doc_b, want)
    if not sk_a or not sk_b:
        print("no sketch found in one of the files")
        return
    diff = diff_recipes(sketch_recipe(sk_a), sketch_recipe(sk_b))
    print(format_diff(diff, os.path.basename(files[0]), os.path.basename(files[1])))


def _first_sketch(doc, want):
    sketches = [o for o in doc.Objects if o.TypeId == "Sketcher::SketchObject"]
    if want:
        sketches = [o for o in sketches if o.Name == want or o.Label == want]
    return sketches[0] if sketches else None


if len(sys.argv) > 1 and os.path.basename(sys.argv[1]) == "diff.py":
    _main()
