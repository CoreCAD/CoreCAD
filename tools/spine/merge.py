"""Git-for-CAD spine, stage 5 (crude): three-way merge of whole-part recipes by id.

Given a common ancestor (base) and two branches that edited it separately (ours,
theirs), produce a merged recipe. Where the branches touched *different* objects, or
different *fields* of the same object (by durable UUID), the edits combine
automatically; where they touched the *same* field in incompatible ways, we do NOT
guess -- we record a conflict. Refusing to silently produce a wrong merge is the honesty
the whole version-control story rests on.

Grain of the merge:
- Objects carry a durable UUID (Amendment 3, Clause 3.1), so the feature tree gets a real
  three-way merge with add/add, modify/modify, modify/delete, delete/modify detection.
- Within one edited feature the merge is *field-level*: params, refs and expressions
  three-way independently, so branch A editing a pad's length and branch B its taper
  combine cleanly. Only a genuinely divergent same-field edit conflicts. (This is the
  sec.9.1 "primitives are fine-mergeable for free -- fields have names, not positions"
  property, applied to every feature's parameter block.)
- A sketch edited on both sides recurses into the stage-4 sketch merge (geometry gets
  real conflict detection; constraints stay content-merged, its honest crude limit).

On conflict the merged output keeps "ours" as a provisional value and the conflict is
reported alongside -- the caller decides, exactly as a human resolves a git conflict.

Usage:
    FreeCADCmd merge.py <base.FCStd> <ours.FCStd> <theirs.FCStd>              # whole part
    FreeCADCmd merge.py <base.FCStd> <ours.FCStd> <theirs.FCStd> sketch [Name]    # stage-4
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import sketch_recipe, part_recipe  # noqa: E402  (path set above)


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


# --- Whole-part merge (stage 5) --------------------------------------------------

_MISSING = object()  # sentinel: a key absent on a side (distinct from a value of None)


def _merge_dict(base, ours, theirs, field, node_id, conflicts):
    """Field-level three-way merge of a flat dict (a node's params or refs).

    A key changed on only one side takes that side; changed the same way on both takes
    it; changed divergently conflicts (ours kept provisional). Deletion is a change to
    _MISSING and merges by the same rule.
    """
    merged = {}
    for key in set(base) | set(ours) | set(theirs):
        b = base.get(key, _MISSING)
        o = ours.get(key, _MISSING)
        th = theirs.get(key, _MISSING)
        if o == th:
            chosen = o
        elif o == b:
            chosen = th          # only theirs changed
        elif th == b:
            chosen = o           # only ours changed
        else:
            conflicts.append({"kind": f"modify/modify {field}.{key}", "id": node_id,
                              "detail": {"ours": None if o is _MISSING else o,
                                         "theirs": None if th is _MISSING else th}})
            chosen = o           # provisional: ours wins
        if chosen is not _MISSING:
            merged[key] = chosen
    return merged


def _merge_edited_node(node_id, b, o, th, conflicts):
    """Both sides edited node `node_id` (differently). Merge it field by field."""
    merged = {"type": o.get("type", b.get("type")),
              "name": o.get("name", b.get("name"))}
    if o.get("type") != th.get("type") and o.get("type") != b.get("type") \
            and th.get("type") != b.get("type"):
        conflicts.append({"kind": "modify/modify type", "id": node_id,
                          "detail": {"ours": o.get("type"), "theirs": th.get("type")}})

    merged["params"] = _merge_dict(b.get("params", {}), o.get("params", {}),
                                   th.get("params", {}), "params", node_id, conflicts)
    merged["refs"] = _merge_dict(b.get("refs", {}), o.get("refs", {}),
                                 th.get("refs", {}), "refs", node_id, conflicts)

    be, oe, the = (n.get("expressions", []) for n in (b, o, th))
    if oe == the:
        merged_exprs = oe
    elif oe == be:
        merged_exprs = the
    elif the == be:
        merged_exprs = oe
    else:
        conflicts.append({"kind": "modify/modify expressions", "id": node_id,
                          "detail": {"ours": oe, "theirs": the}})
        merged_exprs = oe
    if merged_exprs:
        merged["expressions"] = merged_exprs

    if "sketch" in b and "sketch" in o and "sketch" in th:
        sk, sk_conf = merge_recipes(b["sketch"], o["sketch"], th["sketch"])
        merged["sketch"] = sk
        for c in sk_conf:
            conflicts.append({"kind": f"sketch:{c['kind']}", "id": node_id,
                              "detail": c.get("detail")})
    elif "sketch" in o:
        merged["sketch"] = o["sketch"]
    return merged


def _merge_features(base, ours, theirs):
    """Three-way merge of the id-keyed feature maps. Returns (merged, conflicts)."""
    merged = {}
    conflicts = []
    for t in set(base) | set(ours) | set(theirs):
        b, o, th = base.get(t), ours.get(t), theirs.get(t)

        if b is not None:
            ours_edited = o is not None and o != b
            theirs_edited = th is not None and th != b
            ours_deleted = o is None
            theirs_deleted = th is None

            if ours_deleted and theirs_deleted:
                continue
            if ours_deleted and not theirs_edited:
                continue
            if theirs_deleted and not ours_edited:
                continue
            if ours_deleted and theirs_edited:
                conflicts.append({"kind": "delete/modify", "id": t,
                                  "detail": "ours deleted, theirs modified"})
                merged[t] = th
                continue
            if theirs_deleted and ours_edited:
                conflicts.append({"kind": "modify/delete", "id": t,
                                  "detail": "ours modified, theirs deleted"})
                merged[t] = o
                continue
            if ours_edited and theirs_edited and o != th:
                merged[t] = _merge_edited_node(t, b, o, th, conflicts)
            elif ours_edited:
                merged[t] = o
            else:
                merged[t] = th
        else:
            # Added in a branch. Distinct creations get distinct UUIDs, so a same-UUID
            # add on both sides is degenerate but handled for safety.
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


def _merge_order(base, ours, theirs, merged_ids):
    """Best-effort merged order over surviving ids. Not conflict-detected (crude pass).

    Take ours' order as the spine, drop dead ids, append survivors ours never listed
    (in theirs' order, then base's). Order is data, not identity -- a real order merge
    is a later refinement, like durable constraint identity.
    """
    seen = set()
    result = []
    for uid in list(ours) + list(theirs) + list(base):
        if uid in merged_ids and uid not in seen:
            result.append(uid)
            seen.add(uid)
    return result


def merge_parts(base, ours, theirs):
    """Three-way merge of two whole-part recipes over a common ancestor. Pure."""
    feats, conflicts = _merge_features(
        base["features"], ours["features"], theirs["features"]
    )
    order = _merge_order(base.get("order", []), ours.get("order", []),
                         theirs.get("order", []), set(feats))
    merged = {"kind": "part", "features": feats, "order": order}
    return merged, conflicts


def format_part_merge(merged, conflicts):
    lines = []
    if conflicts:
        lines.append(f"CONFLICTS ({len(conflicts)}):")
        for c in conflicts:
            detail = c["detail"] if isinstance(c.get("detail"), str) else "both sides changed it"
            lines.append(f"  ! {c['kind']}  {c['id'][:8]}  ({detail})")
    else:
        lines.append("clean merge (no conflicts)")
    feats = merged["features"]
    lines.append(f"merged: {len(feats)} features")
    for t in merged["order"]:
        node = feats[t]
        lines.append(f"  {t[:8]} {node['name']} [{node['type'].split('::')[-1]}]")
    return "\n".join(lines)


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
        print("usage: FreeCADCmd merge.py <base> <ours> <theirs> [sketch [Name]]")
        return
    docs = [App.openDocument(f) for f in files[:3]]

    if "sketch" not in sys.argv:
        recs = [part_recipe(d) for d in docs]
        merged, conflicts = merge_parts(recs[0], recs[1], recs[2])
        print(format_part_merge(merged, conflicts))
        return

    want = next((a for a in sys.argv[1:] if not a.endswith(".FCStd")
                 and not a.endswith(".py") and a != "sketch"), None)
    recs = []
    for d in docs:
        sk = _first_sketch(d, want)
        if not sk:
            print(f"no sketch found in {d.Name}")
            return
        recs.append(sketch_recipe(sk))
    merged, conflicts = merge_recipes(recs[0], recs[1], recs[2])
    print(format_merge(merged, conflicts))


if len(sys.argv) > 1 and os.path.basename(sys.argv[1]) == "merge.py":
    _main()
