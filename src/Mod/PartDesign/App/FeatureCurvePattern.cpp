// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <cmath>
#include <limits>
#include <optional>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepLib_FindSurface.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <Geom_Plane.hxx>
#include <Precision.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Quaternion.hxx>

#include <Base/Exception.h>
#include <Mod/Part/App/PartFeature.h>

#include "FeatureCurvePattern.h"

using namespace PartDesign;

namespace
{

/// One connected run of edges, measured by length from its start.
class PathCurve
{
public:
    explicit PathCurve(const std::vector<TopoDS_Edge>& edges)
    {
        Handle(TopTools_HSequenceOfShape) loose = new TopTools_HSequenceOfShape;
        for (const TopoDS_Edge& edge : edges) {
            loose->Append(edge);
        }
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(loose, Precision::Confusion(), false, wires);
        if (wires.IsNull() || wires->Length() != 1) {
            throw Base::ValueError("The path must be one connected curve");
        }
        const TopoDS_Wire wire = TopoDS::Wire(wires->Value(1));

        for (BRepTools_WireExplorer it(wire); it.More(); it.Next()) {
            Segment segment {
                BRepAdaptor_Curve(it.Current()),
                it.Current().Orientation() == TopAbs_REVERSED,
                total,
                0.0,
                it.Current()
            };
            segment.length = GCPnts_AbscissaPoint::Length(segment.curve);
            total += segment.length;
            segments.push_back(std::move(segment));
        }
        if (segments.empty() || total < Precision::Confusion()) {
            throw Base::ValueError("The path has no length");
        }

        gp_Pnt start;
        gp_Pnt end;
        gp_Vec tangent;
        at(0.0, start, tangent);
        at(total, end, tangent);
        closed = start.Distance(end) < Precision::Confusion();

        BRepLib_FindSurface plane(wire, Precision::Confusion(), /*OnlyPlane=*/true);
        if (plane.Found()) {
            normal = Handle(Geom_Plane)::DownCast(plane.Surface())->Axis().Direction();
        }
    }

    double length() const
    {
        return total;
    }

    bool isClosed() const
    {
        return closed;
    }

    const std::optional<gp_Dir>& planeNormal() const
    {
        return normal;
    }

    /// How far along the path the point nearest @p point lies.
    double lengthTo(const gp_Pnt& point) const
    {
        const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(point);
        double nearest = std::numeric_limits<double>::max();
        double along = 0.0;
        for (const Segment& segment : segments) {
            BRepExtrema_DistShapeShape distance(vertex, segment.edge);
            if (!distance.IsDone() || distance.NbSolution() == 0 || distance.Value() >= nearest) {
                continue;
            }
            nearest = distance.Value();
            const double first = segment.curve.FirstParameter();
            const double last = segment.curve.LastParameter();
            double u = first;
            if (distance.SupportTypeShape2(1) == BRepExtrema_IsOnEdge) {
                distance.ParOnEdgeS2(1, u);
            }
            else if (
                distance.PointOnShape2(1).Distance(segment.curve.Value(last))
                < distance.PointOnShape2(1).Distance(segment.curve.Value(first))
            ) {
                u = last;
            }
            along = segment.start
                + (segment.reversed ? GCPnts_AbscissaPoint::Length(segment.curve, u, last)
                                    : GCPnts_AbscissaPoint::Length(segment.curve, first, u));
        }
        return along;
    }

    /// The point @p along the path from its start, and the path's direction there.
    void at(double along, gp_Pnt& point, gp_Vec& tangent) const
    {
        auto segment = segments.begin();
        while (segment + 1 != segments.end() && along > segment->start + segment->length) {
            ++segment;
        }
        const double local = along - segment->start;
        const double u = segment->reversed
            ? GCPnts_AbscissaPoint(segment->curve, -local, segment->curve.LastParameter()).Parameter()
            : GCPnts_AbscissaPoint(segment->curve, local, segment->curve.FirstParameter()).Parameter();
        segment->curve.D1(u, point, tangent);
        if (segment->reversed) {
            tangent.Reverse();
        }
        if (tangent.Magnitude() < Precision::Confusion()) {
            throw Base::ValueError("The path has no direction at a copy");
        }
        tangent.Normalize();
    }

private:
    struct Segment
    {
        BRepAdaptor_Curve curve;
        bool reversed;
        double start;
        double length;
        TopoDS_Edge edge;
    };

    std::vector<Segment> segments;
    double total = 0.0;
    bool closed = false;
    std::optional<gp_Dir> normal;
};

/// The named edges of @p obj, or all its edges when none are named, in the world.
std::vector<TopoDS_Edge> edgesOf(const App::DocumentObject* obj, const std::vector<std::string>& subs)
{
    constexpr auto options = Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform;
    std::vector<TopoDS_Edge> edges;
    const auto collect = [&edges](const TopoDS_Shape& shape) {
        for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
            edges.push_back(TopoDS::Edge(it.Current()));
        }
    };

    const bool whole = std::ranges::all_of(subs, &std::string::empty);
    if (whole) {
        collect(Part::Feature::getTopoShape(obj, options).getShape());
    }
    else {
        for (const std::string& sub : subs) {
            const TopoDS_Shape shape
                = Part::Feature::getTopoShape(obj, options, sub.c_str()).getShape();
            if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) {
                throw Base::ValueError("The path must be made of edges");
            }
            collect(shape);
        }
    }
    if (edges.empty()) {
        throw Base::ValueError("The path has no edges");
    }
    return edges;
}

}  // namespace


PROPERTY_SOURCE(PartDesign::CurvePattern, PartDesign::Transformed)

const char* CurvePattern::ModeEnums[] = {"Extent", "Spacing", nullptr};

const App::PropertyIntegerConstraint::Constraints CurvePattern::intOccurrences
    = {1, std::numeric_limits<int>::max(), 1};

CurvePattern::CurvePattern()
{
    ADD_PROPERTY_TYPE(
        Path,
        (nullptr),
        "CurvePattern",
        App::Prop_None,
        "The connected edges the copies are spaced along, or a sketch to use all of."
    );
    ADD_PROPERTY_TYPE(
        Reversed,
        (false),
        "CurvePattern",
        App::Prop_None,
        "Run the copies the other way along the path"
    );
    ADD_PROPERTY_TYPE(
        Mode,
        (0L),
        "CurvePattern",
        App::Prop_None,
        "Selects how the pattern is dimensioned.\n'Extent': Uses the length along the path from "
        "the first to the last instance.\n'Spacing': Uses the length along the path between "
        "consecutive instances."
    );
    ADD_PROPERTY_TYPE(
        Length,
        (100.0),
        "CurvePattern",
        App::Prop_None,
        "The length along the path from the first to the last instance. On a closed path, the "
        "whole length spaces the instances evenly around it."
    );
    ADD_PROPERTY_TYPE(
        Offset,
        (10.0),
        "CurvePattern",
        App::Prop_None,
        "The length along the path between consecutive instances."
    );
    ADD_PROPERTY_TYPE(
        Occurrences,
        (2),
        "CurvePattern",
        App::Prop_None,
        "The total number of instances, including the original feature."
    );
    ADD_PROPERTY_TYPE(
        KeepOrientation,
        (false),
        "CurvePattern",
        App::Prop_None,
        "Keep every instance facing the way the original does, instead of turning with the path"
    );
    Occurrences.setConstraints(&intOccurrences);
    Mode.setEnums(ModeEnums);
    Offset.setReadOnly(true);
}

void CurvePattern::onChanged(const App::Property* prop)
{
    if (prop == &Mode) {
        const bool extent = Mode.getValue() == 0;
        Length.setReadOnly(!extent);
        Offset.setReadOnly(extent);
    }
    Transformed::onChanged(prop);
}

short CurvePattern::mustExecute() const
{
    if (Path.isTouched() || Reversed.isTouched() || Mode.isTouched() || Length.isTouched()
        || Offset.isTouched() || Occurrences.isTouched() || KeepOrientation.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

bool CurvePattern::isPath(const App::DocumentObject* obj)
{
    try {
        [[maybe_unused]] const PathCurve path(edgesOf(obj, {}));
        return true;
    }
    catch (const Standard_Failure&) {
    }
    catch (const Base::Exception&) {
    }
    return false;
}

static PathCurve pathOf(const App::PropertyLinkSub& path)
{
    if (!path.getValue()) {
        throw Base::ValueError("No path specified");
    }
    return PathCurve(edgesOf(path.getValue(), path.getSubValues()));
}

double CurvePattern::lengthAhead()
{
    const PathCurve path = pathOf(Path);
    if (path.isClosed()) {
        return path.length();
    }
    const Base::Vector3d centre = centreOfOriginals();
    const double start = path.lengthTo(gp_Pnt(centre.x, centre.y, centre.z));
    return Reversed.getValue() ? start : path.length() - start;
}

std::list<gp_Trsf> CurvePattern::getTransformations()
{
    const int occurrences = Occurrences.getValue();
    std::list<gp_Trsf> transformations {gp_Trsf()};
    if (occurrences < 2) {
        return transformations;
    }

    const PathCurve path = pathOf(Path);
    const double tolerance = Precision::Confusion();

    double step = Offset.getValue();
    if (Mode.getValue() == 0) {
        const bool allAround = path.isClosed() && Length.getValue() > path.length() - tolerance;
        step = allAround ? path.length() / occurrences : Length.getValue() / (occurrences - 1);
    }
    if (step < tolerance) {
        throw Base::ValueError("The copies must be spaced apart along the path");
    }
    if (Reversed.getValue()) {
        step = -step;
    }

    const Base::Vector3d centre = centreOfOriginals();
    const double start = path.lengthTo(gp_Pnt(centre.x, centre.y, centre.z));
    gp_Pnt startPoint;
    gp_Vec startTangent;
    path.at(start, startPoint, startTangent);

    gp_Trsf toOrigin;
    toOrigin.SetTranslation(gp_Vec(startPoint, gp::Origin()));

    for (int i = 1; i < occurrences; ++i) {
        double along = start + i * step;
        if (path.isClosed()) {
            along = std::fmod(along, path.length());
            if (along < 0.0) {
                along += path.length();
            }
        }
        else if (along < -tolerance || along > path.length() + tolerance) {
            throw Base::ValueError("The copies run past the end of the path");
        }

        gp_Pnt point;
        gp_Vec tangent;
        path.at(along, point, tangent);

        gp_Trsf turn;
        if (!KeepOrientation.getValue()) {
            if (const auto& normal = path.planeNormal()) {
                turn.SetRotation(
                    gp_Ax1(gp::Origin(), *normal),
                    startTangent.AngleWithRef(tangent, gp_Vec(*normal))
                );
            }
            else {
                turn.SetRotation(gp_Quaternion(startTangent, tangent));
            }
        }
        gp_Trsf toPoint;
        toPoint.SetTranslation(gp_Vec(gp::Origin(), point));
        transformations.push_back(toPoint * turn * toOrigin);
    }
    return transformations;
}
