"""Git-for-CAD spine, stage 3 (crude): diff two sketch recipes by id.

Given two saved versions of a part, line their sketches up *by durable id* (never by
position) and report what changed: geometry added / removed / changed, constraints
added / removed. This is the deliberately-dumb structural diff -- no face matching, no
cleverness. It is the first place the spine visibly behaves like git.

- Geometry is keyed by its durable tag (stage 1), so add/remove/change is an exact,
  order-independent set comparison. A moved-but-unedited line is *unchanged* here even
  if it shifted position in the list -- which is the whole point of keying by id.
- Constraints carry no Python-exposed durable id yet (see recipe.py), so they are
  content-identified: a genuinely edited constraint shows as one removed + one added.
  Honest for the crude pass; durable constraint identity is a later refinement.

Usage:
    FreeCADCmd diff.py <old.FCStd> <new.FCStd> [SketchName]
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import sketch_recipe  # noqa: E402  (path set above)


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


def _first_sketch(doc, want):
    sketches = [o for o in doc.Objects if o.TypeId == "Sketcher::SketchObject"]
    if want:
        sketches = [o for o in sketches if o.Name == want or o.Label == want]
    return sketches[0] if sketches else None


def _main():
    import FreeCAD as App

    files = [a for a in sys.argv if a.endswith(".FCStd")]
    if len(files) < 2:
        print("usage: FreeCADCmd diff.py <old.FCStd> <new.FCStd> [SketchName]")
        return
    want = next(
        (a for a in sys.argv[1:] if not a.endswith(".FCStd") and not a.endswith(".py")),
        None,
    )

    doc_a = App.openDocument(files[0])
    sk_a = _first_sketch(doc_a, want)
    doc_b = App.openDocument(files[1])
    sk_b = _first_sketch(doc_b, want)
    if not sk_a or not sk_b:
        print("no sketch found in one of the files")
        return

    diff = diff_recipes(sketch_recipe(sk_a), sketch_recipe(sk_b))
    print(format_diff(diff, os.path.basename(files[0]), os.path.basename(files[1])))


if len(sys.argv) > 1 and os.path.basename(sys.argv[1]) == "diff.py":
    _main()
