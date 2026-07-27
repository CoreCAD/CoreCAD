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
walks the STEP/IGES tree and hands back, per top-level component, its local geometry
and its instance placement -- minting no document objects. This module writes each
component to its own ``.cpart`` document and drives the headless assembly builder
(UtilsAssembly) to produce a ``.cassembly`` that links those parts at their imported
placements. No ``App::Part`` is involved anywhere.

Only single-level assemblies are handled for now; a nested sub-assembly is refused
rather than silently flattened.
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


def importAssembly(step_path, dest_dir=None):
    """Import ``step_path`` (a single-level STEP/IGES assembly) as a live assembly.

    Each component becomes a standalone ``.cpart`` document holding its local
    geometry; a ``.cassembly`` document links them at their imported placements and
    grounds the first. All files are written to ``dest_dir`` (default: a folder named
    after the source file, beside it). Returns the AssemblyObject.

    Raises NotImplementedError if the file contains nested sub-assemblies, and
    ValueError if it contains no components.
    """
    import Import  # Assembly -> Import only at call time, never at module import.

    structure = Import.readAssemblyStructure(step_path)
    components = structure["components"]
    if not components:
        raise ValueError(f"No importable components found in {step_path!r}.")
    if any(comp["is_assembly"] for comp in components):
        raise NotImplementedError(
            "Nested sub-assemblies are not yet supported; only single-level "
            "assemblies can currently be imported as live assemblies."
        )

    stem = os.path.splitext(os.path.basename(step_path))[0]
    if dest_dir is None:
        dest_dir = os.path.join(os.path.dirname(os.path.abspath(step_path)), stem)
    os.makedirs(dest_dir, exist_ok=True)

    asm_name = structure["name"].strip() or stem

    # The assembly document must exist on disk before it can hold a cross-document
    # link: a component is a file-to-file reference, so its owner needs a path.
    assembly = UtilsAssembly.createAssembly(asm_name)
    assembly.Document.saveAs(os.path.join(dest_dir, asm_name + ".cassembly"))

    used_names = set()
    first_link = None
    for comp in components:
        name = _unique_name(comp["name"], used_names)

        leaf_doc = App.newDocument(name, type=App.DocTypePart)
        feature = leaf_doc.addObject("Part::Feature", "Body")
        feature.Shape = comp["shape"]
        feature.Label = name
        leaf_doc.recompute()
        leaf_doc.saveAs(os.path.join(dest_dir, name + ".cpart"))

        link = UtilsAssembly.addComponent(assembly, feature, comp["placement"], label=name)
        if first_link is None:
            first_link = link

    # A live assembly needs a fixed base to solve against; ground the first part.
    UtilsAssembly.groundComponent(assembly, first_link)
    assembly.Document.recompute()
    assembly.solve()
    assembly.Document.save()
    return assembly
