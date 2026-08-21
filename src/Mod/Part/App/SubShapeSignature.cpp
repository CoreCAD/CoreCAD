// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cmath>
# include <string>

# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <BRepGProp.hxx>
# include <BRepLProp_SLProps.hxx>
# include <BRep_Tool.hxx>
# include <GProp_GProps.hxx>
# include <Precision.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Shape.hxx>
# include <TopoDS_Vertex.hxx>
# include <gp_Dir.hxx>
# include <gp_Pnt.hxx>
#endif

#include "SubShapeSignature.h"

namespace Part
{

namespace
{

// Tolerances validated to give zero collisions on a maximally symmetric solid
// (a cube's six faces and twelve edges) while absorbing recompute noise. The
// architecture commits to the *approach*, not a fixed tolerance schedule; these
// are the first, provable values, refined when the import path is tuned against
// real files.
constexpr double posTol = 1e-4;   // positions and lengths (document units)
constexpr double dirTol = 1e-3;   // unit-direction components
constexpr double areaTol = 1e-4;  // surface area

// Quantize to a tolerance bucket so equal-within-tolerance values compare equal.
// Output is an integer, so the rendered signature carries no locale-dependent
// decimal formatting.
long long quant(double value, double tol)
{
    return std::llround(value / tol);
}

std::string triple(double x, double y, double z, double tol)
{
    return std::to_string(quant(x, tol)) + ',' + std::to_string(quant(y, tol)) + ','
        + std::to_string(quant(z, tol));
}

char surfaceCode(GeomAbs_SurfaceType type)
{
    switch (type) {
        case GeomAbs_Plane:
            return 'P';
        case GeomAbs_Cylinder:
            return 'Y';
        case GeomAbs_Cone:
            return 'O';
        case GeomAbs_Sphere:
            return 'S';
        case GeomAbs_Torus:
            return 'T';
        case GeomAbs_BezierSurface:
        case GeomAbs_BSplineSurface:
            return 'B';
        case GeomAbs_SurfaceOfRevolution:
            return 'R';
        case GeomAbs_SurfaceOfExtrusion:
            return 'E';
        default:
            return 'X';
    }
}

char curveCode(GeomAbs_CurveType type)
{
    switch (type) {
        case GeomAbs_Line:
            return 'L';
        case GeomAbs_Circle:
            return 'C';
        case GeomAbs_Ellipse:
            return 'E';
        case GeomAbs_Hyperbola:
            return 'H';
        case GeomAbs_Parabola:
            return 'P';
        case GeomAbs_BezierCurve:
        case GeomAbs_BSplineCurve:
            return 'B';
        default:
            return 'X';
    }
}

std::string faceSignature(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    const gp_Pnt centre = props.CentreOfMass();

    BRepAdaptor_Surface surf(face);
    const char code = surfaceCode(surf.GetType());

    // Orientation at the parametric midpoint, flipped to the face's own sense so
    // two coincident faces of opposite orientation are distinguished.
    std::string normalPart = "-";
    const double uMid = 0.5 * (surf.FirstUParameter() + surf.LastUParameter());
    const double vMid = 0.5 * (surf.FirstVParameter() + surf.LastVParameter());
    BRepLProp_SLProps slProps(surf, uMid, vMid, 1, Precision::Confusion());
    if (slProps.IsNormalDefined()) {
        gp_Dir normal = slProps.Normal();
        if (face.Orientation() == TopAbs_REVERSED) {
            normal.Reverse();
        }
        normalPart = triple(normal.X(), normal.Y(), normal.Z(), dirTol);
    }

    return std::string("F") + code + ':' + triple(centre.X(), centre.Y(), centre.Z(), posTol) + ':'
        + normalPart + ':' + std::to_string(quant(props.Mass(), areaTol));
}

std::string edgeSignature(const TopoDS_Edge& edge)
{
    GProp_GProps props;
    BRepGProp::LinearProperties(edge, props);
    const gp_Pnt centre = props.CentreOfMass();

    BRepAdaptor_Curve curve(edge);
    const char code = curveCode(curve.GetType());

    return std::string("E") + code + ':' + triple(centre.X(), centre.Y(), centre.Z(), posTol) + ':'
        + std::to_string(quant(props.Mass(), posTol));
}

std::string vertexSignature(const TopoDS_Vertex& vertex)
{
    const gp_Pnt point = BRep_Tool::Pnt(vertex);
    return std::string("V:") + triple(point.X(), point.Y(), point.Z(), posTol);
}

}  // namespace

std::string subShapeSignature(const TopoDS_Shape& sub)
{
    if (sub.IsNull()) {
        return {};
    }
    switch (sub.ShapeType()) {
        case TopAbs_FACE:
            return faceSignature(TopoDS::Face(sub));
        case TopAbs_EDGE:
            return edgeSignature(TopoDS::Edge(sub));
        case TopAbs_VERTEX:
            return vertexSignature(TopoDS::Vertex(sub));
        default:
            return {};
    }
}

}  // namespace Part
