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

// clang-format off
using namespace Sketcher;
using namespace Base;
namespace sp = std::placeholders;
namespace bio = boost::iostreams;

FC_LOG_LEVEL_INIT("Sketch", true, true)

PROPERTY_SOURCE(Sketcher::SketchObject, Part::Part2DObject)

SketchObject::SketchObject() : geoLastId(0)
{
    ADD_PROPERTY_TYPE(
        Geometry, (nullptr), "Sketch", (App::PropertyType)(App::Prop_None), "Sketch geometry");
    ADD_PROPERTY_TYPE(Constraints,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None),
                      "Sketch constraints");
    ADD_PROPERTY_TYPE(ExternalGeometry,
                      (nullptr, nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None | App::Prop_ReadOnly),
                      "Sketch external geometry");
    ADD_PROPERTY_TYPE(ExternalTypes,
                      ({}),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None | App::Prop_Hidden),
                      "Sketch external geometry type: 0 = projection, 1 = intersection, 2 = both.");
    ADD_PROPERTY_TYPE(FullyConstrained,
                      (false),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Output | App::Prop_ReadOnly | App::Prop_Hidden),
                      "Sketch is fully constrained");
    ADD_PROPERTY_TYPE(Exports,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Hidden),"Sketch export geometry");
    ADD_PROPERTY_TYPE(ExternalGeo,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Hidden),"Sketch external geometry");
    ADD_PROPERTY_TYPE(ArcFitTolerance,
                      (0.0),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None),
                      "Tolerance for fitting arcs of projected external geometry");
    ADD_PROPERTY(InternalShape,
                 (Part::TopoShape()));
    ADD_PROPERTY_TYPE(MakeInternals,
                      (false),
                      "Internal Geometry",
                      App::Prop_None,
                      "Enables selection of closed profiles within a sketch as input for operations");

    Geometry.setOrderRelevant(true);

    allowUnaligned = true;

    initExternalGeo();

    rebuildVertexIndex();

    lastDoF = 0;
    lastHasConflict = false;
    lastHasRedundancies = false;
    lastHasPartialRedundancies = false;
    lastHasMalformedConstraints = false;
    lastSolverStatus = 0;
    lastSolveTime = 0;

    solverNeedsUpdate = false;

    noRecomputes = false;

    //NOLINTBEGIN
    ExpressionEngine.setValidator(
        std::bind(&Sketcher::SketchObject::validateExpression, this, sp::_1, sp::_2));

    constraintsRemovedConn = Constraints.signalConstraintsRemoved.connect(
        std::bind(&Sketcher::SketchObject::constraintsRemoved, this, sp::_1));
    constraintsRenamedConn = Constraints.signalConstraintsRenamed.connect(
        std::bind(&Sketcher::SketchObject::constraintsRenamed, this, sp::_1));
    //NOLINTEND

    analyser = new SketchAnalysis(this);

    internaltransaction = false;
    managedoperation = false;

    registerElementCache(internalPrefix(), &InternalShape);
}

SketchObject::~SketchObject() {
    delete analyser;
}

void SketchObject::setupObject()
{
    ParameterGrp::handle hGrpp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
    ArcFitTolerance.setValue(hGrpp->GetFloat("ArcFitTolerance", Precision::Confusion()*10.0));
    MakeInternals.setValue(hGrpp->GetBool("MakeInternals", true));
    inherited::setupObject();
}

short SketchObject::mustExecute() const
{
    if (Geometry.isTouched())
        return 1;
    if (Constraints.isTouched())
        return 1;
    if (ExternalGeometry.isTouched())
        return 1;
    if (ExternalGeo.isTouched())
        return 1;
    return Part2DObject::mustExecute();
}

App::DocumentObjectExecReturn* SketchObject::execute()
{
    try {
        App::DocumentObjectExecReturn* rtn = Part2DObject::execute();// to positionBySupport
        if (rtn != App::DocumentObject::StdReturn)
            // error
            return rtn;
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    // setup and diagnose the sketch
    try {
        rebuildExternalGeometry();
        Constraints.acceptGeometry(getCompleteGeometry());
    }
    catch (const Base::Exception&) {
        // 9/16/24: We used to clear the constraints here, but we no longer want to do that
        // as missing reference geometry is not considered an error while we sort out sketcher UI.
        // Base::Console().error("%s\nClear constraints to external geometry\n", e.what());
        // we cannot trust the constraints of external geometries, so remove them
        //  delConstraintsToExternal();
    }

    // This includes a regular solve including full geometry update, except when an error
    // ensues
    int err = this->solve(true);

    if (err == -4) {// over-constrained sketch
        std::string msg = "Over-constrained sketch\n";
        appendConflictMsg(lastConflicting, msg);
        return new App::DocumentObjectExecReturn(msg.c_str(), this);
    }
    else if (err == -3) {// conflicting constraints
        std::string msg = "Sketch with conflicting constraints\n";
        appendConflictMsg(lastConflicting, msg);
        return new App::DocumentObjectExecReturn(msg.c_str(), this);
    }
    else if (err == -2) {// redundant constraints
        std::string msg = "Sketch with redundant constraints\n";
        appendRedundantMsg(lastRedundant, msg);
        return new App::DocumentObjectExecReturn(msg.c_str(), this);
    }
    else if (err == -5) {
        std::string msg = "Sketch with malformed constraints\n";
        appendMalformedConstraintsMsg(lastMalformedConstraints, msg);
        return new App::DocumentObjectExecReturn(msg.c_str(), this);
    }
    else if (err == -1) {// Solver failed
        return new App::DocumentObjectExecReturn("Solving the sketch failed", this);
    }

    // this is not necessary for sketch representation in edit mode, unless we want to trigger an
    // update of the objects that depend on this sketch (like pads)
    buildShape();

    return App::DocumentObject::StdReturn;
}

static bool inline checkSmallEdge(const Part::TopoShape &s) {
    if (s.shapeType() != TopAbs_EDGE)
        return false;
    BRepAdaptor_Curve adapt(TopoDS::Edge(s.getShape()));
    return GCPnts_AbscissaPoint::Length(adapt, Precision::Confusion()) <= Precision::Confusion();
}

// clang-format on
void SketchObject::buildShape()
{
    // We use the following instead to map element names

    std::vector<Part::TopoShape> shapes;
    std::vector<Part::TopoShape> vertices;
    int geoId = 0;

    auto addVertex = [&vertices](auto vertex, auto name) {
        if (!vertex.hasElementMap()) {
            vertex.resetElementMap(std::make_shared<Data::ElementMap>());
        }
        vertex.setElementName(
            Data::IndexedName::fromConst("Vertex", 1),
            Data::MappedName::fromRawData(name.c_str()),
            0L
        );
        vertices.push_back(vertex);
        vertices.back().copyElementMap(vertex, Part::OpCodes::Sketch);
    };

    auto addEdge = [this, &shapes](auto geo, auto indexedName) {
        shapes.push_back(getEdge(geo, convertSubName(indexedName, false).c_str()));
        if (checkSmallEdge(shapes.back())) {
            FC_WARN("Edge too small: " << indexedName);
        }
    };

    // get the geometry after running the solver
    auto geometries = solvedSketch.extractGeometry();
    for (auto geo : geometries) {
        ++geoId;
        if (GeometryFacade::getConstruction(geo)) {
            continue;
        }
        if (geo->isDerivedFrom<Part::GeomPoint>()) {
            int idx = getVertexIndexGeoPos(geoId - 1, Sketcher::PointPos::start);
            addVertex(
                Part::TopoShape {TopoDS::Vertex(geo->toShape())},
                convertSubName(Data::IndexedName::fromConst("Vertex", idx + 1), false)
            );
        }
        else {
            auto indexedName = Data::IndexedName::fromConst("Edge", geoId);
            addEdge(geo, indexedName);
        }
    }

    for (auto geo : geometries) {
        delete geo;
    }

    for (int i = 2; i < ExternalGeo.getSize(); ++i) {
        auto geo = ExternalGeo[i];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if (!egf->testFlag(ExternalGeometryExtension::Defining)) {
            continue;
        }

        auto indexedName = Data::IndexedName::fromConst("ExternalEdge", i - 1);

        if (geo->isDerivedFrom<Part::GeomPoint>()) {
            addVertex(
                Part::TopoShape {TopoDS::Vertex(geo->toShape())},
                convertSubName(indexedName, false)
            );
        }
        else {
            addEdge(geo, indexedName);
        }
    }

    internalElementMap.clear();

    if (shapes.empty() && vertices.empty()) {
        InternalShape.setValue(Part::TopoShape());
        Shape.setValue(Part::TopoShape());
        return;
    }
    Part::TopoShape result(0, getDocument()->getStringHasher());
    if (vertices.empty()) {
        // Notice here we supply op code Part::OpCodes::Sketch to makeElementWires().
        result.makeElementWires(shapes, Part::OpCodes::Sketch);
    }
    else {
        std::vector<Part::TopoShape> results;
        if (!shapes.empty()) {
            // Note, that we HAVE TO add the Part::OpCodes::Sketch op code to all
            // geometry exposed through the Shape property, because
            // SketchObject::getElementName() relies on this op code to
            // differentiate geometries that are exposed with those in edit
            // mode.
            auto wires = Part::TopoShape().makeElementWires(shapes, Part::OpCodes::Sketch);
            for (const auto& wire : wires.getSubTopoShapes(TopAbs_WIRE)) {
                results.push_back(wire);
            }
        }
        results.insert(results.end(), vertices.begin(), vertices.end());
        result.makeElementCompound(results);
    }
    result.Tag = getID();
    InternalShape.setValue(buildInternals(result.located(TopLoc_Location())));
    // Must set Shape property after InternalShape so that
    // GeoFeature::updateElementReference() can run properly on change of Shape
    // property, because some reference may pointing to the InternalShape
    Shape.setValue(result);
}
// clang-format off

const std::map<std::string,std::string> SketchObject::getInternalElementMap() const
{
    if (!internalElementMap.empty() || !MakeInternals.getValue())
        return internalElementMap;

    const auto& internalShape = InternalShape.getShape();
    auto shape = Shape.getShape().located(TopLoc_Location());
    if (!internalShape.isNull() && !shape.isNull()) {
        std::vector<std::string> names;
        std::string prefix;
        const std::array<TopAbs_ShapeEnum, 2> types = {TopAbs_VERTEX, TopAbs_EDGE};
        for (const auto &type : types) {
            prefix = internalPrefix() + Part::TopoShape::shapeName(type);
            std::size_t len = prefix.size();
            int i=0;
            for (const auto &v : internalShape.getSubTopoShapes(type)) {
                ++i;
                shape.findSubShapesWithSharedVertex(v, &names, Data::SearchOption::CheckGeometry
                                                |Data::SearchOption::SingleResult);
                if (names.empty())
                    continue;
                prefix += std::to_string(i);
                internalElementMap[prefix] = names.front();
                internalElementMap[names.front()] = prefix;
                prefix.resize(len);
                names.clear();
            }
        }
    }
    return internalElementMap;
}

Part::TopoShape SketchObject::buildInternals(const Part::TopoShape &edges) const {
    if (!MakeInternals.getValue())
        return Part::TopoShape();

    try {
        Part::TopoShape result(getID(), getDocument()->getStringHasher());
        result = result.makeElementFace(edges.getSubTopoShapes(TopAbs_WIRE),
                /*op*/"",
                /*maker*/"Part::FaceMakerBuildFace",
                /*pln*/nullptr
        );

        // Append open wires (edges not part of any closed face)
        Part::WireJoiner joiner;
        joiner.setTightBound(true);
        joiner.setMergeEdges(true);
        joiner.addShape(edges);
        Part::TopoShape openWires(getID(), getDocument()->getStringHasher());
        joiner.getOpenWires(openWires, "SKF");

        if (openWires.isNull()) {
            return result;  // No open wires, return either face or empty toposhape
        }
        if (result.isNull()) {
            return openWires;   // No face, but we have open wires to return as a shape
        }
        return result.makeElementCompound({result, openWires}); // Compound and return both
    } catch (Base::Exception &e) {
        FC_WARN("Failed to make face for sketch: " << e.what());
    } catch (Standard_Failure &e) {
        FC_WARN("Failed to make face for sketch: " << e.GetMessageString());
    }
    return Part::TopoShape();
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

// NOLINTNEXTLINE
BOOST_GEOMETRY_REGISTER_POINT_3D(Base::Vector3d, double, bg::cs::cartesian, x, y, z)

class SketchObject::GeoHistory
{
private:
    static constexpr int bgiMaxElements = 16;

    using Parameters = bgi::linear<bgiMaxElements>;
    using IdSet = std::set<long>;
    using IdSets = std::pair<IdSet, IdSet>;
    using AdjList = std::list<IdSet>;

    // associate a geo with connected ones on both points
    using AdjMap = std::map<long, IdSets>;

    // maps start/end points to all existing geo to query and update adjacencies
    using Value = std::pair<Base::Vector3d, AdjList::iterator>;

    AdjList adjlist;
    AdjMap adjmap;
    bgi::rtree<Value,Parameters> rtree;

public:
    AdjList::iterator find(const Base::Vector3d &pt,bool strict=true){
        std::vector<Value> ret;
        rtree.query(bgi::nearest(pt, 1), std::back_inserter(ret));
        if (!ret.empty()) {
            // NOTE: we are using square distance here, the 1e-6 threshold is
            // very forgiving. We should have used Precision::SquareConfisuion(),
            // which is 1e-14. However, there is a problem with current
            // commandGeoCreate. They create new geometry with initial point of
            // the exact mouse position, instead of the preselected point
            // position, and rely on auto constraint to snap in the new
            // geometry. So, we cannot use a very strict threshold here.
            double tol = strict?Precision::SquareConfusion()*10:1e-6;
            double d = Base::DistanceP2(ret[0].first,pt);
            if(d<tol) {
                return ret[0].second;
            }
        }
        return adjlist.end();
    }

    void clear() {
        rtree.clear();
        adjlist.clear();
    }

    void update(const Base::Vector3d &pt, long id) {
        FC_TRACE("update " << id << ", " << FC_xyz(pt));
        auto it = find(pt);
        if(it==adjlist.end()) {
            adjlist.emplace_back();
            it = adjlist.end();
            --it;
            rtree.insert(std::make_pair(pt,it));
        }
        it->insert(id);
    }

    void finishUpdate(const std::map<long,int> &geomap) {
        IdSet oldset;
        for(auto &idset : adjlist) {
            oldset.clear();
            for(long _id : idset) {
                long id = abs(_id);
                auto& v = adjmap[id];
                auto& adj = _id > 0 ? v.first : v.second;
                for (auto it = adj.begin(); it != adj.end(); /* don't advance here */) {
                    long other = *it;
                    auto removeId = it++;  // grab ID we might erase, and advance
                    if (geomap.find(other) == geomap.end()) {
                        // remember those deleted IDs to swap in below
                        oldset.insert(other);
                    }
                    else if (idset.find(other) == idset.end()) {
                        // delete any existing IDs that are no longer in the adj list
                        adj.erase(removeId);
                    }
                }
                // now merge the current ones
                for(long _id2 : idset) {
                    long id2 = abs(_id2);
                    if(id!=id2) {
                        adj.insert(id2);
                    }
                }
            }
            // now reset the adjacency list with only those deleted id's,
            // because the whole purpose of this history is to try to reuse
            // deleted id.
            idset.swap(oldset);
        }
    }

    AdjList::iterator end() {
        return adjlist.end();
    }

    size_t size() {
        return rtree.size();
    }
};

void SketchObject::updateGeoHistory() {
    if(!geoHistoryLevel) return;

    if (!geoHistory) {
        geoHistory = std::make_unique<GeoHistory>();
    }

    Base::TimeTracker tracker("updateGeoHistory");
    const auto &geos = getInternalGeometry();
    geoHistory->clear();
    for (auto geo : geos) {
        auto pstart = getPoint(geo, PointPos::start);
        auto pend = getPoint(geo, PointPos::end);
        long id = GeometryFacade::getId(geo);
        geoHistory->update(pstart,id);
        if(pstart!=pend)
            geoHistory->update(pend,-id);
    }
    geoHistory->finishUpdate(geoMap);
}

// clang-format on

void SketchObject::generateId(const Part::Geometry* geo)
{
    auto preReturn = [this, &geo](long newId) {
        GeometryFacade::setId(geo, newId);
        geoMap[Sketcher::GeometryFacade::getId(geo)] = (long)Geometry.getSize();
    };

    auto isNotInGeoMap = [this](auto& id) {
        if (geoMap.find(id) == geoMap.end()) {
            return true;
        }
        FC_TRACE("ignore " << id);
        return false;
    };

    if (geoHistoryLevel == 0) {
        preReturn(mintGeometryId(geo));
        return;
    }

    if (!geoHistory) {
        updateGeoHistory();
    }

    // Search geo history to see if the start point and end point belongs to
    // some deleted geometries. Prefer matching both start and end point. If
    // can't then try start and then end. Generate new id if none is found.
    auto pstart = getPoint(geo, PointPos::start);
    auto it = geoHistory->find(pstart, false);
    auto pend = getPoint(geo, PointPos::end);
    auto it2 = it;
    if (pstart != pend) {
        it2 = geoHistory->find(pend, false);
        if (it2 == geoHistory->end()) {
            it2 = it;
        }
    }
    std::vector<long> found;

    if (geoHistoryLevel <= 1 && (it == geoHistory->end() || it2 == it)) {
        // level <= 1 means we only reuse id if both start and end matches
        preReturn(mintGeometryId(geo));
        return;
    }

    if (it != geoHistory->end()) {
        // `find_if` avoids checking twice
        auto iterOfId = std::ranges::find_if(*it, isNotInGeoMap);
        if (iterOfId != it->end() && it2 == it) {
            preReturn(*iterOfId);
            return;
        }
        std::copy_if(iterOfId, it->end(), std::back_inserter(found), isNotInGeoMap);
    }
    if (found.empty()) {
        // no candidate exists
        if (it2 == it) {
            preReturn(mintGeometryId(geo));
            return;
        }
        auto iterOfId = std::ranges::find_if(*it, isNotInGeoMap);
        if (iterOfId != it->end()) {
            preReturn(*iterOfId);
            return;
        }
        preReturn(mintGeometryId(geo));
        return;
    }

    auto isInIt2 = [&it2](auto& id) {
        if (it2->find(id) != it2->end()) {
            return true;
        }
        FC_TRACE("ignore " << id);
        return false;
    };

    // already some candidate exists, search for matching of both
    // points
    if (it2 != it) {
        auto iterOfId = std::ranges::find_if(found, isInIt2);
        if (iterOfId != found.end()) {
            preReturn(*iterOfId);
            return;
        }
    }
    FC_TRACE("found " << found.front());
    preReturn(found.front());
}
// clang-format off

int SketchObject::setTextAndFont(int ConstrId, std::string& newText, std::string& newFont, bool isHeight, bool isConstruction)
{
;    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // set the changed value for the constraint
    if (this->Constraints.hasInvalidGeometry()) {
        return -6;
    }
    const std::vector<Constraint*>& vals = this->Constraints.getValues();
    if (ConstrId < 0 || ConstrId >= int(vals.size())) {
        return -1;
    }

    auto* constr = vals[ConstrId];
    if (constr->Type != Text || !constr->hasElement(0)) {
        return -1;
    }

    // First we replace the old geometries by the new text.
    const std::string oldText = constr->getText();
    const std::string oldFont = constr->getFont();
    const bool oldIsHeight = constr->getIsTextHeight();
    int handleGeoId = constr->getGeoId(0);
    int firstTextGeoId = constr->getGeoId(1);
    bool hasExistingText = firstTextGeoId != GeoEnum::GeoUndef;
    bool handleLast = handleGeoId > firstTextGeoId;

    if (hasExistingText) {
        // Check if text is construction or normal geos
        auto* geo1 = getGeometry(firstTextGeoId);
        isConstruction = GeometryFacade::getConstruction(geo1);

        // Delete all the old text geos. Not the handle!
        std::vector<int> geoIdsToDelete;
        for (int i = 1; constr->hasElement(i); ++i) {
            if (constr->getGeoId(i) == GeoEnum::GeoUndef) {
                continue;
            }
            geoIdsToDelete.push_back(constr->getGeoId(i));
            if (handleLast) {
                --handleGeoId; // handle line is added after all text geos.
            }
        }

        delGeometries(geoIdsToDelete);
    }

    auto* line = dynamic_cast<const Part::GeomLineSegment*>(getGeometry(handleGeoId));
    if (!line) {
        return -1;
    }

    // Generate text geos based on new text/font :
    std::vector<std::unique_ptr<Part::Geometry>> newGeos;
    std::vector<TopoDS_Shape> shapes = Part::makeTextWires(newText, newFont);
    Part::transformAndConvertToGeometry(newGeos,
                                    shapes,
                                    line->getStartPoint(),
                                    line->getEndPoint(),
                                    isHeight);

    // Add the geometries to sketch
    int lastGeoid = getHighestCurveIndex();
    std::vector<Part::Geometry*> newGeosRawPtrs;
    newGeosRawPtrs.reserve(newGeos.size());

    // Populate the raw pointer vector and release ownership from the unique_ptrs.
    for (auto& geo_ptr : newGeos) {
        if (isConstruction) {
            Sketcher::GeometryFacade::setConstruction(geo_ptr.get(), isConstruction);
        }
        // Add the raw pointer to the new vector.
        newGeosRawPtrs.push_back(geo_ptr.get());
        // Release ownership from the unique_ptr. The SketchObject will now manage this memory.
        geo_ptr.release();
    }
    newGeos.clear();
    addGeometry(newGeosRawPtrs);

    int newLastGeoid = getHighestCurveIndex();

    // If there was text geos, they were deleted, which deleted the text constraint.
    // In this case create a new constraint to replace it.
    if (hasExistingText) {
        constr = new Constraint();
        constr->Type = Text;
        constr->truncateElements(0); // remove the First/Second/Third that are created automatically
        constr->addElement(GeoElementId(handleGeoId));
    }
    for (int i = lastGeoid + 1; i <= newLastGeoid; ++i) {
        constr->addElement(GeoElementId(i));
    }
    constr->setText(newText);
    constr->setFont(newFont);
    constr->setIsTextHeight(isHeight);

    if (hasExistingText) {
        addConstraint(constr);
    }

    int err = solve();

    if (err) {
        constr->setText(oldText);
        constr->setFont(oldFont);
        constr->setIsTextHeight(oldIsHeight);
    }

    return err;
}

void SketchObject::acceptGeometry()
{
    Constraints.acceptGeometry(getCompleteGeometry());
    rebuildVertexIndex();
    signalElementsChanged();
}

int SketchObject::setGeometry(int GeoId, const Part::Geometry *geo) {
    std::unique_ptr<Part::Geometry> g(geo->clone());
    if(GeoId>=0 && GeoId <Geometry.getSize()) {
        Geometry.set1Value(GeoId,std::move(g));
    } else if(GeoId <= GeoEnum::RefExt && -GeoId-1 < ExternalGeo.getSize()) {
        ExternalGeo.set1Value(-GeoId-1,std::move(g));
    } else
        return -1;
    return 0;
}

bool SketchObject::evaluateSupport()
{
    // returns false if the shape is broken, null or non-planar
    App::DocumentObject* link = AttachmentSupport.getValue();
    if (!link || !Part::hasShape(link))
        return false;
    return true;
}

bool SketchObject::isInGroup(int geoId, bool includeHandle) const
{
    const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

    for (const auto& constr : vals) {
        if (constr->Type == Group || constr->Type == Text) {
            // First is the group construction line. We include it or not in our search.
            int iStart = includeHandle ? 0 : 1;
            for (int i = iStart; constr->hasElement(i); ++i) {
                if (constr->getGeoId(i) == geoId) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool SketchObject::isGroupHandle(int geoId) const
{
    const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

    for (const auto& constr : vals) {
        if (constr->Type == Group || constr->Type == Text) {
            if (constr->getGeoId(0) == geoId) {
                return true;
            }
        }
    }
    return false;
}

int SketchObject::getGroupHandleIfInGroup(int geoId)
{
    const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

    for (const auto& constr : vals) {
        if (constr->Type == Group || constr->Type == Text) {
            // First is the group construction line.
            int groupHandleGeoId = -1;
            for (int i = 0; constr->hasElement(i); ++i) {
                if (i == 0) {
                    groupHandleGeoId = constr->getGeoId(i);
                }
                else if (constr->getGeoId(i) == geoId) {
                    return groupHandleGeoId;
                }
            }
        }
    }
    return geoId;
}

std::set<int> SketchObject::getGroupGeometries(int handleGeoId) const
{
    std::set<int> geoIds;
    const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();
    for (const auto& constr : vals) {
        if (constr->Type == Group || constr->Type == Text) {
            if (constr->getGeoId(0) == handleGeoId) {
                for (int i = 1; constr->hasElement(i); ++i) {
                    geoIds.insert(constr->getElement(i).GeoId);
                }
            }
        }
    }
    return geoIds;
}

PyObject* SketchObject::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new SketchObjectPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

unsigned int SketchObject::getMemSize() const
{
    return 0;
}

static inline bool checkMigration(Part::PropertyGeometryList &prop)
{
    for (auto g : prop.getValues()) {
        if(g->hasExtension(Part::GeometryMigrationExtension::getClassTypeId())
            || !g->hasExtension(SketchGeometryExtension::getClassTypeId()))
            return true;
    }
    return false;
}

void SketchObject::onChanged(const App::Property* prop)
{
    if (prop == &Geometry) {
        onGeometryChanged();
    }
    else if (prop == &Constraints) {
        onConstraintsChanged();
    }
    else if (prop == &ExternalGeo && !prop->testStatus(App::Property::User3)) {
        onExternalGeoChanged();
    }
    else if (prop == &ExternalGeometry) {
        onExternalGeometryChanged();
    }
    else if (prop == &Placement) {
        onPlacementChanged();
    }
    else if (prop == &ExpressionEngine) {
        onExpressionEngineChanged();
    }
#if 0
    // For now do not delete anything (#0001791). When changing the support
    // face it might be better to check which external geometries can be kept.
    else if (prop == &AttachmentSupport) {
        onAttachmentSupportChanged();
    }
#endif
    Part::Part2DObject::onChanged(prop);
}

void SketchObject::onGeometryChanged()
{
    if (isRestoring() && checkMigration(Geometry)) {
        // Construction migration to extension
        for (auto geometryValue : Geometry.getValues()) {
            if (!geometryValue->hasExtension(
                      Part::GeometryMigrationExtension::getClassTypeId())) {
                continue;
            }

            auto ext = std::static_pointer_cast<Part::GeometryMigrationExtension>(
                geometryValue
                ->getExtension(Part::GeometryMigrationExtension::getClassTypeId())
                .lock());

            // at this point IA geometry is already migrated
            auto gf = GeometryFacade::getFacade(geometryValue);

            if (ext->testMigrationType(Part::GeometryMigrationExtension::Construction)) {
                bool oldconstr = ext->getConstruction()
                    || (geometryValue->is<Part::GeomPoint>() && !gf->isInternalAligned());
                gf->setConstruction(oldconstr);
            }
            if (ext->testMigrationType(Part::GeometryMigrationExtension::GeometryId)) {
                gf->setId(ext->getId());
            }
        }
    }
    geoMap.clear();
    const auto &vals = getInternalGeometry();
    for (long i = 0; i < (long)vals.size(); ++i) {
        auto geo = vals[i];
        auto gf = GeometryFacade::getFacade(geo);
        if (gf->getId() == 0) {
            gf->setId(mintGeometryId(geo));
        }
        else if (gf->getId() > geoLastId) {
            geoLastId = gf->getId();
        }
        while (!geoMap.insert(std::make_pair(gf->getId(), i)).second) {
            FC_WARN("duplicate geometry id " << gf->getId());  // NOLINT
            gf->setId(mintGeometryId(geo));
        }
    }
    updateGeoHistory();

    auto doc = getDocument();

    if (doc && doc->isPerformingTransaction()) {
        // undo/redo
        setStatus(App::PendingTransactionUpdate, true);
        return;
    }

    if (internaltransaction) {
        return;
    }

    // internal sketchobject operations changing both geometry and constraints will
    // explicitly perform an update

    if (managedoperation || isRestoring()) {
        // if geometry changed, the constraint geometry indices must be updated
        acceptGeometry();
        return;
    }

    // this change was not effect via SketchObject, but using direct access to
    // properties, check input data

    // declares constraint invalid if indices go beyond the geometry and any
    // call to getValues with return an empty list until this is fixed.
    bool invalidinput = Constraints.checkConstraintIndices(
        getHighestCurveIndex(), -getExternalGeometryCount());

    if (!invalidinput) {
        acceptGeometry();
    }
    else {
        Base::Console().error(
            this->getFullLabel() + " SketchObject::onChanged ",
            QT_TRANSLATE_NOOP("Notifications", "Unmanaged change of Constraint "
                              "Property results in invalid constraint indices") "\n");
    }
    Base::StateLocker lock(internaltransaction, true);
    setUpSketch();
}

void SketchObject::onConstraintsChanged()
{
    auto doc = getDocument();

    if (doc && doc->isPerformingTransaction()) {
        // undo/redo
        setStatus(App::PendingTransactionUpdate, true);
        return;
    }

    if (internaltransaction) {
        return;
    }

    if (managedoperation || isRestoring()) {
        Constraints.checkGeometry(getCompleteGeometry());
        return;
    }

    // this change was not effect via SketchObject, but using direct access to
    // properties, check input data

    // declares constraint invalid if indices go beyond the geometry and any
    // call to getValues with return an empty list until this is fixed.
    bool invalidinput = Constraints.checkConstraintIndices(
        getHighestCurveIndex(), -getExternalGeometryCount());

    if (!invalidinput) {
        if (Constraints.checkGeometry(getCompleteGeometry())) {
            // if there are invalid geometry indices in the constraints, we need
            // to update them
            acceptGeometry();
        }
    }
    else {
        Base::Console().error(
            this->getFullLabel() + " SketchObject::onChanged ",
            QT_TRANSLATE_NOOP("Notifications", "Unmanaged change of Constraint "
                              "Property results in invalid constraint indices") "\n");
    }
    Base::StateLocker lock(internaltransaction, true);
    setUpSketch();
}

/// not to be confused with `onExternalGeometryChanged`. These names may need fixing.
void SketchObject::onExternalGeoChanged()
{
    if (ExternalGeo.testStatus(App::Property::User3)) {
        return;
    }

    auto doc = getDocument();

    if (doc && doc->isPerformingTransaction()) {
        setStatus(App::PendingTransactionUpdate, true);
    }

    if (isRestoring() && checkMigration(ExternalGeo)) {
        for (auto geometryValue : ExternalGeo.getValues()) {
            if (!geometryValue->hasExtension(
                    Part::GeometryMigrationExtension::getClassTypeId())) {
                continue;
            }

            auto ext = std::static_pointer_cast<Part::GeometryMigrationExtension>(
                geometryValue
                ->getExtension(Part::GeometryMigrationExtension::getClassTypeId())
                .lock());
            std::unique_ptr<ExternalGeometryFacade> egf;
            if (ext->testMigrationType(Part::GeometryMigrationExtension::GeometryId)) {
                egf = ExternalGeometryFacade::getFacade(geometryValue);
                egf->setId(ext->getId());
            }

            if (!ext->testMigrationType(Part::GeometryMigrationExtension::ExternalReference)) {
                continue;
            }

            if (!egf) {
                egf = ExternalGeometryFacade::getFacade(geometryValue);
            }
            egf->setRef(ext->getRef());
            egf->setRefIndex(ext->getRefIndex());
            egf->setFlags(ext->getFlags());
        }
    }
    externalGeoRefMap.clear();
    externalGeoMap.clear();
    std::set<std::string> detached;
    for(int i=0; i<ExternalGeo.getSize(); ++i) {
        auto geo = ExternalGeo[i];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if (egf->testFlag(ExternalGeometryExtension::Detached)) {
            if (!egf->getRef().empty()) {
                detached.insert(egf->getRef());
                egf->setRef(std::string());
            }
            egf->setFlag(ExternalGeometryExtension::Detached,false);
            egf->setFlag(ExternalGeometryExtension::Missing,false);
        }
        if (egf->getId() > geoLastId) {
            geoLastId = egf->getId();
        }
        if (!externalGeoMap.emplace(egf->getId(), i).second) {
            const long id = mintGeometryId(geo);
            FC_WARN("duplicate geometry id " << egf->getId() << " -> " << id);  // NOLINT
            egf->setId(id);
            externalGeoMap[egf->getId()] = i;
        }
        if (!egf->getRef().empty()) {
            externalGeoRefMap[egf->getRef()].push_back(egf->getId());
        }
    }
    if (detached.empty()) {
        signalElementsChanged();
        return;
    }

    auto objs = ExternalGeometry.getValues();
    assert(externalGeoRef.size() == objs.size());
    auto itObj = objs.begin();
    auto subs = ExternalGeometry.getSubValues();
    auto itSub = subs.begin();
    for (const auto& i : externalGeoRef) {
        if (detached.count(i) == 0U) {
            ++itObj;
            ++itSub;
            continue;
        }

        itObj = objs.erase(itObj);
        itSub = subs.erase(itSub);
        auto& refs = externalGeoRefMap[i];
        for (long id : refs) {
            auto it = externalGeoMap.find(id);
            if (it!=externalGeoMap.end()) {
                auto geo = ExternalGeo[it->second];
                ExternalGeometryFacade::getFacade(geo)->setRef(std::string());
            }
        }
        refs.clear();
    }
    ExternalGeometry.setValues(objs, subs);
}

void SketchObject::onExternalGeometryChanged()
{
    auto doc = getDocument();

    if (doc && doc->isPerformingTransaction()) {
        setStatus(App::PendingTransactionUpdate, true);
    }

    if(!isRestoring()) {
        // must wait till onDocumentRestored() when shadow references are
        // fully restored
        updateGeometryRefs();
        signalElementsChanged();
    }
}

void SketchObject::onPlacementChanged()
{
    if (ExternalGeometry.getSize() > 0) {
        touch();
    }
}

void SketchObject::onExpressionEngineChanged()
{
    auto doc = getDocument();

    if (!isRestoring() && doc && !doc->isPerformingTransaction() && noRecomputes
        && !managedoperation) {
        // if we do not have a recompute, the sketch must be solved to
        // update the DoF of the solver, constraints and UI
        try {
            auto res = ExpressionEngine.execute();
            if (res) {
                FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": "
                       << res->Why);  // NOLINT
                delete res;
            }
        } catch (Base::Exception &e) {
            e.reportException();
            FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": "
                   << e.what());  // NOLINT
        }
        solve();
    }
}

void SketchObject::onAttachmentSupportChanged()
{
    // make sure not to change anything while restoring this object
    if (isRestoring()) {
        return;
    }

    // if support face has changed then clear the external geometry
    delConstraintsToExternal();
    for (int i=0; i < getExternalGeometryCount(); i++) {
        delExternal(0);
    }
    rebuildExternalGeometry();
}

void SketchObject::onUpdateElementReference(const App::Property *prop)
{
    if(prop == &ExternalGeometry) {
        updateGeoRef = true;
        // Must call updateGeometryRefs() now to avoid the case of recursive
        // property change (e.g. temporary object removal in SubShapeBinder)
        // afterwards causing assertion failure, although this may mean extra
        // call of updateGeometryRefs() later in onChange().
        updateGeometryRefs();
        signalElementsChanged();
    }
}

void SketchObject::onUndoRedoFinished()
{
    // upon undo/redo, PropertyConstraintList does not have updated valid geometry keys, which
    // results in empty constraint lists when using getValues
    //
    // The sketch will also have invalid vertex indices, requiring a call to rebuildVertexIndex
    //
    // Historically this was "solved" by issuing a recompute, which is absolutely unnecessary and
    // prevents solve() from working before such a recompute in case it is redoing an operation with
    // invalid data.
    Constraints.checkConstraintIndices(getHighestCurveIndex(), -getExternalGeometryCount());
    acceptGeometry();
    synchroniseGeometryState();
    solve();
}

void SketchObject::synchroniseGeometryState()
{
    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    for (size_t i = 0; i < vals.size(); i++) {
        auto gf = GeometryFacade::getFacade(vals[i]);

        auto facadeInternalAlignment = gf->getInternalType();
        auto facadeBlockedState = gf->getBlocked();

        Sketcher::InternalType::InternalType constraintInternalAlignment = InternalType::None;
        bool constraintBlockedState = false;

        for (auto cstr : Constraints.getValues()) {
            if (cstr->First == int(i)) {
                getInternalTypeState(cstr, constraintInternalAlignment);
                getBlockedState(cstr, constraintBlockedState);
            }
        }

        if (constraintInternalAlignment != facadeInternalAlignment)
            gf->setInternalType(constraintInternalAlignment);

        if (constraintBlockedState != facadeBlockedState)
            gf->setBlocked(constraintBlockedState);
    }
}

// Python Sketcher feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Sketcher::SketchObjectPython, Sketcher::SketchObject)
template<>
const char* Sketcher::SketchObjectPython::getViewProviderName() const
{
    return "SketcherGui::ViewProviderPython";
}
template<>
PyObject* Sketcher::SketchObjectPython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<SketchObjectPy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class SketcherExport FeaturePythonT<Sketcher::SketchObject>;
}// namespace App

// clang-format on
