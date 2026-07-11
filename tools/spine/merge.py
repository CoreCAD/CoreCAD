"""Git-for-CAD spine, stage 4 (crude): three-way merge of sketch recipes by id.

Given a common ancestor (base) and two branches that edited it separately (ours,
theirs), produce a merged recipe. Where the branches touched *different* entities (by
durable id) the edits combine automatically; where they touched the *same* entity in
incompatible ways, we do NOT guess -- we record a conflict. Refusing to silently
produce a wrong merge is the honesty the whole version-control story rests on.

Grain of the merge:
- Geometry carries a durable tag (stages 1-2), so it gets a real three-way merge with
  proper conflict detection: modify/modify, modify/delete, delete/modify.
- Constraints carry no durable id yet (see recipe.py), so they are merged by content
  set-union/difference and conflicts among them are NOT detected. This is the honest
  limit of the crude pass; durable constraint identity is the refinement that lifts it.

On conflict the merged output keeps "ours" as a provisional value and the conflict is
reported alongside -- the caller decides, exactly as a human resolves a git conflict.

Usage:
    FreeCADCmd merge.py <base.FCStd> <ours.FCStd> <theirs.FCStd> [SketchName]
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import sketch_recipe  # noqa: E402  (path set above)


def _con_sig(con):
    return json.dumps(con, sort_keys=True)


def _merge_geometry(base, ours, theirs):
    """Three-way merge of the id-keyed geometry maps. Returns (merged, conflicts)."""
    merged = {}
    conflicts = []
    all_tags = set(base) | set(ours) | set(theirs)

    for t in sorted(all_tags):
        b = base.get(t)
        o = ours.get(t)
        th = theirs.get(t)

        if b is not None:
            # Entity existed in the ancestor: classify each side as unchanged/edited/deleted.
            ours_edited = o is not None and o != b
            theirs_edited = th is not None and th != b
            ours_deleted = o is None
            theirs_deleted = th is None

            if ours_deleted and theirs_deleted:
                continue  # both deleted -> gone, agreement
            if ours_deleted and not theirs_edited:
                continue  # ours deleted, theirs left it alone -> delete
            if theirs_deleted and not ours_edited:
                continue  # theirs deleted, ours left it alone -> delete
            if ours_deleted and theirs_edited:
                conflicts.append({"kind": "delete/modify", "id": t,
                                  "detail": "ours deleted, theirs modified"})
                merged[t] = th  # provisional: keep the surviving edit
                continue
            if theirs_deleted and ours_edited:
                conflicts.append({"kind": "modify/delete", "id": t,
                                  "detail": "ours modified, theirs deleted"})
                merged[t] = o
                continue
            # Neither deleted from here on.
            if ours_edited and theirs_edited and o != th:
                conflicts.append({"kind": "modify/modify", "id": t,
                                  "detail": {"ours": o, "theirs": th}})
                merged[t] = o  # provisional: ours wins
            elif ours_edited:
                merged[t] = o
            else:
                merged[t] = th  # theirs edited, or both unchanged (th == b == o)
        else:
            # Added in a branch (not in ancestor). Distinct creations get distinct tags,
            # so a same-tag add on both sides is degenerate but handled for safety.
            if o is not None and th is not None:
                if o == th:
                    merged[t] = o
                else:
                    conflicts.append({"kind": "add/add", "id": t,
                                      "detail": {"ours": o, "theirs": th}})
                    merged[t] = o
            else:
                merged[t] = o if o is not None else th

    return merged, conflicts


def _merge_constraints(base, ours, theirs):
    """Set-union/difference merge of content-identified constraints (no conflict check)."""
    by_sig = {}
    for con in base + ours + theirs:
        by_sig[_con_sig(con)] = con
    base_s = {_con_sig(c) for c in base}
    ours_s = {_con_sig(c) for c in ours}
    theirs_s = {_con_sig(c) for c in theirs}

    added = (ours_s - base_s) | (theirs_s - base_s)
    removed = (base_s - ours_s) | (base_s - theirs_s)
    final = (base_s | added) - removed
    return [by_sig[s] for s in sorted(final)]


def merge_recipes(base, ours, theirs):
    """Three-way merge of two sketch recipes over a common ancestor. Pure."""
    geo, conflicts = _merge_geometry(
        base["geometry"], ours["geometry"], theirs["geometry"]
    )
    cons = _merge_constraints(
        base["constraints"], ours["constraints"], theirs["constraints"]
    )
    merged = {"kind": "sketch", "geometry": geo, "constraints": cons}
    return merged, conflicts


def format_merge(merged, conflicts):
    lines = []
    if conflicts:
        lines.append(f"CONFLICTS ({len(conflicts)}):")
        for c in conflicts:
            lines.append(f"  ! {c['kind']}  {c['id'][:8]}  ({c['detail'] if isinstance(c['detail'], str) else 'both sides changed it'})")
    else:
        lines.append("clean merge (no conflicts)")
    g = merged["geometry"]
    lines.append(f"merged: {len(g)} geometry, {len(merged['constraints'])} constraints")
    for t, geo in sorted(g.items()):
        lines.append(f"  {t[:8]} {geo['type'].split('::')[-1]}")
    return "\n".join(lines)


def _first_sketch(doc, want):
    sketches = [o for o in doc.Objects if o.TypeId == "Sketcher::SketchObject"]
    if want:
        sketches = [o for o in sketches if o.Name == want or o.Label == want]
    return sketches[0] if sketches else None


def _main():
    import FreeCAD as App

    files = [a for a in sys.argv if a.endswith(".FCStd")]
    if len(files) < 3:
        print("usage: FreeCADCmd merge.py <base.FCStd> <ours.FCStd> <theirs.FCStd> [SketchName]")
        return
    want = next(
        (a for a in sys.argv[1:] if not a.endswith(".FCStd") and not a.endswith(".py")),
        None,
    )

    recs = []
    for f in files[:3]:
        doc = App.openDocument(f)
        sk = _first_sketch(doc, want)
        if not sk:
            print(f"no sketch found in {os.path.basename(f)}")
            return
        recs.append(sketch_recipe(sk))

    merged, conflicts = merge_recipes(recs[0], recs[1], recs[2])
    print(format_merge(merged, conflicts))


if len(sys.argv) > 1 and os.path.basename(sys.argv[1]) == "merge.py":
    _main()
