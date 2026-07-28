# SPDX-License-Identifier: LGPL-2.1-or-later
# /****************************************************************************
#                                                                           *
#    Copyright (c) 2026 Cruth (Sean Barton)                                 *
#                                                                           *
#    This file is part of FreeCAD.                                          *
#                                                                           *
#    FreeCAD is free software: you can redistribute it and/or modify it     *
#    under the terms of the GNU Lesser General Public License as            *
#    published by the Free Software Foundation, either version 2.1 of the   *
#    License, or (at your option) any later version.                        *
#                                                                           *
#    FreeCAD is distributed in the hope that it will be useful, but         *
#    WITHOUT ANY WARRANTY; without even the implied warranty of             *
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
#    Lesser General Public License for more details.                        *
#                                                                           *
#    You should have received a copy of the GNU Lesser General Public       *
#    License along with FreeCAD. If not, see                                *
#    <https://www.gnu.org/licenses/>.                                       *
#                                                                           *
# ***************************************************************************/

"""Import a STEP/IGES assembly as a *live* CoreCAD assembly.

This is a thin translator. The honest structured reader (Import.readAssemblyStructure)
walks the STEP/IGES tree to full depth and hands back, per component, its local geometry,
its instance placement, and (for a sub-assembly) its own children -- minting no document
objects. This module writes each leaf component to its own ``.cpart`` document and drives
the headless assembly builder (UtilsAssembly) to produce a ``.cassembly`` per assembly
level: a leaf is linked with an ``App::Link``, a nested sub-assembly gets its own
``.cassembly`` (built first) and is linked into its parent with an ``Assembly::AssemblyLink``.
No ``App::Part`` is involved anywhere.

Nested sub-assemblies are imported recursively. All files for one import land in a single
folder; leaf parts, every sub-assembly, and the top assembly each get a uniquely named
file there.
"""

import os

import FreeCAD as App

import UtilsAssembly


def _unique_name(base, used):
    """Return a filesystem-safe name derived from ``base``, unique within ``used``."""
    name = "".join(c if (c.isalnum() or c in "-_ ") else "_" for c in base.strip())
    name = name.strip() or "Part"
    candidate = name
    suffix = 1
    while candidate in used:
        suffix += 1
        candidate = f"{name}_{suffix}"
    used.add(candidate)
    return candidate


def importAssembly(step_path, dest_dir=None, rigid=False, structure=None):
    """Import ``step_path`` (a STEP/IGES assembly, nested to any depth) as a live assembly.

    Each leaf component becomes a standalone ``.cpart`` document holding its local
    geometry; each assembly level becomes a ``.cassembly`` that links its components at
    their imported placements and grounds the first. A nested sub-assembly is built into
    its own ``.cassembly`` and linked into its parent with an ``Assembly::AssemblyLink``.
    All files are written to ``dest_dir`` (default: a folder named after the source file,
    beside it). Returns the top-level AssemblyObject.

    ``rigid`` controls how nested sub-assemblies are linked: rigid (block) or flexible
    (their own joints honoured). It currently defaults to flexible while the rigid
    render path (#75) is being finished; the default flips to rigid once that lands.

    ``structure`` may hold an already-read reader dict (as returned by
    ``Import.readAssemblyStructure``) to avoid parsing the file twice.

    Raises ValueError if the file contains no components.
    """
    if structure is None:
        import Import  # Assembly -> Import only at call time, never at module import.

        structure = Import.readAssemblyStructure(step_path)
    components = structure["components"]
    if not components:
        raise ValueError(f"No importable components found in {step_path!r}.")

    stem = os.path.splitext(os.path.basename(step_path))[0]
    if dest_dir is None:
        dest_dir = os.path.join(os.path.dirname(os.path.abspath(step_path)), stem)
    os.makedirs(dest_dir, exist_ok=True)

    asm_name = structure["name"].strip() or stem
    # One name pool for the whole tree so every file in dest_dir is uniquely named.
    used_names = set()
    return _buildAssembly(asm_name, components, dest_dir, used_names, rigid)


def _looksLikeAssembly(components):
    """Whether an imported structure warrants a live assembly rather than a loose part.

    A lone leaf solid stays a plain ``Part::Feature`` (imported the standard way);
    anything with more than one top-level component, or any nested sub-assembly,
    becomes a live ``.cassembly``.
    """
    if len(components) != 1:
        return True
    return bool(components[0]["is_assembly"])


def _delegateStandardImport(filename, docname=None):
    """Fall back to the standard (non-live) importer for a single-part file.

    Uses the GUI importer when a GUI is up (so colours/progress match a plain
    File>Open), otherwise the headless one.
    """
    if App.GuiUp:
        import ImportGui as importer
    else:
        import Import as importer
    if docname is None:
        return importer.open(filename)
    return importer.insert(filename, docname)


def open(filename):  # noqa: A001 - the import framework calls a module-level open().
    """File>Open handler for STEP: a live assembly when structured, else a loose part."""
    import Import  # Assembly -> Import only at call time, never at module import.

    structure = Import.readAssemblyStructure(filename)
    if not _looksLikeAssembly(structure["components"]):
        return _delegateStandardImport(filename)

    assembly = importAssembly(filename, structure=structure)
    if App.GuiUp:
        try:
            import FreeCADGui as Gui

            App.setActiveDocument(assembly.Document.Name)
            Gui.SendMsgToActiveView("ViewFit")
        except Exception:  # a failed view fit must not fail the import
            pass
    return assembly


def insert(filename, docname):
    """Insert (merge-into-document) handler.

    A live assembly is inherently its own set of documents, so a request to merge one
    into an existing document keeps the standard (non-live) importer; single parts do
    too.
    """
    return _delegateStandardImport(filename, docname)


def _buildAssembly(asm_name, components, dest_dir, used_names, rigid):
    """Build one ``.cassembly`` level and return its AssemblyObject.

    ``components`` is a list of reader dicts (each with name/shape/placement/is_assembly/
    children). Sub-assemblies are built recursively before they are linked in.
    """
    asm_name = _unique_name(asm_name, used_names)

    # The assembly document must exist on disk before it can hold a cross-document link:
    # a component is a file-to-file reference, so its owner needs a path.
    assembly = UtilsAssembly.createAssembly(asm_name)
    assembly.Document.saveAs(os.path.join(dest_dir, asm_name + ".cassembly"))

    first_component = None
    for comp in components:
        if comp["is_assembly"]:
            # Build the sub-assembly's own .cassembly first, then link it in.
            sub = _buildAssembly(comp["name"], comp["children"], dest_dir, used_names, rigid)
            name = sub.Document.Label
            component = UtilsAssembly.addSubAssembly(
                assembly, sub, comp["placement"], rigid=rigid, label=name, name=name
            )
        else:
            name = _unique_name(comp["name"], used_names)
            leaf_doc = App.newDocument(name, type=App.DocTypePart)
            feature = leaf_doc.addObject("Part::Feature", "Body")
            feature.Shape = comp["shape"]
            feature.Label = name
            leaf_doc.recompute()
            leaf_doc.saveAs(os.path.join(dest_dir, name + ".cpart"))
            component = UtilsAssembly.addComponent(assembly, feature, comp["placement"], label=name)

        if first_component is None:
            first_component = component

    # A live assembly needs a fixed base to solve against; ground the first component.
    _groundFirst(assembly, first_component)
    assembly.Document.recompute()
    assembly.solve()
    assembly.Document.save()
    return assembly


def _groundFirst(assembly, component):
    """Ground ``component`` in ``assembly`` so the solver has a fixed base.

    Grounding a flexible sub-assembly link does not pin its internals, so ground one of
    its internal parts instead; a rigid link (or a plain part) is grounded directly.
    """
    if component.isDerivedFrom("Assembly::AssemblyLink") and not component.Rigid:
        base = _flexibleBase(component)
        if base is not None:
            component = base
    UtilsAssembly.groundComponent(assembly, component)


def _flexibleBase(link):
    """Return the internal part of a flexible sub-assembly link to ground, or None.

    Prefers the part that mirrors the sub-assembly's own grounded object; falls back to
    the first linkable child.
    """
    linked = link.LinkedObject
    src_grounded = None
    if linked is not None:
        for obj in linked.InListRecursive:
            if obj.isDerivedFrom("Assembly::GroundedJoint"):
                src_grounded = obj.ObjectToGround
                break

    candidate = None
    for child in link.Group:
        if candidate is None and (
            child.isDerivedFrom("App::Link") or child.isDerivedFrom("Part::Feature")
        ):
            candidate = child
        if (
            src_grounded is not None
            and hasattr(child, "LinkedObject")
            and child.LinkedObject == src_grounded
        ):
            return child
    return candidate
