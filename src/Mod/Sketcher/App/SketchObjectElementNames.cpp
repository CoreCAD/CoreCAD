// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>               *
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

#include <algorithm>
#include <map>
#include <optional>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/geometry.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <App/Expression.h>
#include <App/FeaturePythonPyImp.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <App/ObjectIdentifier.h>
#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Part/App/GeometryMigrationExtension.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/WireJoiner.h>

#include <iomanip>
#include <memory>

#include "GeoEnum.h"
#include "SketchObject.h"
#include "Constraint.h"
#include "SketchObjectPy.h"
#include "ExternalGeometryFacade.h"


#undef DEBUG
// #define DEBUG

using namespace Sketcher;
using namespace Base;
namespace sp = std::placeholders;
namespace bio = boost::iostreams;

FC_LOG_LEVEL_INIT("Sketch", true, true)

// clang-format off
static const char *hasSketchMarker(const char *name) {
    static std::string marker(Part::TopoShape::elementMapPrefix()+Part::OpCodes::Sketch);
    if (!name)
        return nullptr;
    return strstr(name,marker.c_str());
}

// clang-format on

// The id is stamped into every edge and vertex name the sketch hands downstream. A running
// count alone gives a shape added on one branch the same id as a different shape added on
// another, so the low half is projected from the shape's durable tag. The count stays in the
// high half because names are compared numerically and a face takes its lowest edge name:
// newer shapes must sort after older ones, or a trim renames the face it trims.
namespace
{
constexpr int geometryCountShift = 32;
constexpr unsigned long geometryTagMask = 0xFFFFFFFFUL;

// Written as "<count>h<tag part>": the string table splits a trailing number off a name and keeps
// it in an int, so the whole 64-bit id may not end a name. Eight hex digits always fit.
void writeGeometryId(std::ostream& out, long id)
{
    out << (id >> geometryCountShift) << 'h' << std::hex << std::setw(8) << std::setfill('0')
        << (static_cast<unsigned long>(id) & geometryTagMask) << std::dec << std::setfill(' ');
}

bool readGeometryId(std::istream& in, long& id)
{
    long count = 0;
    char separator = 0;
    unsigned long tagPart = 0;
    if (!(in >> count >> separator) || separator != 'h' || !(in >> std::hex >> tagPart)) {
        return false;
    }
    in >> std::dec;
    id = (count << geometryCountShift) | static_cast<long>(tagPart & geometryTagMask);
    return true;
}
}  // namespace

long SketchObject::mintGeometryId(const Part::Geometry* geo)
{
    const boost::uuids::uuid tag = geo->getTag();
    uint32_t hash = 2166136261U;
    for (const uint8_t byte : tag) {
        hash ^= byte;
        hash *= 16777619U;
    }
    const long count = (geoLastId >> geometryCountShift) + 1;
    geoLastId = (count << geometryCountShift) | static_cast<long>(hash);
    return geoLastId;
}

// clang-format off

App::DocumentObject *SketchObject::getSubObject(
        const char *subname, PyObject **pyObj,
        Base::Matrix4D *pmat, bool transform, int depth) const
{
    while(subname && *subname=='.') ++subname; // skip leading .
    std::string sub;
    const char *mapped = Data::isMappedElement(subname);
    if(!subname || !subname[0]) {
        return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    }
    const char *element = Data::findElementName(subname);
    if(element != subname) {
        const char *dot = strchr(subname,'.');
        if(!dot)
            return 0;
        std::string name(subname,dot-subname);
        auto child = Exports.find(name.c_str());
        if(!child)
            return 0;
        return child->getSubObject(dot+1,pyObj,pmat,true,depth+1);
    }

    Data::IndexedName indexedName = checkSubName(subname);
    int index = indexedName.getIndex();
    const char * shapetype = indexedName.getType();
    const Part::Geometry *geo = 0;
    Part::TopoShape subshape;
    Base::Vector3d point;

    if (auto realType = convertInternalName(indexedName.getType())) {
        if (realType[0] == '\0')
            subshape = InternalShape.getShape();
        else {
            auto shapeType = Part::TopoShape::shapeType(realType, true);
            if (shapeType != TopAbs_SHAPE)
                subshape = InternalShape.getShape().getSubTopoShape(shapeType, indexedName.getIndex(), true);
        }
        if (subshape.isNull())
            return nullptr;
    }
    else if (!pyObj || !mapped) {
        if (!pyObj
            || (index > 0
                && !boost::algorithm::contains(subname, "edge")
                && !boost::algorithm::contains(subname, "vertex")))
            return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    }
    else {
        subshape = Shape.getShape().getSubTopoShape(subname, true);
        if (!subshape.isNull())
            return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    }

    if (subshape.isNull()) {
        if (boost::equals(shapetype,"Edge") ||
            boost::equals(shapetype,"edge")) {
            geo = getGeometry(index - 1);
            if (!geo)
                return nullptr;
        }
        else if (boost::equals(shapetype,"ExternalEdge")) {
            int GeoId = index - 1;
            GeoId = -GeoId - 3;
            geo = getGeometry(GeoId);
            if(!geo)
                return nullptr;
        }
        else if (boost::equals(shapetype,"Vertex") ||
                 boost::equals(shapetype,"vertex")) {
            int VtId = index- 1;
            int GeoId;
            PointPos PosId;
            getGeoVertexIndex(VtId,GeoId,PosId);
            if (PosId==PointPos::none)
                return nullptr;
            point = getPoint(GeoId,PosId);
        }
        else if (boost::equals(shapetype,"RootPoint"))
            point = getPoint(Sketcher::GeoEnum::RtPnt,PointPos::start);
        else if (boost::equals(shapetype,"H_Axis"))
            geo = getGeometry(Sketcher::GeoEnum::HAxis);
        else if (boost::equals(shapetype,"V_Axis"))
            geo = getGeometry(Sketcher::GeoEnum::VAxis);
        else if (boost::equals(shapetype,"Constraint")) {
            int ConstrId = PropertyConstraintList::getIndexFromConstraintName(shapetype);
            const std::vector< Constraint * > &vals = this->Constraints.getValues();
            if (ConstrId < 0 || ConstrId >= int(vals.size()))
                return nullptr;
            if(pyObj)
                *pyObj = vals[ConstrId]->getPyObject();
            return const_cast<SketchObject*>(this);
        }
        else {
            return nullptr;
        }
    }

    if (pmat && transform)
        *pmat *= Placement.getValue().toMatrix();

    if (!pyObj) {
        return const_cast<SketchObject*>(this);
    }

    // pyObj exists from here
    Part::TopoShape shape;
    std::string name = convertSubName(indexedName,false);
    if (geo) {
        shape = getEdge(geo,name.c_str());
        if(pmat && !shape.isNull()) {
            shape.transformShape(*pmat,false,true);
        }
    }
    else if (!subshape.isNull()) {
        shape = subshape;
        if (pmat) {
            shape.transformShape(*pmat,false,true);
        }
    }
    else {
        if(pmat) {
            point = (*pmat)*point;
        }
        shape = BRepBuilderAPI_MakeVertex(gp_Pnt(point.x,point.y,point.z)).Vertex();
        // Originally in ComplexGeoData::setElementName
        // LinkStable/src/App/ComplexGeoData.cpp#L1631
        // No longer possible after map separated in ElementMap.cpp
        if (!shape.hasElementMap()) {
            shape.resetElementMap(std::make_shared<Data::ElementMap>());
        }
        shape.setElementName(Data::IndexedName::fromConst("Vertex", 1),
                             Data::MappedName::fromRawData(name.c_str()),0);
    }
    shape.Tag = getID();
    *pyObj = Py::new_reference_to(Part::shape2pyshape(shape));

    return const_cast<SketchObject*>(this);
}

std::vector<Data::IndexedName>
SketchObject::getHigherElements(const char *element, bool silent) const
{
    std::vector<Data::IndexedName> res;
    // App::ObjEditing is not in main yet. Only in LinkStage.
    // It is not a problem yet because getHigherElements is still unused.
    // see https://github.com/FreeCAD/FreeCAD/issues/20753
    if (false /*testStatus(App::ObjEditing)*/) {
        if (boost::istarts_with(element, "vertex")) {
            int n = 0;
            int index = atoi(element+6);
            for (auto cstr : Constraints.getValues()) {
                ++n;
                if (cstr->Type != Sketcher::Coincident) {
                    continue;
                }
                for (int i=0; i<2; ++i) {
                    int geoid = i ? cstr->Second : cstr->First;
                    const Sketcher::PointPos &pos = i ? cstr->SecondPos : cstr->FirstPos;
                    if(geoid >= 0 && index == getSolvedSketch().getPointId(geoid, pos) + 1)
                        res.push_back(Data::IndexedName::fromConst("Constraint", n));
                };
            }
        }
        return res;
    }

    auto getNames = [this, &silent, &res](const char *element) {
        bool internal = boost::starts_with(element, internalPrefix());
        const auto &shape = internal ? InternalShape.getShape() : Shape.getShape();
        for (const auto &indexedName : shape.getHigherElements(element+(internal?internalPrefix().size() : 0), silent)) {
            if (!internal) {
                res.push_back(indexedName);
            }
            else if (boost::equals(indexedName.getType(), "Face")
                    || boost::equals(indexedName.getType(), "Edge")
                    || boost::equals(indexedName.getType(), "Wire")) {
                res.emplace_back((internalPrefix() + indexedName.getType()).c_str(), indexedName.getIndex());
            }
        }
    };
    getNames(element);
    const auto &elementMap = getInternalElementMap();
    auto it = elementMap.find(element);
    if (it != elementMap.end()) {
        res.emplace_back(it->second.c_str());
        getNames(it->second.c_str());
    }
    return res;
}

std::vector<const char *> SketchObject::getElementTypes(bool all) const
{
    if (!all)
        return Part::Part2DObject::getElementTypes();
    static std::vector<const char *> res { Part::TopoShape::shapeName(TopAbs_VERTEX).c_str(),
                Part::TopoShape::shapeName(TopAbs_EDGE).c_str(),
                "ExternalEdge",
                "Constraint",
                "InternalEdge",
                "InternalFace",
                "InternalVertex",
              };
    return res;
}

void SketchObject::setExpression(const App::ObjectIdentifier& path,
                                 std::shared_ptr<App::Expression> expr)
{
    DocumentObject::setExpression(path, std::move(expr));

    if (noRecomputes) {
        // if we do not have a recompute, the sketch must be solved to update the DoF of the solver,
        // constraints and UI
        try {
            auto res = ExpressionEngine.execute();
            if (res) {
                FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": "
                                              << res->Why);
                delete res;
            }
        }
        catch (Base::Exception& e) {
            e.reportException();
            FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": " << e.what());
        }
        solve();
    }
}

const std::string &SketchObject::internalPrefix()
{
    static std::string _prefix("Internal");
    return _prefix;
}

const char *SketchObject::convertInternalName(const char *name)
{
    if (name && boost::starts_with(name, internalPrefix()))
        return name + internalPrefix().size();
    return nullptr;
}

App::ElementNamePair SketchObject::getElementName(
        const char *name, ElementNameType type) const
{
    App::ElementNamePair ret;
    if(!name) return ret;

    if(hasSketchMarker(name))
        return Part2DObject::getElementName(name,type);

    const char *mapped = Data::isMappedElement(name);
    Data::IndexedName index = checkSubName(name);
    index.appendToStringBuffer(ret.oldName);
    if (auto realName = convertInternalName(ret.oldName.c_str())) {
        Data::MappedElement mappedElement;
        if (mapped)
            mappedElement = InternalShape.getShape().getElementName(name);
        else if (type == ElementNameType::Export)
            ret.newName = getExportElementName(InternalShape.getShape(), realName).newName;
        else
            mappedElement = InternalShape.getShape().getElementName(realName);

        if (mapped || type != ElementNameType::Export) {
            if (mappedElement.index) {
                ret.oldName = internalPrefix();
                mappedElement.index.appendToStringBuffer(ret.oldName);
            }
            if (mappedElement.name) {
                ret.newName = Data::ComplexGeoData::elementMapPrefix();
                mappedElement.name.appendToBuffer(ret.newName);
            }
            else if (mapped)
                ret.newName = name;
        }

        if (ret.newName.size()) {
            if (auto dot = strrchr(ret.newName.c_str(), '.'))
                ret.newName.resize(dot+1-ret.newName.c_str());
            else
                ret.newName += ".";
            ret.newName += ret.oldName;
        }
        if (mapped && (!mappedElement.index || !mappedElement.name))
            ret.oldName.insert(0, Data::MISSING_PREFIX);
        return ret;
    }

    if(!mapped) {
        auto occindex = Part::TopoShape::shapeTypeAndIndex(name);
        if (occindex.second)
            return Part2DObject::getElementName(name,type);
    }
    if(index && type==ElementNameType::Export) {
        if(boost::starts_with(ret.oldName,"Vertex"))
            ret.oldName[0] = 'v';
        else if(boost::starts_with(ret.oldName,"Edge"))
            ret.oldName[0] = 'e';
    }
    ret.newName = convertSubName(index, true);
    if(!Data::isMappedElement(ret.newName.c_str()))
        ret.newName.clear();
    return ret;
}

Data::IndexedName SketchObject::checkSubName(const char *subname) const
{
    static std::vector<const char *> types = {
        "Edge",
        "Vertex",
        "edge",
        "vertex",
        "ExternalEdge",
        "RootPoint",
        "H_Axis",
        "V_Axis",
        "Constraint",

        // other feature from LS3 not related to TNP
        "InternalEdge",
        "InternalFace",
        "InternalVertex",
    };

    if(!subname) return Data::IndexedName();
    const char *mappedSubname = Data::isMappedElement(subname);

    // if not a mapped name parse the indexed name directly, uppercasing "edge" and "vertex"
    if(!mappedSubname)  {
        Data::IndexedName result(subname, types, true);
        if (boost::equals(result.getType(), "edge"))
            return Data::IndexedName("Edge", result.getIndex());
        if (boost::equals(result.getType(), "vertex"))
            return Data::IndexedName("Vertex", result.getIndex());
        return result;
    }

    bio::stream<bio::array_source> iss(mappedSubname+1, std::strlen(mappedSubname+1));
    long id = -1;
    bool valid = false;
    switch (mappedSubname[0]) {
        case '\0':  // check length != 0
            break;

        case 'g':  // = geometry
        case 'e':  // = external geometry
            if (readGeometryId(iss, id)) {
                valid = true;
            }
            break;

        // for RootPoint, H_Axis, V_Axis
        default: {
            const char* dot = strchr(mappedSubname, '.');
            if (dot) {
                mappedSubname = dot + 1;
            }
            return Data::IndexedName(mappedSubname, types, false);
        }
    }

    if (!valid) {
        FC_ERR("invalid subname " << subname);
        return Data::IndexedName();
    }

    int geoId;
    const Part::Geometry* geo = 0;
    switch (mappedSubname[0]) {
        case 'g': {
            auto it = geoMap.find(id);
            if (it != geoMap.end()) {
                geoId = it->second;
                geo = getGeometry(geoId);
            }
            break;
        }
        case 'e': {
            auto it = externalGeoMap.find(id);
            if (it != externalGeoMap.end()) {
                geoId = -it->second - 1;
                geo = getGeometry(geoId);
            }
            break;
        }
    }
    if (geo && GeometryFacade::getId(geo) == id) {
        char sep;
        int posId = static_cast<int>(PointPos::none);
        if ((iss >> sep >> posId) && sep == 'v') {
            int idx = getVertexIndexGeoPos(geoId, static_cast<PointPos>(posId));

            // Outside edit-mode circles exposes the seam point but not the center, while in edit-mode we expose the center but not the seam.
            // getVertexIndexGeoPos searching for a circle start point (g1v1 for example) (which happens outside of edit mode) will fail.
            // see https://github.com/FreeCAD/FreeCAD/issues/25089
            // The following fix works because circles have always 1 vertex, whether in or out of edit mode.
            if (idx < 0 && (static_cast<PointPos>(posId) == PointPos::start || static_cast<PointPos>(posId) == PointPos::end)) {
                if (geo->is<Part::GeomCircle>() || geo->is<Part::GeomEllipse>()) {
                    idx = getVertexIndexGeoPos(geoId, PointPos::mid);
                }
            }

            if (idx < 0) {
                FC_ERR("invalid subname " << subname);
                return Data::IndexedName();
            }
            return Data::IndexedName::fromConst("Vertex", idx + 1);
        }
        else if (geoId >= 0) {
            return Data::IndexedName::fromConst("Edge", geoId + 1);
        }
        else {
            return Data::IndexedName::fromConst("ExternalEdge", -geoId - 2);
        }
    }
    FC_ERR("cannot find subname " << subname);

    return Data::IndexedName();
}

std::string SketchObject::convertSubName(const char *subname, bool postfix) const {
    return convertSubName(checkSubName(subname), postfix);
}

std::string SketchObject::convertSubName(const Data::IndexedName &indexedName, bool postfix) const {
    std::ostringstream ss;
    if (auto realType = convertInternalName(indexedName.getType())) {
        auto mapped = InternalShape.getShape().getMappedName(
                Data::IndexedName::fromConst(realType, indexedName.getIndex()));
        if (!mapped) {
            if (postfix)
                ss << indexedName;
        } else if (postfix)
            ss << Data::ComplexGeoData::elementMapPrefix() << mapped << '.' << indexedName;
        else
            ss << mapped;
        return ss.str();
    }
    int geoId;
    PointPos posId;
    if (!geoIdFromShapeType(indexedName, geoId, posId)) {
        ss << indexedName;
        return ss.str();
    }
    if (geoId == Sketcher::GeoEnum::HAxis ||
        geoId == Sketcher::GeoEnum::VAxis ||
        geoId == Sketcher::GeoEnum::RtPnt) {
        if (postfix)
            ss << Data::ELEMENT_MAP_PREFIX;
        ss << indexedName;
        if (postfix)
            ss << '.' << indexedName;
        return ss.str();
    }

    auto geo = getGeometry(geoId);
    if (!geo) {
        std::string res = indexedName.toString();
        return res;
    }
    if (postfix)
        ss << Data::ELEMENT_MAP_PREFIX;
    ss << (geoId >= 0 ? 'g' : 'e');
    writeGeometryId(ss, GeometryFacade::getFacade(geo)->getId());
    if (posId != PointPos::none)
        ss << 'v' << static_cast<int>(posId);
    if (postfix) {
        // rename Edge to edge, and Vertex to vertex to avoid ambiguous of
        // element mapping of the public shape and internal geometry.
        if (indexedName.getIndex() <= 0)
            ss << '.' << indexedName;
        else if (boost::starts_with(indexedName.getType(), "Edge"))
            ss << ".e" << (indexedName.getType() + 1) << indexedName.getIndex();
        else if (boost::starts_with(indexedName.getType(), "Vertex"))
            ss << ".v" << (indexedName.getType() + 1) << indexedName.getIndex();
        else
            ss << '.' << indexedName;
    }
    return ss.str();
}

// clang-format on
