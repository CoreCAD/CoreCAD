// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2023 Ondsel <development@ondsel.com>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#pragma once

#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>

#include <Mod/Assembly/AssemblyGlobal.h>

#include <App/FeaturePython.h>
#include <App/Part.h>

#include <Base/Vector3D.h>

#include <cstddef>
#include <functional>
#include <utility>

namespace App
{
class DocumentObject;
}  // namespace App

namespace Base
{
class Placement;
}  // namespace Base

namespace Assembly
{

// This enum has to be the same as the one in JointObject.py
enum class JointType
{
    Fixed,
    Revolute,
    Cylindrical,
    Slider,
    Ball,
    Distance,
    Parallel,
    Perpendicular,
    Angle,
    RackPinion,
    Screw,
    Gears,
    Belt,
};

enum class GeometryType
{
    Point = 0,

    // Edges
    Line = 1,
    Curve = 2,
    Circle = 3,

    // Faces
    Place = 4,
    Cylinder = 5,
    Sphere = 6,
    Cone = 7,
    Torus = 8,
};

enum class DistanceType
{
    PointPoint,

    LineLine,
    LineCircle,
    CircleCircle,

    PlanePlane,
    PlaneCylinder,
    PlaneSphere,
    PlaneCone,
    PlaneTorus,
    CylinderCylinder,
    CylinderSphere,
    CylinderCone,
    CylinderTorus,
    ConeCone,
    ConeTorus,
    ConeSphere,
    TorusTorus,
    TorusSphere,
    SphereSphere,

    PointPlane,
    PointCylinder,
    PointSphere,
    PointCone,
    PointTorus,

    LinePlane,
    LineCylinder,
    LineSphere,
    LineCone,
    LineTorus,

    CurvePlane,
    CurveCylinder,
    CurveSphere,
    CurveCone,
    CurveTorus,

    PointLine,
    PointCurve,

    Other,
};

class AssemblyObject;
class JointGroup;

AssemblyExport void swapJCS(const App::DocumentObject* joint);

AssemblyExport bool isEdgeType(
    const App::DocumentObject* obj,
    const std::string& elName,
    const GeomAbs_CurveType type
);
AssemblyExport bool isFaceType(
    const App::DocumentObject* obj,
    const std::string& elName,
    const GeomAbs_SurfaceType type
);
AssemblyExport double getFaceRadius(const App::DocumentObject* obj, const std::string& elName);
AssemblyExport double getEdgeRadius(const App::DocumentObject* obj, const std::string& elName);

AssemblyExport DistanceType getDistanceType(App::DocumentObject* joint);
AssemblyExport JointGroup* getJointGroup(const App::DocumentObject* assemblyOrLink);

// The assembly an object belongs to, found by walking up through whatever containers
// stand between (a simulation sits in a SimulationGroup, an exploded view in a
// ViewGroup, and so on). Depth-bounded so a cycle cannot hang the search.
//
// The Python originals each looked only ONE level up the InList and so answered
// "none" for anything not held by the assembly directly.
AssemblyExport AssemblyObject* getOwningAssembly(const App::DocumentObject* obj);

AssemblyExport std::vector<App::DocumentObject*> getAssemblyComponents(const AssemblyObject* assembly);

// getters to get from properties
AssemblyExport void setJointActivated(const App::DocumentObject* joint, bool val);
AssemblyExport bool getJointActivated(const App::DocumentObject* joint);
AssemblyExport double getJointAngle(const App::DocumentObject* joint);
AssemblyExport double getJointDistance(const App::DocumentObject* joint);
AssemblyExport double getJointDistance2(const App::DocumentObject* joint);
AssemblyExport JointType getJointType(const App::DocumentObject* joint);
AssemblyExport std::string getElementFromProp(const App::DocumentObject* obj, const char* propName);
AssemblyExport std::string getElementTypeFromProp(const App::DocumentObject* obj, const char* propName);
AssemblyExport App::DocumentObject* getObjFromProp(
    const App::DocumentObject* joint,
    const char* propName
);
AssemblyExport App::DocumentObject* getObjFromRef(App::DocumentObject* obj, const std::string& sub);
AssemblyExport App::DocumentObject* getObjFromRef(const App::PropertyXLinkSub* prop);
AssemblyExport App::DocumentObject* getObjFromJointRef(
    const App::DocumentObject* joint,
    const char* propName
);
AssemblyExport App::DocumentObject* getLinkedObjFromRef(
    const App::DocumentObject* joint,
    const char* propName
);
// Get the moving part from a selection, which has the full path.
AssemblyExport App::DocumentObject* getMovingPartFromSel(
    const AssemblyObject* assemblyObject,
    App::DocumentObject* obj,
    const std::string& sub
);
AssemblyExport App::DocumentObject* getMovingPartFromRef(const App::PropertyXLinkSub* prop);
AssemblyExport App::DocumentObject* getMovingPartFromRef(App::DocumentObject* joint, const char* pName);
AssemblyExport std::vector<std::string> getSubAsList(const App::PropertyXLinkSub* prop);
AssemblyExport std::vector<std::string> getSubAsList(
    const App::DocumentObject* joint,
    const char* propName
);
// ============================== Reference validity ===============================

// Whether a stored reference can be acted on: it names an object, carries at least
// minSubs sub-elements, and the topological-naming layer has not marked its first
// sub-element broken (a "?" in the name). A reference that cannot say what it points
// at is not a reason to guess -- callers do nothing rather than act on the wrong
// sub-shape (P7). Ports UtilsAssembly.isRefValid.
AssemblyExport bool isRefValid(const App::PropertyXLinkSub* prop, std::size_t minSubs = 1);

// ============================== Extent and centre ================================

// World-frame centre of an object's bounding box, taken from its SHAPE.
//
// The Python original read this from the object's ViewObject, which made an
// assembly's extent unavailable headless and put a data-layer computation on top of
// the view layer. A shape knows its own extent, so the dependency now points the
// right way (three-level split: need points downward only). Returns the origin for an
// object with no shape.
AssemblyExport Base::Vector3d getGlobalBoundBoxCenter(const App::DocumentObject* obj);

// Centre and overall size of an assembly, from the shapes of its components. Used to
// scale a radial explosion. Falls back to (origin, 100) for an assembly whose extent
// cannot be determined, matching the former Python behaviour.
AssemblyExport std::pair<Base::Vector3d, double> getComAndSize(const AssemblyObject* assembly);

AssemblyExport void syncPlacements(App::DocumentObject* src, App::DocumentObject* to);
AssemblyExport double getJointCurrentValue(App::DocumentObject* joint, bool isAngle);

// Local coordinate system a joint connector sits at, for a reference to a sub-shape.
// Split internally into an identity-resolution boundary and pure geometry (see the
// .cpp). Ports UtilsAssembly.findPlacement (#59). The raw (obj, subs) overload is the
// primitive; the PropertyXLinkSub overload unwraps a stored reference onto it.
AssemblyExport Base::Placement findPlacement(
    App::DocumentObject* obj,
    const std::vector<std::string>& subs,
    bool ignoreVertex
);
AssemblyExport Base::Placement findPlacement(const App::PropertyXLinkSub* ref, bool ignoreVertex);

// Redraw a joint's frames after something has moved: a frame's position depends on
// where the component is, not only on what the joint stores, so a property change on
// the joint alone does not cover it.
//
// The App layer must not depend on the view layer, so it holds no more than a
// handler the Gui module registers at start-up. Headless nothing registers one and
// the call does nothing.
using JointRedrawHandler = std::function<void(App::DocumentObject*)>;
AssemblyExport void setJointRedrawHandler(JointRedrawHandler handler);
AssemblyExport void redrawJointViewProvider(App::DocumentObject* joint);

}  // namespace Assembly
