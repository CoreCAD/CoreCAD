// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Vec.hxx>

#include <Base/Exception.h>
#include <Mod/Part/App/Part2DObject.h>

#include "FeaturePointPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::PointPattern, PartDesign::Transformed)

PointPattern::PointPattern()
{
    ADD_PROPERTY_TYPE(
        Points,
        (nullptr),
        "PointPattern",
        App::Prop_None,
        "The sketch whose points place the copies. Construction points are not used."
    );
}

short PointPattern::mustExecute() const
{
    if (Points.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

Base::Vector3d PointPattern::centreOfOriginals()
{
    std::vector<Part::TopoShape> pieces;
    if (static_cast<Mode>(TransformMode.getValue()) == Mode::WholeShape) {
        pieces.push_back(getBaseTopoShape());
    }
    else {
        for (auto* original : getOriginals()) {
            auto* feature = freecad_cast<PartDesign::FeatureAddSub*>(original);
            if (!feature) {
                continue;
            }
            Part::TopoShape added;
            Part::TopoShape cut;
            feature->getAddSubShape(added, cut);
            const gp_Trsf location = feature->getLocation().Transformation();
            for (Part::TopoShape* half : {&added, &cut}) {
                if (!half->isNull()) {
                    pieces.push_back(half->makeElementTransform(location));
                }
            }
        }
    }

    Base::Vector3d centre;
    if (pieces.empty() || !Part::TopoShape().makeElementCompound(pieces).getCenterOfGravity(centre)) {
        throw Base::ValueError("Cannot find the centre of the features to copy");
    }
    return centre;
}

std::list<gp_Trsf> PointPattern::getTransformations()
{
    auto* sketch = freecad_cast<Part::Part2DObject*>(Points.getValue());
    if (!sketch) {
        throw Base::ValueError("No sketch of points specified");
    }

    // A sketch shape is a compound whose loose vertex children are its points; the
    // vertices of its wires are line ends, not points.
    std::vector<gp_Pnt> points;
    const TopoDS_Shape shape = sketch->Shape.getShape().getShape();
    if (!shape.IsNull()) {
        for (TopoDS_Iterator it(shape); it.More(); it.Next()) {
            if (it.Value().ShapeType() == TopAbs_VERTEX) {
                points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(it.Value())));
            }
        }
    }
    if (points.empty()) {
        throw Base::ValueError("The sketch has no points to place copies on");
    }

    const Base::Vector3d centre = centreOfOriginals();
    Base::Vector3d normal;
    sketch->Placement.getValue().getRotation().multVec(Base::Vector3d(0, 0, 1), normal);
    const gp_Vec planeNormal(normal.x, normal.y, normal.z);
    const gp_Pnt from(centre.x, centre.y, centre.z);

    std::list<gp_Trsf> transformations {gp_Trsf()};
    for (const gp_Pnt& point : points) {
        gp_Vec move(from, point);
        move -= planeNormal * move.Dot(planeNormal);
        if (move.Magnitude() < Precision::Confusion()) {
            continue;
        }
        gp_Trsf trsf;
        trsf.SetTranslation(move);
        transformations.push_back(trsf);
    }
    return transformations;
}
