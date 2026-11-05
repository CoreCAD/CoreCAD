"""Git-for-CAD spine: Clause 6.5 proof -- split-vs-edit is a modify/delete conflict.

Amendment 6 makes a durable id (`Part::Geometry` tag) survive a 1->1 sketch edit and
*retire* on a count-changing split. This script proves the payoff at the merge layer:
when one branch edits a sketch entity and another branch splits that same entity, the
three-way recipe merge must NOT silently combine them -- it must surface a modify/delete
conflict, exactly as git does when one side edits a line another side deleted.

It is self-contained: it builds an ancestor part, an "ours" branch (edit the line in
place -- tag survives) and a "theirs" branch (split the line -- parent tag retires, two
fresh children), then runs the real `merge_parts` and asserts the conflict. No spine code
is special-cased for this; the conflict falls straight out of the tag mechanism.

    FreeCADCmd demo_split_vs_edit.py        # prints PASS/FAIL, exits nonzero on FAIL
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recipe import part_recipe            # noqa: E402  (path set above)
from merge import merge_parts, merge_recipes  # noqa: E402


def _build_fixtures(workdir):
    """Create base / ours / theirs .FCStd files. Returns (paths, line_tag)."""
    import FreeCAD as App
    import Part
    from FreeCAD import Vector

    base_f = os.path.join(workdir, "base.FCStd")
    ours_f = os.path.join(workdir, "ours.FCStd")
    theirs_f = os.path.join(workdir, "theirs.FCStd")

    # ancestor: one sketch, one line
    doc = App.newDocument("base")
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    gid = sk.addGeometry(Part.LineSegment(Vector(0, 0, 0), Vector(10, 0, 0)), False)
    doc.recompute()
    line_tag = sk.Geometry[gid].Tag
    doc.saveAs(base_f)
    App.closeDocument(doc.Name)

    # ours: edit the line's end point in place -> the entity stays one entity, keeps its tag
    doc = App.openDocument(base_f)
    doc.Objects[0].moveGeometry(0, 2, Vector(10, 5, 0))  # PointPos end == 2
    doc.recompute()
    assert doc.Objects[0].Geometry[0].Tag == line_tag, "edit must keep the durable id"
    doc.saveAs(ours_f)
    App.closeDocument(doc.Name)

    # theirs: split the line -> count-changing, parent tag retires, two fresh children
    doc = App.openDocument(base_f)
    doc.Objects[0].split(0, Vector(5, 0, 0))
    doc.recompute()
    assert line_tag not in [g.Tag for g in doc.Objects[0].Geometry], \
        "split must retire the parent id"
    doc.saveAs(theirs_f)
    App.closeDocument(doc.Name)

    return (base_f, ours_f, theirs_f), line_tag


def main():
    import FreeCAD as App

    with tempfile.TemporaryDirectory() as workdir:
        (base_f, ours_f, theirs_f), line_tag = _build_fixtures(workdir)
        recs = [part_recipe(App.openDocument(f)) for f in (base_f, ours_f, theirs_f)]

    _, conflicts = merge_parts(recs[0], recs[1], recs[2])

    # The whole-part merge relabels a nested sketch conflict with the sketch's uid, so also
    # merge the sketch directly to show the conflict pins the split line's exact tag.
    sk_recs = [r["features"][r["order"][0]]["sketch"] for r in recs]
    _, sk_conflicts = merge_recipes(*sk_recs)

    part_md = [c for c in conflicts if "modify/delete" in c["kind"]]
    line_md = [c for c in sk_conflicts
               if c["kind"] == "modify/delete" and c["id"] == line_tag]

    ok = bool(part_md) and bool(line_md)
    print("line tag under contention :", line_tag)
    print("part-level conflicts      :", [(c["kind"], c["id"][:8]) for c in conflicts])
    print("sketch-level conflicts    :", [(c["kind"], c["id"][:8]) for c in sk_conflicts])
    print()
    print("PASS: split-vs-edit surfaces as a modify/delete conflict on the split line"
          if ok else "FAIL: expected a modify/delete conflict pinning the split line")
    return 0 if ok else 1


# FreeCADCmd runs a script with __name__ = the module basename (not "__main__"), so guard on
# this file being the invoked script. Flush before exit: piped stdout is block-buffered and
# FreeCADCmd's teardown after SystemExit would otherwise drop the report.
if os.path.basename(sys.argv[1] if len(sys.argv) > 1 else "") == "demo_split_vs_edit.py":
    rc = main()
    sys.stdout.flush()
    sys.exit(rc)
