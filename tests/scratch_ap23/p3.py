exec(open("/tmp/claude-1000/-home-sean-Repos-CoreCAD/8388b849-92a7-461a-9267-014366fc7922/scratchpad/ap23/p2.py").read().split("run(\"cells\"")[0])
run("byaddress", lambda s: s.insertRows("1", 1), lambda s: s.set("B1", "=A2"))
def b_alias(s):
    s.set("B1", "=A2")
run("alias", lambda s: (s.insertRows("1", 1)), lambda s: (s.setAlias("A2", "w"), s.set("B1", "=w")))
# another sheet points into Sheet by address
def other(name, edit_a, edit_b):
    import FreeCAD as App, shutil, os
    base = os.path.join(d, name + "_o.FCStd")
    doc = App.newDocument(name)
    s = doc.addObject("Spreadsheet::Sheet", "Sheet"); t = doc.addObject("Spreadsheet::Sheet", "Other")
    s.set("A1", "1"); s.set("A2", "2"); t.set("A1", "0"); doc.recompute(); doc.saveAs(base)
    for n in "ab": shutil.copy(base, os.path.join(d, name + "_" + n + ".FCStd"))
    A = App.openDocument(os.path.join(d, name + "_a.FCStd"), duplicate="same")
    B = App.openDocument(os.path.join(d, name + "_b.FCStd"), duplicate="same")
    edit_a(A); edit_b(B); A.recompute(); B.recompute()
    r = App.mergeDocuments(doc, A, B, True)
    m = r.get("document")
    print("==", name, [c["detail"] for c in r["conflicts"]], r.get("refusal"),
          m and {o.Label: {a: o.getContents(a) + "→" + str(o.get(a)) for a in o.getUsedCells()} for o in m.Objects if o.TypeId == "Spreadsheet::Sheet"})
other("crossdoc", lambda D: D.getObject("Sheet").insertRows("1", 1), lambda D: D.getObject("Other").set("A1", "=Sheet.A2"))
