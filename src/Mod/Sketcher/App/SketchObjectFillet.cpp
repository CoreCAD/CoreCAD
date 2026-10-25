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

std::vector<int> SketchObject::chooseFilletsEdges(const std::vector<int>& GeoIdList) const
{
    if (GeoIdList.size() == 2) {
        return GeoIdList;
    }

    std::vector<int> dst;
    for (auto id : GeoIdList) {
        if (!GeometryFacade::getFacade(getGeometry(id))->getConstruction()) {
            dst.push_back(id);

            if (dst.size() > 2) {
                return {};
            }
        }
    }
    return dst;
}
int SketchObject::fillet(int GeoId, PointPos PosId, double radius, bool trim, bool createCorner, bool chamfer)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return -1;

    // Find the other geometry Id associated with the coincident point
    std::vector<int> GeoIdList;
    std::vector<PointPos> PosIdList;
    getDirectlyCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);

    GeoIdList = chooseFilletsEdges(GeoIdList);

    // only coincident points between two (non-external) edges can be filleted
    if (GeoIdList.size() != 2 || GeoIdList[0] < 0 || GeoIdList[1] < 0) {
        return -1;
    }

    const Part::Geometry* geo1 = getGeometry(GeoIdList[0]);
    const Part::Geometry* geo2 = getGeometry(GeoIdList[1]);

    if (geo1->is<Part::GeomLineSegment>()
        && geo2->is<Part::GeomLineSegment>()) {
        auto* lineSeg1 = static_cast<const Part::GeomLineSegment*>(geo1);
        auto* lineSeg2 = static_cast<const Part::GeomLineSegment*>(geo2);

        Base::Vector3d midPnt1 = (lineSeg1->getStartPoint() + lineSeg1->getEndPoint()) / 2;
        Base::Vector3d midPnt2 = (lineSeg2->getStartPoint() + lineSeg2->getEndPoint()) / 2;
        return fillet(GeoIdList[0], GeoIdList[1], midPnt1, midPnt2, radius, trim, createCorner, chamfer);
    }

    return -1;
}

int SketchObject::fillet(int GeoId1, int GeoId2, const Base::Vector3d& refPnt1,
                         const Base::Vector3d& refPnt2, double radius, bool trim, bool createCorner, bool chamfer)
{
    if (GeoId1 < 0 || GeoId1 > getHighestCurveIndex() || GeoId2 < 0 || GeoId2 > getHighestCurveIndex()) {
        return -1;
    }

    // If either of the two input lines are locked, don't try to trim since it won't work anyway
    const Part::Geometry* geo1 = getGeometry(GeoId1);
    const Part::Geometry* geo2 = getGeometry(GeoId2);
    if (trim && (GeometryFacade::getBlocked(geo1) || GeometryFacade::getBlocked(geo2))) {
        trim = false;
    }

    int pos1 = 0;
    int pos2 = 0;
    bool reverse = false;
    std::unique_ptr<Part::GeomArcOfCircle> arc(createFilletGeometry(geo1, geo2, refPnt1, refPnt2, radius, pos1, pos2, reverse));
    if (!arc) {
        return -1;
    }

    int filletId = addGeometry(arc.get());
    if (filletId < 0) {
        return -1;
    }

    PointPos PosId1 = static_cast<PointPos>(pos1);
    PointPos PosId2= static_cast<PointPos>(pos2);
    PointPos filletPosId1 = PointPos::none;
    PointPos filletPosId2 = PointPos::none;

    Base::Vector3d p1 = arc->getStartPoint(true);
    Base::Vector3d p2 = arc->getEndPoint(true);

    if (trim) {
        if (createCorner && geo1->is<Part::GeomLineSegment>() && geo2->is<Part::GeomLineSegment>()) {
            transferFilletConstraints(GeoId1, PosId1, GeoId2, PosId2);
        }
        else {
            delConstraintOnPoint(GeoId1, PosId1, false);
            delConstraintOnPoint(GeoId2, PosId2, false);
        }

        if (reverse) {
            filletPosId1 = PointPos::start;
            filletPosId2 = PointPos::end;
            moveGeometry(GeoId1, PosId1, p1, false, true);
            moveGeometry(GeoId2, PosId2, p2, false, true);
        }
        else {
            filletPosId1 = PointPos::end;
            filletPosId2 = PointPos::start;
            moveGeometry(GeoId1, PosId1, p2, false, true);
            moveGeometry(GeoId2, PosId2, p1, false, true);
        }

        auto tangent1 = std::make_unique<Sketcher::Constraint>();
        auto tangent2 = std::make_unique<Sketcher::Constraint>();

        tangent1->Type = Sketcher::Tangent;
        tangent1->First = GeoId1;
        tangent1->FirstPos = PosId1;
        tangent1->Second = filletId;
        tangent1->SecondPos = filletPosId1;

        tangent2->Type = Sketcher::Tangent;
        tangent2->First = GeoId2;
        tangent2->FirstPos = PosId2;
        tangent2->Second = filletId;
        tangent2->SecondPos = filletPosId2;

        addConstraint(std::move(tangent1));
        addConstraint(std::move(tangent2));
    }

    if (chamfer) {
        auto line = std::make_unique<Part::GeomLineSegment>();
        line->setPoints(p1, p2);
        int lineGeoId = addGeometry(line.get());


        auto coinc1 = std::make_unique<Sketcher::Constraint>();
        auto coinc2 = std::make_unique<Sketcher::Constraint>();

        coinc1->Type = Sketcher::Coincident;
        coinc1->First = lineGeoId;
        coinc1->FirstPos = filletPosId1;

        coinc2->Type = Sketcher::Coincident;
        coinc2->First = lineGeoId;
        coinc2->FirstPos = filletPosId2;

        if (trim) {
            coinc1->Second = GeoId1;
            coinc1->SecondPos = PosId1;
            coinc2->Second = GeoId2;
            coinc2->SecondPos = PosId2;
        }
        else {
            coinc1->Second = filletId;
            coinc1->SecondPos = PointPos::start;
            coinc2->Second = filletId;
            coinc2->SecondPos = PointPos::end;
        }

        addConstraint(std::move(coinc1));
        addConstraint(std::move(coinc2));

        setConstruction(filletId, true);
    }

    // if we do not have a recompute after the geometry creation, the sketch must be solved to
    // update the DoF of the solver
    if (noRecomputes) {
        solve();
    }

    return 0;
}
// clang-format on
