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
#include <cmath>

#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Base/Vector3D.h>

#include <memory>

#include "GeoEnum.h"
#include "SketchObject.h"


#undef DEBUG
// #define DEBUG

using namespace Sketcher;
using namespace Base;

// clang-format off
int SketchObject::extend(int GeoId, double increment, PointPos endpoint)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return -1;

    const std::vector<Part::Geometry*>& geomList = getInternalGeometry();
    Part::Geometry* geom = geomList[GeoId];
    int retcode = -1;
    if (geom->is<Part::GeomLineSegment>()) {
        auto* seg = static_cast<Part::GeomLineSegment*>(geom);
        Base::Vector3d startVec = seg->getStartPoint();
        Base::Vector3d endVec = seg->getEndPoint();
        if (endpoint == PointPos::start) {
            Base::Vector3d newPoint = startVec - endVec;
            double scaleFactor = newPoint.Length() + increment;
            newPoint.Normalize();
            newPoint.Scale(scaleFactor, scaleFactor, scaleFactor);
            newPoint = newPoint + endVec;
            retcode = moveGeometry(GeoId, Sketcher::PointPos::start, newPoint, false, true);
        }
        else if (endpoint == PointPos::end) {
            Base::Vector3d newPoint = endVec - startVec;
            double scaleFactor = newPoint.Length() + increment;
            newPoint.Normalize();
            newPoint.Scale(scaleFactor, scaleFactor, scaleFactor);
            newPoint = newPoint + startVec;
            retcode = moveGeometry(GeoId, Sketcher::PointPos::end, newPoint, false, true);
        }
    }
    else if (geom->is<Part::GeomArcOfCircle>()) {
        auto* arc = static_cast<Part::GeomArcOfCircle*>(geom);
        double startArc, endArc;
        arc->getRange(startArc, endArc, true);
        if (endpoint == PointPos::start) {
            arc->setRange(startArc - increment, endArc, true);
            retcode = 0;
        }
        else if (endpoint == PointPos::end) {
            arc->setRange(startArc, endArc + increment, true);
            retcode = 0;
        }
    }
    if (retcode == 0 && noRecomputes) {
        solve();
    }
    return retcode;
}

// clang-format on
bool SketchObject::seekTrimPoints(
    int GeoId,
    const Base::Vector3d& point,
    int& GeoId1,
    Base::Vector3d& intersect1,
    int& GeoId2,
    Base::Vector3d& intersect2
)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        return false;
    }

    auto geos = getCompleteGeometry();  // this includes the axes too

    geos.resize(geos.size() - 2);  // remove the axes to avoid intersections with the axes

    int localindex1, localindex2;

    // Not found in will be returned as -1, not as GeoUndef, Part WB is agnostic to the concept of
    // GeoUndef
    if (
        !Part2DObject::seekTrimPoints(geos, GeoId, point, localindex1, intersect1, localindex2, intersect2)
    ) {
        return false;
    }

    // invalid complete geometry indices are mapped to GeoUndef
    GeoId1 = getGeoIdFromCompleteGeometryIndex(localindex1);
    GeoId2 = getGeoIdFromCompleteGeometryIndex(localindex2);

    return true;
}

// given a geometry and a point, returns the corresponding parameter of the geometry point
// closest to the point. Wrapped around a try-catch so the calling operation can fail without
// throwing an exception.
bool getIntersectionParameter(const Part::Geometry* geo, const Base::Vector3d point, double& pointParam)
{
    const auto* curve = static_cast<const Part::GeomCurve*>(geo);

    try {
        curve->closestParameter(point, pointParam);
    }
    catch (Base::CADKernelError& e) {
        e.reportException();
        return false;
    }

    return true;
}

bool arePointsWithinPrecision(const Base::Vector3d& point1, const Base::Vector3d& point2)
{
    // From testing: 500x (or 0.000050) is needed in order to not falsely distinguish points
    // calculated with seekTrimPoints
    return ((point1 - point2).Length() < 500 * Precision::Confusion());
}

bool areParamsWithinApproximation(double param1, double param2)
{
    // From testing: 500x (or 0.000050) is needed in order to not falsely distinguish points
    // calculated with seekTrimPoints
    return (std::abs(param1 - param2) < Precision::PApproximation());
}

// returns true if the point defined by (GeoId1, pos1) can be considered to be coincident with
// point.
bool isPointAtPosition(const SketchObject* obj, int GeoId1, PointPos pos1, const Base::Vector3d& point)
{
    Base::Vector3d pp = obj->getPoint(GeoId1, pos1);

    return arePointsWithinPrecision(point, pp);
}

// Checks whether preexisting constraints must be converted to new constraints.
// Preexisting point on object constraints get converted to coincidents.
// Returns:
//     - The constraint that should be used to constraint GeoId and cuttingGeoId
std::unique_ptr<Constraint> transformPreexistingConstraintForTrim(
    const SketchObject* obj,
    const Constraint* constr,
    int GeoId,
    int cuttingGeoId,
    const Base::Vector3d& cutPointVec,
    int newGeoId,
    PointPos newPosId
)
{
    /* TODO: It is possible that the trimming entity has both a PointOnObject constraint to the
     * trimmed entity, and a simple Tangent constraint to the trimmed entity. In this case we
     * want to change to a single end-to-end tangency, i.e we want to ensure that constrType1
     * is set to Sketcher::Tangent, that the secondPos1 is captured from the PointOnObject,
     * and also make sure that the PointOnObject constraint is deleted.
     */
    // TODO: Symmetric and distance constraints (sometimes together) can be changed to something
    std::unique_ptr<Constraint> newConstr;
    if (cuttingGeoId == GeoEnum::GeoUndef || !constr->involvesGeoId(cuttingGeoId)
        || !constr->involvesGeoIdAndPosId(GeoId, PointPos::none)) {
        return newConstr;
    }
    switch (constr->Type) {
        case PointOnObject: {
            // we might want to transform this (and the new point-on-object constraints) into a
            // coincidence At this stage of the check the point has to be an end of `cuttingGeoId`
            // on the edge of `GeoId`.
            if (isPointAtPosition(obj, constr->First, constr->FirstPos, cutPointVec)) {
                // We already know the point-on-object is on the whole of GeoId
                newConstr.reset(constr->copy());
                newConstr->Type = Sketcher::Coincident;
                newConstr->Second = newGeoId;
                newConstr->SecondPos = newPosId;
            }
            break;
        }
        case Tangent:
        case Perpendicular: {
            // These may have to be turned into endpoint-to-endpoint or endpoint-to-edge
            // TODO: could there be tangent/perpendicular constraints not involving the trim that
            // are modified below?
            newConstr.reset(constr->copy());
            newConstr->substituteIndexAndPos(GeoId, PointPos::none, newGeoId, newPosId);
            // make sure the first position is a point
            if (newConstr->FirstPos == PointPos::none) {
                std::swap(newConstr->First, newConstr->Second);
                std::swap(newConstr->FirstPos, newConstr->SecondPos);
            }
            // there is no need for the third point if it exists
            newConstr->Third = GeoEnum::GeoUndef;
            newConstr->ThirdPos = PointPos::none;
            break;
        }
        default:
            break;
    }
    return newConstr;
}

std::unique_ptr<Constraint> getNewConstraintAtTrimCut(
    const SketchObject* obj,
    int cuttingGeoId,
    int cutGeoId,
    PointPos cutPosId,
    const Base::Vector3d& cutPointVec
)
{
    auto newConstr = std::make_unique<Sketcher::Constraint>();
    newConstr->First = cutGeoId;
    newConstr->FirstPos = cutPosId;
    newConstr->Second = cuttingGeoId;
    if (isPointAtPosition(obj, cuttingGeoId, PointPos::start, cutPointVec)) {
        newConstr->Type = Sketcher::Coincident;
        newConstr->SecondPos = PointPos::start;
    }
    else if (isPointAtPosition(obj, cuttingGeoId, PointPos::end, cutPointVec)) {
        newConstr->Type = Sketcher::Coincident;
        newConstr->SecondPos = PointPos::end;
    }
    else {
        // Points are sufficiently far apart: use point-on-object
        newConstr->Type = Sketcher::PointOnObject;
        newConstr->SecondPos = PointPos::none;
    }
    return newConstr;
}

bool isGeoIdAllowedForTrim(const SketchObject* obj, int GeoId)
{
    const auto* geo = obj->getGeometry(GeoId);

    return GeoId >= 0 && GeoId <= obj->getHighestCurveIndex()
        && GeometryFacade::isInternalType(geo, InternalType::None);
}

bool getParamLimitsOfNewGeosForTrim(
    const SketchObject* obj,
    int GeoId,
    std::array<int, 2>& cuttingGeoIds,
    std::array<Base::Vector3d, 2>& cutPoints,
    std::vector<std::pair<double, double>>& paramsOfNewGeos
)
{
    const auto* geoAsCurve = obj->getGeometry<Part::GeomCurve>(GeoId);
    double firstParam = geoAsCurve->getFirstParameter();
    double lastParam = geoAsCurve->getLastParameter();
    double cut0Param {firstParam}, cut1Param {lastParam};

    bool allParamsFound = getIntersectionParameter(geoAsCurve, cutPoints[0], cut0Param)
        && getIntersectionParameter(geoAsCurve, cutPoints[1], cut1Param);
    if (!allParamsFound) {
        return false;
    }

    if (!obj->isClosedCurve(geoAsCurve) && areParamsWithinApproximation(firstParam, cut0Param)) {
        cuttingGeoIds[0] = GeoEnum::GeoUndef;
    }

    if (!obj->isClosedCurve(geoAsCurve) && areParamsWithinApproximation(lastParam, cut1Param)) {
        cuttingGeoIds[1] = GeoEnum::GeoUndef;
    }

    size_t numUndefs = std::count(cuttingGeoIds.begin(), cuttingGeoIds.end(), GeoEnum::GeoUndef);

    if (numUndefs == 0 && arePointsWithinPrecision(cutPoints[0], cutPoints[1])) {
        // If both points are detected and are coincident, deletion is the only option.
        paramsOfNewGeos.clear();
        return true;
    }

    paramsOfNewGeos.assign(2 - numUndefs, {firstParam, lastParam});

    if (paramsOfNewGeos.empty()) {
        return true;
    }

    if (obj->isClosedCurve(geoAsCurve)) {
        paramsOfNewGeos.pop_back();
    }

    if (cuttingGeoIds[0] != GeoEnum::GeoUndef) {
        paramsOfNewGeos.front().second = cut0Param;
    }
    if (cuttingGeoIds[1] != GeoEnum::GeoUndef) {
        paramsOfNewGeos.back().first = cut1Param;
    }

    return true;
}

void createArcsFromGeoWithLimits(
    const Part::GeomCurve* geo,
    const std::vector<std::pair<double, double>>& paramsOfNewGeos,
    std::vector<Part::Geometry*>& newGeos
)
{
    for (auto& [u1, u2] : paramsOfNewGeos) {
        auto newGeo = static_cast<const Part::GeomCurve*>(geo)->createArc(u1, u2);
        assert(newGeo);
        newGeos.emplace_back(newGeo);
    }
}

void createNewConstraintsForTrim(
    const SketchObject* obj,
    const int GeoId,
    const std::array<int, 2>& cuttingGeoIds,
    const std::array<Base::Vector3d, 2>& cutPoints,
    const std::vector<int>& newIds,
    const std::vector<const Part::Geometry*> newGeos,
    std::vector<int>& idsOfOldConstraints,
    std::vector<Constraint*>& newConstraints,
    std::set<int, std::greater<>>& geoIdsToBeDeleted,
    std::map<Constraint*, int>& newToOldConstraintMap,
    std::vector<std::string>& retiredConstraintLabels,
    std::vector<std::string>& forcedPickLabels
)
{
    const auto& allConstraints = obj->Constraints.getValues();

    bool isPoint1ConstrainedOnGeoId1 = false;
    bool isPoint2ConstrainedOnGeoId2 = false;

    for (const auto& oldConstrId : idsOfOldConstraints) {
        // trim-specific changes first
        const Constraint* con = allConstraints[oldConstrId];
        if (con->Type == InternalAlignment) {
            geoIdsToBeDeleted.insert(con->First);
            continue;
        }
        if (auto newConstr = transformPreexistingConstraintForTrim(
                obj,
                con,
                GeoId,
                cuttingGeoIds[0],
                cutPoints[0],
                newIds.front(),
                PointPos::end
            )) {
            newConstraints.push_back(newConstr.release());
            newToOldConstraintMap[newConstraints.back()] = oldConstrId;  // Map new to old
            isPoint1ConstrainedOnGeoId1 = true;
            continue;
        }
        if (auto newConstr = transformPreexistingConstraintForTrim(
                obj,
                con,
                GeoId,
                cuttingGeoIds[1],
                cutPoints[1],
                newIds.back(),
                PointPos::start
            )) {
            newConstraints.push_back(newConstr.release());
            newToOldConstraintMap[newConstraints.back()] = oldConstrId;  // Map new to old
            isPoint2ConstrainedOnGeoId2 = true;
            continue;
        }
        // We have already transferred all constraints on endpoints to the new pieces.
        // If there is still any left, this means one of the remaining pieces was degenerate.
        if (!(con->Type == Angle || con->involvesGeoIdAndPosId(GeoId, PointPos::none))) {
            continue;
        }
        // constraint has not yet been changed
        size_t sizeBefore = newConstraints.size();
        bool forcedPick = false;
        obj->deriveConstraintsForPieces(GeoId, newIds, newGeos, con, newConstraints, &forcedPick);
        // Map all newly added derived constraints to the old ID
        for (size_t i = sizeBefore; i < newConstraints.size(); ++i) {
            newToOldConstraintMap[newConstraints[i]] = oldConstrId;
        }
        // §4.7 / P7: a sever-trim (newIds.size() > 1) retires the entity into two pieces. A
        // constraint that produced no replacement is residue — its subject was the whole retired
        // entity, so it has no piece to bind to and is dropped below. Unlike a delete the user did
        // not ask to retire it, so surface the loss loudly rather than silently (see split).
        if (newIds.size() > 1 && newConstraints.size() == sizeBefore) {
            retiredConstraintLabels.push_back(con->Name.empty() ? con->typeToString() : con->Name);
        }
        // A constraint that was carried but could equally have gone to more than one piece (its
        // point sits exactly on the cut boundary) was attached to one piece arbitrarily. Disclose
        // that forced choice too rather than making it silently (§4.7 / P7; see split).
        else if (forcedPick) {
            forcedPickLabels.push_back(con->Name.empty() ? con->typeToString() : con->Name);
        }
    }

    // Add point-on-object/coincidence constraints with the newly exposed points.
    // This will need to account for the constraints that were already converted
    // to coincident or end-to-end tangency/perpendicularity.
    // TODO: Tangent/perpendicular not yet covered

    if (cuttingGeoIds[0] != GeoEnum::GeoUndef && !isPoint1ConstrainedOnGeoId1) {
        newConstraints.emplace_back(
            getNewConstraintAtTrimCut(obj, cuttingGeoIds[0], newIds.front(), PointPos::end, cutPoints[0])
                .release()
        );
    }

    if (cuttingGeoIds[1] != GeoEnum::GeoUndef && !isPoint2ConstrainedOnGeoId2) {
        newConstraints.emplace_back(
            getNewConstraintAtTrimCut(obj, cuttingGeoIds[1], newIds.back(), PointPos::start, cutPoints[1])
                .release()
        );
    }
}

int SketchObject::trim(int GeoId, const Base::Vector3d& point)
{
    if (!isGeoIdAllowedForTrim(this, GeoId)) {
        return -1;
    }
    // Remove internal geometry beforehand for now
    // FIXME: we should be able to transfer these to new curves smoothly
    // auto geo = getGeometry(GeoId);
    const auto* geoAsCurve = getGeometry<Part::GeomCurve>(GeoId);

    if (geoAsCurve == nullptr) {
        return -1;
    }

    bool isOriginalCurveConstruction = GeometryFacade::getConstruction(geoAsCurve);
    bool isOriginalCurvePeriodic = isClosedCurve(geoAsCurve);

    //******************* Step A => Detection of intersection - Common to all Geometries
    //****************************************//
    // GeoIds intersecting the curve around `point`
    std::array<int, 2> cuttingGeoIds {GeoEnum::GeoUndef, GeoEnum::GeoUndef};
    // Points at the intersection
    std::array<Base::Vector3d, 2> cutPoints;

    // Using SketchObject wrapper, as Part2DObject version returns GeoId = -1 when intersection not
    // found, which is wrong for a GeoId (axis). seekTrimPoints returns:
    // - For a parameter associated with "point" between an intersection and the end point
    // (non-periodic case) cuttingGeoIds[0] != GeoUndef and cuttingGeoIds[1] == GeoUndef
    // - For a parameter associated with "point" between the start point and an intersection
    // (non-periodic case) cuttingGeoIds[1] != GeoUndef and cuttingGeoIds[0] == GeoUndef
    // - For a parameter associated with "point" between two intersection points, cuttingGeoIds[0]
    // != GeoUndef and cuttingGeoIds[1] != GeoUndef
    //
    // FirstParam < point1param < point2param < LastParam
    if (!SketchObject::seekTrimPoints(
            GeoId,
            point,
            cuttingGeoIds[0],
            cutPoints[0],
            cuttingGeoIds[1],
            cutPoints[1]
        )) {
        // If no suitable trim points are found, then trim defaults to deleting the geometry
        delGeometry(GeoId);
        return 0;
    }

    // TODO: find trim parameters
    std::vector<std::pair<double, double>> paramsOfNewGeos;
    paramsOfNewGeos.reserve(2);
    if (!getParamLimitsOfNewGeosForTrim(this, GeoId, cuttingGeoIds, cutPoints, paramsOfNewGeos)) {
        return -1;
    }

    //******************* Step B => Creation of new geometries
    //****************************************//
    std::vector<int> newIds;
    std::vector<Part::Geometry*> newGeos;
    std::vector<const Part::Geometry*> newGeosAsConsts;
    std::map<Constraint*, int> newToOldConstraintMap;

    switch (paramsOfNewGeos.size()) {
        case 0: {
            delGeometry(GeoId);
            return 0;
        }
        case 1: {
            newIds.push_back(GeoId);
            break;
        }
        case 2: {
            newIds.push_back(GeoId);
            newIds.push_back(getHighestCurveIndex() + 1);
            break;
        }
        default: {
            return -1;
        }
    }

    createArcsFromGeoWithLimits(geoAsCurve, paramsOfNewGeos, newGeos);
    for (const auto* geo : newGeos) {
        newGeosAsConsts.push_back(geo);
    }

    //******************* Step C => Creation of new constraints
    //****************************************//
    // Now that we have the new curves, change constraints as needed
    // Some are covered with `deriveConstraintsForPieces`, others are specific to trim
    // FIXME: We are using non-smart pointers since that's what's needed in `addConstraints`.
    const auto& allConstraints = this->Constraints.getValues();
    std::vector<Constraint*> newConstraints;
    std::vector<int> idsOfOldConstraints;
    std::set<int, std::greater<>> geoIdsToBeDeleted;
    getConstraintIndices(GeoId, idsOfOldConstraints);
    // remove the constraints that we want to manually transfer
    // We could transfer beforehand but in case of exception that transfer is permanent
    if (!isOriginalCurvePeriodic) {
        std::erase_if(idsOfOldConstraints, [&GeoId, &allConstraints, &cuttingGeoIds](const auto& i) {
            auto* constr = allConstraints[i];
            bool involvesStart = constr->involvesGeoIdAndPosId(GeoId, PointPos::start);
            bool involvesEnd = constr->involvesGeoIdAndPosId(GeoId, PointPos::end);
            bool keepStart = cuttingGeoIds[0] != GeoEnum::GeoUndef;
            bool keepEnd = cuttingGeoIds[1] != GeoEnum::GeoUndef;
            bool involvesBothButNotBothKept = involvesStart && involvesEnd && !(keepStart && keepEnd);
            return !involvesBothButNotBothKept
                && ((involvesStart && keepStart) || (involvesEnd && keepEnd));
        });
    }
    std::erase_if(idsOfOldConstraints, [&GeoId, &allConstraints](const auto& i) {
        return (allConstraints[i]->involvesGeoIdAndPosId(GeoId, PointPos::mid));
    });

    std::vector<std::string> retiredConstraintLabels;
    std::vector<std::string> forcedPickLabels;
    createNewConstraintsForTrim(
        this,
        GeoId,
        cuttingGeoIds,
        cutPoints,
        newIds,
        newGeosAsConsts,
        idsOfOldConstraints,
        newConstraints,
        geoIdsToBeDeleted,
        newToOldConstraintMap,
        retiredConstraintLabels,
        forcedPickLabels
    );

    //******************* Step D => Replacing geometries and constraints
    //****************************************//

    // Constraints related to start/mid/end points of original
    [[maybe_unused]] auto constrainAsEqual = [this](int GeoId1, int GeoId2) {
        auto newConstr = std::make_unique<Sketcher::Constraint>();

        // Build Constraints associated with new pair of arcs
        newConstr->Type = Sketcher::Equal;
        newConstr->First = GeoId1;
        newConstr->FirstPos = Sketcher::PointPos::none;
        newConstr->Second = GeoId2;
        newConstr->SecondPos = Sketcher::PointPos::none;
        addConstraint(std::move(newConstr));
    };

    std::map<Constraint*, std::shared_ptr<App::Expression>> exprBackup;
    for (auto const& [newConstr, oldId] : newToOldConstraintMap) {
        if (oldId >= 0 && oldId < (int)allConstraints.size()) {
            auto exprInfo = getExpression(Constraints.createPath(oldId));
            if (exprInfo.expression) {
                exprBackup[newConstr] = std::shared_ptr<App::Expression>(exprInfo.expression->copy());
            }
        }
    }

    delConstraints(std::move(idsOfOldConstraints), DeleteOption::NoFlag);

    if (!isOriginalCurvePeriodic) {
        transferConstraints(GeoId, PointPos::start, newIds.front(), PointPos::start, true);
        transferConstraints(GeoId, PointPos::end, newIds.back(), PointPos::end, true);
    }
    bool geomHasMid = geoAsCurve->isDerivedFrom<Part::GeomConic>()
        || geoAsCurve->isDerivedFrom<Part::GeomArcOfConic>();
    if (geomHasMid) {
        transferConstraints(GeoId, PointPos::mid, newIds.front(), PointPos::mid, true);
        // Make centers coincident
        if (newIds.size() > 1) {
            auto* joint = new Constraint();
            joint->Type = Coincident;
            joint->First = newIds.front();
            joint->FirstPos = PointPos::mid;
            joint->Second = newIds.back();
            joint->SecondPos = PointPos::mid;
            newConstraints.push_back(joint);

            // Any radius etc. equality constraints here
            // TODO: There could be some form of equality between the constraints here. However, it
            // may happen that this is imposed by an elaborate set of additional constraints. When
            // that happens, this causes redundant constraints, and in worse cases (incorrect)
            // complaints of over-constraint and solver failures.

            // if (std::ranges::none_of(newConstraints, [](const auto& constr) {
            //         return constr->Type == ConstraintType::Equal;
            //     })) {
            //     constrainAsEqual(newIds.front(), newIds.back());
            // }
            // TODO: ensure alignment as well?
        }
    }

    // Architecture §4.7: a shorten-only trim keeps the entity as one entity, so it keeps its
    // durable id. replaceGeometries already carries the solver-local integer id (copyId); the
    // durable tag must ride along too, but only in the 1->1 case (paramsOfNewGeos.size() == 1). A
    // middle-removal trim (paramsOfNewGeos.size() == 2) severs the entity in two: the parent tag
    // retires and both children mint fresh (createArcsFromGeoWithLimits already did this), so we
    // leave the tag alone there. No parent->child lineage is recorded across the sever: it is
    // treated the same as deleting the entity and drawing the children anew.
    if (paramsOfNewGeos.size() == 1) {
        newGeos.front()->copyTagFrom(geoAsCurve);
    }

    replaceGeometries({GeoId}, newGeos);
    for (auto newId : newIds) {
        setConstruction(newId, isOriginalCurveConstruction);
    }

    if (noRecomputes) {
        solve();
    }

    for (auto& deletedGeoId : geoIdsToBeDeleted) {
        for (auto& cons : newConstraints) {
            changeConstraintAfterDeletingGeo(cons, deletedGeoId);
        }
    }
    std::erase_if(newConstraints, [](const auto& constr) {
        return constr->Type == ConstraintType::None;
    });
    delGeometries(geoIdsToBeDeleted.begin(), geoIdsToBeDeleted.end());

    int lastAddedIndex = addConstraints(newConstraints);
    int firstAddedIndex = lastAddedIndex - (int)newConstraints.size() + 1;

    // Restore expressions
    for (int i = 0; i < (int)newConstraints.size(); ++firstAddedIndex, ++i) {
        auto it = exprBackup.find(newConstraints[i]);
        if (it != exprBackup.end()) {
            setExpression(Constraints.createPath(firstAddedIndex), it->second);
        }
    }

    if (noRecomputes) {
        solve();
    }

    //******************* Cleanup
    //****************************************//

    // Since we used regular "non-smart" pointers, we have to handle cleanup
    for (auto& cons : newConstraints) {
        delete cons;
    }

    if (!retiredConstraintLabels.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < retiredConstraintLabels.size(); ++i) {
            joined += (i != 0 ? ", " : "") + retiredConstraintLabels[i];
        }
        Base::Console().warning(
            "Trim severed geometry %d; %zu constraint(s) could not follow it and were removed: "
            "%s. Their subject was the whole entity, which no longer exists as a single edge.\n",
            GeoId,
            retiredConstraintLabels.size(),
            joined.c_str()
        );
    }

    if (!forcedPickLabels.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < forcedPickLabels.size(); ++i) {
            joined += (i != 0 ? ", " : "") + forcedPickLabels[i];
        }
        Base::Console().warning(
            "Trim severed geometry %d; %zu constraint(s) could apply to more than one of the new "
            "pieces and each was attached to one piece: %s. Reattach it if the wrong piece was "
            "chosen.\n",
            GeoId,
            forcedPickLabels.size(),
            joined.c_str()
        );
    }

    return 0;
}

int SketchObject::split(int GeoId, const Base::Vector3d& point)
{
    // No need to check input data validity as this is an sketchobject managed operation

    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        return -1;
    }

    // FIXME: we should be able to transfer these to new curves smoothly
    deleteUnusedInternalGeometryAndUpdateGeoId(GeoId);
    const auto* geoAsCurve = getGeometry<Part::GeomCurve>(GeoId);

    bool isOriginalCurvePeriodic = isClosedCurve(geoAsCurve);
    std::vector<int> newIds;
    std::vector<Part::Geometry*> newGeos;
    std::vector<Constraint*> newConstraints;

    double splitParam;
    geoAsCurve->closestParameter(point, splitParam);

    // TODO: find trim parameters
    std::vector<std::pair<double, double>> paramsOfNewGeos(
        isOriginalCurvePeriodic ? 1 : 2,
        {geoAsCurve->getFirstParameter(), geoAsCurve->getLastParameter()}
    );
    paramsOfNewGeos.front().second = isOriginalCurvePeriodic
        ? (splitParam + geoAsCurve->getLastParameter() - geoAsCurve->getFirstParameter())
        : splitParam;
    paramsOfNewGeos.back().first = splitParam;

    switch (paramsOfNewGeos.size()) {
        case 0: {
            delGeometry(GeoId);
            return 0;
        }
        case 1: {
            newIds.push_back(GeoId);
            break;
        }
        case 2: {
            newIds.push_back(GeoId);
            newIds.push_back(getHighestCurveIndex() + 1);
            break;
        }
        default: {
            return -1;
        }
    }

    createArcsFromGeoWithLimits(geoAsCurve, paramsOfNewGeos, newGeos);

    std::vector<int> idsOfOldConstraints;
    getConstraintIndices(GeoId, idsOfOldConstraints);

    const auto& allConstraints = this->Constraints.getValues();

    std::erase_if(idsOfOldConstraints, [&GeoId, &allConstraints](const auto& i) {
        return !allConstraints[i]->involvesGeoIdAndPosId(GeoId, PointPos::none);
    });

    // Architecture §4.7 / P7: a split retires the parent entity; constraints that referenced it
    // are carried to the pieces where that is decidable without resemblance. The residue — a
    // dimension or relation whose subject was the whole retired entity — has no piece to bind to
    // and is dropped. Dropping it is honest (its subject genuinely ceased to exist), but the user
    // did not directly ask to retire it, so the loss is surfaced loudly rather than silently.
    // A constraint may also be carried to a piece that was chosen arbitrarily because more than
    // one piece could equally have taken it (a point sitting exactly on the split boundary). That
    // pick is disclosed too (§4.7 / P7: a forced choice is surfaced, not made silently), so the
    // user can reattach it if the wrong piece was chosen.
    std::vector<std::string> retiredConstraintLabels;
    std::vector<std::string> forcedPickLabels;
    for (const auto& oldConstrId : idsOfOldConstraints) {
        Constraint* con = allConstraints[oldConstrId];
        bool forcedPick = false;
        if (!deriveConstraintsForPieces(GeoId, newIds, con, newConstraints, &forcedPick)) {
            retiredConstraintLabels.push_back(con->Name.empty() ? con->typeToString() : con->Name);
        }
        else if (forcedPick) {
            forcedPickLabels.push_back(con->Name.empty() ? con->typeToString() : con->Name);
        }
    }

    // This also seems to reset SketchObject::Geometry.
    // TODO: figure out why, and if that check must be used
    geoAsCurve = getGeometry<Part::GeomCurve>(GeoId);

    if (!isOriginalCurvePeriodic) {
        auto* joint = new Constraint();
        joint->Type = Coincident;
        joint->First = newIds.front();
        joint->FirstPos = PointPos::end;
        joint->Second = newIds.back();
        joint->SecondPos = PointPos::start;
        newConstraints.push_back(joint);

        transferConstraints(GeoId, PointPos::start, newIds.front(), PointPos::start);
        transferConstraints(GeoId, PointPos::end, newIds.back(), PointPos::end);
    }

    // This additional constraint is there to maintain existing behavior.
    // TODO: Decide whether to remove it altogether or also apply to other curves with centers.
    if (geoAsCurve->is<Part::GeomArcOfCircle>()) {
        auto* joint = new Constraint();
        joint->Type = Coincident;
        joint->First = newIds.front();
        joint->FirstPos = PointPos::mid;
        joint->Second = newIds.back();
        joint->SecondPos = PointPos::mid;
        newConstraints.push_back(joint);
    }

    if (geoAsCurve->isDerivedFrom<Part::GeomConic>()
        || geoAsCurve->isDerivedFrom<Part::GeomArcOfConic>()) {
        transferConstraints(GeoId, PointPos::mid, newIds.front(), PointPos::mid);
    }

    // Architecture §4.7: splitting a closed curve (circle/ellipse) at one point yields a single
    // open arc — the entity stays one entity, so it keeps its durable id (paramsOfNewGeos.size()
    // == 1). Splitting an open curve is a 1->N count-changing event: the parent tag retires and
    // every child mints fresh (the constructor already does this, and replaceGeometries carries
    // only the solver-local integer id to child-0), so we leave the tag alone in that case. No
    // parent->child lineage is recorded across the split: a split is treated the same as deleting
    // the entity and drawing the children anew.
    if (paramsOfNewGeos.size() == 1) {
        newGeos.front()->copyTagFrom(geoAsCurve);
    }

    delConstraints(std::move(idsOfOldConstraints), DeleteOption::NoSolve);
    replaceGeometries({GeoId}, newGeos);
    addConstraints(newConstraints);

    // `if (noRecomputes)` results in a failed test (`testPD_TNPSketchPadSketchSplit(self)`)
    // TODO: figure out why, and if that check must be used
    solve();

    for (auto& cons : newConstraints) {
        delete cons;
    }

    if (!retiredConstraintLabels.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < retiredConstraintLabels.size(); ++i) {
            joined += (i != 0 ? ", " : "") + retiredConstraintLabels[i];
        }
        Base::Console().warning(
            "Split retired geometry %d; %zu constraint(s) could not follow it and were removed: "
            "%s. Their subject was the whole entity, which no longer exists as a single edge.\n",
            GeoId,
            retiredConstraintLabels.size(),
            joined.c_str()
        );
    }

    if (!forcedPickLabels.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < forcedPickLabels.size(); ++i) {
            joined += (i != 0 ? ", " : "") + forcedPickLabels[i];
        }
        Base::Console().warning(
            "Split of geometry %d: %zu constraint(s) could apply to more than one of the new "
            "pieces; each was attached to one piece: %s. Reattach it if the wrong piece was "
            "chosen.\n",
            GeoId,
            forcedPickLabels.size(),
            joined.c_str()
        );
    }

    return 0;
}
// clang-format off

int SketchObject::join(
    int geoId1,
    Sketcher::PointPos posId1,
    int geoId2,
    Sketcher::PointPos posId2,
    int continuity
)
{
    // No need to check input data validity as this is an sketchobject managed operation

    Base::StateLocker lock(managedoperation, true);

    if (Sketcher::PointPos::start != posId1 && Sketcher::PointPos::end != posId1
        && Sketcher::PointPos::start != posId2 && Sketcher::PointPos::end != posId2) {
        THROWM(ValueError, "Invalid positions: points must be start or end points of a curve.");
        return -1;
    }

    if (geoId1 == geoId2) {
        THROWM(ValueError, "Connecting the end points of the same curve is not yet supported.");
        return -1;
    }

    if (geoId1 < 0 || geoId1 > getHighestCurveIndex() || geoId2 < 0
        || geoId2 > getHighestCurveIndex()) {
        return -1;
    }

    // get the old splines
    auto* geo1 = dynamic_cast<const Part::GeomCurve*>(getGeometry(geoId1));
    auto* geo2 = dynamic_cast<const Part::GeomCurve*>(getGeometry(geoId2));

    if (GeometryFacade::getConstruction(geo1) != GeometryFacade::getConstruction(geo2)) {
        THROWM(ValueError, "Cannot join construction and non-construction geometries.");
        return -1;
    }
    bool areOriginalCurvesConstruction = GeometryFacade::getConstruction(geo1);

    // TODO: make both curves b-splines here itself
    if (!geo1 || !geo2) {
        return -1;
    }

    // TODO: is there a cleaner way to get our mutable bsp's?
    // we need the splines to be mutable because we may reverse them
    // and/or change their degree
    std::unique_ptr<Part::GeomBSplineCurve> bsp1(
        geo1->toNurbs(geo1->getFirstParameter(), geo1->getLastParameter())
    );
    std::unique_ptr<Part::GeomBSplineCurve> bsp2(
        geo2->toNurbs(geo2->getFirstParameter(), geo2->getLastParameter())
    );

    if (bsp1->isPeriodic() || bsp2->isPeriodic()) {
        THROWM(ValueError, "It is only possible to join non-periodic curves.");
        return -1;
    }

    // reverse the splines if needed: join end of 1st to start of 2nd
    if (Sketcher::PointPos::start == posId1) {
        bsp1->reverse();
    }
    if (Sketcher::PointPos::end == posId2) {
        bsp2->reverse();
    }

    // ensure the degrees of both curves are the same
    if (bsp1->getDegree() < bsp2->getDegree()) {
        bsp1->increaseDegree(bsp2->getDegree());
    }
    else if (bsp2->getDegree() < bsp1->getDegree()) {
        bsp2->increaseDegree(bsp1->getDegree());
    }

    // TODO: Check for tangent constraint here
    bool makeC1Continuous = (continuity >= 1);

    // TODO: Rescale one or both sections to fulfill some purpose.
    // This could include making param between [0,1], and/or making
    // C1 continuity possible.
    if (makeC1Continuous) {
        // We assume here that there is already G1 continuity.
        // Just scale parameters to get C1.
        Base::Vector3d slope1 = bsp1->firstDerivativeAtParameter(bsp1->getLastParameter());
        Base::Vector3d slope2 = bsp2->firstDerivativeAtParameter(bsp2->getFirstParameter());
        // TODO: slope2 can technically be a zero vector
        // But that seems not possible unless the spline is trivial.
        // Prove or account for the possibility.
        double scale = slope2.Length() / slope1.Length();
        bsp2->scaleKnotsToBounds(0, scale * (bsp2->getLastParameter() - bsp2->getFirstParameter()));
    }

    // set up vectors for new poles, knots, mults
    std::vector<Base::Vector3d> poles1 = bsp1->getPoles();
    std::vector<double> weights1 = bsp1->getWeights();
    std::vector<double> knots1 = bsp1->getKnots();
    std::vector<int> mults1 = bsp1->getMultiplicities();
    std::vector<Base::Vector3d> poles2 = bsp2->getPoles();
    std::vector<double> weights2 = bsp2->getWeights();
    std::vector<double> knots2 = bsp2->getKnots();
    std::vector<int> mults2 = bsp2->getMultiplicities();

    std::vector<Base::Vector3d> newPoles(std::move(poles1));
    std::vector<double> newWeights(std::move(weights1));
    std::vector<double> newKnots(std::move(knots1));
    std::vector<int> newMults(std::move(mults1));

    poles2.erase(poles2.begin());
    if (makeC1Continuous) {
        newPoles.erase(newPoles.end() - 1);
    }
    newPoles.insert(
        newPoles.end(),
        std::make_move_iterator(poles2.begin()),
        std::make_move_iterator(poles2.end())
    );

    // TODO: Weights might need to be scaled
    weights2.erase(weights2.begin());
    if (makeC1Continuous) {
        newWeights.erase(newWeights.end() - 1);
    }
    newWeights.insert(
        newWeights.end(),
        std::make_move_iterator(weights2.begin()),
        std::make_move_iterator(weights2.end())
    );

    // knots of the second spline come after all of the first
    double offset = newKnots.back() - knots2.front();
    knots2.erase(knots2.begin());
    for (auto& knot : knots2) {
        knot += offset;
    }
    newKnots.insert(
        newKnots.end(),
        std::make_move_iterator(knots2.begin()),
        std::make_move_iterator(knots2.end())
    );

    // end knots can have a multiplicity of (degree + 1)
    if (bsp1->getDegree() < newMults.back()) {
        newMults.back() = bsp1->getDegree();
        if (makeC1Continuous) {
            newMults.back() -= 1;
        }
    }

    mults2.erase(mults2.begin());
    newMults.insert(
        newMults.end(),
        std::make_move_iterator(mults2.begin()),
        std::make_move_iterator(mults2.end())
    );

    auto* newSpline = new Part::GeomBSplineCurve(
        newPoles,
        newWeights,
        newKnots,
        newMults,
        bsp1->getDegree(),
        false,
        true
    );

    // int newGeoId = addGeometry(newSpline);
    std::vector<Part::Geometry*> newGeos {newSpline};
    replaceGeometries({geoId1, geoId2}, newGeos);

    exposeInternalGeometry(geoId1);
    setConstruction(geoId1, areOriginalCurvesConstruction);

    // TODO: transfer constraints on the non-connected ends
    auto otherPosId1 = (Sketcher::PointPos::start == posId1) ? Sketcher::PointPos::end
                                                             : Sketcher::PointPos::start;
    auto otherPosId2 = (Sketcher::PointPos::start == posId2) ? Sketcher::PointPos::end
                                                             : Sketcher::PointPos::start;

    transferConstraints(geoId1, otherPosId1, geoId1, PointPos::start, true);
    transferConstraints(geoId2, otherPosId2, geoId1, PointPos::end, true);

    return 0;
}
// clang-format on
