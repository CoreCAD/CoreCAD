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

int SketchObject::addSymmetric(
    const std::vector<int>& geoIdList,
    int refGeoId,
    Sketcher::PointPos refPosId,
    bool addSymmetryConstraints
)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& constrvals = this->Constraints.getValues();
    std::vector<Constraint*> newconstrVals(constrvals);

    std::map<int, int> geoIdMap;
    std::map<int, bool> isStartEndInverted;

    // Find out if reference is aligned with V or H axis,
    // if so we can keep Vertical and Horizontal constraints in the mirrored geometry.
    bool refIsLine = refPosId == Sketcher::PointPos::none;
    bool refIsAxisAligned = refGeoId == Sketcher::GeoEnum::VAxis
        || refGeoId == Sketcher::GeoEnum::HAxis || !refIsLine
        || std::ranges::any_of(constrvals, [&refGeoId](auto* constr) {
                                return constr->getElement(0).GeoId == refGeoId
                                    && (constr->Type == Sketcher::Vertical
                                        || constr->Type == Sketcher::Horizontal);
                            });

    std::vector<Part::Geometry*> symgeos
        = getSymmetric(geoIdList, geoIdMap, isStartEndInverted, refGeoId, refPosId);

    // Perturb geometry to avoid numerical singularities in the solver (Jacobian Rank).
    // If geometry is "perfect", the solver cannot distinguish between the derivative
    // of a Symmetry constraint and an Equal constraint, flagging one as redundant.
    // see https://github.com/FreeCAD/FreeCAD/issues/13551
    // This does not happen with other arcs types.
    if (addSymmetryConstraints) {
        for (auto* geo : symgeos) {
            if (auto* arc = dynamic_cast<Part::GeomArcOfCircle*>(geo)) {
                double start, end;
                arc->getRange(start, end, true);
                arc->setRange(start + Precision::Angular(), end, true);
            }
        }
    }

    {
        addGeometry(symgeos);

        for (auto* constr : constrvals) {
            // we look in the map, because we might have skipped internal alignment geometry
            auto firstIt = geoIdMap.find(constr->getElement(0).GeoId);

            if (firstIt == geoIdMap.end()) {
                continue;
            }

            // if First of constraint is in geoIdList
            if (addSymmetryConstraints && constr->Type != Sketcher::InternalAlignment) {
                // If we are making symmetric constraints, we only want to copy topological
                // constraints to maintain the structural shape (Coincident, and
                // endpoint-to-endpoint Tangent/Perpendicular).
                bool keepTopological = false;
                if (constr->Type == Sketcher::Coincident) {
                    keepTopological = true;
                }
                else if (constr->Type == Sketcher::Tangent || constr->Type == Sketcher::Perpendicular) {
                    if (constr->FirstPos != Sketcher::PointPos::none
                        && constr->SecondPos != Sketcher::PointPos::none) {
                        keepTopological = true;
                    }
                }

                if (!keepTopological) {
                    continue;
                }
            }

            if (constr->getElement(1).GeoId == GeoEnum::GeoUndef  //
                /*&& constr->getElement(2).GeoId == GeoEnum::GeoUndef*/) {
                if (refIsAxisAligned
                    && (constr->Type == Sketcher::DistanceX || constr->Type == Sketcher::DistanceY)) {
                    // In this case we want to keep the Vertical, Horizontal constraints.
                    // DistanceX and DistanceY constraints should also be possible to keep in
                    // this case, but keeping them causes segfault, not sure why.

                    continue;
                }
                if (!refIsAxisAligned
                    && (constr->Type == Sketcher::DistanceX     //
                        || constr->Type == Sketcher::DistanceY  //
                        || constr->Type == Sketcher::Vertical   //
                        || constr->Type == Sketcher::Horizontal)) {
                    // this includes all non-directional single GeoId constraints, as radius,
                    // diameter, weight,...
                    continue;
                }
                Constraint* constNew = constr->copy();
                constNew->Name = "";
                GeoElementId rep = constr->getElement(0);
                rep.GeoId = firstIt->second;
                constNew->setElement(0, rep);
                newconstrVals.push_back(constNew);

                continue;
            }

            // other geoids intervene in this constraint
            auto secondIt = geoIdMap.find(constr->getElement(1).GeoId);

            if (secondIt == geoIdMap.end()) {
                continue;
            }

            // Second is also in the list

            auto flipStartEndIfRelevant =
                [&isStartEndInverted](GeoElementId geId, Sketcher::PointPos& posIdNew) {
                    if (isStartEndInverted[geId.GeoId]) {
                        if (geId.Pos == Sketcher::PointPos::start) {
                            posIdNew = Sketcher::PointPos::end;
                        }
                        else if (geId.Pos == Sketcher::PointPos::end) {
                            posIdNew = Sketcher::PointPos::start;
                        }
                    }
                };

            if (constr->getElement(2).GeoId == GeoEnum::GeoUndef) {
                if (!(constr->Type == Sketcher::Coincident        //
                      || constr->Type == Sketcher::Perpendicular  //
                      || constr->Type == Sketcher::Parallel       //
                      || constr->Type == Sketcher::Tangent        //
                      || constr->Type == Sketcher::Distance       //
                      || constr->Type == Sketcher::Equal          //
                      || constr->Type == Sketcher::Angle          //
                      || constr->Type == Sketcher::PointOnObject  //
                      || constr->Type == Sketcher::InternalAlignment)) {
                    continue;
                }

                Constraint* constNew = constr->copy();

                // If making a dependent symmetry copy, Tangent/Perp would overconstrain the angle
                // (which is already locked by the symmetries of the endpoints/centers).
                // Downgrade endpoint-to-endpoint constraints to Coincident to keep topology.
                if (addSymmetryConstraints
                    && (constNew->Type == Sketcher::Tangent
                        || constNew->Type == Sketcher::Perpendicular)) {
                    constNew->Type = Sketcher::Coincident;
                }

                constNew->Name = "";
                auto rep0 = constNew->getElement(0);
                auto rep1 = constNew->getElement(1);
                rep0.GeoId = firstIt->second;
                rep1.GeoId = secondIt->second;
                flipStartEndIfRelevant(constr->getElement(0), rep0.Pos);
                flipStartEndIfRelevant(constr->getElement(1), rep1.Pos);
                constNew->setElement(0, rep0);
                constNew->setElement(1, rep1);

                if (constNew->Type == Tangent || constNew->Type == Perpendicular) {
                    AutoLockTangencyAndPerpty(constNew, true);
                }

                if ((constr->Type == Sketcher::Angle) && (refPosId == Sketcher::PointPos::none)) {
                    constNew->setValue(-constr->getValue());
                }

                newconstrVals.push_back(constNew);
                continue;
            }

            // three GeoIds intervene in constraint
            auto thirdIt = geoIdMap.find(constr->getElement(2).GeoId);

            if (thirdIt == geoIdMap.end()) {
                continue;
            }

            // Third is also in the list
            Constraint* constNew = constr->copy();
            constNew->Name = "";
            auto rep0 = constNew->getElement(0);
            auto rep1 = constNew->getElement(1);
            auto rep2 = constNew->getElement(2);
            rep0.GeoId = firstIt->second;
            rep1.GeoId = secondIt->second;
            rep2.GeoId = thirdIt->second;
            flipStartEndIfRelevant(constr->getElement(0), rep0.Pos);
            flipStartEndIfRelevant(constr->getElement(1), rep1.Pos);
            flipStartEndIfRelevant(constr->getElement(2), rep2.Pos);
            constNew->setElement(0, rep0);
            constNew->setElement(1, rep1);
            constNew->setElement(2, rep2);
            newconstrVals.push_back(constNew);
        }

        if (!addSymmetryConstraints) {
            if (newconstrVals.size() > constrvals.size()) {
                Constraints.setValues(std::move(newconstrVals));
            }

            // we delayed update, so trigger it now.
            // Update geometry indices and rebuild vertexindex now via onChanged, so that
            // ViewProvider::UpdateData is triggered.
            Geometry.touch();

            return Geometry.getSize() - 1;
        }

        // Retrieve all coincident point groups in the original sketch
        auto coincidenceGroups = getCoincidenceGroups();
        std::set<std::pair<int, Sketcher::PointPos>> symmetricConstrainedOriginalPoints;

        auto markPointConstrained = [&](int geoId, Sketcher::PointPos pos) {
            std::pair<int, Sketcher::PointPos> pt {geoId, pos};
            symmetricConstrainedOriginalPoints.insert(pt);
            for (const auto& group : coincidenceGroups) {
                if (group.find(geoId) != group.end() && group.at(geoId) == pos) {
                    for (const auto& member : group) {
                        symmetricConstrainedOriginalPoints.insert(
                            std::make_pair(member.first, member.second)
                        );
                    }
                    break;
                }
            }
        };

        // Helper to check if a point lies on the symmetry reference
        auto isPointOnReference = [&](const Base::Vector3d& pt) {
            if (refPosId == Sketcher::PointPos::none) {
                // Reference is a line (Axis of Symmetry)
                const Part::Geometry* georef = getGeometry(refGeoId);
                if (georef && georef->is<Part::GeomLineSegment>()) {
                    auto* refLine = static_cast<const Part::GeomLineSegment*>(georef);
                    Base::Vector3d l1 = refLine->getStartPoint();
                    Base::Vector3d l2 = refLine->getEndPoint();
                    double dx = l2.x - l1.x;
                    double dy = l2.y - l1.y;
                    double line_len_sq = dx * dx + dy * dy;
                    if (line_len_sq > Precision::SquareConfusion()) {
                        double area = (pt.x - l1.x) * dy - (pt.y - l1.y) * dx;
                        if (std::abs(area) / sqrt(line_len_sq) < Precision::Confusion()) {
                            return true;
                        }
                    }
                }
            }
            else {
                // Reference is a point (Point Symmetry)
                Base::Vector3d refPt;
                if (refGeoId == Sketcher::GeoEnum::RtPnt) {
                    refPt = Base::Vector3d(0, 0, 0);
                }
                else {
                    refPt = getPoint(refGeoId, refPosId);
                }
                if ((pt - refPt).Length() < Precision::Confusion()) {
                    return true;
                }
            }
            return false;
        };

        auto createSymConstr =
            [&](int first, int second, Sketcher::PointPos firstPos, Sketcher::PointPos secondPos) {
                std::pair<int, Sketcher::PointPos> pt {first, firstPos};
                // If this point (or any coincident peer) already got a symmetry constraint, skip
                if (symmetricConstrainedOriginalPoints.count(pt) > 0) {
                    return;
                }

                Base::Vector3d pCoords = getPoint(first, firstPos);

                if (isPointOnReference(pCoords)) {
                    // Point is on the mirror axis: use Coincident to stitch and avoid singularity
                    auto* coincConstr = new Constraint();
                    coincConstr->Type = Sketcher::Coincident;
                    coincConstr->setElement(0, GeoElementId {first, firstPos});
                    coincConstr->setElement(1, GeoElementId {second, secondPos});
                    newconstrVals.push_back(coincConstr);
                }
                else {
                    // Standard symmetry constraint for off-axis points
                    auto* symConstr = new Constraint();
                    symConstr->Type = Sketcher::Symmetric;
                    symConstr->setElement(0, GeoElementId {first, firstPos});
                    symConstr->setElement(1, GeoElementId {second, secondPos});
                    symConstr->setElement(2, GeoElementId {refGeoId, refPosId});
                    newconstrVals.push_back(symConstr);
                }

                markPointConstrained(first, firstPos);
            };
        auto createEqualityConstr = [&](int first, int second) {
            auto* symConstr = new Constraint();
            symConstr->Type = Equal;
            symConstr->setElement(0, GeoElementId {first});
            symConstr->setElement(1, GeoElementId {second});
            newconstrVals.push_back(symConstr);
        };

        for (auto geoIdPair : geoIdMap) {
            int geoId1 = geoIdPair.first;
            int geoId2 = geoIdPair.second;
            const Part::Geometry* geo = getGeometry(geoId1);

            if (geo->is<Part::GeomLineSegment>()) {
                auto gf = GeometryFacade::getFacade(geo);
                if (!gf->isInternalAligned()) {
                    // Note internal aligned lines (ellipse, parabola, hyperbola) are causing
                    // redundant constraint.
                    createSymConstr(
                        geoId1,
                        geoId2,
                        PointPos::start,
                        isStartEndInverted[geoId1] ? PointPos::end : PointPos::start
                    );
                    createSymConstr(
                        geoId1,
                        geoId2,
                        PointPos::end,
                        isStartEndInverted[geoId1] ? PointPos::start : PointPos::end
                    );
                }
            }
            else if (geo->is<Part::GeomCircle>() || geo->is<Part::GeomEllipse>()) {
                createEqualityConstr(geoId1, geoId2);
                createSymConstr(geoId1, geoId2, PointPos::mid, PointPos::mid);
            }
            else if (
                geo->is<Part::GeomArcOfCircle>()        //
                || geo->is<Part::GeomArcOfEllipse>()    //
                || geo->is<Part::GeomArcOfHyperbola>()  //
                || geo->is<Part::GeomArcOfParabola>()
            ) {
                createEqualityConstr(geoId1, geoId2);
                createSymConstr(
                    geoId1,
                    geoId2,
                    PointPos::start,
                    isStartEndInverted[geoId1] ? PointPos::end : PointPos::start
                );
                createSymConstr(
                    geoId1,
                    geoId2,
                    PointPos::end,
                    isStartEndInverted[geoId1] ? PointPos::start : PointPos::end
                );
            }
            else if (geo->is<Part::GeomPoint>()) {
                auto gf = GeometryFacade::getFacade(geo);
                if (!gf->isInternalAligned()) {
                    createSymConstr(geoId1, geoId2, PointPos::start, PointPos::start);
                }
            }
            // Note bspline has symmetric by the internal aligned circles.
        }

        if (newconstrVals.size() > constrvals.size()) {
            Constraints.setValues(std::move(newconstrVals));
        }
    }

    // we delayed update, so trigger it now.
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return Geometry.getSize() - 1;
}

std::vector<Part::Geometry*> SketchObject::getSymmetric(
    const std::vector<int>& geoIdList,
    std::map<int, int>& geoIdMap,
    std::map<int, bool>& isStartEndInverted,
    int refGeoId,
    Sketcher::PointPos refPosId
)
{
    using std::numbers::pi;

    std::vector<Part::Geometry*> symmetricVals;
    bool refIsLine = refPosId == Sketcher::PointPos::none;
    int cgeoid = getHighestCurveIndex() + 1;

    auto shouldCopyGeometry = [&](auto* geo, int geoId) -> bool {
        auto gf = GeometryFacade::getFacade(geo);
        if (!gf->isInternalAligned()) {
            // Return true if not internal aligned, indicating it should always be copied
            return true;
        }

        // only add if the corresponding geometry it defines is also in the list.
        const auto& constraints = Constraints.getValues();
        auto constrIt = std::ranges::find_if(constraints, [&geoId](auto* c) {
            return c->Type == Sketcher::InternalAlignment && c->getElement(0).GeoId == geoId;
        });
        int definedGeo = (constrIt == constraints.end()) ? GeoEnum::GeoUndef
                                                         : (*constrIt)->getElement(1).GeoId;
        // Return true if definedGeo is in geoIdList, false otherwise
        return std::ranges::find(geoIdList, definedGeo) != geoIdList.end();
    };

    if (refIsLine) {
        const Part::Geometry* georef = getGeometry(refGeoId);
        if (!georef->is<Part::GeomLineSegment>()) {
            return {};
        }

        auto* refGeoLine = static_cast<const Part::GeomLineSegment*>(georef);
        // line
        Base::Vector3d refstart = refGeoLine->getStartPoint();
        Base::Vector3d vectline = refGeoLine->getEndPoint() - refstart;

        for (auto geoId : geoIdList) {
            const Part::Geometry* geo = getGeometry(geoId);
            Part::Geometry* geosym;

            if (!shouldCopyGeometry(geo, geoId)) {
                continue;
            }

            geosym = geo->copy();

            // Handle Geometry
            if (geosym->is<Part::GeomLineSegment>()) {
                auto* geosymline = static_cast<Part::GeomLineSegment*>(geosym);
                Base::Vector3d sp = geosymline->getStartPoint();
                Base::Vector3d ep = geosymline->getEndPoint();

                geosymline->setPoints(
                    sp + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp),
                    ep + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomCircle>()) {
                auto* geosymcircle = static_cast<Part::GeomCircle*>(geosym);
                Base::Vector3d cp = geosymcircle->getCenter();

                geosymcircle->setCenter(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfCircle>()) {
                auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geosym);
                Base::Vector3d sp = geoaoc->getStartPoint(true);
                Base::Vector3d ep = geoaoc->getEndPoint(true);
                Base::Vector3d cp = geoaoc->getCenter();

                Base::Vector3d ssp = sp
                    + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp);
                Base::Vector3d sep = ep
                    + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                double theta1 = Base::fmod(atan2(sep.y - scp.y, sep.x - scp.x), 2.f * std::numbers::pi);
                double theta2 = Base::fmod(atan2(ssp.y - scp.y, ssp.x - scp.x), 2.f * std::numbers::pi);

                geoaoc->setCenter(scp);
                geoaoc->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomEllipse>()) {
                auto* geosymellipse = static_cast<Part::GeomEllipse*>(geosym);
                Base::Vector3d cp = geosymellipse->getCenter();

                Base::Vector3d majdir = geosymellipse->getMajorAxisDir();
                double majord = geosymellipse->getMajorRadius();
                double minord = geosymellipse->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymellipse->setMajorAxisDir(sf1 - scp);

                geosymellipse->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfEllipse>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfEllipse*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = 2.0 * pi - theta1;
                theta2 = 2.0 * pi - theta2;
                std::swap(theta1, theta2);
                if (theta1 < 0) {
                    theta1 += 2.0 * pi;
                    theta2 += 2.0 * pi;
                }

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfHyperbola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfHyperbola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord + minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfParabola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfParabola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d f1 = geosymaoe->getFocus();

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setXAxisDir(sf1 - scp);
                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomBSplineCurve>()) {
                auto* geosymbsp = static_cast<Part::GeomBSplineCurve*>(geosym);

                std::vector<Base::Vector3d> poles = geosymbsp->getPoles();

                for (auto& pole : poles) {
                    pole = pole
                        + 2.0 * (pole.Perpendicular(refGeoLine->getStartPoint(), vectline) - pole);
                }

                geosymbsp->setPoles(poles);

                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomPoint>()) {
                auto* geosympoint = static_cast<Part::GeomPoint*>(geosym);
                Base::Vector3d cp = geosympoint->getPoint();

                geosympoint->setPoint(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else {
                Base::Console().error("Unsupported Geometry!! Just copying it.\n");
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }

            symmetricVals.push_back(geosym);
            geoIdMap.insert(std::make_pair(geoId, cgeoid));
            cgeoid++;
        }

        return symmetricVals;
    }

    // reference is a point
    Vector3d refpoint;
    const Part::Geometry* georef = getGeometry(refGeoId);

    if (georef->is<Part::GeomPoint>()) {
        refpoint = static_cast<const Part::GeomPoint*>(georef)->getPoint();
    }
    else if (refGeoId == -1 && refPosId == Sketcher::PointPos::start) {
        refpoint = Vector3d(0, 0, 0);
    }
    else {
        if (refPosId == Sketcher::PointPos::none) {
            Base::Console().error("Wrong PointPosId.\n");
            return {};
        }
        refpoint = getPoint(georef, refPosId);
    }

    for (auto geoId : geoIdList) {
        const Part::Geometry* geo = getGeometry(geoId);
        Part::Geometry* geosym;

        if (!shouldCopyGeometry(geo, geoId)) {
            continue;
        }

        geosym = geo->copy();

        // Handle Geometry
        if (geosym->is<Part::GeomLineSegment>()) {
            auto* geosymline = static_cast<Part::GeomLineSegment*>(geosym);
            Base::Vector3d sp = geosymline->getStartPoint();
            Base::Vector3d ep = geosymline->getEndPoint();
            Base::Vector3d ssp = sp + 2.0 * (refpoint - sp);
            Base::Vector3d sep = ep + 2.0 * (refpoint - ep);

            geosymline->setPoints(ssp, sep);
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomCircle>()) {
            auto* geosymcircle = static_cast<Part::GeomCircle*>(geosym);
            Base::Vector3d cp = geosymcircle->getCenter();

            geosymcircle->setCenter(cp + 2.0 * (refpoint - cp));
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomArcOfCircle>()) {
            auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geosym);
            Base::Vector3d sp = geoaoc->getStartPoint(true);
            Base::Vector3d ep = geoaoc->getEndPoint(true);
            Base::Vector3d cp = geoaoc->getCenter();

            Base::Vector3d ssp = sp + 2.0 * (refpoint - sp);
            Base::Vector3d sep = ep + 2.0 * (refpoint - ep);
            Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

            double theta1 = Base::fmod(atan2(ssp.y - scp.y, ssp.x - scp.x), 2.f * pi);
            double theta2 = Base::fmod(atan2(sep.y - scp.y, sep.x - scp.x), 2.f * pi);

            geoaoc->setCenter(scp);
            geoaoc->setRange(theta1, theta2, true);
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomEllipse>()) {
            auto* geosymellipse = static_cast<Part::GeomEllipse*>(geosym);
            Base::Vector3d cp = geosymellipse->getCenter();

            Base::Vector3d majdir = geosymellipse->getMajorAxisDir();
            double majord = geosymellipse->getMajorRadius();
            double minord = geosymellipse->getMinorRadius();
            double df = sqrt(majord * majord - minord * minord);
            Base::Vector3d f1 = cp + df * majdir;

            Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
            Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

            geosymellipse->setMajorAxisDir(sf1 - scp);

            geosymellipse->setCenter(scp);
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomArcOfEllipse>()) {
            auto* geosymaoe = static_cast<Part::GeomArcOfEllipse*>(geosym);
            Base::Vector3d cp = geosymaoe->getCenter();

            Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
            double majord = geosymaoe->getMajorRadius();
            double minord = geosymaoe->getMinorRadius();
            double df = sqrt(majord * majord - minord * minord);
            Base::Vector3d f1 = cp + df * majdir;

            Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
            Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

            geosymaoe->setMajorAxisDir(sf1 - scp);

            geosymaoe->setCenter(scp);
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomArcOfHyperbola>()) {
            auto* geosymaoe = static_cast<Part::GeomArcOfHyperbola*>(geosym);
            Base::Vector3d cp = geosymaoe->getCenter();

            Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
            double majord = geosymaoe->getMajorRadius();
            double minord = geosymaoe->getMinorRadius();
            double df = sqrt(majord * majord + minord * minord);
            Base::Vector3d f1 = cp + df * majdir;

            Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
            Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

            geosymaoe->setMajorAxisDir(sf1 - scp);

            geosymaoe->setCenter(scp);
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomArcOfParabola>()) {
            auto* geosymaoe = static_cast<Part::GeomArcOfParabola*>(geosym);
            Base::Vector3d cp = geosymaoe->getCenter();
            Base::Vector3d f1 = geosymaoe->getFocus();

            Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
            Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

            geosymaoe->setXAxisDir(sf1 - scp);
            geosymaoe->setCenter(scp);

            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else if (geosym->is<Part::GeomBSplineCurve>()) {
            auto* geosymbsp = static_cast<Part::GeomBSplineCurve*>(geosym);

            std::vector<Base::Vector3d> poles = geosymbsp->getPoles();

            for (auto& pole : poles) {
                pole = pole + 2.0 * (refpoint - pole);
            }

            geosymbsp->setPoles(poles);
        }
        else if (geosym->is<Part::GeomPoint>()) {
            auto* geosympoint = static_cast<Part::GeomPoint*>(geosym);
            Base::Vector3d cp = geosympoint->getPoint();

            geosympoint->setPoint(cp + 2.0 * (refpoint - cp));
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }
        else {
            Base::Console().error("Unsupported Geometry!! Just copying it.\n");
            isStartEndInverted.insert(std::make_pair(geoId, false));
        }

        symmetricVals.push_back(geosym);
        geoIdMap.insert(std::make_pair(geoId, cgeoid));
        cgeoid++;
    }

    return symmetricVals;
}

int SketchObject::addCopy(
    const std::vector<int>& geoIdList,
    const Base::Vector3d& displacement,
    bool moveonly,
    bool clone,
    int csize,
    int rsize,
    bool constraindisplacement,
    double perpscale
)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& geovals = getInternalGeometry();
    std::vector<Part::Geometry*> newgeoVals(geovals);

    const std::vector<Constraint*>& constrvals = this->Constraints.getValues();
    std::vector<Constraint*> newconstrVals(constrvals);

    if (!moveonly) {
        newgeoVals.reserve(geovals.size() + geoIdList.size());
    }

    std::vector<int> newgeoIdList(geoIdList);

    if (newgeoIdList.empty()) {  // default option to operate on all the geometry
        for (int i = 0; i < int(geovals.size()); i++) {
            newgeoIdList.push_back(i);
        }
    }

    int cgeoid = getHighestCurveIndex() + 1;

    int iterfirstgeoid = -1;

    Base::Vector3d iterfirstpoint;

    int refgeoid = -1;

    int colrefgeoid = 0, rowrefgeoid = 0;

    int currentrowfirstgeoid = -1, prevrowstartfirstgeoid = -1, prevfirstgeoid = -1;

    Sketcher::PointPos refposId = Sketcher::PointPos::none;

    std::map<int, int> geoIdMap;

    Base::Vector3d perpendicularDisplacement
        = Base::Vector3d(perpscale * displacement.y, perpscale * -displacement.x, 0);

    int x, y;

    auto makeCopyAtRowColumn = [&](int x, int y) {
        // the reference for constraining array elements is the first valid point of the first
        // element
        if (x == 0 && y == 0) {
            const Part::Geometry* geo = getGeometry(*(newgeoIdList.begin()));

            auto gf = GeometryFacade::getFacade(geo);

            if (gf->isInternalAligned() && !moveonly) {
                // only add this geometry if the corresponding geometry it defines is also in
                // the list.

                auto constrIt = std::ranges::find_if(constrvals, [&newgeoIdList](auto c) {
                    return (
                        c->Type == Sketcher::InternalAlignment && c->First == *(newgeoIdList.begin())
                    );
                });

                int definedGeo = (constrIt != constrvals.end()) ? (*constrIt)->Second
                                                                : GeoEnum::GeoUndef;

                if (std::ranges::find(newgeoIdList, definedGeo) == newgeoIdList.end()) {
                    // the first element setting the reference is an internal alignment
                    // geometry, wherein the geometry it defines is not part of the copy
                    // operation.
                    THROWM(
                        Base::ValueError,
                        "A move/copy/array operation on an internal alignment geometry is "
                        "only possible together with the geometry it defines."
                    );
                }
            }

            refgeoid = *(newgeoIdList.begin());
            currentrowfirstgeoid = refgeoid;
            iterfirstgeoid = refgeoid;
            if (geo->is<Part::GeomCircle>() || geo->is<Part::GeomEllipse>()) {
                refposId = Sketcher::PointPos::mid;
            }
            else {
                refposId = Sketcher::PointPos::start;
            }

            return;  // the first element is already in place
        }

        prevfirstgeoid = iterfirstgeoid;

        iterfirstgeoid = cgeoid;

        if (x == 0) {  // if first element of second row
            prevrowstartfirstgeoid = currentrowfirstgeoid;
            currentrowfirstgeoid = cgeoid;
        }

        int index = 0;
        for (auto it = newgeoIdList.cbegin(); it != newgeoIdList.cend(); ++it, ++index) {
            const Part::Geometry* geo = getGeometry(*it);

            Part::Geometry* geocopy;

            auto gf = GeometryFacade::getFacade(geo);

            if (gf->isInternalAligned() && !moveonly) {
                // only add this geometry if the corresponding geometry it defines is also in
                // the list.
                int definedGeo = GeoEnum::GeoUndef;

                auto constrIt = std::ranges::find_if(constrvals, [&it](auto c) {
                    return (c->Type == Sketcher::InternalAlignment && c->First == *it);
                });
                if (constrIt != constrvals.end()) {
                    definedGeo = (*constrIt)->Second;
                }

                if (std::ranges::find(newgeoIdList, definedGeo) == newgeoIdList.end()) {
                    // we should not copy internal alignment geometry, unless the element they
                    // define is also mirrored
                    continue;
                }
            }

            // We have already cloned all geometry and constraints, we only need a copy if not
            // moving
            if (!moveonly) {
                geocopy = geo->copy();
                generateId(geocopy);
            }
            else {
                geocopy = newgeoVals[*it];
            }

            // Handle Geometry
            if (geocopy->is<Part::GeomLineSegment>()) {
                auto* geosymline = static_cast<Part::GeomLineSegment*>(geocopy);
                Base::Vector3d ep = geosymline->getEndPoint();
                Base::Vector3d ssp = geosymline->getStartPoint() + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geosymline->setPoints(
                    ssp,
                    ep + double(x) * displacement + double(y) * perpendicularDisplacement
                );

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = ssp;
                }
            }
            else if (geocopy->is<Part::GeomCircle>()) {
                auto* geosymcircle = static_cast<Part::GeomCircle*>(geocopy);
                Base::Vector3d cp = geosymcircle->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geosymcircle->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = scp;
                }
            }
            else if (geocopy->is<Part::GeomArcOfCircle>()) {
                auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geocopy);
                Base::Vector3d cp = geoaoc->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geoaoc->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = geoaoc->getStartPoint(true);
                }
            }
            else if (geocopy->is<Part::GeomEllipse>()) {
                auto* geosymellipse = static_cast<Part::GeomEllipse*>(geocopy);
                Base::Vector3d cp = geosymellipse->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geosymellipse->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = scp;
                }
            }
            else if (geocopy->is<Part::GeomArcOfEllipse>()) {
                auto* geoaoe = static_cast<Part::GeomArcOfEllipse*>(geocopy);
                Base::Vector3d cp = geoaoe->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geoaoe->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = geoaoe->getStartPoint(true);
                }
            }
            else if (geocopy->is<Part::GeomArcOfHyperbola>()) {
                auto* geoaoe = static_cast<Part::GeomArcOfHyperbola*>(geocopy);
                Base::Vector3d cp = geoaoe->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geoaoe->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = geoaoe->getStartPoint(true);
                }
            }
            else if (geocopy->is<Part::GeomArcOfParabola>()) {
                auto* geoaoe = static_cast<Part::GeomArcOfParabola*>(geocopy);
                Base::Vector3d cp = geoaoe->getCenter();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;

                geoaoe->setCenter(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = geoaoe->getStartPoint(true);
                }
            }
            else if (geocopy->is<Part::GeomBSplineCurve>()) {
                auto* geobsp = static_cast<Part::GeomBSplineCurve*>(geocopy);

                std::vector<Base::Vector3d> poles = geobsp->getPoles();

                for (auto& pole : poles) {
                    pole = pole + double(x) * displacement + double(y) * perpendicularDisplacement;
                }

                geobsp->setPoles(poles);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = geobsp->getStartPoint();
                }
            }
            else if (geocopy->is<Part::GeomPoint>()) {
                auto* geopoint = static_cast<Part::GeomPoint*>(geocopy);
                Base::Vector3d cp = geopoint->getPoint();
                Base::Vector3d scp = cp + double(x) * displacement
                    + double(y) * perpendicularDisplacement;
                geopoint->setPoint(scp);

                if (it == newgeoIdList.begin()) {
                    iterfirstpoint = scp;
                }
            }
            else {
                Base::Console().error("Unsupported Geometry!! Just skipping it.\n");
                continue;
            }

            if (!moveonly) {  // we are copying
                newgeoVals.push_back(geocopy);
                geoIdMap.insert(std::make_pair(*it, cgeoid));
                cgeoid++;
            }
        }

        if (moveonly) {
            return;
        }

        // handle geometry constraints
        for (const auto& constr : constrvals) {
            auto fit = geoIdMap.find(constr->First);

            if (fit == geoIdMap.end()) {
                continue;
            }

            // First of constraint is in geoIdList
            if (constr->Second == GeoEnum::GeoUndef /*&& constr->Third == GeoEnum::GeoUndef*/) {
                if ((constr->Type == Sketcher::DistanceX || constr->Type == Sketcher::DistanceY)
                    && constr->FirstPos != Sketcher::PointPos::none) {
                    continue;
                }
                // if it is not a point locking DistanceX/Y
                if ((constr->Type == Sketcher::DistanceX || constr->Type == Sketcher::DistanceY
                     || constr->Type == Sketcher::Distance || constr->Type == Sketcher::Diameter
                     || constr->Type == Sketcher::Weight || constr->Type == Sketcher::Radius)
                    && clone) {
                    // Distances on a single Element are mapped to equality
                    // constraints in clone mode
                    Constraint* constNew = constr->copy();
                    constNew->Type = Sketcher::Equal;
                    constNew->isDriving = true;
                    // first is already (constr->First)
                    constNew->Second = fit->second;
                    newconstrVals.push_back(constNew);
                    continue;
                }
                if (!(constr->Type == Sketcher::Angle && clone)) {
                    Constraint* constNew = constr->copy();
                    constNew->First = fit->second;
                    newconstrVals.push_back(constNew);
                    continue;
                }
                if (getGeometry(constr->First)->is<Part::GeomLineSegment>()) {
                    // Angles on a single Element are mapped to parallel
                    // constraints in clone mode
                    Constraint* constNew = constr->copy();
                    constNew->Type = Sketcher::Parallel;
                    constNew->isDriving = true;
                    // first is already (constr->First)
                    constNew->Second = fit->second;
                    newconstrVals.push_back(constNew);
                }
                continue;
            }

            // other geoids intervene in this constraint
            auto sit = geoIdMap.find(constr->Second);

            if (sit == geoIdMap.end()) {
                continue;
            }

            // Second is also in the list
            if (constr->Third == GeoEnum::GeoUndef) {
                if ((constr->Type == Sketcher::DistanceX || constr->Type == Sketcher::DistanceY
                     || constr->Type == Sketcher::Distance)
                    && (constr->First == constr->Second) && clone) {
                    // Distances on a two Elements, which must be points of the
                    // same line are mapped to equality constraints in clone
                    // mode
                    Constraint* constNew = constr->copy();
                    constNew->Type = Sketcher::Equal;
                    constNew->isDriving = true;
                    constNew->FirstPos = Sketcher::PointPos::none;
                    // first is already (constr->First)
                    constNew->Second = fit->second;
                    constNew->SecondPos = Sketcher::PointPos::none;
                    newconstrVals.push_back(constNew);
                    continue;
                }
                // remaining, this includes InternalAlignment constraints
                Constraint* constNew = constr->copy();
                constNew->First = fit->second;
                constNew->Second = sit->second;
                newconstrVals.push_back(constNew);
                continue;
            }

            auto tit = geoIdMap.find(constr->Third);

            if (tit != geoIdMap.end()) {
                continue;
            }

            // Third is also in the list
            Constraint* constNew = constr->copy();
            constNew->First = fit->second;
            constNew->Second = sit->second;
            constNew->Third = tit->second;

            newconstrVals.push_back(constNew);
        }

        // handle inter-geometry constraints
        if (!constraindisplacement) {
            // after each creation reset map so that the key-value is univoque (only for
            // operations other than move)
            geoIdMap.clear();
        }

        // add a construction line
        auto* constrline = new Part::GeomLineSegment();

        // position of the reference point
        Base::Vector3d sp = getPoint(refgeoid, refposId)
            + ((x == 0) ? (double(x) * displacement + double(y - 1) * perpendicularDisplacement)
                        : (double(x - 1) * displacement + double(y) * perpendicularDisplacement));

        // position of the current instance corresponding point
        Base::Vector3d ep = iterfirstpoint;
        constrline->setPoints(sp, ep);
        GeometryFacade::setConstruction(constrline, true);

        generateId(constrline);
        newgeoVals.push_back(constrline);

        Constraint* constNew;

        if (x == 0) {
            // first element of a row

            // add coincidents for construction line
            constNew = new Constraint();
            constNew->Type = Sketcher::Coincident;
            constNew->First = prevrowstartfirstgeoid;
            constNew->FirstPos = refposId;
            constNew->Second = cgeoid;
            constNew->SecondPos = Sketcher::PointPos::start;
            newconstrVals.push_back(constNew);

            constNew = new Constraint();
            constNew->Type = Sketcher::Coincident;
            constNew->First = iterfirstgeoid;
            constNew->FirstPos = refposId;
            constNew->Second = cgeoid;
            constNew->SecondPos = Sketcher::PointPos::end;
            newconstrVals.push_back(constNew);

            // it is the first added element of this row in the perpendicular to
            // displacementvector direction
            if (y == 1) {
                rowrefgeoid = cgeoid;
                cgeoid++;

                // add length (or equal if perpscale==1) and perpendicular
                if (perpscale == 1.0) {
                    constNew = new Constraint();
                    constNew->Type = Sketcher::Equal;
                    constNew->First = rowrefgeoid;
                    constNew->FirstPos = Sketcher::PointPos::none;
                    constNew->Second = colrefgeoid;
                    constNew->SecondPos = Sketcher::PointPos::none;
                    newconstrVals.push_back(constNew);
                }
                else {
                    constNew = new Constraint();
                    constNew->Type = Sketcher::Distance;
                    constNew->First = rowrefgeoid;
                    constNew->FirstPos = Sketcher::PointPos::none;
                    constNew->setValue(perpendicularDisplacement.Length());
                    newconstrVals.push_back(constNew);
                }

                constNew = new Constraint();
                constNew->Type = Sketcher::Perpendicular;
                constNew->First = rowrefgeoid;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->Second = colrefgeoid;
                constNew->SecondPos = Sketcher::PointPos::none;
                newconstrVals.push_back(constNew);
            }
            else {
                // it is just one more element in the col direction
                cgeoid++;

                // all other first rowers get an equality and perpendicular constraint
                constNew = new Constraint();
                constNew->Type = Sketcher::Equal;
                constNew->First = rowrefgeoid;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->Second = cgeoid - 1;
                constNew->SecondPos = Sketcher::PointPos::none;
                newconstrVals.push_back(constNew);

                constNew = new Constraint();
                constNew->Type = Sketcher::Perpendicular;
                constNew->First = cgeoid - 1;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->Second = colrefgeoid;
                constNew->SecondPos = Sketcher::PointPos::none;
                newconstrVals.push_back(constNew);
            }
        }
        else {
            // any element not being the first element of a row

            // add coincidents for construction line
            constNew = new Constraint();
            constNew->Type = Sketcher::Coincident;
            constNew->First = prevfirstgeoid;
            constNew->FirstPos = refposId;
            constNew->Second = cgeoid;
            constNew->SecondPos = Sketcher::PointPos::start;
            newconstrVals.push_back(constNew);

            constNew = new Constraint();
            constNew->Type = Sketcher::Coincident;
            constNew->First = iterfirstgeoid;
            constNew->FirstPos = refposId;
            constNew->Second = cgeoid;
            constNew->SecondPos = Sketcher::PointPos::end;
            newconstrVals.push_back(constNew);

            if (y == 0 && x == 1) {
                // first element of the first row
                colrefgeoid = cgeoid;
                cgeoid++;

                // add length and Angle
                constNew = new Constraint();
                constNew->Type = Sketcher::Distance;
                constNew->First = colrefgeoid;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->setValue(displacement.Length());
                newconstrVals.push_back(constNew);

                constNew = new Constraint();
                constNew->Type = Sketcher::Angle;
                constNew->First = colrefgeoid;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->setValue(atan2(displacement.y, displacement.x));
                newconstrVals.push_back(constNew);
            }
            else {
                // any other element
                cgeoid++;

                // all other elements get an equality and parallel constraint
                constNew = new Constraint();
                constNew->Type = Sketcher::Equal;
                constNew->First = colrefgeoid;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->Second = cgeoid - 1;
                constNew->SecondPos = Sketcher::PointPos::none;
                newconstrVals.push_back(constNew);

                constNew = new Constraint();
                constNew->Type = Sketcher::Parallel;
                constNew->First = cgeoid - 1;
                constNew->FirstPos = Sketcher::PointPos::none;
                constNew->Second = colrefgeoid;
                constNew->SecondPos = Sketcher::PointPos::none;
                newconstrVals.push_back(constNew);
            }
        }

        // after each creation reset map so that the key-value is univoque (only for
        // operations other than move)
        geoIdMap.clear();
    };

    for (y = 0; y < rsize; y++) {
        for (x = 0; x < csize; x++) {
            makeCopyAtRowColumn(x, y);
        }
    }

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker preventUpdate(internaltransaction, true);
        Geometry.setValues(std::move(newgeoVals));

        if (newconstrVals.size() > constrvals.size()) {
            Constraints.setValues(std::move(newconstrVals));
        }
    }

    // we inhibited update, so we trigger it now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return Geometry.getSize() - 1;
}
