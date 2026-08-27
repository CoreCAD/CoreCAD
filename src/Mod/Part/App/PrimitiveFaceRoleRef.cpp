// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <string>

# include <TopLoc_Location.hxx>
# include <TopoDS_Shape.hxx>
# include <gp_Trsf.hxx>
#endif

#include "PrimitiveFaceRoleRef.h"
#include "PartFeature.h"
#include "PrimitiveFaceRole.h"
#include "TopoShape.h"

namespace Part
{

std::string capturePrimitiveFaceRole(const Feature& feature, const std::string& subName)
{
    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull() || subName.empty()) {
        return {};
    }

    const TopoDS_Shape localFace = stored.getSubShape(subName.c_str(), /*silent*/ true);
    if (localFace.IsNull()) {
        return {};
    }

    // Lift shape and face into the feature's world frame, and read the role in the
    // placement frame that lift carries -- the same world-frame contract the
    // primitiveFaceRole compute seam was proven against.
    const TopLoc_Location loc = feature.getLocation();
    const TopoDS_Shape worldSolid = stored.getShape().Moved(loc);
    const TopoDS_Shape worldFace = localFace.Moved(loc);
    const gp_Trsf localToWorld = worldSolid.Location().Transformation();

    return primitiveFaceRole(worldFace, worldSolid, localToWorld);
}

std::string resolvePrimitiveFaceRole(const Feature& feature, const std::string& role)
{
    const TopoShape& stored = feature.Shape.getShape();
    if (stored.isNull() || role.empty()) {
        return {};
    }

    const TopLoc_Location loc = feature.getLocation();
    const TopoDS_Shape worldSolid = stored.getShape().Moved(loc);
    const gp_Trsf localToWorld = worldSolid.Location().Transformation();

    const TopoDS_Shape worldFace = resolveFaceByRole(worldSolid, localToWorld, role);
    if (worldFace.IsNull()) {
        return {};
    }

    // Map the resolved face back to the positional sub-name the kernel presently
    // gives it. The world faces share topology with the stored faces (Moved keeps
    // the same TShape), so IsSame against each stored face lifted by the same
    // location finds the ordinal getSubShape would resolve.
    const int faceCount = static_cast<int>(stored.countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= faceCount; ++i) {
        const TopoDS_Shape localFace = stored.getSubShape(TopAbs_FACE, i, /*silent*/ true);
        if (!localFace.IsNull() && localFace.Moved(loc).IsSame(worldFace)) {
            return "Face" + std::to_string(i);
        }
    }
    return {};
}

}  // namespace Part
