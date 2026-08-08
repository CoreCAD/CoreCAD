// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/***************************************************************************
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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <sstream>

# include <BRepAdaptor_Curve.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Edge.hxx>
#endif

#include <App/GeoFeature.h>
#include <App/PropertyGeo.h>
#include <Base/Console.h>
#include <Base/Parameter.h>
#include <App/Application.h>
#include <App/Document.h>

#include "ShapeExtension.h"
#include "PropertyTopoShape.h"
#include "PartPyCXX.h"
#include "TopoShapePy.h"

using namespace Part;

FC_LOG_LEVEL_INIT("Part", true, true)

EXTENSION_PROPERTY_SOURCE(Part::ShapeExtension, App::DocumentObjectExtension)

ShapeExtension::ShapeExtension()
{
    initExtensionType(ShapeExtension::getExtensionClassTypeId());
}

ShapeExtension::~ShapeExtension() = default;

bool ShapeExtension::extensionGetSubObject(
    App::DocumentObject*& ret,
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* mat,
    bool transform,
    int depth
) const
{
    (void)depth;

    while (subname && *subname == '.') {
        ++subname;  // skip leading .
    }

    // A '.' inside subname (that is not part of a mapped-element token) means the
    // reference navigates to a child object, not a sub-element of our own shape.
    // Decline so the base DocumentObject::getSubObject performs child navigation.
    if (subname && !Data::isMappedElement(subname) && strchr(subname, '.')) {
        return false;
    }

    auto* owner = freecad_cast<App::GeoFeature*>(getExtendedContainer());
    if (!owner) {
        return false;
    }

    // The backing geometry, obtained through the object's own delegation hook: a
    // stored Shape for a normal feature, a derived Tip shape for a Body.
    auto* geomProp = freecad_cast<const PropertyPartShape*>(owner->getPropertyOfGeometry());
    if (!geomProp) {
        return false;
    }

    Base::Matrix4D _mat;
    auto& matref = mat ? *mat : _mat;
    if (transform) {
        if (auto* pla = freecad_cast<App::PropertyPlacement*>(owner->getPropertyByName("Placement"))) {
            matref *= pla->getValue().toMatrix();
        }
    }

    if (!pyObj) {
        // TopoShape::hasSubShape is kind of slow, let's cut ourselves some slack here.
        ret = const_cast<App::GeoFeature*>(owner);
        return true;
    }

    try {
        TopoShape ts(geomProp->getShape());
        bool doTransform = matref != ts.getTransform();
        if (doTransform) {
            ts.setShape(ts.getShape().Located(TopLoc_Location()), false);
        }
        if (subname && *subname && !ts.isNull()) {
            ts = ts.getSubTopoShape(subname, true);
        }
        if (doTransform && !ts.isNull()) {
            static int sCopy = -1;
            if (sCopy < 0) {
                ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Mod/Part/General"
                );
                sCopy = hGrp->GetBool("CopySubShape", false) ? 1 : 0;
            }
            bool copy = sCopy ? true : false;
            if (!copy) {
                // Work around OCC bug on transforming circular edge with an
                // offset surface. The bug probably affects other shape types, too.
                TopExp_Explorer exp(ts.getShape(), TopAbs_EDGE);
                if (exp.More()) {
                    auto edge = TopoDS::Edge(exp.Current());
                    exp.Next();
                    if (!exp.More()) {
                        BRepAdaptor_Curve curve(edge);
                        copy = curve.GetType() == GeomAbs_Circle;
                    }
                }
            }
            ts.transformShape(matref, copy, true);
        }
        *pyObj = Py::new_reference_to(shape2pyshape(ts));
        ret = const_cast<App::GeoFeature*>(owner);
        return true;
    }
    catch (Standard_Failure& e) {
        std::ostringstream str;
        Standard_CString msg = e.GetMessageString();
        str << e.DynamicType()->get_type_name() << " ";
        str << (msg ? msg : "No OCCT Exception Message");
        str << ": " << owner->getFullName();
        if (subname) {
            str << '.' << subname;
        }
        FC_LOG(str.str());
        ret = nullptr;
        return true;
    }
}

namespace Part
{

bool hasShape(const App::DocumentObject* obj)
{
    return obj != nullptr && obj->hasExtension(ShapeExtension::getExtensionClassTypeId());
}

TopoShape getShape(const App::DocumentObject* obj)
{
    if (const auto* geo = freecad_cast<const App::GeoFeature*>(obj)) {
        if (const auto* prop = freecad_cast<const PropertyPartShape*>(geo->getPropertyOfGeometry())) {
            return prop->getShape();
        }
    }
    return {};
}

std::vector<App::DocumentObject*> getShapeObjects(const App::Document* doc)
{
    std::vector<App::DocumentObject*> result;
    if (doc == nullptr) {
        return result;
    }
    for (auto* obj : doc->getObjects()) {
        if (hasShape(obj)) {
            result.push_back(obj);
        }
    }
    return result;
}

}  // namespace Part
