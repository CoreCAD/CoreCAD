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

bool SketchObject::convertToNURBS(int GeoId)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId > getHighestCurveIndex()
        || (GeoId < 0 && -GeoId > static_cast<int>(ExternalGeo.getSize())) || GeoId == -1
        || GeoId == -2)
        return false;

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->is<Part::GeomPoint>())
        return false;

    const auto* geo1 = static_cast<const Part::GeomCurve*>(geo);

    Part::GeomBSplineCurve* bspline;

    try {
        bspline = geo1->toNurbs(geo1->getFirstParameter(), geo1->getLastParameter());

        if (geo1->isDerivedFrom<Part::GeomArcOfConic>()) {
            const auto* geoaoc = static_cast<const Part::GeomArcOfConic*>(geo1);

            if (geoaoc->isReversed())
                bspline->reverse();
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().error("%s\n", e.what());
        // revert to original values
        return false;
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    // Block checks and updates in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker preventUpdate(internaltransaction, true);

        if (GeoId < 0) {// external geometry
            newVals.push_back(bspline);
            generateId(bspline);
        }
        else {// normal geometry

            newVals[GeoId] = bspline;
            GeometryFacade::copyId(geo, bspline);
            // 1->1 reparametrise: the B-spline is the same sketch entity, so it keeps its durable
            // tag as well as its solver-local id.
            GeometryFacade::copyTag(geo, bspline);

            const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

            std::vector<Constraint*> newcVals(cvals);

            int index = cvals.size() - 1;
            // delete constraints on this elements other than coincident constraints (bspline does
            // not support them currently), except for coincidents on mid point of the
            // to-be-converted curve.
            for (; index >= 0; index--) {
                auto otherthancoincident = cvals[index]->Type != Sketcher::Coincident
                    && cvals[index]->involvesGeoId(GeoId);

                auto coincidentonmidpoint = cvals[index]->Type == Sketcher::Coincident
                    && cvals[index]->involvesGeoIdAndPosId(GeoId, Sketcher::PointPos::mid);

                if (otherthancoincident || coincidentonmidpoint)
                    newcVals.erase(newcVals.begin() + index);
            }

            this->Constraints.setValues(std::move(newcVals));
        }

        Geometry.setValues(std::move(newVals));
    }

    // trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return true;
}

namespace
{
/// The constraints once the B-spline at `GeoId` becomes `newCurve`. A pole or knot helper stays
/// aligned, re-indexed, where `newCurve` has the same pole or knot; `stale` gets the helpers that
/// align to one it no longer has.
std::vector<Constraint*> realignBSplineHelpers(const std::vector<Constraint*>& constraints,
                                               int GeoId,
                                               const Part::GeomBSplineCurve& oldCurve,
                                               const Part::GeomBSplineCurve& newCurve,
                                               std::vector<int>& stale)
{
    std::vector<Base::Vector3d> poles = oldCurve.getPoles();
    std::vector<Base::Vector3d> newPoles = newCurve.getPoles();

    // on fully removing a knot the knot geometry changes
    std::vector<double> knots = oldCurve.getKnots();
    std::vector<double> newKnots = newCurve.getKnots();

    std::map<Sketcher::InternalAlignmentType, std::vector<int>> indexInNew {
        {Sketcher::BSplineControlPoint, {}},
        {Sketcher::BSplineKnotPoint, {}}
    };
    indexInNew[Sketcher::BSplineControlPoint].reserve(poles.size());
    indexInNew[Sketcher::BSplineKnotPoint].reserve(knots.size());

    for (const auto& pole : poles) {
        const auto it = std::ranges::find(newPoles, pole);
        indexInNew[Sketcher::BSplineControlPoint].emplace_back(it - newPoles.begin());
    }
    std::ranges::replace(indexInNew[Sketcher::BSplineControlPoint], int(newPoles.size()), -1);

    for (const auto& knot : knots) {
        const auto it = std::ranges::find(newKnots, knot);
        indexInNew[Sketcher::BSplineKnotPoint].emplace_back(it - newKnots.begin());
    }
    std::ranges::replace(indexInNew[Sketcher::BSplineKnotPoint], int(newKnots.size()), -1);

    std::vector<Constraint*> newcVals(0);

    for (const auto& constr : constraints) {
        if (!(constr->Type == Sketcher::InternalAlignment && constr->Second == GeoId)) {
            newcVals.push_back(constr);
            continue;
        }

        int index = indexInNew.at(constr->AlignmentType).at(constr->InternalAlignmentIndex);

        if (index == -1) {
            stale.push_back(constr->First);
            continue;
        }

        Constraint* newConstr = constr->clone();
        newConstr->InternalAlignmentIndex = index;
        newcVals.push_back(newConstr);
    }
    return newcVals;
}
}  // namespace

bool SketchObject::increaseBSplineDegree(int GeoId, int degreeincrement /*= 1*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        return false;
    }

    const Part::Geometry* geo = getGeometry(GeoId);

    if (!geo->is<Part::GeomBSplineCurve>()) {
        return false;
    }

    const auto* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    const Handle(Geom_BSplineCurve) curve = Handle(Geom_BSplineCurve)::DownCast(bsp->handle());

    std::unique_ptr<Part::GeomBSplineCurve> bspline(new Part::GeomBSplineCurve(curve));

    try {
        int cdegree = bspline->getDegree();

        bspline->increaseDegree(cdegree + degreeincrement);
    }
    catch (const Base::Exception& e) {
        Base::Console().error("%s\n", e.what());
        return false;
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    GeometryFacade::copyId(geo, bspline.get());
    // 1->1 reparametrise: same sketch entity, so it keeps its durable tag too.
    GeometryFacade::copyTag(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    // AcceptGeometry called from onChanged
    Geometry.setValues(std::move(newVals));

    return true;
}

bool SketchObject::decreaseBSplineDegree(int GeoId, int degreedecrement /*= 1*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return false;

    const Part::Geometry* geo = getGeometry(GeoId);

    if (!geo->is<Part::GeomBSplineCurve>())
        return false;

    const auto* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    const Handle(Geom_BSplineCurve) curve = Handle(Geom_BSplineCurve)::DownCast(bsp->handle());

    std::unique_ptr<Part::GeomBSplineCurve> bspline(new Part::GeomBSplineCurve(curve));

    try {
        int cdegree = bspline->getDegree();

        // degree must be >= 1
        int maxdegree = cdegree - degreedecrement;
        if (maxdegree == 0)
            return false;
        bspline->approximate(Precision::Confusion(), 20, maxdegree, GeomAbs_C0);
    }
    catch (const Base::Exception& e) {
        Base::Console().error("%s\n", e.what());
        return false;
    }

    // The re-fit is a new curve with its own poles, knots and weights, so every helper is replaced:
    // one kept by a chance match would carry an old weight onto it. The curve itself is edited in
    // place and keeps its ids and the constraints on it.
    std::vector<int> delGeoId;
    std::vector<Constraint*> newcVals;
    for (Constraint* constr : Constraints.getValues()) {
        if (constr->Type == Sketcher::InternalAlignment && constr->Second == GeoId) {
            delGeoId.push_back(constr->First);
        }
        else {
            newcVals.push_back(constr);
        }
    }

    std::vector<Part::Geometry*> newVals(getInternalGeometry());

    GeometryFacade::copyId(geo, bspline.get());
    // 1->1 reparametrise: same sketch entity, so it keeps its durable tag too.
    GeometryFacade::copyTag(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    {
        Base::StateLocker preventUpdate(internaltransaction, true);
        Geometry.setValues(std::move(newVals));
        Constraints.setValues(std::move(newcVals));
    }

    if (!delGeoId.empty()) {
        delGeometriesExclusiveList(delGeoId);
    }
    else {
        Geometry.touch();
    }
    const auto removedBefore = std::ranges::count_if(delGeoId, [&](int id) { return id < GeoId; });
    exposeInternalGeometry(GeoId - static_cast<int>(removedBefore));

    return true;
}

// clang-format on
bool SketchObject::modifyBSplineKnotMultiplicity(int GeoId, int knotIndex, int multiplicityincr)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "B-spline Geometry Index (GeoID) is out of bounds.")
        );
    }

    if (multiplicityincr == 0) {
        // no change in multiplicity
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "You are requesting no change in knot multiplicity.")
        );
    }

    const Part::Geometry* geo = getGeometry(GeoId);

    if (!geo->is<Part::GeomBSplineCurve>()) {
        THROWMT(
            Base::TypeError,
            QT_TRANSLATE_NOOP("Exceptions", "The Geometry Index (GeoId) provided is not a B-spline.")
        );
    }

    const auto* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    int degree = bsp->getDegree();

    if (knotIndex > bsp->countKnots() || knotIndex < 1) {
        // knotindex in OCC 1 -> countKnots
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP(
                "Exceptions",
                "The knot index is out of bounds. Note that in accordance with "
                "OCC notation, the first knot has index 1 and not zero."
            )
        );
    }

    std::unique_ptr<Part::GeomBSplineCurve> bspline;

    int curmult = bsp->getMultiplicity(knotIndex);

    // zero is removing the knot, degree is just positional continuity
    if ((curmult + multiplicityincr) > degree) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP(
                "Exceptions",
                "The multiplicity cannot be increased beyond the degree of the B-spline."
            )
        );
    }

    // zero is removing the knot, degree is just positional continuity
    if ((curmult + multiplicityincr) < 0) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "The multiplicity cannot be decreased beyond zero.")
        );
    }

    try {
        bspline.reset(static_cast<Part::GeomBSplineCurve*>(bsp->clone()));

        if (multiplicityincr > 0) {  // increase multiplicity
            bspline->increaseMultiplicity(knotIndex, curmult + multiplicityincr);
        }
        else {  // decrease multiplicity
            bool result = bspline->removeKnot(knotIndex, curmult + multiplicityincr, 1E6);

            if (!result) {
                THROWMT(
                    Base::CADKernelError,
                    QT_TRANSLATE_NOOP(
                        "Exceptions",
                        "OCC is unable to decrease the multiplicity within the "
                        "maximum tolerance."
                    )
                );
            }
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().error("%s\n", e.what());
        return false;
    }

    // we succeeded with the multiplicity modification, so alignment geometry may be
    // invalid/inconsistent for the new bspline
    std::vector<int> delGeoId;
    std::vector<Constraint*> newcVals
        = realignBSplineHelpers(Constraints.getValues(), GeoId, *bsp, *bspline, delGeoId);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    GeometryFacade::copyId(geo, bspline.get());
    // 1->1 reparametrise: same sketch entity, so it keeps its durable tag too.
    GeometryFacade::copyTag(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker preventUpdate(internaltransaction, true);
        Geometry.setValues(std::move(newVals));

        this->Constraints.setValues(std::move(newcVals));
    }

    // Trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    if (!delGeoId.empty()) {
        delGeometriesExclusiveList(delGeoId);
    }
    else {
        Geometry.touch();
    }
    return true;
}

bool SketchObject::insertBSplineKnot(int GeoId, double param, int multiplicity)
{
    // TODO: Check if this is still valid: no need to check input data validity as this is an
    // sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // handling unacceptable cases
    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "B-spline Geometry Index (GeoID) is out of bounds.")
        );
    }

    if (multiplicity == 0) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "Knot cannot have zero multiplicity.")
        );
    }

    const Part::Geometry* geo = getGeometry(GeoId);

    if (!geo->is<Part::GeomBSplineCurve>()) {
        THROWMT(
            Base::TypeError,
            QT_TRANSLATE_NOOP("Exceptions", "The Geometry Index (GeoId) provided is not a B-spline.")
        );
    }

    const auto* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    int degree = bsp->getDegree();
    double firstParam = bsp->getFirstParameter();
    double lastParam = bsp->getLastParameter();

    if (multiplicity > degree) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP(
                "Exceptions",
                "Knot multiplicity cannot be higher than the degree of the B-spline."
            )
        );
    }

    if (param > lastParam || param < firstParam) {
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "Knot cannot be inserted outside the B-spline parameter range.")
        );
    }

    std::unique_ptr<Part::GeomBSplineCurve> bspline;

    // run the command
    try {
        bspline.reset(static_cast<Part::GeomBSplineCurve*>(bsp->clone()));

        bspline->insertKnot(param, multiplicity);
    }
    catch (const Base::Exception& e) {
        Base::Console().error("%s\n", e.what());
        return false;
    }

    // once command is run update the internal geometries
    std::vector<int> delGeoId;

    std::vector<Base::Vector3d> poles = bsp->getPoles();
    std::vector<Base::Vector3d> newPoles = bspline->getPoles();
    std::vector<int> poleIndexInNew(poles.size(), -1);

    for (size_t j = 0; j < poles.size(); j++) {
        const auto it = std::ranges::find(newPoles, poles[j]);
        poleIndexInNew[j] = it - newPoles.begin();
    }
    std::ranges::replace(poleIndexInNew, int(newPoles.size()), -1);

    std::vector<double> knots = bsp->getKnots();
    std::vector<double> newKnots = bspline->getKnots();
    std::vector<int> knotIndexInNew(knots.size(), -1);

    for (size_t j = 0; j < knots.size(); j++) {
        const auto it = std::ranges::find(newKnots, knots[j]);
        knotIndexInNew[j] = it - newKnots.begin();
    }
    std::ranges::replace(knotIndexInNew, int(newKnots.size()), -1);

    const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

    std::vector<Constraint*> newcVals(0);

    // modify pole and knot constraints
    for (const auto& constr : cvals) {
        if (!(constr->Type == Sketcher::InternalAlignment && constr->Second == GeoId)) {
            newcVals.push_back(constr);
            continue;
        }

        std::vector<int>* indexInNew = nullptr;

        if (constr->AlignmentType == Sketcher::BSplineControlPoint) {
            indexInNew = &poleIndexInNew;
        }
        else if (constr->AlignmentType == Sketcher::BSplineKnotPoint) {
            indexInNew = &knotIndexInNew;
        }
        else {
            // it is a bspline geometry, but not a controlpoint or knot
            newcVals.push_back(constr);
            continue;
        }

        if (indexInNew && indexInNew->at(constr->InternalAlignmentIndex) == -1) {
            // it is an internal alignment geometry that is no longer valid
            // => delete it and the pole circle
            delGeoId.push_back(constr->First);
            continue;
        }

        Constraint* newConstr = constr->clone();
        newConstr->InternalAlignmentIndex = indexInNew->at(constr->InternalAlignmentIndex);
        newcVals.push_back(newConstr);
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    GeometryFacade::copyId(geo, bspline.get());
    // 1->1 reparametrise: same sketch entity, so it keeps its durable tag too.
    GeometryFacade::copyTag(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker preventUpdate(internaltransaction, true);
        Geometry.setValues(std::move(newVals));

        this->Constraints.setValues(std::move(newcVals));
    }

    // Trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    if (!delGeoId.empty()) {
        // NOTE: There have been a couple of instances when knot insertion has
        // led to a segmentation fault: see
        // https://forum.freecad.org/viewtopic.php?f=19&t=64962&sid=10272db50a635c633260517b14ecad37.
        // If a segfault happens again and a `Geometry.touch()` here fixes it,
        // it is possible that `delGeometriesExclusiveList` is causing an update
        // in constraint GUI features during an intermediate step.
        // See 247a9f0876a00e08c25b07d1f8802479d8623e87 for suggestions.
        // Geometry.touch();
        delGeometriesExclusiveList(delGeoId);
        return true;
    }

    Geometry.touch();

    return true;
}
// clang-format on
