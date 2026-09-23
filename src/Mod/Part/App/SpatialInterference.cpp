// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <BRepBndLib.hxx>
# include <Bnd_Box.hxx>
# include <BRepGProp.hxx>
# include <GProp_GProps.hxx>
# include <Precision.hxx>
# include <Standard_Failure.hxx>
# include <TopoDS_Shape.hxx>
#endif

#include <numeric>

#include <App/Document.h>
#include <App/DocumentObject.h>

#include "PartFeature.h"
#include "SpatialInterference.h"

namespace Part
{

bool sharesVolume(const TopoShape& first, const TopoShape& second)
{
    if (first.isNull() || second.isNull()) {
        return false;
    }

    TopoDS_Shape inter;
    try {
        inter = first.common(second.getShape());
    }
    catch (const Standard_Failure&) {
        return false;
    }
    if (inter.IsNull()) {
        return false;
    }

    GProp_GProps props;
    BRepGProp::VolumeProperties(inter, props);
    return props.Mass() > Precision::Confusion();
}

namespace
{

/// True when some other shape-carrying object builds on @p obj.
bool isConsumed(const App::DocumentObject* obj)
{
    for (const App::DocumentObject* user : obj->getInList()) {
        if (user && user->isDerivedFrom<ShapeFeature>()) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<App::DocumentObject*> independentSolids(App::Document* doc)
{
    std::vector<App::DocumentObject*> solids;
    if (!doc) {
        return solids;
    }

    for (auto* obj : doc->getObjectsOfType(ShapeFeature::getClassTypeId())) {
        if (isConsumed(obj)) {
            continue;
        }
        // A body reads its own shape here, the mirror it refreshes from its tip on
        // every recompute. Should that mirror ever go, a body would need asking in
        // its own terms -- there is nothing else in this sweep that is not simply
        // the shape the object is holding.
        const TopoShape& shape = static_cast<ShapeFeature*>(obj)->Shape.getShape();
        if (shape.isNull() || shape.countSubShapes(TopAbs_SOLID) == 0) {
            continue;
        }
        solids.push_back(obj);
    }
    return solids;
}

std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> overlappingPairs(App::Document* doc)
{
    std::vector<std::pair<App::DocumentObject*, App::DocumentObject*>> pairs;

    const std::vector<App::DocumentObject*> solids = independentSolids(doc);
    std::vector<TopoShape> shapes;
    std::vector<Bnd_Box> boxes;
    std::vector<App::DocumentObject*> kept;
    for (auto* obj : solids) {
        TopoShape shape = static_cast<ShapeFeature*>(obj)->Shape.getShape();
        Bnd_Box box;
        try {
            BRepBndLib::Add(shape.getShape(), box);
        }
        catch (const Standard_Failure&) {
            continue;
        }
        if (box.IsVoid()) {
            continue;
        }
        kept.push_back(obj);
        shapes.push_back(shape);
        boxes.push_back(box);
    }

    for (std::size_t i = 0; i < kept.size(); ++i) {
        for (std::size_t j = i + 1; j < kept.size(); ++j) {
            if (boxes[i].IsOut(boxes[j])) {
                continue;
            }
            if (sharesVolume(shapes[i], shapes[j])) {
                pairs.emplace_back(kept[i], kept[j]);
            }
        }
    }
    return pairs;
}

std::size_t connectedComponentCount(const std::vector<TopoShape>& shapes)
{
    if (shapes.empty()) {
        return 0;
    }

    // Union-find over the overlap graph. Cheaper and more honest than fusing everything and
    // counting the solids that come back: the fuse would have to succeed, and a boolean
    // failure would silently read as "they merged".
    std::vector<std::size_t> parent(shapes.size());
    std::iota(parent.begin(), parent.end(), 0);

    auto find = [&parent](std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];  // path halving
            x = parent[x];
        }
        return x;
    };

    std::vector<Bnd_Box> boxes(shapes.size());
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (shapes[i].isNull()) {
            continue;
        }
        try {
            BRepBndLib::Add(shapes[i].getShape(), boxes[i]);
        }
        catch (const Standard_Failure&) {
            boxes[i].SetVoid();
        }
    }

    for (std::size_t i = 0; i < shapes.size(); ++i) {
        for (std::size_t j = i + 1; j < shapes.size(); ++j) {
            if (boxes[i].IsVoid() || boxes[j].IsVoid() || boxes[i].IsOut(boxes[j])) {
                continue;
            }
            if (find(i) == find(j)) {
                continue;  // already known to be the same piece
            }
            if (sharesVolume(shapes[i], shapes[j])) {
                parent[find(i)] = find(j);
            }
        }
    }

    std::size_t components = 0;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        if (find(i) == i) {
            ++components;
        }
    }
    return components;
}

}  // namespace Part
