import FreeCAD as App, shutil, os
d = "/tmp/claude-1000/-home-sean-Repos-CoreCAD/8388b849-92a7-461a-9267-014366fc7922/scratchpad/ap23"
def run(name, edit_a, edit_b):
    base = os.path.join(d, name + "_o.FCStd")
    doc = App.newDocument(name)
    s = doc.addObject("Spreadsheet::Sheet", "Sheet")
    s.set("A1", "1"); s.set("A2", "2"); s.set("A3", "=A2*10"); doc.recompute(); doc.saveAs(base)
    for n in "ab": shutil.copy(base, os.path.join(d, name + "_" + n + ".FCStd"))
    A = App.openDocument(os.path.join(d, name + "_a.FCStd"), duplicate="same")
    B = App.openDocument(os.path.join(d, name + "_b.FCStd"), duplicate="same")
    edit_a(A.getObject("Sheet")); edit_b(B.getObject("Sheet")); A.recompute(); B.recompute()
    r = App.mergeDocuments(doc, A, B, True)
    out = [c["detail"] for c in r["conflicts"]]
    m = r.get("document")
    if m is not None:
        sh = [o for o in m.Objects if o.TypeId == "Spreadsheet::Sheet"][0]
        out.append({a: sh.getContents(a) + "→" + str(sh.get(a)) for a in sh.getUsedCells()})
    else:
        out.append(("refusal", r.get("refusal")))
    print("==", name, out)
run("cells", lambda s: s.set("A1", "100"), lambda s: s.set("A2", "200"))
run("rows", lambda s: s.set("A1", "100"), lambda s: (s.insertRows("1", 1), s.set("A1", "0")))
run("same", lambda s: s.set("A1", "100"), lambda s: s.set("A1", "5"))
run("formula", lambda s: s.insertRows("1", 1), lambda s: s.set("A3", "=A1+A2"))
run("newrow", lambda s: s.set("B5", "7"), lambda s: s.set("C5", "8"))
run("newrow_moved", lambda s: (s.insertRows("1", 1), s.set("B6", "7")), lambda s: s.set("C5", "8"))
