// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <set>
# include <vector>

# include <BRep_Builder.hxx>
# include <BRepAlgoAPI_Common.hxx>
# include <BRepAlgoAPI_Cut.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Compound.hxx>
# include <TopTools_ListOfShape.hxx>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Exception.h>

#include "PartFeature.h"
#include "ShapeComparison.h"

using namespace Part;

namespace
{

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Shape> solids;
    if (shape.IsNull()) {
        return solids;
    }
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        solids.push_back(it.Current());
    }
    return solids;
}

TopoShape compoundOf(const std::vector<TopoDS_Shape>& solids)
{
    if (solids.empty()) {
        return {};
    }
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const TopoDS_Shape& solid : solids) {
        builder.Add(compound, solid);
    }
    return TopoShape(compound);
}

TopTools_ListOfShape listOf(const std::vector<TopoDS_Shape>& shapes)
{
    TopTools_ListOfShape list;
    for (const TopoDS_Shape& shape : shapes) {
        list.Append(shape);
    }
    return list;
}

/// `objects` less `tools`, each a list of solids. Each list may overlap within itself: the leaves
/// of one document need not be disjoint.
TopoDS_Shape cut(const std::vector<TopoDS_Shape>& objects, const std::vector<TopoDS_Shape>& tools)
{
    BRepAlgoAPI_Cut op;
    op.SetArguments(listOf(objects));
    op.SetTools(listOf(tools));
    op.Build();
    if (!op.IsDone()) {
        throw Base::RuntimeError("Could not work out the material that changed");
    }
    return op.Shape();
}

TopoDS_Shape common(const std::vector<TopoDS_Shape>& objects, const std::vector<TopoDS_Shape>& tools)
{
    BRepAlgoAPI_Common op;
    op.SetArguments(listOf(objects));
    op.SetTools(listOf(tools));
    op.Build();
    if (!op.IsDone()) {
        throw Base::RuntimeError("Could not work out the material both versions share");
    }
    return op.Shape();
}

}  // namespace

TopoShape Part::documentSolids(const App::Document& doc)
{
    std::vector<const App::DocumentObject*> carriers;
    std::set<const App::DocumentObject*> builtOn;
    for (const App::DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        const TopoShape shape = Feature::getTopoShape(obj, ShapeOption::Transform);
        if (shape.isNull() || solidsOf(shape.getShape()).empty()) {
            continue;
        }
        carriers.push_back(obj);
        for (const App::DocumentObject* used : obj->getOutList()) {
            builtOn.insert(used);
        }
    }

    std::vector<TopoDS_Shape> solids;
    for (const App::DocumentObject* obj : carriers) {
        if (builtOn.count(obj) != 0) {
            continue;
        }
        const TopoShape shape = Feature::getTopoShape(obj, ShapeOption::Transform);
        for (const TopoDS_Shape& solid : solidsOf(shape.getShape())) {
            solids.push_back(solid);
        }
    }
    return compoundOf(solids);
}

ShapeComparison Part::compareShapes(const TopoShape& before, const TopoShape& after)
{
    const std::vector<TopoDS_Shape> earlier = solidsOf(before.getShape());
    const std::vector<TopoDS_Shape> later = solidsOf(after.getShape());

    ShapeComparison comparison;
    if (earlier.empty()) {
        comparison.added = compoundOf(later);
        return comparison;
    }
    if (later.empty()) {
        comparison.removed = compoundOf(earlier);
        return comparison;
    }

    comparison.added = compoundOf(solidsOf(cut(later, earlier)));
    comparison.removed = compoundOf(solidsOf(cut(earlier, later)));
    comparison.shared = compoundOf(solidsOf(common(earlier, later)));
    return comparison;
}
