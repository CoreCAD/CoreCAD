// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>

#include <Base/Tools.h>
#include <Mod/Part/App/FaceMakerCheese.h>

#include "FeatureHole.h"

#include <numbers>

namespace PartDesign
{

static gp_Pnt toPnt(gp_Vec dir)
{
    return {dir.X(), dir.Y(), dir.Z()};
}

void Hole::rotateToNormal(const gp_Dir& helixAxis, const gp_Dir& normalAxis, TopoDS_Shape& helixShape) const
{
    auto getRotationAxis = [](const gp_Dir& dir1, const gp_Dir& dir2, gp_Dir& dir3, double& angle) {
        if (dir1.IsEqual(dir2, Precision::Angular())) {
            return false;
        }

        angle = acos(dir1 * dir2);
        if (dir1.IsOpposite(dir2, Precision::Angular())) {
            // Create a vector that is not parallel to dir1
            gp_XYZ xyz(dir1.XYZ());
            if (fabs(xyz.X()) <= fabs(xyz.Y()) && fabs(xyz.X()) <= fabs(xyz.Z())) {
                xyz.SetX(1.0);
            }
            else if (fabs(xyz.Y()) <= fabs(xyz.X()) && fabs(xyz.Y()) <= fabs(xyz.Z())) {
                xyz.SetY(1.0);
            }
            else {
                xyz.SetZ(1.0);
            }
            dir3 = dir1.Crossed(gp_Dir(xyz));
        }
        else {
            dir3 = dir1.Crossed(dir2);
        }
        return true;
    };
    // rotate the helixAxis so that it is pointing in the normalAxis.
    double angle;
    gp_Dir rotAxis;
    if (getRotationAxis(helixAxis, normalAxis, rotAxis, angle)) {
        gp_Pnt origo(0.0, 0.0, 0.0);
        gp_Trsf mov = helixShape.Location().Transformation();
        mov.SetRotation(gp_Ax1(origo, rotAxis), angle);
        TopLoc_Location loc2(mov);
        helixShape.Move(loc2);
    }
}

gp_Vec Hole::computePerpendicular(const gp_Vec& zDir) const
{
    // Define xDir
    gp_Vec xDir;

    /* Compute xDir normal to zDir */
    if (std::abs(zDir.Z() - zDir.X()) > Precision::Confusion()) {
        xDir = gp_Vec(zDir.Z(), 0, -zDir.X());
    }
    else if (std::abs(zDir.Z() - zDir.Y()) > Precision::Confusion()) {
        xDir = gp_Vec(zDir.Y(), -zDir.X(), 0);
    }
    else {
        xDir = gp_Vec(0, -zDir.Z(), zDir.Y());
    }

    // Normalize xDir; this is needed as the computation above does not necessarily give
    // a unit-length vector.
    xDir.Normalize();
    return xDir;
}

TopoDS_Shape Hole::makeThread(const gp_Vec& xDir, const gp_Vec& zDir, double length)
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread type out of range"));
    }
    if (threadSize < 0) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread size out of range"));
    }

    bool leftHanded = (bool)ThreadDirection.getValue();

    // Nomenclature and formulae according to Figure 1 of ISO 68-1
    // this is the same for all metric and UTS threads as stated here:
    // https://en.wikipedia.org/wiki/File:ISO_and_UTS_Thread_Dimensions.svg
    // Rmaj is half of the major diameter
    double Rmaj = threadDescription[threadType][threadSize].diameter / 2;
    double Pitch = getThreadPitch();

    double clearance;  // clearance to be added on the diameter
    if (UseCustomThreadClearance.getValue()) {
        clearance = CustomThreadClearance.getValue() / 2;
    }
    else {
        clearance = getThreadClassClearance() / 2;
    }
    double RmajC = Rmaj + clearance;
    double marginZ = 0.001;

    BRepBuilderAPI_MakeWire mkThreadWire;
    double H;
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "BSP" || threadTypeStr == "BSW" || threadTypeStr == "BSF") {
        H = 0.960491 * Pitch;              // Height of Sharp V
        double radius = 0.137329 * Pitch;  // radius of the crest
        // construct the cross section going counter-clockwise
        // --------------
        // P    | p4
        // 5/8P |                p3
        //      |                         crest
        // 3/8P |                p2
        // 0    | p1
        // --------------
        //      | base-sharpV             Rmaj     H

        // the little adjustment of p1 and p4 is here to prevent coincidencies
        double marginX = std::tan(Base::toRadians(62.5)) * marginZ;

        gp_Pnt p1 = toPnt((RmajC - 5 * H / 6 + marginX) * xDir + marginZ * zDir);
        gp_Pnt p4 = toPnt((RmajC - 5 * H / 6 + marginX) * xDir + (Pitch - marginZ) * zDir);

        // Calculate positions for p2 and p3
        double p23x = RmajC - radius * 0.58284013094;

        gp_Pnt p2 = toPnt(p23x * xDir + 3 * Pitch / 8 * zDir);
        gp_Pnt p3 = toPnt(p23x * xDir + 5 * Pitch / 8 * zDir);
        gp_Pnt crest = toPnt((RmajC)*xDir + Pitch / 2 * zDir);

        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        Handle(Geom_TrimmedCurve) arc1 = GC_MakeArcOfCircle(p2, crest, p3).Value();
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(arc1).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p4, p1).Edge());
    }
    else {
        H = sqrt(3) / 2 * Pitch;  // height of fundamental triangle
        double h = 7 * H / 8;     // distance from Rmaj to the base
        // construct the cross section going counter-clockwise
        // pitch
        // --------------
        // P     | p4
        // 9/16P |                p3
        // 7/16P |                p2
        // 0     | p1
        // --------------
        //       | base-sharpV    Rmaj

        // the little adjustment of p1 and p4 is here to prevent coincidencies
        double marginX = std::tan(Base::toRadians(60.0)) * marginZ;
        gp_Pnt p1 = toPnt((RmajC - h + marginX) * xDir + marginZ * zDir);
        gp_Pnt p2 = toPnt((RmajC)*xDir + 7 * Pitch / 16 * zDir);
        gp_Pnt p3 = toPnt((RmajC)*xDir + 9 * Pitch / 16 * zDir);
        gp_Pnt p4 = toPnt((RmajC - h + marginX) * xDir + (Pitch - marginZ) * zDir);

        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
        if (threadTypeStr == "ISOTyre") {
            gp_Pnt crest = toPnt((RmajC + (Pitch / 32)) * xDir + Pitch / 2 * zDir);
            Handle(Geom_TrimmedCurve) arc1 = GC_MakeArcOfCircle(p2, crest, p3).Value();
            mkThreadWire.Add(BRepBuilderAPI_MakeEdge(arc1).Edge());
        }
        else {
            mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p2, p3).Edge());
        }
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
        mkThreadWire.Add(BRepBuilderAPI_MakeEdge(p4, p1).Edge());
    }

    mkThreadWire.Build();
    TopoDS_Wire threadWire = mkThreadWire.Wire();

    // create the helix path
    double threadDepth = ThreadDepth.getValue();
    double helixLength = threadDepth + Pitch / 2;
    double holeDepth = Depth.getValue();
    std::string threadDepthMethod(ThreadDepthType.getValueAsString());
    std::string depthMethod(DepthType.getValueAsString());
    if (threadDepthMethod != "Dimension") {
        if (depthMethod == "ThroughAll") {
            threadDepth = length;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + 2 * Pitch;
        }
        else if (threadDepthMethod == "Tapped (DIN76)") {
            threadDepth = holeDepth - getThreadRunout();
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 2;
        }
        else {  // Hole depth
            threadDepth = holeDepth;
            ThreadDepth.setValue(threadDepth);
            helixLength = threadDepth + Pitch / 8;
        }
    }
    else {
        if (depthMethod == "Dimension") {
            // the thread must not be deeper than the hole
            // thus the max helixLength is holeDepth + P / 8;
            if (threadDepth > (holeDepth - Pitch / 2)) {
                helixLength = holeDepth + Pitch / 8;
            }
        }
    }
    double helixAngle = Tapered.getValue() ? TaperedAngle.getValue() - 90 : 0.0;
    TopoDS_Shape helix = TopoShape().makeLongHelix(Pitch, helixLength, Rmaj, helixAngle, leftHanded);

    gp_Pnt origo(0.0, 0.0, 0.0);
    gp_Dir dir_axis1(0.0, 0.0, 1.0);  // pointing along the helix axis, as created.
    gp_Dir dir_axis2(1.0, 0.0, 0.0);  // pointing towards the helix start point, as created.

    // Reverse the direction of the helix. So that it goes into the material
    gp_Trsf mov;
    mov.SetRotation(gp_Ax1(origo, dir_axis2), std::numbers::pi);
    TopLoc_Location loc1(mov);
    helix.Move(loc1);

    // rotate the helix so that it is pointing in the zdir.
    rotateToNormal(dir_axis1, zDir, helix);

    // create the pipe shell
    BRepOffsetAPI_MakePipeShell mkPS(TopoDS::Wire(helix));
    mkPS.SetTolerance(Precision::Confusion());
    mkPS.SetTransitionMode(BRepBuilderAPI_Transformed);
    mkPS.SetMode(true);  // This is for frenet
    mkPS.Add(threadWire);
    if (!mkPS.IsReady()) {
        throw Base::CADKernelError(QT_TRANSLATE_NOOP("Exception", "Error: Thread could not be built"));
    }
    TopoDS_Shape shell = mkPS.Shape();

    // create faces at the ends of the pipe shell
    TopTools_ListOfShape sim;
    mkPS.Simulate(2, sim);
    std::vector<TopoDS_Wire> frontwires, backwires;
    frontwires.push_back(TopoDS::Wire(sim.First()));
    backwires.push_back(TopoDS::Wire(sim.Last()));
    // build the end faces
    TopoDS_Shape front = Part::FaceMakerCheese::makeFace(frontwires);
    TopoDS_Shape back = Part::FaceMakerCheese::makeFace(backwires);

    // sew the shell and end faces
    BRepBuilderAPI_Sewing sewer;
    sewer.SetTolerance(Precision::Confusion());
    sewer.Add(front);
    sewer.Add(back);
    sewer.Add(shell);
    sewer.Perform();

    // make the closed off shell into a solid
    BRepBuilderAPI_MakeSolid mkSolid;
    mkSolid.Add(TopoDS::Shell(sewer.SewedShape()));
    if (!mkSolid.IsDone()) {
        throw Base::CADKernelError(QT_TRANSLATE_NOOP("Exception", "Error: Result is not a solid"));
    }
    TopoDS_Shape result = mkSolid.Shape();

    // check if the algorithm has confused the inside and outside of the solid
    BRepClass3d_SolidClassifier SC(result);
    SC.PerformInfinitePoint(Precision::Confusion());
    if (SC.State() == TopAbs_IN) {
        result.Reverse();
    }

    // we are done
    return result;
}

}  // namespace PartDesign
