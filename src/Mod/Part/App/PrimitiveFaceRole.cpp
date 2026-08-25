// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <array>
# include <string>

# include <BRepAdaptor_Surface.hxx>
# include <BRepGProp.hxx>
# include <GProp_GProps.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Shape.hxx>
# include <gp_Pnt.hxx>
# include <gp_Trsf.hxx>
# include <gp_Vec.hxx>
#endif

#include "PrimitiveFaceRole.h"

namespace Part
{

namespace
{

// The six axis roles of a box, paired with the local-frame unit direction each
// names. Order is irrelevant; the dominant-component match below selects one.
constexpr std::array<std::pair<const char*, std::array<double, 3>>, 6> boxAxes = {{
    {"+X", {1.0, 0.0, 0.0}},
    {"-X", {-1.0, 0.0, 0.0}},
    {"+Y", {0.0, 1.0, 0.0}},
    {"-Y", {0.0, -1.0, 0.0}},
    {"+Z", {0.0, 0.0, 1.0}},
    {"-Z", {0.0, 0.0, -1.0}},
}};

gp_Pnt centreOfMass(const TopoDS_Shape& shape, bool linearFace)
{
    GProp_GProps props;
    if (linearFace) {
        BRepGProp::SurfaceProperties(shape, props);
    }
    else {
        BRepGProp::VolumeProperties(shape, props);
    }
    return props.CentreOfMass();
}

}  // namespace

std::string primitiveBoxFaceRole(
    const TopoDS_Shape& face,
    const TopoDS_Shape& solid,
    const gp_Trsf& localToWorld
)
{
    if (face.IsNull() || face.ShapeType() != TopAbs_FACE || solid.IsNull()) {
        return {};
    }

    BRepAdaptor_Surface surf(TopoDS::Face(face));
    if (surf.GetType() != GeomAbs_Plane) {
        return {};  // a box face is planar; anything else is not this regime's concern
    }

    // Which side of the box centre this face sits on, read in the local frame.
    // Both centroids are in world; their difference cancels the placement's
    // translation, and the rotation names the axis. Centroids carry through a
    // rigid motion exactly, so this is invariant to how the feature is posed.
    const gp_Pnt faceCentre = centreOfMass(face, /*linearFace*/ true);
    const gp_Pnt solidCentre = centreOfMass(solid, /*linearFace*/ false);
    gp_Vec outward(solidCentre, faceCentre);
    outward.Transform(localToWorld.Inverted());

    const char* best = "";
    double bestDot = 0.0;
    for (const auto& [name, axis] : boxAxes) {
        const double dot = outward.X() * axis[0] + outward.Y() * axis[1] + outward.Z() * axis[2];
        if (dot > bestDot) {
            bestDot = dot;
            best = name;
        }
    }
    return best;
}

}  // namespace Part
