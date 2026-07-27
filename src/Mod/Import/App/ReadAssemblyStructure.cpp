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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <TDF_Label.hxx>
# include <TDF_LabelSequence.hxx>
# include <TDataStd_Name.hxx>
# include <TopLoc_Location.hxx>
# include <XCAFDoc_DocumentTool.hxx>
# include <gp_Trsf.hxx>
#endif

#include <Base/Matrix.h>

#include "ReadAssemblyStructure.h"

using namespace Import;

namespace
{
Base::Placement toPlacement(const TopLoc_Location& loc)
{
    Base::Matrix4D mtrx;
    Part::TopoShape::convertToMatrix(loc.Transformation(), mtrx);
    Base::Placement placement;
    placement.fromMatrix(mtrx);
    return placement;
}
}  // namespace

ReadAssemblyStructure::ReadAssemblyStructure(Handle(TDocStd_Document) hDoc)
{
    shapeTool_ = XCAFDoc_DocumentTool::ShapeTool(hDoc->Main());
    read();
}

std::string ReadAssemblyStructure::labelName(const TDF_Label& label) const
{
    Handle(TDataStd_Name) name;
    if (label.FindAttribute(TDataStd_Name::GetID(), name)) {
        const TCollection_ExtendedString& extstr = name->Get();
        std::vector<char> buf(extstr.LengthOfCString() + 1, '\0');
        Standard_PCharacter ptr = buf.data();
        extstr.ToUTF8CString(ptr);
        return std::string(buf.data());
    }
    return {};
}

AssemblyComponent ReadAssemblyStructure::buildComponent(const TDF_Label& component)
{
    // A component label references a prototype (the shared part definition) and
    // carries its own location (the instance's pose within the assembly). We
    // keep the two apart: the prototype gives the local geometry, the location
    // gives the placement.
    TDF_Label prototype = component;
    shapeTool_->GetReferredShape(component, prototype);

    AssemblyComponent comp;
    comp.name = labelName(prototype);
    if (comp.name.empty()) {
        comp.name = labelName(component);
    }
    comp.placement = toPlacement(shapeTool_->GetLocation(component));
    comp.isAssembly = shapeTool_->IsAssembly(prototype);
    comp.shape = Part::TopoShape(shapeTool_->GetShape(prototype));

    // A sub-assembly carries its own direct components; recurse to full depth so
    // the caller can build a live assembly for each nested level.
    if (comp.isAssembly) {
        readComponentsInto(prototype, comp.children);
    }
    return comp;
}

void ReadAssemblyStructure::readComponentsInto(
    const TDF_Label& assembly,
    std::vector<AssemblyComponent>& out
)
{
    TDF_LabelSequence subs;
    shapeTool_->GetComponents(assembly, subs);
    for (Standard_Integer i = 1; i <= subs.Length(); ++i) {
        out.push_back(buildComponent(subs.Value(i)));
    }
}

void ReadAssemblyStructure::read()
{
    TDF_LabelSequence freeShapes;
    shapeTool_->GetFreeShapes(freeShapes);

    // The common case: a single free shape that is the root assembly. Its direct
    // components become the top-level instances.
    if (freeShapes.Length() == 1 && shapeTool_->IsAssembly(freeShapes.Value(1))) {
        const TDF_Label root = freeShapes.Value(1);
        rootName_ = labelName(root);
        readComponentsInto(root, components_);
        return;
    }

    // No assembly wrapper: a flat set of free shapes. Each free shape is itself a
    // top-level component, at its own location (usually identity). rootName_ stays
    // empty so the caller can name the assembly after the source file.
    for (Standard_Integer i = 1; i <= freeShapes.Length(); ++i) {
        components_.push_back(buildComponent(freeShapes.Value(i)));
    }
}
