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

// clang-format off
using namespace Sketcher;
using namespace Base;

int SketchObject::moveGeometries(const std::vector<GeoElementId>& geoEltIds, const Base::Vector3d& toPoint, bool relative,
                            bool updateGeoBeforeMoving)
{

    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // if we are moving a point at SketchObject level, we need to start from a solved sketch
    // if we have conflicts we can forget about moving. However, there is the possibility that we
    // need to do programmatically moves of new geometry that has not been solved yet and that
    // because they were programmatically generated won't generate a conflict. This is the case of
    // Fillet for example. This is why exceptionally, it may be required to update the sketch
    // geometry to that of of SketchObject upon moving. => use updateGeometry parameter = true then


    if (updateGeoBeforeMoving || solverNeedsUpdate) {
        lastDoF = solvedSketch.setUpSketch(
            getCompleteGeometry(), Constraints.getValues(), getExternalGeometryCount());

        retrieveSolverDiagnostics();

        solverNeedsUpdate = false;
    }

    if (lastDoF < 0)// over-constrained sketch
        return -1;
    if (lastHasConflict)// conflicting constraints
        return -1;

    // move the point and solve
    lastSolverStatus = solvedSketch.moveGeometries(geoEltIds, toPoint, relative);

    // moving the point can not result in a conflict that we did not have
    // or a redundancy that we did not have before, or a change of DoF

    if (lastSolverStatus == 0) {
        std::vector<Part::Geometry*> geomlist = solvedSketch.extractGeometry();
        Geometry.setValues(geomlist);
        for (auto* geo :  geomlist) {
            delete geo;
        }
    }

    solvedSketch.resetInitMove();// reset solver point moving mechanism

    return lastSolverStatus;
}

int SketchObject::moveGeometry(int geoId, PointPos pos, const Base::Vector3d& toPoint, bool relative,
    bool updateGeoBeforeMoving)
{
    std::vector<GeoElementId> geoEltIds = { GeoElementId(geoId, pos) };
    return moveGeometries(geoEltIds, toPoint, relative, updateGeoBeforeMoving);
}

int SketchObject::delGeometries(const std::vector<int>& GeoIds, DeleteOptions options)
{
    return delGeometries(GeoIds.begin(), GeoIds.end(), options);
}

template <class InputIt>
int SketchObject::delGeometries(InputIt first, InputIt last, DeleteOptions options)
{
    std::vector<int> sGeoIds;
    std::vector<int> negativeGeoIds;

    // Separate GeoIds into negative (external) and non-negative GeoIds
    for (auto it = first; it != last; ++it) {
        int geoId = *it;
        if (geoId < 0 && geoId <= GeoEnum::RefExt) {
            negativeGeoIds.push_back(geoId);
        }
        else if (geoId >= 0){
            sGeoIds.push_back(geoId);
        }
    }

    // Handle negative GeoIds by calling delExternal
    if (!negativeGeoIds.empty()) {
        int result = delExternal(negativeGeoIds);
        if (result != 0) {
            return result; // Return if deletion of external geometries failed
        }
    }

    // Proceed with non-negative GeoIds
    if (sGeoIds.empty()) {
        return 0; // No positive GeoIds to delete
    }

    // if a GeoId has internal geometry, it must delete internal geometries too
    for (auto c : Constraints.getValues()) {
        if (c->Type == InternalAlignment) {
            auto pos = std::ranges::find(sGeoIds, c->Second);

            if (pos != sGeoIds.end()) {
                sGeoIds.push_back(c->First);
            }
        }
    }

    std::ranges::sort(sGeoIds);
    // eliminate duplicates
    auto newend = std::unique(sGeoIds.begin(), sGeoIds.end());
    sGeoIds.resize(std::distance(sGeoIds.begin(), newend));

    return delGeometriesExclusiveList(sGeoIds, options);
}

// Trim deletes through a set ordered from the highest index down.
template int SketchObject::delGeometries(std::set<int, std::greater<>>::iterator,
                                         std::set<int, std::greater<>>::iterator,
                                         DeleteOptions);

// clang-format on
void SketchObject::replaceGeometries(std::vector<int> oldGeoIds, std::vector<Part::Geometry*>& newGeos)
{
    auto& vals = getInternalGeometry();
    auto newVals(vals);

    if (std::ranges::any_of(oldGeoIds, [](auto geoId) { return geoId < 0; })) {
        THROWM(ValueError, "Cannot replace external geometries and axes.");
    }

    auto oldGeoIdIter = oldGeoIds.begin();
    auto newGeoIter = newGeos.begin();

    for (; oldGeoIdIter != oldGeoIds.end() && newGeoIter != newGeos.end();
         ++oldGeoIdIter, ++newGeoIter) {
        GeometryFacade::copyId(getGeometry(*oldGeoIdIter), *newGeoIter);
        newVals[*oldGeoIdIter] = *newGeoIter;
    }

    for (; newGeoIter != newGeos.end(); ++newGeoIter) {
        generateId(*newGeoIter);
        newVals.push_back(*newGeoIter);
    }

    // Set geometries first, then delete the old ones. This allows to use `delGeometries`.
    Geometry.setValues(std::move(newVals));

    delGeometries(oldGeoIdIter, oldGeoIds.end());
}
