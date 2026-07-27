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

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakePlane.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Conic.hxx>
#include <Geom_ElementarySurface.hxx>
#include <Geom_Plane.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>
#include <Precision.hxx>


#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <App/Link.h>

#include <Base/Console.h>
#include <Base/Placement.h>
#include <Base/Tools.h>
#include <Base/Interpreter.h>

#include <cmath>
#include <set>

#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/PartDesign/App/Body.h>

#include "AssemblyUtils.h"
#include "AssemblyObject.h"
#include "AssemblyLink.h"

#include "JointGroup.h"


namespace PartApp = Part;

// ======================================= Utils ======================================
namespace Assembly
{

void swapJCS(const App::DocumentObject* joint)
{
    if (!joint) {
        return;
    }

    auto pPlc1 = joint->getPropertyByName<App::PropertyPlacement>("Placement1");
    auto pPlc2 = joint->getPropertyByName<App::PropertyPlacement>("Placement2");
    if (pPlc1 && pPlc2) {
        const auto temp = pPlc1->getValue();
        pPlc1->setValue(pPlc2->getValue());
        pPlc2->setValue(temp);
    }
    auto pRef1 = joint->getPropertyByName<App::PropertyXLinkSub>("Reference1");
    auto pRef2 = joint->getPropertyByName<App::PropertyXLinkSub>("Reference2");
    if (pRef1 && pRef2) {
        auto temp = pRef1->getValue();
        auto subs1 = pRef1->getSubValues();
        auto subs2 = pRef2->getSubValues();
        pRef1->setValue(pRef2->getValue());
        pRef1->setSubValues(std::move(subs2));
        pRef2->setValue(temp);
        pRef2->setSubValues(std::move(subs1));
    }
}

bool isEdgeType(const App::DocumentObject* obj, const std::string& elName, const GeomAbs_CurveType type)
{
    auto* base = dynamic_cast<const PartApp::Feature*>(obj);
    if (!base) {
        return false;
    }

    const auto& TopShape = base->Shape.getShape();

    // Check for valid face types
    const auto edge = TopoDS::Edge(TopShape.getSubShape(elName.c_str()));
    BRepAdaptor_Curve sf(edge);

    return sf.GetType() == type;
}

bool isFaceType(const App::DocumentObject* obj, const std::string& elName, const GeomAbs_SurfaceType type)
{
    auto* base = dynamic_cast<const PartApp::Feature*>(obj);
    if (!base) {
        return false;
    }

    const auto TopShape = base->Shape.getShape();

    // Check for valid face types
    const auto face = TopoDS::Face(TopShape.getSubShape(elName.c_str()));
    BRepAdaptor_Surface sf(face);

    return sf.GetType() == type;
}

double getFaceRadius(const App::DocumentObject* obj, const std::string& elt)
{
    auto* base = dynamic_cast<const PartApp::Feature*>(obj);
    if (!base) {
        return 0.0;
    }

    const PartApp::TopoShape& TopShape = base->Shape.getShape();

    // Check for valid face types
    TopoDS_Face face = TopoDS::Face(TopShape.getSubShape(elt.c_str()));
    BRepAdaptor_Surface sf(face);

    const auto type = sf.GetType();
    return type == GeomAbs_Cylinder ? sf.Cylinder().Radius()
        : type == GeomAbs_Sphere    ? sf.Sphere().Radius()
                                    : 0.0;
}

double getEdgeRadius(const App::DocumentObject* obj, const std::string& elt)
{
    auto* base = dynamic_cast<const PartApp::Feature*>(obj);
    if (!base) {
        return 0.0;
    }

    const auto& TopShape = base->Shape.getShape();

    // Check for valid face types
    const auto edge = TopoDS::Edge(TopShape.getSubShape(elt.c_str()));
    BRepAdaptor_Curve sf(edge);

    return sf.GetType() == GeomAbs_Circle ? sf.Circle().Radius() : 0.0;
}

DistanceType getDistanceType(App::DocumentObject* joint)
{
    if (!joint) {
        return DistanceType::Other;
    }

    const auto type1 = getElementTypeFromProp(joint, "Reference1");
    const auto type2 = getElementTypeFromProp(joint, "Reference2");
    auto elt1 = getElementFromProp(joint, "Reference1");
    auto elt2 = getElementFromProp(joint, "Reference2");
    auto* obj1 = getLinkedObjFromRef(joint, "Reference1");
    auto* obj2 = getLinkedObjFromRef(joint, "Reference2");

    if (type1 == "Vertex" && type2 == "Vertex") {
        return DistanceType::PointPoint;
    }
    else if (type1 == "Edge" && type2 == "Edge") {
        if (isEdgeType(obj1, elt1, GeomAbs_Line) || isEdgeType(obj2, elt2, GeomAbs_Line)) {
            if (!isEdgeType(obj1, elt1, GeomAbs_Line)) {
                swapJCS(joint);  // make sure that line is first if not 2 lines.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isEdgeType(obj2, elt2, GeomAbs_Line)) {
                return DistanceType::LineLine;
            }
            else if (isEdgeType(obj2, elt2, GeomAbs_Circle)) {
                return DistanceType::LineCircle;
            }
            // TODO : other cases Ellipse, parabola, hyperbola...
        }

        else if (isEdgeType(obj1, elt1, GeomAbs_Circle) || isEdgeType(obj2, elt2, GeomAbs_Circle)) {
            if (!isEdgeType(obj1, elt1, GeomAbs_Circle)) {
                swapJCS(joint);  // make sure that circle is first if not 2 lines.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isEdgeType(obj2, elt2, GeomAbs_Circle)) {
                return DistanceType::CircleCircle;
            }
            // TODO : other cases Ellipse, parabola, hyperbola...
        }
    }
    else if (type1 == "Face" && type2 == "Face") {
        if (isFaceType(obj1, elt1, GeomAbs_Plane) || isFaceType(obj2, elt2, GeomAbs_Plane)) {
            if (!isFaceType(obj1, elt1, GeomAbs_Plane)) {
                swapJCS(joint);  // make sure plane is first if its not 2 planes.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isFaceType(obj2, elt2, GeomAbs_Plane)) {
                return DistanceType::PlanePlane;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Cylinder)) {
                return DistanceType::PlaneCylinder;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Sphere)) {
                return DistanceType::PlaneSphere;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Cone)) {
                return DistanceType::PlaneCone;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Torus)) {
                return DistanceType::PlaneTorus;
            }
        }

        else if (isFaceType(obj1, elt1, GeomAbs_Cylinder) || isFaceType(obj2, elt2, GeomAbs_Cylinder)) {
            if (!isFaceType(obj1, elt1, GeomAbs_Cylinder)) {
                swapJCS(joint);  // make sure cylinder is first if its not 2 cylinders.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isFaceType(obj2, elt2, GeomAbs_Cylinder)) {
                return DistanceType::CylinderCylinder;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Sphere)) {
                return DistanceType::CylinderSphere;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Cone)) {
                return DistanceType::CylinderCone;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Torus)) {
                return DistanceType::CylinderTorus;
            }
        }

        else if (isFaceType(obj1, elt1, GeomAbs_Cone) || isFaceType(obj2, elt2, GeomAbs_Cone)) {
            if (!isFaceType(obj1, elt1, GeomAbs_Cone)) {
                swapJCS(joint);  // make sure cone is first if its not 2 cones.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isFaceType(obj2, elt2, GeomAbs_Cone)) {
                return DistanceType::ConeCone;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Torus)) {
                return DistanceType::ConeTorus;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Sphere)) {
                return DistanceType::ConeSphere;
            }
        }

        else if (isFaceType(obj1, elt1, GeomAbs_Torus) || isFaceType(obj2, elt2, GeomAbs_Torus)) {
            if (!isFaceType(obj1, elt1, GeomAbs_Torus)) {
                swapJCS(joint);  // make sure torus is first if its not 2 torus.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isFaceType(obj2, elt2, GeomAbs_Torus)) {
                return DistanceType::TorusTorus;
            }
            else if (isFaceType(obj2, elt2, GeomAbs_Sphere)) {
                return DistanceType::TorusSphere;
            }
        }

        else if (isFaceType(obj1, elt1, GeomAbs_Sphere) || isFaceType(obj2, elt2, GeomAbs_Sphere)) {
            if (!isFaceType(obj1, elt1, GeomAbs_Sphere)) {
                swapJCS(joint);  // make sure sphere is first if its not 2 spheres.
                std::swap(elt1, elt2);
                std::swap(obj1, obj2);
            }

            if (isFaceType(obj2, elt2, GeomAbs_Sphere)) {
                return DistanceType::SphereSphere;
            }
        }
    }
    else if ((type1 == "Vertex" && type2 == "Face") || (type1 == "Face" && type2 == "Vertex")) {
        if (type1 == "Vertex") {  // Make sure face is the first.
            swapJCS(joint);
            std::swap(elt1, elt2);
            std::swap(obj1, obj2);
        }
        if (isFaceType(obj1, elt1, GeomAbs_Plane)) {
            return DistanceType::PointPlane;
        }
        else if (isFaceType(obj1, elt1, GeomAbs_Cylinder)) {
            return DistanceType::PointCylinder;
        }
        else if (isFaceType(obj1, elt1, GeomAbs_Sphere)) {
            return DistanceType::PointSphere;
        }
        else if (isFaceType(obj1, elt1, GeomAbs_Cone)) {
            return DistanceType::PointCone;
        }
        else if (isFaceType(obj1, elt1, GeomAbs_Torus)) {
            return DistanceType::PointTorus;
        }
    }
    else if ((type1 == "Edge" && type2 == "Face") || (type1 == "Face" && type2 == "Edge")) {
        if (type1 == "Edge") {  // Make sure face is the first.
            swapJCS(joint);
            std::swap(elt1, elt2);
            std::swap(obj1, obj2);
        }
        if (isEdgeType(obj2, elt2, GeomAbs_Line)) {
            if (isFaceType(obj1, elt1, GeomAbs_Plane)) {
                return DistanceType::LinePlane;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Cylinder)) {
                return DistanceType::LineCylinder;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Sphere)) {
                return DistanceType::LineSphere;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Cone)) {
                return DistanceType::LineCone;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Torus)) {
                return DistanceType::LineTorus;
            }
        }
        else {
            // For other curves we consider them as planes for now. Can be refined later.
            if (isFaceType(obj1, elt1, GeomAbs_Plane)) {
                return DistanceType::CurvePlane;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Cylinder)) {
                return DistanceType::CurveCylinder;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Sphere)) {
                return DistanceType::CurveSphere;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Cone)) {
                return DistanceType::CurveCone;
            }
            else if (isFaceType(obj1, elt1, GeomAbs_Torus)) {
                return DistanceType::CurveTorus;
            }
        }
    }
    else if ((type1 == "Vertex" && type2 == "Edge") || (type1 == "Edge" && type2 == "Vertex")) {
        if (type1 == "Vertex") {  // Make sure edge is the first.
            swapJCS(joint);
            std::swap(elt1, elt2);
            std::swap(obj1, obj2);
        }
        if (isEdgeType(obj1, elt1, GeomAbs_Line)) {  // Point on line joint.
            return DistanceType::PointLine;
        }
        else {
            // For other curves we do a point in plane-of-the-curve.
            // Maybe it would be best tangent / distance to the conic? For arcs and
            // circles we could use ASMTRevSphJoint. But is it better than pointInPlane?
            return DistanceType::PointCurve;
        }
    }
    return DistanceType::Other;
}

JointGroup* getJointGroup(const App::DocumentObject* assemblyOrLink)
{
    if (!assemblyOrLink) {
        return nullptr;
    }

    // One assembly per document: the sole JointGroup belongs to this assembly.
    const auto jointGroups = assemblyOrLink->getDocument()->getObjectsOfType(
        JointGroup::getClassTypeId()
    );
    if (jointGroups.empty()) {
        return nullptr;
    }
    return freecad_cast<JointGroup*>(jointGroups.front());
}

void setJointActivated(const App::DocumentObject* joint, bool val)
{
    if (!joint) {
        return;
    }

    if (auto propSuppressed = joint->getPropertyByName<App::PropertyBool>("Suppressed")) {
        propSuppressed->setValue(!val);
    }
}

bool getJointActivated(const App::DocumentObject* joint)
{
    if (!joint) {
        return false;
    }

    if (const auto propActivated = joint->getPropertyByName<App::PropertyBool>("Suppressed")) {
        return !propActivated->getValue();
    }
    return false;
}

double getJointDistance(const App::DocumentObject* joint, const char* propertyName)
{
    if (!joint) {
        return 0.0;
    }

    const auto* prop = joint->getPropertyByName<App::PropertyFloat>(propertyName);
    if (!prop) {
        return 0.0;
    }

    return prop->getValue();
}

double getJointAngle(const App::DocumentObject* joint)
{
    return getJointDistance(joint, "Angle");
}

double getJointDistance(const App::DocumentObject* joint)
{
    return getJointDistance(joint, "Distance");
}

double getJointDistance2(const App::DocumentObject* joint)
{
    return getJointDistance(joint, "Distance2");
}

JointType getJointType(const App::DocumentObject* joint)
{
    if (!joint) {
        return JointType::Fixed;
    }

    const auto* prop = joint->getPropertyByName<App::PropertyEnumeration>("JointType");
    if (!prop) {
        return JointType::Fixed;
    }

    return static_cast<JointType>(prop->getValue());
}

std::vector<std::string> getSubAsList(const App::PropertyXLinkSub* prop)
{
    if (!prop) {
        return {};
    }

    const auto subs = prop->getSubValues();
    if (subs.empty()) {
        return {};
    }

    return Base::Tools::splitSubName(subs[0]);
}

std::vector<std::string> getSubAsList(const App::DocumentObject* obj, const char* pName)
{
    if (!obj) {
        return {};
    }
    return getSubAsList(obj->getPropertyByName<App::PropertyXLinkSub>(pName));
}

std::string getElementFromProp(const App::DocumentObject* obj, const char* pName)
{
    if (!obj) {
        return "";
    }

    const auto names = getSubAsList(obj, pName);
    if (names.empty()) {
        return "";
    }

    return names.back();
}

std::string getElementTypeFromProp(const App::DocumentObject* obj, const char* propName)
{
    // The prop is going to be something like 'Edge14' or 'Face7'. We need 'Edge' or 'Face'
    std::string elementType;
    for (const char ch : getElementFromProp(obj, propName)) {
        if (std::isalpha(ch)) {
            elementType += ch;
        }
    }
    return elementType;
}

App::DocumentObject* getObjFromProp(const App::DocumentObject* joint, const char* pName)
{
    if (!joint) {
        return {};
    }

    const auto* propObj = joint->getPropertyByName<App::PropertyLink>(pName);
    if (!propObj) {
        return {};
    }

    return propObj->getValue();
}

App::DocumentObject* getObjFromRef(App::DocumentObject* comp, const std::string& sub)
{
    if (!comp) {
        return nullptr;
    }

    const auto* doc = comp->getDocument();
    auto names = Base::Tools::splitSubName(sub);
    names.insert(names.begin(), comp->getNameInDocument());

    if (names.size() <= 2) {
        return comp;
    }

    // Lambda function to check if the typeId is a BodySubObject
    const auto isBodySubObject = [](App::DocumentObject* obj) -> bool {
        // PartDesign::Point + Line + Plane + CoordinateSystem
        // getViewProviderName instead of isDerivedFrom to avoid dependency on sketcher
        const auto isDerivedFromVpSketch
            = strcmp(obj->getViewProviderName(), "SketcherGui::ViewProviderSketch") == 0;
        return isDerivedFromVpSketch || obj->isDerivedFrom<PartApp::Datum>()
            || obj->isDerivedFrom<App::DatumElement>()
            || obj->isDerivedFrom<App::LocalCoordinateSystem>();
    };

    // Helper function to handle PartDesign::Body objects
    const auto handlePartDesignBody =
        [&](App::DocumentObject* obj,
            std::vector<std::string>::const_iterator it) -> App::DocumentObject* {
        auto nextIt = std::next(it);
        if (nextIt != names.end()) {
            for (auto* obji : obj->getOutList()) {
                if (*nextIt == obji->getNameInDocument() && isBodySubObject(obji)) {
                    // if obji is a LCS then perhaps we need to resolve one more level
                    if (auto* lcs = freecad_cast<App::LocalCoordinateSystem*>(obji)) {
                        nextIt = std::next(nextIt);
                        if (nextIt != names.end()) {
                            for (auto* objj : lcs->baseObjects()) {
                                if (*nextIt == objj->getNameInDocument()
                                    && objj->isDerivedFrom<App::DatumElement>()) {
                                    return objj;
                                }
                            }
                        }
                    }
                    return obji;
                }
            }
        }
        return obj;
    };


    for (auto it = names.begin(); it != names.end(); ++it) {
        App::DocumentObject* obj = doc->getObject(it->c_str());
        if (!obj) {
            return nullptr;
        }

        if (obj->isDerivedFrom<App::DocumentObjectGroup>()) {
            continue;
        }

        // The last but one name should be the selected
        if (std::next(it) == std::prev(names.end())) {
            return obj;
        }

        if (obj->isDerivedFrom<App::Part>() || obj->isDerivedFrom<Assembly::AssemblyLink>()
            || obj->isLinkGroup()) {
            continue;
        }
        else if (obj->isDerivedFrom<PartDesign::Body>()) {
            return handlePartDesignBody(obj, it);
        }
        else if (obj->isDerivedFrom<PartApp::Feature>()) {
            // Primitive, fastener, gear, etc.
            return obj;
        }
        else if (obj->isLink()) {
            App::DocumentObject* linked_obj = obj->getLinkedObject();
            if (linked_obj->isDerivedFrom<PartDesign::Body>()) {
                auto* retObj = handlePartDesignBody(linked_obj, it);
                return retObj == linked_obj ? obj : retObj;
            }
            else if (linked_obj->isDerivedFrom<PartApp::Feature>()) {
                return obj;
            }
            else {
                doc = linked_obj->getDocument();
                continue;
            }
        }
    }

    return nullptr;
}

App::DocumentObject* getObjFromRef(const App::PropertyXLinkSub* prop)
{
    if (!prop) {
        return nullptr;
    }

    App::DocumentObject* obj = prop->getValue();
    if (!obj) {
        return nullptr;
    }

    const std::vector<std::string> subs = prop->getSubValues();
    if (subs.empty()) {
        return nullptr;
    }

    return getObjFromRef(obj, subs[0]);
}

App::DocumentObject* getObjFromJointRef(const App::DocumentObject* joint, const char* pName)
{
    if (!joint) {
        return nullptr;
    }

    const auto* prop = joint->getPropertyByName<App::PropertyXLinkSub>(pName);
    return getObjFromRef(prop);
}

App::DocumentObject* getLinkedObjFromRef(const App::DocumentObject* joint, const char* pObj)
{
    if (!joint) {
        return nullptr;
    }

    if (const auto* obj = getObjFromJointRef(joint, pObj)) {
        return obj->getLinkedObject(true);
    }
    return nullptr;
}

App::DocumentObject* getMovingPartFromSel(
    const AssemblyObject* assemblyObject,
    App::DocumentObject* obj,
    const std::string& sub
)
{
    if (!obj) {
        return nullptr;
    }

    auto* doc = obj->getDocument();

    auto names = Base::Tools::splitSubName(sub);
    names.insert(names.begin(), obj->getNameInDocument());

    // The assembly is a plain DocumentObjectGroup (it does not claim its children in the
    // selection path), so a component click is rooted at the component itself and the
    // assembly name is absent from the path. Only gate on "pass the assembly" when it
    // actually appears in the path (e.g. a nested part.assembly.part.body); otherwise the
    // path already starts inside this assembly. The assembly can only appear as a same-doc
    // entry, so scan for it in its own document.
    const App::Document* asmDoc = assemblyObject ? assemblyObject->getDocument() : doc;
    bool assemblyPassed = true;
    for (const auto& objName : names) {
        if (asmDoc->getObject(objName.c_str()) == assemblyObject) {
            assemblyPassed = false;
            break;
        }
    }

    for (const auto& objName : names) {
        obj = doc->getObject(objName.c_str());
        if (!obj) {
            continue;
        }

        if (obj->isLink()) {  // update the document if necessary for next object
            doc = obj->getLinkedObject()->getDocument();
        }

        if (obj == assemblyObject) {
            // We make sure we pass the assembly for cases like part.assembly.part.body
            assemblyPassed = true;
            continue;
        }
        if (!assemblyPassed) {
            continue;
        }

        if (obj->isDerivedFrom<App::DocumentObjectGroup>()) {
            continue;  // we ignore groups.
        }

        if (obj->isLinkGroup()) {
            continue;
        }

        // We ignore dynamic sub-assemblies.
        if (obj->isDerivedFrom<Assembly::AssemblyLink>()) {
            const auto* pRigid = obj->getPropertyByName<App::PropertyBool>("Rigid");
            if (pRigid && !pRigid->getValue()) {
                continue;
            }
        }

        return obj;
    }
    return nullptr;
}

App::DocumentObject* getMovingPartFromRef(App::PropertyXLinkSub* prop)
{
    if (!prop) {
        return nullptr;
    }

    return prop->getValue();
}

App::DocumentObject* getMovingPartFromRef(App::DocumentObject* joint, const char* pName)
{
    if (!joint) {
        return nullptr;
    }

    auto* prop = joint->getPropertyByName<App::PropertyXLinkSub>(pName);
    return getMovingPartFromRef(prop);
}

void syncPlacements(App::DocumentObject* src, App::DocumentObject* to)
{
    auto* plcPropSource = dynamic_cast<App::PropertyPlacement*>(src->getPropertyByName("Placement"));
    auto* plcPropLink = dynamic_cast<App::PropertyPlacement*>(to->getPropertyByName("Placement"));

    if (plcPropSource && plcPropLink) {
        if (!plcPropSource->getValue().isSame(plcPropLink->getValue())) {
            plcPropLink->setValue(plcPropSource->getValue());
        }
    }
}
std::vector<App::DocumentObject*> getAssemblyComponents(const AssemblyObject* assembly)
{
    App::Document* doc = assembly ? assembly->getDocument() : nullptr;
    if (doc == nullptr) {
        // No document yet (e.g. mid-construction): nothing to enumerate.
        return {};
    }

    // One assembly per document, and an assembly document holds no geometry of its own:
    // every component arrives as a cross-document reference. So enumerate the document's
    // reference objects directly instead of walking the (retiring) App::Part Group. No
    // feature-input filtering is needed -- intermediate feature inputs (boolean Base/Tool,
    // compound Shapes) live in each part's own document now, never here.
    std::vector<App::DocumentObject*> components;
    for (auto* obj : doc->getObjects()) {
        if (!obj) {
            continue;
        }

        if (auto* asmLink = freecad_cast<Assembly::AssemblyLink*>(obj)) {
            // A rigid sub-assembly counts as one movable part. Flexible sub-assemblies
            // still materialize proxy links via the old machinery; expanding them here
            // would double-count under a document query, so their finer breakdown is
            // deferred to the flexible rebuild. Rigid-only for now; see issue #65.
            components.push_back(asmLink);
        }
        else if (obj->isLinkGroup()) {
            // A link array is one reference standing in for N occurrences: expand it.
            auto* linkGroup = static_cast<App::Link*>(obj);
            for (auto* elt : linkGroup->ElementList.getValues()) {
                components.push_back(elt);
            }
        }
        else if (auto* link = freecad_cast<App::Link*>(obj)) {
            auto* linked = link->getLinkedObject();
            if (linked != nullptr && linked->isDerivedFrom<App::GeoFeature>()
                && !linked->isDerivedFrom<App::LocalCoordinateSystem>()) {
                components.push_back(link);
            }
        }
    }
    return components;
}

// ============================ Joint coordinate systems ============================
//
// findPlacement() computes the local coordinate system a joint connector sits at,
// given a stored reference to a sub-shape on some part. It is deliberately split into
// two halves that never interleave (#59 three-layer template):
//
//   * The identity boundary: resolveReference() plus the single subShape() helper own
//     every reference / sub-element-*name* lookup. This is the one place a future
//     durable sub-shape-id scheme will slot in; today it resolves by the current
//     sub-element name (topological naming as-is). Geometry code never open-codes a
//     name lookup -- it calls subShape() so the mechanism stays swappable.
//   * Pure geometry: computePlacement() consumes resolved sub-shapes and produces a
//     placement. It reads no names and no references, only OCCT geometry.
//
// Ported from UtilsAssembly.findPlacement (#59). Keep the two halves separate.

namespace
{
struct ResolvedReference
{
    App::DocumentObject* obj = nullptr;  // resolved reference object (a component is an App::Link)
    App::DocumentObject* geoObj = nullptr;  // geometry-bearing object: obj, or the object obj links
                                            // to (cross-doc component, #38 G1). All shape reads +
                                            // frame normalization use geoObj, never obj.
    TopoDS_Shape eltShape;                  // primary sub-shape (null for whole-part datums)
    std::string eltType;                    // "Vertex" | "Edge" | "Face" | ""
    int eltIndex = 0;                       // trailing number of the primary sub-element
    std::string vtxName;     // secondary sub-name; resolved contextually via subShape()
    std::string vtxType;     // "" when there is no second sub
    bool wholePart = false;  // elt or vtx empty -> datum-axis / whole-part case
    bool valid = false;
};

// The single name->shape lookup, funnelling every resolution through one seam so a
// durable-id scheme can later replace just this. Returns a null shape on miss.
TopoDS_Shape subShape(const Part::TopoShape& parent, const std::string& name)
{
    try {
        return parent.getSubShape(name.c_str());
    }
    catch (const Base::Exception&) {
        Base::Console().warning("Assembly: unable to find element %s.\n", name.c_str());
        return {};
    }
}

// Trailing element token of a dotted sub-name, e.g. "Asm.Box.Edge16" -> "Edge16".
// Datum roles (X/Y/Z/Point/Line/Plane) resolve to "" so the whole-part branch handles
// them; mirrors UtilsAssembly.getElementName.
std::string elementName(const std::string& sub)
{
    const auto pos = sub.rfind('.');
    const std::string last = pos == std::string::npos ? sub : sub.substr(pos + 1);
    static const std::set<std::string> datumRoles = {"X", "Y", "Z", "Point", "Line", "Plane"};
    return datumRoles.count(last) != 0U ? std::string() : last;
}

// Split "Face7" into ("Face", 7); mirrors UtilsAssembly.extract_type_and_number.
void splitTypeAndNumber(const std::string& name, std::string& type, int& number)
{
    type.clear();
    number = 0;
    std::string digits;
    for (const char ch : name) {
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
            type += ch;
        }
        else if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            digits += ch;
        }
        else {
            break;
        }
    }
    if (!type.empty() && !digits.empty()) {
        number = std::stoi(digits);
    }
    else {
        type.clear();
        number = 0;
    }
}

bool refIsValid(const App::DocumentObject* value, const std::vector<std::string>& subs)
{
    if (!value || subs.empty()) {
        return false;
    }
    return subs.front().find('?') == std::string::npos;
}

// Identity boundary: turn a raw reference (value object + sub-element names) into a
// resolved object + primary sub-shape. This is the funnel every caller reaches, whether
// the reference arrives as a stored PropertyXLinkSub or as a raw [obj, subs] pair.
ResolvedReference resolveReference(App::DocumentObject* value, const std::vector<std::string>& subs)
{
    ResolvedReference r;
    if (!refIsValid(value, subs)) {
        return r;
    }
    r.obj = getObjFromRef(value, subs[0]);
    if (!r.obj) {
        return r;
    }
    // A component is a cross-doc App::Link (assembly reference-only, #38 G1). getObjFromRef hands
    // back the link itself; follow it to the geometry-bearing object. The linked body/primitive
    // supplies the shape in its own frame, which the component's App::Link maps to world via the
    // link Placement; the solver applies that component placement separately, so geometry code
    // reads geoObj (never the link) and normalizes by geoObj's own placement. Mirrors the retired
    // Python findPlacement, which resolved through to the linked object's shape.
    r.geoObj = r.obj->isLink() ? r.obj->getLinkedObject(true) : r.obj;
    if (!r.geoObj) {
        return r;
    }
    r.valid = true;

    const std::string eltName = elementName(subs[0]);
    r.vtxName = subs.size() > 1 ? elementName(subs[1]) : std::string();

    if (eltName.empty() || r.vtxName.empty()) {
        // Whole-part reference (PartDesign datum axis / origin element).
        r.wholePart = true;
        return r;
    }

    int dummy = 0;
    splitTypeAndNumber(eltName, r.eltType, r.eltIndex);
    splitTypeAndNumber(r.vtxName, r.vtxType, dummy);

    const auto* base = dynamic_cast<const Part::Feature*>(r.geoObj);
    if (!base) {
        r.valid = false;
        return r;
    }
    r.eltShape = subShape(base->Shape.getShape(), eltName);
    if (r.eltShape.IsNull()) {
        r.valid = false;
    }
    return r;
}

// --- pure-geometry helpers (no name lookups) ---

Base::Vector3d toVec(const gp_Pnt& p)
{
    return Base::Vector3d(p.X(), p.Y(), p.Z());
}

Base::Vector3d toVec(const gp_Dir& d)
{
    return Base::Vector3d(d.X(), d.Y(), d.Z());
}

double round10(double v)
{
    return std::round(v * 1e10) / 1e10;
}

Base::Vector3d round10(const Base::Vector3d& v)
{
    return Base::Vector3d(round10(v.x), round10(v.y), round10(v.z));
}

// Rotation of a coordinate system relative to the global one, matching the
// GeometryCurvePy/GeometrySurfacePy `.Rotation` getters used by the former Python.
Base::Rotation rotationFromAx2(const gp_Ax2& ax2)
{
    gp_Trsf trsf;
    trsf.SetTransformation(gp_Ax3(ax2), gp_Ax3());
    const gp_Quaternion q = trsf.GetRotation();
    return {q.X(), q.Y(), q.Z(), q.W()};
}

// Part.Plane(origin, normal).Rotation -- the frame whose z-axis is `normal`.
Base::Rotation planeRotationFromNormal(const gp_Dir& normal)
{
    const GC_MakePlane mk(gp_Pnt(0, 0, 0), normal);
    const Handle(Geom_Plane) plane = mk.Value();
    return rotationFromAx2(plane->Position().Ax2());
}

gp_Pnt faceCenterOfMass(const TopoDS_Face& face)
{
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    return props.CentreOfMass();
}

// Intersection of two known-intersecting cylinder axes; falls back to `fallback`
// (the primary cylinder centre) when the axes are parallel/skew. Ports the geometric
// core of UtilsAssembly.findCylindersIntersection.
Base::Vector3d axisIntersection(
    const gp_Pnt& p1,
    const gp_Dir& d1,
    const gp_Pnt& p2,
    const gp_Dir& d2,
    const Base::Vector3d& fallback
)
{
    const gp_Vec v1(d1);
    const gp_Vec v2(d2);
    const gp_Vec cross = v1.Crossed(v2);
    const double denom = cross.SquareMagnitude();
    if (denom < Precision::SquareConfusion()) {
        return fallback;  // parallel axes
    }
    const gp_Vec r(p1, p2);
    const double t = gp_Vec(r.Crossed(v2)).Dot(cross) / denom;
    const gp_Pnt hit = p1.Translated(v1.Scaled(t));
    return toVec(hit);
}

const Part::TopoShape* objTopoShape(const App::DocumentObject* obj)
{
    const auto* base = dynamic_cast<const Part::Feature*>(obj);
    return base ? &base->Shape.getShape() : nullptr;
}

Base::Placement objPlacement(const App::DocumentObject* obj)
{
    if (const auto* p = obj->getPropertyByName<App::PropertyPlacement>("Placement")) {
        return p->getValue();
    }
    return {};
}

// Whole-part references (a PartDesign datum axis picked as a whole) map to fixed frames.
Base::Placement datumAxisPlacement(const App::DocumentObject* obj)
{
    const auto* line = dynamic_cast<const App::Line*>(obj);
    if (!line) {
        return {};
    }
    const std::string role = line->Role.getValue();
    if (role == "X_Axis" || role == "Y_Axis") {
        return Base::Placement(Base::Vector3d(), Base::Rotation(0.5, 0.5, 0.5, 0.5));
    }
    if (role == "Z_Axis") {
        return Base::Placement(Base::Vector3d(), Base::Rotation(-0.5, 0.5, -0.5, 0.5));
    }
    return {};
}

// Pure geometry: resolved sub-shapes in, local placement out. Name lookups (for the
// contextual secondary sub-element) go through subShape(), never open-coded here.
Base::Placement computePlacement(const ResolvedReference& r, bool ignoreVertex)
{
    if (r.wholePart) {
        return datumAxisPlacement(r.geoObj);
    }

    Base::Placement plc;
    bool isLine = false;

    if (r.eltType == "Vertex") {
        plc.setPosition(toVec(BRep_Tool::Pnt(TopoDS::Vertex(r.eltShape))));
    }
    else if (r.eltType == "Edge") {
        const TopoDS_Edge edge = TopoDS::Edge(r.eltShape);
        BRepAdaptor_Curve adapt(edge);
        const GeomAbs_CurveType ctype = adapt.GetType();

        // translation
        if (r.vtxType == "Edge" || ignoreVertex) {
            if (ctype == GeomAbs_Circle) {
                plc.setPosition(toVec(adapt.Circle().Location()));
            }
            else if (ctype == GeomAbs_Line) {
                const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge));
                const gp_Pnt b = BRep_Tool::Pnt(TopExp::LastVertex(edge));
                plc.setPosition((toVec(a) + toVec(b)) * 0.5);
            }
        }
        else {
            const Part::TopoShape* shape = objTopoShape(r.geoObj);
            const TopoDS_Shape vtx = shape ? subShape(*shape, r.vtxName) : TopoDS_Shape();
            if (vtx.IsNull()) {
                return {};
            }
            plc.setPosition(toVec(BRep_Tool::Pnt(TopoDS::Vertex(vtx))));
        }

        // rotation
        if (ctype == GeomAbs_Circle) {
            plc.setRotation(rotationFromAx2(adapt.Circle().Position()));
        }
        else if (ctype == GeomAbs_Line) {
            isLine = true;
            const Base::Vector3d n = round10(toVec(adapt.Line().Direction()));
            plc.setRotation(planeRotationFromNormal(gp_Dir(n.x, n.y, n.z)));
        }
    }
    else if (r.eltType == "Face") {
        const TopoDS_Face face = TopoDS::Face(r.eltShape);
        BRepAdaptor_Surface adapt(face);
        const GeomAbs_SurfaceType stype = adapt.GetType();

        // translation
        if (r.vtxType == "Face" || ignoreVertex) {
            if (stype == GeomAbs_Cylinder) {
                const gp_Cylinder cyl = adapt.Cylinder();
                const Base::Vector3d center = toVec(cyl.Location());
                const Base::Vector3d centerOfG = toVec(faceCenterOfMass(face)) - center;
                Base::Vector3d proj;
                proj.ProjectToLine(centerOfG, toVec(cyl.Axis().Direction()));
                plc.setPosition(center + centerOfG + proj);
            }
            else if (stype == GeomAbs_Torus) {
                plc.setPosition(toVec(adapt.Torus().Location()));
            }
            else if (stype == GeomAbs_Sphere) {
                plc.setPosition(toVec(adapt.Sphere().Location()));
            }
            else if (stype == GeomAbs_Cone) {
                plc.setPosition(toVec(adapt.Cone().Apex()));
            }
            else {
                plc.setPosition(toVec(faceCenterOfMass(face)));
            }
        }
        else if (r.vtxType == "Edge") {
            // Secondary edge is resolved *within the face* (its center is wanted).
            const TopoDS_Shape sub = subShape(Part::TopoShape(face), r.vtxName);
            if (!sub.IsNull()) {
                BRepAdaptor_Curve cadapt(TopoDS::Edge(sub));
                if (cadapt.GetType() == GeomAbs_Circle) {
                    plc.setPosition(toVec(cadapt.Circle().Location()));
                }
                else if (stype == GeomAbs_Cylinder && cadapt.GetType() == GeomAbs_BSplineCurve) {
                    // Two intersecting cylinders: sit at their axes' intersection.
                    const gp_Cylinder cyl = adapt.Cylinder();
                    Base::Vector3d hit(toVec(cyl.Location()));
                    for (int i = 1;; ++i) {
                        const std::string name = "Face" + std::to_string(i);
                        const Part::TopoShape* shp = objTopoShape(r.geoObj);
                        if (!shp) {
                            break;
                        }
                        TopoDS_Shape fj;
                        try {
                            fj = shp->getSubShape(name.c_str());
                        }
                        catch (const Base::Exception&) {
                            break;  // ran past the last face
                        }
                        if (i == r.eltIndex || fj.IsNull() || fj.ShapeType() != TopAbs_FACE) {
                            continue;
                        }
                        BRepAdaptor_Surface sj(TopoDS::Face(fj));
                        if (sj.GetType() != GeomAbs_Cylinder) {
                            continue;
                        }
                        const gp_Cylinder cj = sj.Cylinder();
                        hit = axisIntersection(
                            cyl.Location(),
                            cyl.Axis().Direction(),
                            cj.Location(),
                            cj.Axis().Direction(),
                            hit
                        );
                        break;
                    }
                    plc.setPosition(hit);
                }
            }
        }
        else {
            const Part::TopoShape* shape = objTopoShape(r.geoObj);
            const TopoDS_Shape vtx = shape ? subShape(*shape, r.vtxName) : TopoDS_Shape();
            if (vtx.IsNull()) {
                return {};
            }
            plc.setPosition(toVec(BRep_Tool::Pnt(TopoDS::Vertex(vtx))));
        }

        // rotation: any elementary surface exposes a frame; free-form ones do not.
        const Handle(Geom_ElementarySurface)
            es = Handle(Geom_ElementarySurface)::DownCast(BRep_Tool::Surface(face));
        if (!es.IsNull()) {
            plc.setRotation(rotationFromAx2(es->Position().Ax2()));
        }
    }

    // Draft arrays carry the array placement on the Shape; strip it back out.
    if (r.geoObj->getPropertyByName("ExpandArray") != nullptr) {
        if (const auto* baseProp = r.geoObj->getPropertyByName<App::PropertyLink>("Base")) {
            if (const App::DocumentObject* baseObj = baseProp->getValue()) {
                plc = objPlacement(baseObj).inverse() * plc;
            }
        }
    }

    // Everything above is in the geometry object's own frame; make it relative to that frame (the
    // component-local frame the solver expects). Use geoObj, not the link -- the link's Placement
    // is the component's global placement, applied downstream by the solver.
    plc = objPlacement(r.geoObj).inverse() * plc;

    if (r.eltType == "Vertex") {
        plc.setRotation(Base::Rotation());
    }
    else if (isLine) {
        const Base::Vector3d n = round10(plc.getRotation().multVec(Base::Vector3d(0, 0, 1)));
        plc.setRotation(planeRotationFromNormal(gp_Dir(n.x, n.y, n.z)));
    }

    return plc;
}
}  // namespace

Base::Placement findPlacement(
    App::DocumentObject* obj,
    const std::vector<std::string>& subs,
    bool ignoreVertex
)
{
    const ResolvedReference resolved = resolveReference(obj, subs);
    if (!resolved.valid) {
        return {};
    }
    return computePlacement(resolved, ignoreVertex);
}

Base::Placement findPlacement(const App::PropertyXLinkSub* ref, bool ignoreVertex)
{
    if (!ref) {
        return {};
    }
    return findPlacement(ref->getValue(), ref->getSubValues(), ignoreVertex);
}

double getJointCurrentValue(App::DocumentObject* joint, bool isAngle)
{
    Base::Placement plc1 = App::GeoFeature::getPlacementFromProp(joint, "Placement1");
    Base::Placement plc2 = App::GeoFeature::getPlacementFromProp(joint, "Placement2");

    auto* ref1 = dynamic_cast<App::PropertyXLinkSub*>(joint->getPropertyByName("Reference1"));
    auto* ref2 = dynamic_cast<App::PropertyXLinkSub*>(joint->getPropertyByName("Reference2"));
    if (!ref1 || !ref2) {
        return 0.0;
    }
    Base::Placement obj_global_plc1 = App::GeoFeature::getGlobalPlacement(nullptr, ref1);
    Base::Placement obj_global_plc2 = App::GeoFeature::getGlobalPlacement(nullptr, ref2);

    plc1 = obj_global_plc1 * plc1;
    plc2 = obj_global_plc2 * plc2;

    Base::Placement plc3 = plc1.inverse() * plc2;

    if (isAngle) {
        Base::Vector3d x_axis = plc3.getRotation().multVec(Base::Vector3d(1, 0, 0));
        return std::atan2(x_axis.y, x_axis.x);
    }
    return (plc1.getPosition() - plc2.getPosition()).Length()
        * (plc3.getPosition().z < 0 ? -1.0 : 1.0);
}

void redrawJointViewProvider(App::DocumentObject* joint)
{
    if (!joint) {
        return;
    }

    Base::PyGILStateLocker lock;

    // joint.ViewObject.Proxy.redrawJointPlacements(joint) — the same hop the former
    // Python Joint proxy forwarder made, now that the App object carries no Proxy.
    Py::Object jointPy(joint->getPyObject(), true);
    if (!jointPy.hasAttr("ViewObject")) {
        return;
    }
    Py::Object viewObject = jointPy.getAttr("ViewObject");
    if (viewObject.isNone() || !viewObject.hasAttr("Proxy")) {
        return;
    }
    Py::Object proxy = viewObject.getAttr("Proxy");
    if (proxy.isNone() || !proxy.hasAttr("redrawJointPlacements")) {
        return;
    }
    Py::Object attr = proxy.getAttr("redrawJointPlacements");
    if (attr.ptr() && attr.isCallable()) {
        Py::Tuple args(1);
        args.setItem(0, jointPy);
        Py::Callable(attr).apply(args);
    }
}
}  // namespace Assembly
