// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <cmath>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>

#include "FeatureFillPattern.h"

using namespace PartDesign;

namespace
{

/// The flat faces copies are packed into, all in one plane, in the world.
struct Region
{
    std::vector<TopoDS_Face> faces;
    gp_Pnt origin;
    gp_Dir normal;
    gp_Dir xDirection;
};

Region regionOf(const App::DocumentObject* obj, const std::vector<std::string>& subs)
{
    constexpr auto options = Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform;
    Region region;

    const bool whole = std::ranges::all_of(subs, &std::string::empty);
    if (whole) {
        const TopoDS_Shape shape = Part::Feature::getTopoShape(obj, options).getShape();
        std::vector<Part::TopoShape> outlines;
        for (TopExp_Explorer it(shape, TopAbs_WIRE); it.More(); it.Next()) {
            if (BRep_Tool::IsClosed(it.Current())) {
                outlines.emplace_back(it.Current());
            }
        }
        if (outlines.empty()) {
            throw Base::ValueError("The boundary must be a closed outline");
        }
        const TopoDS_Shape faces = Part::TopoShape(0).makeElementFace(outlines).getShape();
        for (TopExp_Explorer it(faces, TopAbs_FACE); it.More(); it.Next()) {
            region.faces.push_back(TopoDS::Face(it.Current()));
        }
    }
    else {
        for (const std::string& sub : subs) {
            const TopoDS_Shape shape
                = Part::Feature::getTopoShape(obj, options, sub.c_str()).getShape();
            if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
                throw Base::ValueError("The boundary must be made of faces");
            }
            region.faces.push_back(TopoDS::Face(shape));
        }
    }
    if (region.faces.empty()) {
        throw Base::ValueError("The boundary has no area");
    }

    for (const TopoDS_Face& face : region.faces) {
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane) {
            throw Base::ValueError("The boundary must be flat");
        }
        const gp_Pln plane = surface.Plane();
        if (&face == &region.faces.front()) {
            region.origin = plane.Location();
            region.normal = plane.Axis().Direction();
            region.xDirection = plane.XAxis().Direction();
        }
        else if (
            !plane.Axis().Direction().IsParallel(region.normal, Precision::Angular())
            || plane.Distance(region.origin) > Precision::Confusion()
        ) {
            throw Base::ValueError("The boundary faces must lie in one plane");
        }
    }

    // A sketch's rows follow its own horizontal axis, not whatever the face maker chose.
    if (const auto* sketch = freecad_cast<const Part::Part2DObject*>(obj); sketch && whole) {
        Base::Vector3d x;
        sketch->Placement.getValue().getRotation().multVec(Base::Vector3d(1, 0, 0), x);
        const gp_Vec along(x.x, x.y, x.z);
        if (along.Magnitude() > Precision::Confusion()
            && !gp_Dir(along).IsParallel(region.normal, Precision::Angular())) {
            region.xDirection = gp_Dir(gp_Vec(region.normal).Crossed(along).Crossed(region.normal));
        }
    }
    return region;
}

}  // namespace


PROPERTY_SOURCE(PartDesign::FillPattern, PartDesign::Transformed)

const char* FillPattern::LayoutEnums[] = {"Grid", "Staggered", nullptr};

FillPattern::FillPattern()
{
    ADD_PROPERTY_TYPE(
        Boundary,
        (nullptr),
        "FillPattern",
        App::Prop_None,
        "The flat faces the copies are packed into, or a sketch whose closed outlines are used."
    );
    ADD_PROPERTY_TYPE(
        Layout,
        (0L),
        "FillPattern",
        App::Prop_None,
        "How the copies are arranged.\n'Grid': rows of copies lined up one above the other.\n"
        "'Staggered': every other row shifted by half the spacing."
    );
    ADD_PROPERTY_TYPE(
        Spacing,
        (10.0),
        "FillPattern",
        App::Prop_None,
        "The distance between neighbouring copies in a row."
    );
    ADD_PROPERTY_TYPE(
        RowSpacing,
        (10.0),
        "FillPattern",
        App::Prop_None,
        "The distance between neighbouring rows."
    );
    ADD_PROPERTY_TYPE(
        Margin,
        (0.0),
        "FillPattern",
        App::Prop_None,
        "How far inside the boundary the centre of every copy must be."
    );
    ADD_PROPERTY_TYPE(
        Angle,
        (0.0),
        "FillPattern",
        App::Prop_None,
        "The angle the rows run at, from the boundary's horizontal direction."
    );
    Layout.setEnums(LayoutEnums);
}

short FillPattern::mustExecute() const
{
    if (Boundary.isTouched() || Layout.isTouched() || Spacing.isTouched() || RowSpacing.isTouched()
        || Margin.isTouched() || Angle.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

bool FillPattern::isBoundary(const App::DocumentObject* obj)
{
    try {
        [[maybe_unused]] const Region region = regionOf(obj, {});
        return true;
    }
    catch (const Standard_Failure&) {
    }
    catch (const Base::Exception&) {
    }
    return false;
}

std::list<gp_Trsf> FillPattern::getTransformations()
{
    if (!Boundary.getValue()) {
        throw Base::ValueError("No boundary specified");
    }
    const Region region = regionOf(Boundary.getValue(), Boundary.getSubValues());

    const double tolerance = Precision::Confusion();
    const double spacing = Spacing.getValue();
    const double rowSpacing = RowSpacing.getValue();
    if (spacing < tolerance || rowSpacing < tolerance) {
        throw Base::ValueError("The copies must be spaced apart");
    }

    const Base::Vector3d centre = centreOfOriginals();
    gp_Pnt start(centre.x, centre.y, centre.z);
    const gp_Vec normal(region.normal);
    start.Translate(-normal * gp_Vec(region.origin, start).Dot(normal));

    const gp_Vec along
        = gp_Vec(region.xDirection)
              .Rotated(gp_Ax1(gp::Origin(), region.normal), Base::toRadians(Angle.getValue()));
    const gp_Vec across = normal.Crossed(along);

    Bnd_Box box;
    TopoDS_Compound edges;
    BRep_Builder builder;
    builder.MakeCompound(edges);
    for (const TopoDS_Face& face : region.faces) {
        BRepBndLib::Add(face, box);
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            builder.Add(edges, it.Current());
        }
    }
    double xMin, yMin, zMin, xMax, yMax, zMax;
    box.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0;
    for (const double x : {xMin, xMax}) {
        for (const double y : {yMin, yMax}) {
            for (const double z : {zMin, zMax}) {
                const gp_Vec corner(start, gp_Pnt(x, y, z));
                uMin = std::min(uMin, corner.Dot(along));
                uMax = std::max(uMax, corner.Dot(along));
                vMin = std::min(vMin, corner.Dot(across));
                vMax = std::max(vMax, corner.Dot(across));
            }
        }
    }

    const auto firstRow = static_cast<long>(std::floor(vMin / rowSpacing));
    const auto lastRow = static_cast<long>(std::ceil(vMax / rowSpacing));
    const auto firstColumn = static_cast<long>(std::floor(uMin / spacing)) - 1;
    const auto lastColumn = static_cast<long>(std::ceil(uMax / spacing)) + 1;
    constexpr double maxPlaces = 100000.0;
    if (double(lastRow - firstRow + 1) * double(lastColumn - firstColumn + 1) > maxPlaces) {
        throw Base::ValueError("The spacing is too small for the boundary");
    }

    const bool staggered = Layout.getValue() == 1;
    const double margin = Margin.getValue();
    const auto fits = [&](const gp_Pnt& point) {
        const bool inside = std::ranges::any_of(region.faces, [&](const TopoDS_Face& face) {
            return BRepClass_FaceClassifier(face, point, tolerance).State() == TopAbs_IN;
        });
        if (!inside || margin < tolerance) {
            return inside;
        }
        BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(), edges);
        return distance.IsDone() && distance.Value() > margin - tolerance;
    };

    std::list<gp_Trsf> transformations {gp_Trsf()};
    for (long row = firstRow; row <= lastRow; ++row) {
        const double shift = staggered && (row & 1) ? spacing / 2 : 0.0;
        for (long column = firstColumn; column <= lastColumn; ++column) {
            if (row == 0 && column == 0) {
                continue;
            }
            const gp_Vec move = along * (column * spacing + shift) + across * (row * rowSpacing);
            if (!fits(start.Translated(move))) {
                continue;
            }
            gp_Trsf trsf;
            trsf.SetTranslation(move);
            transformations.push_back(trsf);
        }
    }
    if (transformations.size() == 1) {
        throw Base::ValueError("No copies fit inside the boundary");
    }
    return transformations;
}
