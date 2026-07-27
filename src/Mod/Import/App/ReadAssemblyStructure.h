// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Cruth (Sean Barton)                                *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 ***************************************************************************/

#ifndef IMPORT_READASSEMBLYSTRUCTURE_H
#define IMPORT_READASSEMBLYSTRUCTURE_H

#include <string>
#include <vector>

#include <TDocStd_Document.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Base/Placement.h>
#include <Mod/Import/ImportGlobal.h>
#include <Mod/Part/App/TopoShape.h>

class TDF_Label;
class TopLoc_Location;

namespace Import
{

/// One direct component (instance) of a top-level assembly, read straight from
/// the STEP/IGES XCAF tree. The shape is the prototype's own local geometry and
/// the placement is the instance's pose within the parent assembly -- the two
/// are kept separate so a caller can build a live reference (App::Link) that
/// carries the placement while the geometry lives in its own document.
struct ImportExport AssemblyComponent
{
    std::string name;
    Part::TopoShape shape;
    Base::Placement placement;
    /// True when the referred prototype is itself an assembly (a nested
    /// sub-assembly). The caller decides how to handle nesting.
    bool isAssembly {false};
};

/// Reads the direct component structure of a STEP/IGES assembly document
/// (already loaded into an XCAF TDocStd_Document) without materialising any
/// App::DocumentObject. This is the honest reader layer: it extracts
/// (local shape, instance placement, name) per top-level component and mints
/// nothing -- no App::Part, no Part::Feature. The document-shaping decision is
/// left entirely to the caller.
class ImportExport ReadAssemblyStructure
{
public:
    explicit ReadAssemblyStructure(Handle(TDocStd_Document) hDoc);

    /// Best-effort name of the single top-level assembly, or an empty string
    /// when the file has no assembly wrapper (a flat set of free shapes).
    const std::string& rootName() const
    {
        return rootName_;
    }

    /// The direct components that make up the top-level assembly.
    const std::vector<AssemblyComponent>& components() const
    {
        return components_;
    }

private:
    void read();
    void addComponent(const TDF_Label& component);
    std::string labelName(const TDF_Label& label) const;

    Handle(XCAFDoc_ShapeTool) shapeTool_;
    std::string rootName_;
    std::vector<AssemblyComponent> components_;
};

}  // namespace Import

#endif  // IMPORT_READASSEMBLYSTRUCTURE_H
