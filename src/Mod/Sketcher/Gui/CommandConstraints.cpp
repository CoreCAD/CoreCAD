// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <limits>
#include <cmath>

#include <Precision.hxx>
#include <Bnd_Box.hxx>
#include <QPainter>
#include <algorithm>
#include <sstream>

#include <BRepBndLib.hxx>

#include <boost/range/adaptor/reversed.hpp>

#include <App/Application.h>
#include <Base/Tools.h>
#include <Base/Tools2D.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/Dialogs/DlgCheckableMessageBox.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Notifications.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>
#include <Gui/Selection/SelectionObject.h>
#include <Mod/Sketcher/App/GeometryFacade.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/Sketcher/App/SolverGeometryExtension.h>

#include "CommandConstraints.h"
#include "DrawSketchHandler.h"
#include "EditDatumDialog.h"
#include "Utils.h"
#include "ViewProviderSketch.h"
#include "ui_InsertDatum.h"
#include <Inventor/events/SoKeyboardEvent.h>
#include "SnapManager.h"
#include "CommandConstraintsCommon.h"

// Remove this after pre-commit hook is activated
// clang-format off
using namespace std;
using namespace SketcherGui;
using namespace Sketcher;

ConstraintCreationMode constraintCreationMode = Driving;

namespace SketcherGui::LinearDatumLabelPlacement
{
[[nodiscard]] bool computeLabelPosition(const Sketcher::SketchObject* sketch,
                                        const Sketcher::Constraint* constraint,
                                        Base::Vector2d& position)
{
    if (constraint->Type != Sketcher::DistanceX && constraint->Type != Sketcher::DistanceY
        && constraint->Type != Sketcher::Distance) {
        return false;
    }

    const auto projectPoint = [](const Base::Vector3d& point) {
        return Base::Vector2d(point.x, point.y);
    };

    Base::Vector2d firstPoint;
    Base::Vector2d secondPoint;

    if (constraint->SecondPos != Sketcher::PointPos::none) {
        firstPoint = projectPoint(sketch->getPoint(constraint->First, constraint->FirstPos));
        secondPoint = projectPoint(sketch->getPoint(constraint->Second, constraint->SecondPos));
    }
    else if (constraint->FirstPos != Sketcher::PointPos::none
             && constraint->Second == Sketcher::GeoEnum::GeoUndef) {
        firstPoint = Base::Vector2d(0.0, 0.0);
        secondPoint = projectPoint(sketch->getPoint(constraint->First, constraint->FirstPos));
    }
    else if (constraint->Type == Sketcher::Distance
             && constraint->Second == Sketcher::GeoEnum::GeoUndef
             && constraint->First >= 0
             && constraint->FirstPos == Sketcher::PointPos::none) {
        const Part::Geometry* geo = sketch->getGeometry(constraint->First);

        if (!geo || !isLineSegment(*geo)) {
            return false;
        }

        const auto* lineSegment = static_cast<const Part::GeomLineSegment*>(geo);
        firstPoint = projectPoint(lineSegment->getStartPoint());
        secondPoint = projectPoint(lineSegment->getEndPoint());
    }
    else {
        return false;
    }

    const double eps = Precision::Confusion();
    Base::Vector2d labelDirection(0.0, 0.0);

    switch (constraint->Type) {
        case Sketcher::DistanceX:
            if (secondPoint.x < firstPoint.x - eps) {
                std::swap(firstPoint, secondPoint);
            }
            labelDirection = Base::Vector2d(0.0, -1.0);
            break;
        case Sketcher::DistanceY:
            if (secondPoint.y < firstPoint.y - eps) {
                std::swap(firstPoint, secondPoint);
            }
            if (secondPoint.x > firstPoint.x + eps) {
                labelDirection = Base::Vector2d(1.0, 0.0);
            }
            else if (secondPoint.x < firstPoint.x - eps) {
                labelDirection = Base::Vector2d(-1.0, 0.0);
            }
            else {
                labelDirection = Base::Vector2d(1.0, 0.0);
            }
            break;
        case Sketcher::Distance:
            if (secondPoint.y < firstPoint.y - eps
                || (std::abs(secondPoint.y - firstPoint.y) <= eps
                    && secondPoint.x < firstPoint.x - eps)) {
                std::swap(firstPoint, secondPoint);
            }
            {
                const Base::Vector2d span = secondPoint - firstPoint;
                const double spanLength = span.Length();
                if (spanLength <= eps) {
                    return false;
                }
                labelDirection = span.x >= 0.0
                    ? Base::Vector2d(-span.y / spanLength, span.x / spanLength)
                    : Base::Vector2d(span.y / spanLength, -span.x / spanLength);
            }
            break;
        default:
            return false;
    }

    const Base::Vector2d span = secondPoint - firstPoint;
    position = (firstPoint + secondPoint) * 0.5
        + labelDirection * (constraint->LabelDistance
                            + 0.5 * std::abs(span.x * labelDirection.x
                                             + span.y * labelDirection.y));
    return true;
}
}  // namespace SketcherGui::LinearDatumLabelPlacement

namespace SketcherGui
{
class ViewProviderSketchCommandConstraintsAttorney
{
public:
    static void moveConstraint(ViewProviderSketch& viewProvider,
                               int constraintIndex,
                               const Base::Vector2d& position)
    {
        viewProvider.moveConstraint(constraintIndex, position);
    }
};
}  // namespace SketcherGui

bool isCreateConstraintActive(Gui::Document* doc)
{
    if (doc) {
        // checks if a Sketch View provider is in Edit and is in no special mode
        auto vp = dynamic_cast<SketcherGui::ViewProviderSketch*>(doc->getInEdit());
        if (vp && vp->isInEditMode()) {
            if (vp->getSketchMode() == ViewProviderSketch::STATUS_NONE) {
                if (Gui::Selection().countObjectsOfType<Sketcher::SketchObject>() > 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Utility method to avoid repeating the same code over and over again
void finishDatumConstraint(Gui::Command* cmd,
                           Sketcher::SketchObject* sketch,
                           bool isDriving,
                           unsigned int numberofconstraints)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher");

    // Get the latest constraint
    const std::vector<Sketcher::Constraint*>& ConStr = sketch->Constraints.getValues();
    auto lastConstraintIndex = ConStr.size() - 1;
    Sketcher::Constraint* constr = ConStr[lastConstraintIndex];
    auto lastConstraintType = constr->Type;

    // Guess some reasonable distance for placing the datum text
    Gui::Document* doc = cmd->getActiveGuiDocument();
    double labelPosition = 0.0;
    float labelPositionRandomness = 0.0;

    if (lastConstraintType == Radius || lastConstraintType == Diameter) {
        // Get radius/diameter constraint display angle
        labelPosition = Base::toRadians(hGrp->GetFloat("RadiusDiameterConstraintDisplayBaseAngle", 15.0));
        // Get randomness
        labelPositionRandomness =
            Base::toRadians(hGrp->GetFloat("RadiusDiameterConstraintDisplayAngleRandomness", 0.0));

        // Adds a random value around the base angle, so that possibly overlapping labels get likely
        // a different position.
        if (labelPositionRandomness != 0.0) {
            labelPosition = labelPosition
                + labelPositionRandomness
                    * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5);
        }
    }

    if (doc && doc->getInEdit()
        && doc->getInEdit()->isDerivedFrom<SketcherGui::ViewProviderSketch>()) {
        SketcherGui::ViewProviderSketch* vp =
            static_cast<SketcherGui::ViewProviderSketch*>(doc->getInEdit());

        int firstConstraintIndex = lastConstraintIndex - numberofconstraints + 1;

        for (int i = lastConstraintIndex; i >= firstConstraintIndex; i--) {
            if (lastConstraintType == Radius || lastConstraintType == Diameter) {
                const Part::Geometry* geo = sketch->getGeometry(ConStr[i]->First);

                if (geo && isCircle(*geo)) {
                    ConStr[i]->LabelPosition = labelPosition;
                }
            }
            else {
                Base::Vector2d labelPosition;

                if (LinearDatumLabelPlacement::computeLabelPosition(
                        sketch, ConStr[i], labelPosition)) {
                    ViewProviderSketchCommandConstraintsAttorney::moveConstraint(
                        *vp, i, labelPosition);
                }
            }
        }
        vp->draw(false, false);// Redraw
    }

    bool show = hGrp->GetBool("ShowDialogOnDistanceConstraint", true);

    // Ask for the value of the distance immediately
    if (show && isDriving) {
        EditDatumDialog editDatumDialog(cmd->transactionID(), sketch, ConStr.size() - 1);
        cmd->resetTransactionID();
        editDatumDialog.exec();
    }
    else {
        // no dialog was shown so commit the command
        cmd->commitCommand();
    }

    tryAutoRecompute(sketch);
    cmd->getSelection().clearSelection();
}

void showNoConstraintBetweenExternal(const App::DocumentObject* obj)
{
    Gui::TranslatedUserWarning(
        obj,
        QObject::tr("Wrong selection"),
        QObject::tr("Cannot add a constraint between two external geometries."));
}

void showNoConstraintBetweenFixedGeometry(const App::DocumentObject* obj)
{
    Gui::TranslatedUserWarning(obj,
                               QObject::tr("Wrong selection"),
                               QObject::tr("Cannot add a constraint between two fixed geometries. "
                                           "Fixed geometries include external geometry, "
                                           "blocked geometry, and special points "
                                           "such as B-spline knot points."));
}

bool isGeoConcentricCompatible(const Part::Geometry* geo)
{
    return (isEllipse(*geo) || isArcOfEllipse(*geo) || isCircle(*geo) || isArcOfCircle(*geo));
}

// Removes point-on-object constraints made redundant with certain constraints
// under certain conditions. Currently, that happens only when the constraint is on
// a B-spline, for 3-selection tangent, perpendicular, and angle constraints.
// Returns true if constraints were removed.
// GeoId3 HAS to be the point, and the other two are the curves.
bool removeRedundantPointOnObject(SketchObject* Obj, int GeoId1, int GeoId2, int GeoId3)
{
    const std::vector<Constraint*>& cvals = Obj->Constraints.getValues();

    std::vector<int> cidsToBeRemoved;

    int cid = 0;
    for (auto it = cvals.begin(); it != cvals.end(); ++it, ++cid) {
        if ((*it)->Type == Sketcher::PointOnObject &&
            (((*it)->First == GeoId3 && (*it)->Second == GeoId1) ||
             ((*it)->First == GeoId3 && (*it)->Second == GeoId2))) {

            // ONLY do this if it is a B-spline (or any other where point
            // on object is implied).
            const Part::Geometry* geom = Obj->getGeometry((*it)->Second);
            if (isBSplineCurve(*geom))
                cidsToBeRemoved.push_back(cid);
        }
    }

    if (!cidsToBeRemoved.empty()) {
        for (auto it = cidsToBeRemoved.rbegin(); it != cidsToBeRemoved.rend(); ++it) {
            Gui::cmdAppObjectArgs(Obj,
                                  "delConstraint(%d)",
                                  *it);// remove the preexisting point on object constraint.
        }

        // A substitution requires a solve() so that the autoremove redundants works when
        // Autorecompute not active. However, delConstraint includes such solve() internally. So
        // at this point it is already solved.
        tryAutoRecomputeIfNotSolve(Obj);

        notifyConstraintSubstitutions(QObject::tr("One or two point-on-object constraints were deleted, "
                                                  "since the latest constraint being applied internally applies point-on-object as well."));

        // TODO: find way to get selection here, or clear elsewhere
        // getSelection().clearSelection();
        return true;
    }

    return false;
}

/// Makes an angle constraint between 2 lines
void SketcherGui::makeAngleBetweenTwoLines(Sketcher::SketchObject* Obj,
    Gui::Command* cmd,
    int geoId1,
    int geoId2)
{
    Sketcher::PointPos posId1 = Sketcher::PointPos::none;
    Sketcher::PointPos posId2 = Sketcher::PointPos::none;
    double actAngle;

    if (!calculateAngle(Obj, geoId1, geoId2, posId1, posId2, actAngle)) {
        return;
    }

    if (actAngle == 0.0) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Parallel lines"),
            QObject::tr("An angle constraint cannot be set for two parallel lines."));

        return;
    }

    cmd->openCommand(QT_TRANSLATE_NOOP("Command", "Add angle constraint"));
    Gui::cmdAppObjectArgs(Obj,
        "addConstraint(Sketcher.Constraint('Angle',%d,%d,%d,%d,%f))",
        geoId1,
        static_cast<int>(posId1),
        geoId2,
        static_cast<int>(posId2),
        actAngle);

    if (areBothPointsOrSegmentsFixed(Obj, geoId1, geoId2)
        || constraintCreationMode == Reference) {
        // it is a constraint on a external line, make it non-driving
        const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

        Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
        finishDatumConstraint(cmd, Obj, false);
    }
    else {
        finishDatumConstraint(cmd, Obj, true);
    }
}

bool SketcherGui::calculateAngle(Sketcher::SketchObject* Obj, int& GeoId1, int& GeoId2, Sketcher::PointPos& PosId1, Sketcher::PointPos& PosId2, double& ActAngle)
{
    const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
    const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

    if (!(geom1->is<Part::GeomLineSegment>()) ||
        !(geom2->is<Part::GeomLineSegment>())) {
        return false;
    }

    const Part::GeomLineSegment* lineSeg1 = static_cast<const Part::GeomLineSegment*>(geom1);
    const Part::GeomLineSegment* lineSeg2 = static_cast<const Part::GeomLineSegment*>(geom2);

    // find the two closest line ends
    Base::Vector3d p1[2], p2[2];
    p1[0] = lineSeg1->getStartPoint();
    p1[1] = lineSeg1->getEndPoint();
    p2[0] = lineSeg2->getStartPoint();
    p2[1] = lineSeg2->getEndPoint();

    // Get the intersection point in 2d of the two lines if possible
    Base::Line2d line1(Base::Vector2d(p1[0].x, p1[0].y), Base::Vector2d(p1[1].x, p1[1].y));
    Base::Line2d line2(Base::Vector2d(p2[0].x, p2[0].y), Base::Vector2d(p2[1].x, p2[1].y));
    Base::Vector2d s;
    if (line1.Intersect(line2, s)) {
        // get the end points of the line segments that are closest to the intersection point
        Base::Vector3d s3d(s.x, s.y, p1[0].z);
        if (Base::DistanceP2(s3d, p1[0]) < Base::DistanceP2(s3d, p1[1]))
            PosId1 = Sketcher::PointPos::start;
        else
            PosId1 = Sketcher::PointPos::end;
        if (Base::DistanceP2(s3d, p2[0]) < Base::DistanceP2(s3d, p2[1]))
            PosId2 = Sketcher::PointPos::start;
        else
            PosId2 = Sketcher::PointPos::end;
    }
    else {
        // if all points are collinear
        double length = std::numeric_limits<double>::max();
        for (int i = 0; i <= 1; i++) {
            for (int j = 0; j <= 1; j++) {
                double tmp = Base::DistanceP2(p2[j], p1[i]);
                if (tmp < length) {
                    length = tmp;
                    PosId1 = i ? Sketcher::PointPos::end : Sketcher::PointPos::start;
                    PosId2 = j ? Sketcher::PointPos::end : Sketcher::PointPos::start;
                }
            }
        }
    }

    Base::Vector3d dir1 = ((PosId1 == Sketcher::PointPos::start) ? 1. : -1.) *
        (lineSeg1->getEndPoint() - lineSeg1->getStartPoint());
    Base::Vector3d dir2 = ((PosId2 == Sketcher::PointPos::start) ? 1. : -1.) *
        (lineSeg2->getEndPoint() - lineSeg2->getStartPoint());

    // check if the two lines are parallel
    Base::Vector3d dir3 = dir1 % dir2;
    if (dir3.Length() < Precision::Intersection()) {
        Base::Vector3d dist = (p1[0] - p2[0]) % dir1;
        if (dist.Sqr() > Precision::Intersection()) {
            ActAngle = 0.0;
            return true;
        }
    }

    ActAngle = atan2(dir1.x * dir2.y - dir1.y * dir2.x,
        dir1.y * dir2.y + dir1.x * dir2.x);

    if (ActAngle < 0) {
        ActAngle *= -1;
        std::swap(GeoId1, GeoId2);
        std::swap(PosId1, PosId2);
    }

    return true;
}

/// Makes a simple tangency constraint using extra point + tangent via point
/// ellipse => an ellipse
/// geom2 => any of an ellipse, an arc of ellipse, a circle, or an arc (of circle)
/// geoId1 => geoid of the ellipse
/// geoId2 => geoid of geom2
/// Returns true on success and false on failure
/// the call site is responsible to create, commit and abort commands
/// and recompute if it needs to
bool SketcherGui::makeTangentToEllipseviaNewPoint(Sketcher::SketchObject* Obj,
                                                  const Part::GeomEllipse* ellipse,
                                                  const Part::Geometry* geom2,
                                                  int geoId1,
                                                  int geoId2)
{

    Base::Vector3d center = ellipse->getCenter();
    double majord = ellipse->getMajorRadius();
    double minord = ellipse->getMinorRadius();
    double phi = atan2(ellipse->getMajorAxisDir().y, ellipse->getMajorAxisDir().x);

    Base::Vector3d center2;

    if (isEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomEllipse*>(geom2))->getCenter();
    }
    else if (isArcOfEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfEllipse*>(geom2))->getCenter();
    }
    else if (isCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomCircle*>(geom2))->getCenter();
    }
    else if (isArcOfCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfCircle*>(geom2))->getCenter();
    }

    Base::Vector3d direction = center2 - center;
    double tapprox =
        atan2(direction.y, direction.x) - phi;// we approximate the eccentric anomaly by the polar

    Base::Vector3d PoE = Base::Vector3d(
        center.x + majord * cos(tapprox) * cos(phi) - minord * sin(tapprox) * sin(phi),
        center.y + majord * cos(tapprox) * sin(phi) + minord * sin(tapprox) * cos(phi),
        0);

    try {
        // Add a point
        Gui::cmdAppObjectArgs(Obj, "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)", PoE.x, PoE.y);
        int GeoIdPoint = Obj->getHighestCurveIndex();

        // Point on first object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId1);// constrain major axis
        // Point on second object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId2);// constrain major axis
        // tangent via point
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                              geoId1,
                              geoId2,
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start));
    }
    catch (const Base::Exception& e) {
        Gui::NotifyUserError(Obj,
                             QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                             e.what());

        return false;
    }

    return true;
}

/// Makes a simple tangency constraint using extra point + tangent via point
/// aoe => an arc of ellipse
/// geom2 => any of an arc of ellipse, a circle, or an arc (of circle)
/// geoId1 => geoid of the arc of ellipse
/// geoId2 => geoid of geom2
/// Returns true on success and false on failure
/// the call site is responsible to create, commit and abort commands
/// and recompute if it needs to
bool SketcherGui::makeTangentToArcOfEllipseviaNewPoint(Sketcher::SketchObject* Obj,
                                                       const Part::GeomArcOfEllipse* aoe,
                                                       const Part::Geometry* geom2,
                                                       int geoId1,
                                                       int geoId2)
{

    Base::Vector3d center = aoe->getCenter();
    double majord = aoe->getMajorRadius();
    double minord = aoe->getMinorRadius();
    double phi = atan2(aoe->getMajorAxisDir().y, aoe->getMajorAxisDir().x);

    Base::Vector3d center2;

    if (isArcOfEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfEllipse*>(geom2))->getCenter();
    }
    else if (isCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomCircle*>(geom2))->getCenter();
    }
    else if (isArcOfCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfCircle*>(geom2))->getCenter();
    }

    Base::Vector3d direction = center2 - center;
    double tapprox =
        atan2(direction.y, direction.x) - phi;// we approximate the eccentric anomaly by the polar

    Base::Vector3d PoE = Base::Vector3d(
        center.x + majord * cos(tapprox) * cos(phi) - minord * sin(tapprox) * sin(phi),
        center.y + majord * cos(tapprox) * sin(phi) + minord * sin(tapprox) * cos(phi),
        0);

    try {
        // Add a point
        Gui::cmdAppObjectArgs(Obj, "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)", PoE.x, PoE.y);
        int GeoIdPoint = Obj->getHighestCurveIndex();

        // Point on first object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId1);// constrain major axis
        // Point on second object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId2);// constrain major axis
        // tangent via point
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                              geoId1,
                              geoId2,
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start));
    }
    catch (const Base::Exception& e) {
        Gui::NotifyUserError(Obj,
                             QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                             e.what());
        return false;
    }
    return true;
}

/// Makes a simple tangency constraint using extra point + tangent via point
/// aoh => an arc of hyperbola
/// geom2 => any of an arc of hyperbola, an arc of ellipse, a circle, or an arc (of circle)
/// geoId1 => geoid of the arc of hyperbola
/// geoId2 => geoid of geom2
/// Returns true on success and false on failure
/// the call site is responsible to create, commit and abort commands
/// and recompute if it needs to
bool SketcherGui::makeTangentToArcOfHyperbolaviaNewPoint(Sketcher::SketchObject* Obj,
                                                         const Part::GeomArcOfHyperbola* aoh,
                                                         const Part::Geometry* geom2,
                                                         int geoId1,
                                                         int geoId2)
{

    Base::Vector3d center = aoh->getCenter();
    double majord = aoh->getMajorRadius();
    double minord = aoh->getMinorRadius();
    Base::Vector3d dirmaj = aoh->getMajorAxisDir();
    double phi = atan2(dirmaj.y, dirmaj.x);
    double df = sqrt(majord * majord + minord * minord);
    Base::Vector3d focus = center + df * dirmaj;// positive focus

    Base::Vector3d center2;

    if (isArcOfHyperbola(*geom2)) {
        auto aoh2 = static_cast<const Part::GeomArcOfHyperbola*>(geom2);
        Base::Vector3d dirmaj2 = aoh2->getMajorAxisDir();
        double majord2 = aoh2->getMajorRadius();
        double minord2 = aoh2->getMinorRadius();
        double df2 = sqrt(majord2 * majord2 + minord2 * minord2);
        center2 = aoh2->getCenter() + df2 * dirmaj2;// positive focus
    }
    else if (isArcOfEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfEllipse*>(geom2))->getCenter();
    }
    else if (isEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomEllipse*>(geom2))->getCenter();
    }
    else if (isCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomCircle*>(geom2))->getCenter();
    }
    else if (isArcOfCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfCircle*>(geom2))->getCenter();
    }
    else if (isLineSegment(*geom2)) {
        auto l2 = static_cast<const Part::GeomLineSegment*>(geom2);
        center2 = (l2->getStartPoint() + l2->getEndPoint()) / 2;
    }

    Base::Vector3d direction = center2 - focus;
    double tapprox = atan2(direction.y, direction.x) - phi;

    Base::Vector3d PoH = Base::Vector3d(
        center.x + majord * cosh(tapprox) * cos(phi) - minord * sinh(tapprox) * sin(phi),
        center.y + majord * cosh(tapprox) * sin(phi) + minord * sinh(tapprox) * cos(phi),
        0);

    try {
        // Add a point
        Gui::cmdAppObjectArgs(Obj, "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)", PoH.x, PoH.y);
        int GeoIdPoint = Obj->getHighestCurveIndex();

        // Point on first object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId1);// constrain major axis
        // Point on second object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId2);// constrain major axis
        // tangent via point
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                              geoId1,
                              geoId2,
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start));
    }
    catch (const Base::Exception& e) {
        Gui::NotifyUserError(Obj,
                             QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                             e.what());
        return false;
    }
    return true;
}

/// Makes a simple tangency constraint using extra point + tangent via point
/// aop => an arc of parabola
/// geom2 => any of an arc of parabola, an arc of hyperbola an arc of ellipse, a circle, or an arc
/// (of circle) geoId1 => geoid of the arc of parabola geoId2 => geoid of geom2
/// Returns true on success and false on failure
/// the call site is responsible to create, commit and abort commands
/// and recompute if it needs to
bool SketcherGui::makeTangentToArcOfParabolaviaNewPoint(Sketcher::SketchObject* Obj,
                                                        const Part::GeomArcOfParabola* aop,
                                                        const Part::Geometry* geom2,
                                                        int geoId1,
                                                        int geoId2)
{

    Base::Vector3d focus = aop->getFocus();

    Base::Vector3d center2;

    if (isArcOfParabola(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfParabola*>(geom2))->getFocus();
    }
    else if (isArcOfHyperbola(*geom2)) {
        auto aoh2 = static_cast<const Part::GeomArcOfHyperbola*>(geom2);
        Base::Vector3d dirmaj2 = aoh2->getMajorAxisDir();
        double majord2 = aoh2->getMajorRadius();
        double minord2 = aoh2->getMinorRadius();
        double df2 = sqrt(majord2 * majord2 + minord2 * minord2);
        center2 = aoh2->getCenter() + df2 * dirmaj2;// positive focus
    }
    else if (isArcOfEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfEllipse*>(geom2))->getCenter();
    }
    else if (isEllipse(*geom2)) {
        center2 = (static_cast<const Part::GeomEllipse*>(geom2))->getCenter();
    }
    else if (isCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomCircle*>(geom2))->getCenter();
    }
    else if (isArcOfCircle(*geom2)) {
        center2 = (static_cast<const Part::GeomArcOfCircle*>(geom2))->getCenter();
    }
    else if (isLineSegment(*geom2)) {
        auto l2 = static_cast<const Part::GeomLineSegment*>(geom2);
        center2 = (l2->getStartPoint() + l2->getEndPoint()) / 2;
    }

    Base::Vector3d direction = center2 - focus;

    Base::Vector3d PoP = focus + direction / 2;

    try {
        // Add a point
        Gui::cmdAppObjectArgs(Obj, "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)", PoP.x, PoP.y);
        int GeoIdPoint = Obj->getHighestCurveIndex();

        // Point on first object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId1);// constrain major axis
        // Point on second object
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start),
                              geoId2);// constrain major axis
        // tangent via point
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                              geoId1,
                              geoId2,
                              GeoIdPoint,
                              static_cast<int>(Sketcher::PointPos::start));
    }
    catch (const Base::Exception& e) {
        Gui::NotifyUserError(Obj,
                             QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                             e.what());

        return false;
    }

    return true;
}

void SketcherGui::doEndpointTangency(Sketcher::SketchObject* Obj,
                                     int GeoId1,
                                     int GeoId2,
                                     PointPos PosId1,
                                     PointPos PosId2)
{
    // This code supports simple B-spline endpoint tangency to any other geometric curve
    const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
    const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

    if (geom1 && geom2 && (isBSplineCurve(*geom1) || isBSplineCurve(*geom2))) {
        if (! isBSplineCurve(*geom1)) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
        }
        // GeoId1 is the B-spline now
    }// end of code supports simple B-spline endpoint tangency

    Gui::cmdAppObjectArgs(Obj,
                          "addConstraint(Sketcher.Constraint('Tangent',%d,%d,%d,%d))",
                          GeoId1,
                          static_cast<int>(PosId1),
                          GeoId2,
                          static_cast<int>(PosId2));
}

void SketcherGui::doEndpointToEdgeTangency(Sketcher::SketchObject* Obj,
                                           int GeoId1,
                                           PointPos PosId1,
                                           int GeoId2)
{
    Gui::cmdAppObjectArgs(Obj,
                          "addConstraint(Sketcher.Constraint('Tangent',%d,%d,%d))",
                          GeoId1,
                          static_cast<int>(PosId1),
                          GeoId2);
}

void SketcherGui::notifyConstraintSubstitutions(const QString& message)
{
    Gui::Dialog::DlgCheckableMessageBox::showMessage(
        QObject::tr("Sketcher Constraint Substitution"),
        message,
        QLatin1String("User parameter:BaseApp/Preferences/Mod/Sketcher/General"),
        QLatin1String("NotifyConstraintSubstitutions"),
        true,// Default ParamEntry
        true,// checkbox state
        QObject::tr("Keep notifying about constraint substitutions"));
}

// returns true if successful, false otherwise
bool addConstraintSafely(SketchObject* obj, std::function<void()> constraintadditionfunction)
{
    try {
        constraintadditionfunction();
    }
    catch (const Base::IndexError& e) {
        // Exceptions originating in Python have already been reported by the Interpreter as
        // Untranslated developer intended messages (i.e. to the Report View)
        // Example of exception carrying a static string with a pair in the "Notifications" context
        Gui::NotifyUserError(obj,
                             QT_TRANSLATE_NOOP("Notifications", "Invalid constraint"),
                             e.what());

        tryAutoRecompute(obj);
        return false;
    }
    catch (const Base::Exception&) {
        Gui::TranslatedUserError(
            obj,
            QObject::tr("Error"),
            QObject::tr("Unexpected error. More information may be available in the report view."));

        tryAutoRecompute(obj);
        return false;
    }

    return true;
}

namespace SketcherGui
{

struct SketchSelection
{
    enum GeoType
    {
        Point,
        Line,
        Circle,
        Arc
    };
    int setUp();
    struct SketchSelectionItem
    {
        GeoType type;
        int GeoId;
        bool Extern;
    };
    std::list<SketchSelectionItem> Items;
    QString ErrorMsg;
};

int SketchSelection::setUp()
{
    std::vector<Gui::SelectionObject> selection = Gui::Selection().getSelectionEx();

    Sketcher::SketchObject* SketchObj = nullptr;
    std::vector<std::string> SketchSubNames;
    std::vector<std::string> SupportSubNames;

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() == 1) {
        // if one selectetd, only sketch allowed. should be done by activity of command
        if (!selection[0].getObject()->isDerivedFrom<Sketcher::SketchObject>()) {
            ErrorMsg = QObject::tr("Only the sketch and its support are allowed to be selected");
            return -1;
        }

        SketchSubNames = selection[0].getSubNames();
    }
    else if (selection.size() == 2) {
        if (selection[0].getObject()->isDerivedFrom<Sketcher::SketchObject>()) {
            SketchObj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());
            // check if the none sketch object is the support of the sketch
            if (selection[1].getObject() != SketchObj->AttachmentSupport.getValue()) {
                ErrorMsg = QObject::tr("Only the sketch and its support may be selected");
                return -1;
            }
            // assume always a Part::Feature derived object as support
            assert(Part::hasShape(selection[1].getObject()));
            SketchSubNames = selection[0].getSubNames();
            SupportSubNames = selection[1].getSubNames();
        }
        else if (selection[1].getObject()->isDerivedFrom<Sketcher::SketchObject>()) {
            SketchObj = static_cast<Sketcher::SketchObject*>(selection[1].getObject());
            // check if the none sketch object is the support of the sketch
            if (selection[0].getObject() != SketchObj->AttachmentSupport.getValue()) {
                ErrorMsg = QObject::tr("Only the sketch and its support  may be selected");
                return -1;
            }
            // assume always a Part::Feature derived object as support
            assert(Part::hasShape(selection[0].getObject()));
            SketchSubNames = selection[1].getSubNames();
            SupportSubNames = selection[0].getSubNames();
        }
        else {
            ErrorMsg = QObject::tr("One of the selected has to be on the sketch.");
            return -1;
        }
    }

    return Items.size();
}

}// namespace SketcherGui

/* Constrain commands =======================================================*/

void CmdSketcherConstraint::activated(int /*iMsg*/)
{
    ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
    getSelection().clearSelection();
}

// Comp for dimension tools =============================================

class CmdSketcherCompDimensionTools : public Gui::GroupCommand
{
public:
    CmdSketcherCompDimensionTools()
        : GroupCommand("Sketcher_CompDimensionTools")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Dimension");
        sToolTipText = QT_TR_NOOP("Dimension tools");
        sWhatsThis = "Sketcher_CompDimensionTools";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);
        setRememberLast(false);

        addCommand("Sketcher_Dimension");
        addCommand(); //separator
        addCommand("Sketcher_ConstrainDistanceX");
        addCommand("Sketcher_ConstrainDistanceY");
        addCommand("Sketcher_ConstrainDistance");
        addCommand("Sketcher_ConstrainRadiam");
        addCommand("Sketcher_ConstrainRadius");
        addCommand("Sketcher_ConstrainDiameter");
        addCommand("Sketcher_ConstrainAngle");
        addCommand("Sketcher_ConstrainLock");
    }

    void updateAction(int mode) override
    {
        Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(getAction());
        if (!pcAction) {
            return;
        }

        QList<QAction*> al = pcAction->actions();
        int index = pcAction->property("defaultAction").toInt();
        switch (mode) {
        case Reference:
            al[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Dimension_Driven"));
            //al[1] is the separator
            al[2]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_HorizontalDistance_Driven"));
            al[3]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_VerticalDistance_Driven"));
            al[4]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Length_Driven"));
            al[5]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam_Driven"));
            al[6]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius_Driven"));
            al[7]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter_Driven"));
            al[8]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_InternalAngle_Driven"));
            al[9]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Lock_Driven"));
            getAction()->setIcon(al[index]->icon());
            break;
        case Driving:
            al[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Dimension"));
            //al[1] is the separator
            al[2]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_HorizontalDistance"));
            al[3]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_VerticalDistance"));
            al[4]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Length"));
            al[5]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam"));
            al[6]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius"));
            al[7]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter"));
            al[8]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_InternalAngle"));
            al[9]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Lock"));
            getAction()->setIcon(al[index]->icon());
            break;
        }
    }

    const char* className() const override { return "CmdSketcherCompDimensionTools"; }
};

// Comp for constrain tools =============================================

class CmdSketcherCompConstrainTools : public Gui::GroupCommand
{
public:
    CmdSketcherCompConstrainTools()
        : GroupCommand("Sketcher_CompConstrainTools")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Constrain");
        sToolTipText = QT_TR_NOOP("Constrain tools");
        sWhatsThis = "Sketcher_CompConstrainTools";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);
        setRememberLast(false);

        addCommand("Sketcher_ConstrainCoincidentUnified");
        addCommand("Sketcher_ConstrainHorVer");
        addCommand("Sketcher_ConstrainParallel");
        addCommand("Sketcher_ConstrainPerpendicular");
        addCommand("Sketcher_ConstrainTangent");
        addCommand("Sketcher_ConstrainEqual");
        addCommand("Sketcher_ConstrainSymmetric");
        addCommand("Sketcher_ConstrainBlock");
    }
    const char* className() const override { return "CmdSketcherCompConstrainTools"; }
};

// Comp for toggle constraint tools =============================================

class CmdSketcherCompToggleConstraints : public Gui::GroupCommand
{
public:
    CmdSketcherCompToggleConstraints()
        : GroupCommand("Sketcher_CompToggleConstraints")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Toggle Constraints");
        sToolTipText = QT_TR_NOOP("Toggle constrain tools");
        sWhatsThis = "Sketcher_CompToggleConstraints";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);
        setRememberLast(false);

        addCommand("Sketcher_ToggleDrivingConstraint");
        addCommand("Sketcher_ToggleActiveConstraint");

    }
    const char* className() const override
    {
        return "CmdSketcherCompToggleConstraints";
    }
    bool isActive() override
    {
        return isCommandActive(getActiveGuiDocument());
    }
};

// Comp for horizontal/vertical =============================================

class CmdSketcherCompHorizontalVertical : public Gui::GroupCommand
{
public:
    CmdSketcherCompHorizontalVertical()
        : GroupCommand("Sketcher_CompHorVer")
    {
        sAppModule = "Sketcher";
        sGroup = "Sketcher";
        sMenuText = QT_TR_NOOP("Horizontal/Vertical Constraint");
        sToolTipText = QT_TR_NOOP("Constrains the selected elements either horizontally or vertically");
        sWhatsThis = "Sketcher_CompHorVer";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);
        setRememberLast(false);

        addCommand("Sketcher_ConstrainHorVer");
        addCommand("Sketcher_ConstrainHorizontal");
        addCommand("Sketcher_ConstrainVertical");
    }

    const char* className() const override
    {
        return "CmdSketcherCompHorizontalVertical";
    }

    bool isActive() override
    {
        return isCommandActive(getActiveGuiDocument());
    }
};

//=================================================================================

class CmdSketcherConstrainParallel: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainParallel();
    ~CmdSketcherConstrainParallel() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainParallel";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainParallel::CmdSketcherConstrainParallel()
    : CmdSketcherConstraint("Sketcher_ConstrainParallel")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Parallel Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected lines to be parallel");
    sWhatsThis = "Sketcher_ConstrainParallel";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Parallel";
    sAccel = "P";
    eType = ForEdit;

    // TODO: Also needed: ExternalEdges
    allowedSelSequences = {{SelEdge, SelEdgeOrAxis},
                           {SelEdgeOrAxis, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge}};
}

void CmdSketcherConstrainParallel::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
            getSelection().clearSelection();
        }
        else {
            // TODO: Get the exact message from git history and put it here
            Gui::TranslatedUserWarning(getActiveGuiDocument(),
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select two or more lines from the sketch."));
        }
        return;
    }

    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    // go through the selected subelements
    std::vector<int> ids;
    bool hasAlreadyExternal = false;
    for (auto& subname : selection[0].getSubNames()) {

        int GeoId;
        Sketcher::PointPos PosId;
        getIdsFromName(subname, Obj, GeoId, PosId);

        if (!isEdge(GeoId, PosId)) {
            continue;
        }
        else if (isPointOrSegmentFixed(Obj, GeoId)) {
            if (hasAlreadyExternal) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }
            else {
                hasAlreadyExternal = true;
            }
        }

        // Check that the curve is a line segment
        const Part::Geometry* geo = Obj->getGeometry(GeoId);

        if (!isLineSegment(*geo)) {
            Gui::TranslatedUserWarning(Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("One selected edge is not a valid line."));
            return;
        }
        ids.push_back(GeoId);
    }

    if (ids.size() < 2) {
        Gui::TranslatedUserWarning(Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select at least two lines from the sketch."));
        return;
    }

    // undo command open
    openCommand(QT_TRANSLATE_NOOP("Command", "Add parallel constraint"));
    for (int i = 0; i < int(ids.size() - 1); i++) {
        Gui::cmdAppObjectArgs(selection[0].getObject(),
                              "addConstraint(Sketcher.Constraint('Parallel',%d,%d))",
                              ids[i],
                              ids[i + 1]);
    }
    // finish the transaction and update
    commitCommand();

    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
}

void CmdSketcherConstrainParallel::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    switch (seqIndex) {
        case 0:// {SelEdge, SelEdgeOrAxis}
        case 1:// {SelEdgeOrAxis, SelEdge}
        case 2:// {SelEdge, SelExternalEdge}
        case 3:// {SelExternalEdge, SelEdge}
            // create the constraint
            SketcherGui::ViewProviderSketch* sketchgui =
                static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
            Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

            int GeoId1 = selSeq.at(0).GeoId, GeoId2 = selSeq.at(1).GeoId;

            // Check that the curves are line segments
            if (! isLineSegment(*(Obj->getGeometry(GeoId1))) || ! isLineSegment(*(Obj->getGeometry(GeoId2)))) {
                Gui::TranslatedUserWarning(Obj,
                                           QObject::tr("Wrong selection"),
                                           QObject::tr("The selected edge is not a valid line."));
                return;
            }

            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add parallel constraint"));
            Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                  "addConstraint(Sketcher.Constraint('Parallel',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            // finish the transaction and update
            commitCommand();
            tryAutoRecompute(Obj);
    }
}

// ======================================================================================

class CmdSketcherConstrainPerpendicular: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainPerpendicular();
    ~CmdSketcherConstrainPerpendicular() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainPerpendicular";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainPerpendicular::CmdSketcherConstrainPerpendicular()
    : CmdSketcherConstraint("Sketcher_ConstrainPerpendicular")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Perpendicular Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected lines to be perpendicular");
    sWhatsThis = "Sketcher_ConstrainPerpendicular";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Perpendicular";
    sAccel = "N";
    eType = ForEdit;

    // TODO: there are two more combos: endpoint then curve and endpoint then endpoint
    allowedSelSequences = {{SelEdge, SelEdgeOrAxis},
                           {SelEdgeOrAxis, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge},
                           {SelVertexOrRoot, SelEdge, SelEdgeOrAxis},
                           {SelVertexOrRoot, SelEdgeOrAxis, SelEdge},
                           {SelVertexOrRoot, SelEdge, SelExternalEdge},
                           {SelVertexOrRoot, SelExternalEdge, SelEdge},
                           {SelEdge, SelVertexOrRoot, SelEdgeOrAxis},
                           {SelEdgeOrAxis, SelVertexOrRoot, SelEdge},
                           {SelEdge, SelVertexOrRoot, SelExternalEdge},
                           {SelExternalEdge, SelVertexOrRoot, SelEdge}};
    ;
}

void CmdSketcherConstrainPerpendicular::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
            getSelection().clearSelection();
        }
        else {
            // TODO: Get the exact message from git history and put it here
            QString strBasicHelp =
                QObject::tr("There is a number of ways this constraint can be applied.\n\n"
                            "Accepted combinations: two curves; an endpoint and a curve; two "
                            "endpoints; two curves and a point.",
                            /*disambig.:*/ "perpendicular constraint");
            QString strError =
                QObject::tr("Select some geometry from the sketch.", "perpendicular constraint");
            strError.append(QStringLiteral("\n\n"));
            strError.append(strBasicHelp);
            Gui::TranslatedUserWarning(getActiveGuiDocument(),
                                       QObject::tr("Wrong selection"),
                                       std::move(strError));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = dynamic_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (!Obj || (SubNames.size() != 2 && SubNames.size() != 3)) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Wrong number of selected objects!"));
        return;
    }

    int GeoId1, GeoId2, GeoId3;
    Sketcher::PointPos PosId1, PosId2, PosId3;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);

    if (areBothPointsOrSegmentsFixed(Obj,
                                     GeoId1,
                                     GeoId2)) {// checkBothExternal displays error message
        showNoConstraintBetweenFixedGeometry(Obj);
        return;
    }

    if (SubNames.size() == 3) {
        getIdsFromName(SubNames[2], Obj, GeoId3, PosId3);
        if (isVertex(GeoId1, PosId1) && isVertex(GeoId2, PosId2) && isEdge(GeoId3, PosId3)) {
            // point point line perpendicularity
            openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));
            Gui::cmdAppObjectArgs(
                selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Perpendicular',%d,%d,%d,%d,%d))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                GeoId3);

            removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);
            commitCommand();
            tryAutoRecompute(Obj);
            getSelection().clearSelection();
            return;
        } else {
            // perpendicular via point
            // let's sink the point to be GeoId3. We want to keep the order the two curves have been
            // selected in.
            if (isVertex(GeoId1, PosId1)) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
            }
            if (isVertex(GeoId2, PosId2)) {
                std::swap(GeoId2, GeoId3);
                std::swap(PosId2, PosId3);
            }

            if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

                if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
                    Gui::TranslatedUserWarning(
                        Obj,
                        QObject::tr("Wrong selection"),
                        QObject::tr("Select an edge that is not a B-spline weight."));
                    return;
                }

                openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));

                bool safe = addConstraintSafely(Obj, [&]() {
                    // add missing point-on-object constraints
                    if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                        const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                        if (!(geom1 && isBSplineCurve(*geom1))) {
                            Gui::cmdAppObjectArgs(
                                selection[0].getObject(),
                                "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                GeoId3,
                                static_cast<int>(PosId3),
                                GeoId1);
                        }
                    }

                    if (!IsPointAlreadyOnCurve(GeoId2, GeoId3, PosId3, Obj)) {
                        const Part::Geometry *geom2 = Obj->getGeometry(GeoId2);
                        if (!(geom2 && isBSplineCurve(*geom2))) {
                            Gui::cmdAppObjectArgs(
                                selection[0].getObject(),
                                "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                GeoId3,
                                static_cast<int>(PosId3),
                                GeoId2);
                        }
                    }

                    if (!IsPointAlreadyOnCurve(
                            GeoId1,
                            GeoId3,
                            PosId3,
                            Obj)) {
                        // FIXME: it's a good idea to add a check if the sketch is solved
                        const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                        if (!(geom1 && isBSplineCurve(*geom1))) {
                            Gui::cmdAppObjectArgs(
                                selection[0].getObject(),
                                "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                GeoId3,
                                static_cast<int>(PosId3),
                                GeoId1);
                        }
                    }

                    Gui::cmdAppObjectArgs(
                        selection[0].getObject(),
                        "addConstraint(Sketcher.Constraint('PerpendicularViaPoint',%d,%d,%d,%d))",
                        GeoId1,
                        GeoId2,
                        GeoId3,
                        static_cast<int>(PosId3));

                    removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);
                });

                if (!safe) {
                    return;
                }
                else {
                    commitCommand();
                    tryAutoRecompute(Obj);
                }

                getSelection().clearSelection();

                return;
            }

        }

        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("With 3 objects, there must be 2 curves and 1 point."));
    }
    else if (SubNames.size() == 2) {
        if (isVertex(GeoId1, PosId1)
            && isVertex(GeoId2, PosId2)) {// endpoint-to-endpoint perpendicularity

            if (isSimpleVertex(Obj, GeoId1, PosId1) || isSimpleVertex(Obj, GeoId2, PosId2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr(
                        "Cannot add a perpendicularity constraint at an unconnected point!"));
                return;
            }

            // This code supports simple B-spline endpoint perp to any other geometric curve
            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (geom1 && geom2 && (isBSplineCurve(*geom1) || isBSplineCurve(*geom2))) {
                if (! isBSplineCurve(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                    std::swap(PosId1, PosId2);
                }
                // GeoId1 is the B-spline now
            }// end of code supports simple B-spline endpoint tangency

            openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Perpendicular',%d,%d,%d,%d))",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId2,
                                  static_cast<int>(PosId2));
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
        else if ((isVertex(GeoId1, PosId1) && isEdge(GeoId2, PosId2))
                 || (isEdge(GeoId1, PosId1) && isVertex(GeoId2, PosId2))) {// endpoint-to-curve
            if (isVertex(GeoId2, PosId2)) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
            }

            if (isSimpleVertex(Obj, GeoId1, PosId1)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr(
                        "Cannot add a perpendicularity constraint at an unconnected point!"));
                return;
            }

            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (isBsplinePole(geom2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicularity constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Perpendicular',%d,%d,%d))",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId2);

            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
        else if (isEdge(GeoId1, PosId1)
                 && isEdge(GeoId2, PosId2)) {// simple perpendicularity between GeoId1 and GeoId2

            const Part::Geometry* geo1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geo2 = Obj->getGeometry(GeoId2);
            if (!geo1 || !geo2) {
                return;
            }

            if (! isLineSegment(*geo1) && ! isLineSegment(*geo2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("One of the selected edges should be a line."));
                return;
            }

            // if (isBSplineCurve(*geo1) || isBSplineCurve(*geo2)) {
            //     // unsupported until tangent to B-spline at any point implemented.
            //     Gui::TranslatedUserWarning(
            //         Obj,
            //         QObject::tr("Wrong selection"),
            //         QObject::tr("Perpendicular to B-spline edge currently unsupported."));
            //     return;
            // }

            if (isLineSegment(*geo1)) {
                std::swap(GeoId1, GeoId2);
            }

            if (isBsplinePole(Obj, GeoId1)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            // GeoId2 is the line
            geo1 = Obj->getGeometry(GeoId1);
            geo2 = Obj->getGeometry(GeoId2);

            if (isEllipse(*geo1) || isArcOfEllipse(*geo1) || isArcOfHyperbola(*geo1) || isArcOfParabola(*geo1)) {
                Base::Vector3d center;
                Base::Vector3d majdir;
                Base::Vector3d focus;
                double majord = 0;
                double minord = 0;
                double phi = 0;

                if (isEllipse(*geo1)) {
                    auto ellipse = static_cast<const Part::GeomEllipse*>(geo1);
                    center = ellipse->getCenter();
                    majord = ellipse->getMajorRadius();
                    minord = ellipse->getMinorRadius();
                    majdir = ellipse->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfEllipse(*geo1)) {
                    auto aoe = static_cast<const Part::GeomArcOfEllipse*>(geo1);
                    center = aoe->getCenter();
                    majord = aoe->getMajorRadius();
                    minord = aoe->getMinorRadius();
                    majdir = aoe->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfHyperbola(*geo1)) {
                    auto aoh = static_cast<const Part::GeomArcOfHyperbola*>(geo1);
                    center = aoh->getCenter();
                    majord = aoh->getMajorRadius();
                    minord = aoh->getMinorRadius();
                    majdir = aoh->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfParabola(*geo1)) {
                    auto aop = static_cast<const Part::GeomArcOfParabola*>(geo1);
                    center = aop->getCenter();
                    focus = aop->getFocus();
                }

                const Part::GeomLineSegment* line = static_cast<const Part::GeomLineSegment*>(geo2);

                Base::Vector3d point1 = line->getStartPoint();
                Base::Vector3d PoO;

                if (isArcOfHyperbola(*geo1)) {
                    double df = sqrt(majord * majord + minord * minord);
                    Base::Vector3d direction = point1 - (center + majdir * df);// towards the focus
                    double tapprox = atan2(direction.y, direction.x) - phi;

                    PoO = Base::Vector3d(center.x + majord * cosh(tapprox) * cos(phi)
                                             - minord * sinh(tapprox) * sin(phi),
                                         center.y + majord * cosh(tapprox) * sin(phi)
                                             + minord * sinh(tapprox) * cos(phi),
                                         0);
                }
                else if (isArcOfParabola(*geo1)) {
                    Base::Vector3d direction = point1 - focus;// towards the focus

                    PoO = point1 + direction / 2;
                }
                else {
                    Base::Vector3d direction = point1 - center;
                    double tapprox = atan2(direction.y, direction.x)
                        - phi;// we approximate the eccentric anomaly by the polar

                    PoO = Base::Vector3d(center.x + majord * cos(tapprox) * cos(phi)
                                             - minord * sin(tapprox) * sin(phi),
                                         center.y + majord * cos(tapprox) * sin(phi)
                                             + minord * sin(tapprox) * cos(phi),
                                         0);
                }
                openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));

                try {
                    // Add a point
                    Gui::cmdAppObjectArgs(Obj,
                                          "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)",
                                          PoO.x,
                                          PoO.y);
                    int GeoIdPoint = Obj->getHighestCurveIndex();

                    // Point on first object (ellipse, arc of ellipse)
                    Gui::cmdAppObjectArgs(
                        selection[0].getObject(),
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start),
                        GeoId1);
                    // Point on second object
                    Gui::cmdAppObjectArgs(
                        selection[0].getObject(),
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start),
                        GeoId2);
                    // add constraint: Perpendicular-via-point
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PerpendicularViaPoint',%d,%d,%d,%d))",
                        GeoId1,
                        GeoId2,
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start));
                }
                catch (const Base::Exception& e) {
                    Gui::NotifyUserError(Obj,
                                         QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                                         e.what());
                    abortCommand();

                    tryAutoRecompute(Obj);
                    return;
                }

                commitCommand();
                tryAutoRecompute(Obj);

                getSelection().clearSelection();
                return;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Perpendicular',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
    }
}

void CmdSketcherConstrainPerpendicular::applyConstraint(std::vector<SelIdPair>& selSeq,
                                                        int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef, GeoId3 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1 = Sketcher::PointPos::none, PosId2 = Sketcher::PointPos::none,
                       PosId3 = Sketcher::PointPos::none;

    switch (seqIndex) {
        case 0:// {SelEdge, SelEdgeOrAxis}
        case 1:// {SelEdgeOrAxis, SelEdge}
        case 2:// {SelEdge, SelExternalEdge}
        case 3:// {SelExternalEdge, SelEdge}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;

            // check if the edge already has a Block constraint
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            const Part::Geometry* geo1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geo2 = Obj->getGeometry(GeoId2);
            if (!geo1 || !geo2) {
                return;
            }

            if (! isLineSegment(*geo1) && ! isLineSegment(*geo2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("One of the selected edges should be a line."));
                return;
            }

            // if (isBSplineCurve(*geo1) || isBSplineCurve(*geo2)) {
            //     // unsupported until tangent to B-spline at any point implemented.
            //     Gui::TranslatedUserWarning(
            //         Obj,
            //         QObject::tr("Wrong selection"),
            //         QObject::tr("Perpendicular to B-spline edge currently unsupported."));
            //     return;
            // }

            if (isLineSegment(*geo1)) {
                std::swap(GeoId1, GeoId2);
            }

            if (isBsplinePole(Obj, GeoId1)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            // GeoId2 is the line
            geo1 = Obj->getGeometry(GeoId1);
            geo2 = Obj->getGeometry(GeoId2);

            if (isEllipse(*geo1) || isArcOfEllipse(*geo1) || isArcOfHyperbola(*geo1) || isArcOfParabola(*geo1)) {
                Base::Vector3d center;
                Base::Vector3d majdir;
                Base::Vector3d focus;
                double majord = 0;
                double minord = 0;
                double phi = 0;

                if (isEllipse(*geo1)) {
                    auto ellipse = static_cast<const Part::GeomEllipse*>(geo1);
                    center = ellipse->getCenter();
                    majord = ellipse->getMajorRadius();
                    minord = ellipse->getMinorRadius();
                    majdir = ellipse->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfEllipse(*geo1)) {
                    auto aoe = static_cast<const Part::GeomArcOfEllipse*>(geo1);
                    center = aoe->getCenter();
                    majord = aoe->getMajorRadius();
                    minord = aoe->getMinorRadius();
                    majdir = aoe->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfHyperbola(*geo1)) {
                    auto aoh = static_cast<const Part::GeomArcOfHyperbola*>(geo1);
                    center = aoh->getCenter();
                    majord = aoh->getMajorRadius();
                    minord = aoh->getMinorRadius();
                    majdir = aoh->getMajorAxisDir();
                    phi = atan2(majdir.y, majdir.x);
                }
                else if (isArcOfParabola(*geo1)) {
                    auto aop = static_cast<const Part::GeomArcOfParabola*>(geo1);
                    center = aop->getCenter();
                    focus = aop->getFocus();
                }

                const Part::GeomLineSegment* line = static_cast<const Part::GeomLineSegment*>(geo2);

                Base::Vector3d point1 = line->getStartPoint();
                Base::Vector3d PoO;

                if (isArcOfHyperbola(*geo1)) {
                    double df = sqrt(majord * majord + minord * minord);
                    Base::Vector3d direction = point1 - (center + majdir * df);// towards the focus
                    double tapprox = atan2(direction.y, direction.x) - phi;

                    PoO = Base::Vector3d(center.x + majord * cosh(tapprox) * cos(phi)
                                             - minord * sinh(tapprox) * sin(phi),
                                         center.y + majord * cosh(tapprox) * sin(phi)
                                             + minord * sinh(tapprox) * cos(phi),
                                         0);
                }
                else if (isArcOfParabola(*geo1)) {
                    Base::Vector3d direction = point1 - focus;// towards the focus

                    PoO = point1 + direction / 2;
                }
                else {
                    Base::Vector3d direction = point1 - center;
                    double tapprox = atan2(direction.y, direction.x)
                        - phi;// we approximate the eccentric anomaly by the polar

                    PoO = Base::Vector3d(center.x + majord * cos(tapprox) * cos(phi)
                                             - minord * sin(tapprox) * sin(phi),
                                         center.y + majord * cos(tapprox) * sin(phi)
                                             + minord * sin(tapprox) * cos(phi),
                                         0);
                }
                openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));

                try {
                    // Add a point
                    Gui::cmdAppObjectArgs(Obj,
                                          "addGeometry(Part.Point(App.Vector(%f,%f,0)), True)",
                                          PoO.x,
                                          PoO.y);
                    int GeoIdPoint = Obj->getHighestCurveIndex();

                    // Point on first object (ellipse, arc of ellipse)
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start),
                        GeoId1);
                    // Point on second object
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start),
                        GeoId2);

                    // add constraint: Perpendicular-via-point
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PerpendicularViaPoint',%d,%d,%d,%d))",
                        GeoId1,
                        GeoId2,
                        GeoIdPoint,
                        static_cast<int>(Sketcher::PointPos::start));

                    commitCommand();
                }
                catch (const Base::Exception& e) {
                    Gui::NotifyUserError(Obj,
                                         QT_TRANSLATE_NOOP("Notifications", "Invalid Constraint"),
                                         e.what());
                    abortCommand();
                }

                tryAutoRecompute(Obj);

                getSelection().clearSelection();
                return;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Perpendicular',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            commitCommand();

            tryAutoRecompute(Obj);
            return;
        }
        case 4:// {SelVertexOrRoot, SelEdge, SelEdgeOrAxis}
        case 5:// {SelVertexOrRoot, SelEdgeOrAxis, SelEdge}
        case 6:// {SelVertexOrRoot, SelEdge, SelExternalEdge}
        case 7:// {SelVertexOrRoot, SelExternalEdge, SelEdge}
        {
            // let's sink the point to be GeoId3.
            GeoId1 = selSeq.at(1).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(0).GeoId;
            PosId3 = selSeq.at(0).PosId;

            break;
        }
        case 8: // {SelEdge, SelVertexOrRoot, SelEdgeOrAxis}
        case 9: // {SelEdgeOrAxis, SelVertexOrRoot, SelEdge}
        case 10:// {SelEdge, SelVertexOrRoot, SelExternalEdge}
        case 11:// {SelExternalEdge, SelVertexOrRoot, SelEdge}
        {
            // let's sink the point to be GeoId3.
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(1).GeoId;
            PosId3 = selSeq.at(1).PosId;

            break;
        }
        default:
            return;
    }

    if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

        // check if the edge already has a Block constraint
        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            showNoConstraintBetweenFixedGeometry(Obj);
            return;
        }

        if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select an edge that is not a B-spline weight."));
            return;
        }

        openCommand(QT_TRANSLATE_NOOP("Command", "Add perpendicular constraint"));

        bool safe = addConstraintSafely(Obj, [&]() {
            // add missing point-on-object constraints
            if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                if (!(geom1 && isBSplineCurve(*geom1))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId1);
                }
            }

            if (!IsPointAlreadyOnCurve(GeoId2, GeoId3, PosId3, Obj)) {
                const Part::Geometry *geom2 = Obj->getGeometry(GeoId2);
                if (!(geom2 && isBSplineCurve(*geom2))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId2);
                }
            }

            if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                // FIXME: it's a good idea to add a check if the sketch is solved
                const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                if (!(geom1 && isBSplineCurve(*geom1))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId1);
                }
            }

            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('PerpendicularViaPoint',%d,%d,%d,%d))",
                GeoId1,
                GeoId2,
                GeoId3,
                static_cast<int>(PosId3));

            removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);
        });

        if (!safe) {
            abortCommand();
            return;
        }
        commitCommand();
        tryAutoRecompute(Obj);

        getSelection().clearSelection();

        return;
    }
}

// ======================================================================================

class CmdSketcherConstrainTangent: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainTangent();
    ~CmdSketcherConstrainTangent() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainTangent";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
    // returns true if a substitution took place
    bool substituteConstraintCombinations(SketchObject* Obj, int GeoId1, int GeoId2);
};

CmdSketcherConstrainTangent::CmdSketcherConstrainTangent()
    : CmdSketcherConstraint("Sketcher_ConstrainTangent")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Tangent/Collinear Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements to be tangent or collinear");
    sWhatsThis = "Sketcher_ConstrainTangent";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Tangent";
    sAccel = "T";
    eType = ForEdit;

    allowedSelSequences = {
        {SelEdge, SelEdgeOrAxis},
        {SelEdgeOrAxis, SelEdge},
        {SelEdge, SelExternalEdge},
        {SelExternalEdge, SelEdge}, /* Two Curves */
        {SelVertexOrRoot, SelEdge, SelEdgeOrAxis},
        {SelVertexOrRoot, SelEdgeOrAxis, SelEdge},
        {SelVertexOrRoot, SelEdge, SelExternalEdge},
        {SelVertexOrRoot, SelExternalEdge, SelEdge},
        {SelEdge, SelVertexOrRoot, SelEdgeOrAxis},
        {SelEdgeOrAxis, SelVertexOrRoot, SelEdge},
        {SelEdge, SelVertexOrRoot, SelExternalEdge},
        {SelExternalEdge, SelVertexOrRoot, SelEdge}, /* Two Curves and a Point */
        {SelVertexOrRoot, SelVertex} /*Two Endpoints*/ /*No Place for One Endpoint and One Curve*/};
}

bool CmdSketcherConstrainTangent::substituteConstraintCombinations(SketchObject* Obj,
                                                                   int GeoId1,
                                                                   int GeoId2)
{
    const std::vector<Constraint*>& cvals = Obj->Constraints.getValues();

    int cid = 0;
    for (std::vector<Constraint*>::const_iterator it = cvals.begin(); it != cvals.end();
         ++it, ++cid) {
        if ((*it)->Type == Sketcher::Coincident
            && (((*it)->First == GeoId1 && (*it)->Second == GeoId2)
                || ((*it)->Second == GeoId1 && (*it)->First == GeoId2))
            && ((*it)->FirstPos == Sketcher::PointPos::start
                || (*it)->FirstPos == Sketcher::PointPos::end)
            && ((*it)->SecondPos == Sketcher::PointPos::start
                || (*it)->SecondPos == Sketcher::PointPos::end)) {
            // save values because 'doEndpointTangency' changes the
            // constraint property and thus invalidates this iterator
            int first = (*it)->First;
            int firstpos = static_cast<int>((*it)->FirstPos);

            openCommand(
                QT_TRANSLATE_NOOP("Command", "Swap coincident+tangency with ptp tangency"));

            doEndpointTangency(Obj, (*it)->First, (*it)->Second, (*it)->FirstPos, (*it)->SecondPos);

            Gui::cmdAppObjectArgs(Obj, "delConstraintOnPoint(%d,%d)", first, firstpos);

            commitCommand();
            Obj->solve();// The substitution requires a solve() so that the autoremove redundants
                         // works when Autorecompute not active.
            tryAutoRecomputeIfNotSolve(Obj);

            notifyConstraintSubstitutions(QObject::tr("Endpoint to endpoint tangency was applied. "
                                                      "The coincident constraint was deleted."));

            getSelection().clearSelection();
            return true;
        }
        else if ((*it)->Type == Sketcher::PointOnObject
                 && (((*it)->First == GeoId1 && (*it)->Second == GeoId2)
                     || ((*it)->Second == GeoId1 && (*it)->First == GeoId2))
                 && ((*it)->FirstPos == Sketcher::PointPos::start
                     || (*it)->FirstPos == Sketcher::PointPos::end)) {
            openCommand(
                QT_TRANSLATE_NOOP("Command",
                                  "Swap point on object and tangency with point to curve tangency"));

            doEndpointToEdgeTangency(Obj, (*it)->First, (*it)->FirstPos, (*it)->Second);

            Gui::cmdAppObjectArgs(Obj,
                                  "delConstraint(%d)",
                                  cid);// remove the preexisting point on object constraint.

            commitCommand();

            // A substitution requires a solve() so that the autoremove redundants works when
            // Autorecompute not active. However, delConstraint includes such solve() internally. So
            // at this point it is already solved.
            tryAutoRecomputeIfNotSolve(Obj);

            notifyConstraintSubstitutions(QObject::tr("Endpoint to edge tangency was applied. The "
                                                      "point on object constraint was deleted."));

            getSelection().clearSelection();
            return true;
        }
    }

    return false;
}

void CmdSketcherConstrainTangent::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
            getSelection().clearSelection();
        }
        else {
            QString strBasicHelp =
                QObject::tr("There are a number of ways this constraint can be applied.\n\n"
                            "Accepted combinations: two curves; an endpoint and a curve; two "
                            "endpoints; two curves and a point.",
                            /*disambig.:*/ "tangent constraint");
            QString strError =
                QObject::tr("Select some geometry from the sketch.", "tangent constraint");
            strError.append(QStringLiteral("\n\n"));
            strError.append(strBasicHelp);
            Gui::TranslatedUserWarning(getActiveGuiDocument(),
                                       QObject::tr("Wrong selection"),
                                       std::move(strError));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.size() != 2 && SubNames.size() != 3) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Wrong number of selected objects!"));
        return;
    }

    int GeoId1, GeoId2, GeoId3;
    Sketcher::PointPos PosId1, PosId2, PosId3;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);

    if (areBothPointsOrSegmentsFixed(Obj,
                                     GeoId1,
                                     GeoId2)) {// checkBothExternal displays error message
        showNoConstraintBetweenFixedGeometry(Obj);
        return;
    }
    if (SubNames.size() == 3) {// tangent via point
        getIdsFromName(SubNames[2], Obj, GeoId3, PosId3);
        // let's sink the point to be GeoId3. We want to keep the order the two curves have been
        // selected in.
        if (isVertex(GeoId1, PosId1)) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
        }
        if (isVertex(GeoId2, PosId2)) {
            std::swap(GeoId2, GeoId3);
            std::swap(PosId2, PosId3);
        }

        if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

            if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));

            bool safe = addConstraintSafely(Obj, [&]() {
                // add missing point-on-object constraints
                if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                    const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                    if (!(geom1 && isBSplineCurve(*geom1))) {
                        Gui::cmdAppObjectArgs(
                            selection[0].getObject(),
                            "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                            GeoId3,
                            static_cast<int>(PosId3),
                            GeoId1);
                    }
                }

                if (!IsPointAlreadyOnCurve(GeoId2, GeoId3, PosId3, Obj)) {
                    const Part::Geometry *geom2 = Obj->getGeometry(GeoId2);
                    if (!(geom2 && isBSplineCurve(*geom2))) {
                        Gui::cmdAppObjectArgs(
                            selection[0].getObject(),
                            "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                            GeoId3,
                            static_cast<int>(PosId3),
                            GeoId2);
                    }
                }

                if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                    // FIXME: it's a good idea to add a check if the sketch is solved
                    const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                    if (!(geom1 && isBSplineCurve(*geom1))) {
                        Gui::cmdAppObjectArgs(
                            selection[0].getObject(),
                            "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                            GeoId3,
                            static_cast<int>(PosId3),
                            GeoId1);
                    }
                }

                Gui::cmdAppObjectArgs(
                    selection[0].getObject(),
                    "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                    GeoId1,
                    GeoId2,
                    GeoId3,
                    static_cast<int>(PosId3));

                removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);
            });

            if (!safe) {
                abortCommand();
                return;
            }
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();

            return;
        }

        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("With 3 objects, there must be 2 curves and 1 point."));
    }
    else if (SubNames.size() == 2) {

        if (isVertex(GeoId1, PosId1) && isVertex(GeoId2, PosId2)) {// endpoint-to-endpoint tangency

            if (isBsplineKnot(Obj, GeoId2)) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
            }

            if (isSimpleVertex(Obj, GeoId1, PosId1) || isSimpleVertex(Obj, GeoId2, PosId2)) {

                if (isBsplineKnot(Obj, GeoId1)) {
                    const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

                    if (! geom2 || ! isLineSegment(*geom2)) {
                        Gui::TranslatedUserWarning(
                            Obj,
                            QObject::tr("Wrong selection"),
                            QObject::tr("Tangent constraint at B-spline knot is only supported "
                                        "with lines!"));
                        return;
                    }
                }
                else {
                    Gui::TranslatedUserWarning(
                        Obj,
                        QObject::tr("Wrong selection"),
                        QObject::tr("Cannot add a tangency constraint at an unconnected point!"));
                    return;
                }
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));
            doEndpointTangency(Obj, GeoId1, GeoId2, PosId1, PosId2);
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
        else if ((isVertex(GeoId1, PosId1) && isEdge(GeoId2, PosId2))
                 || (isEdge(GeoId1, PosId1)
                     && isVertex(GeoId2, PosId2))) {// endpoint-to-curve/knot-to-curve tangency
            if (isVertex(GeoId2, PosId2)) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
            }

            if (isSimpleVertex(Obj, GeoId1, PosId1)) {
                if (isBsplineKnot(Obj, GeoId1)) {
                    const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

                    if (!geom2 || ! isLineSegment(*geom2)) {
                        Gui::TranslatedUserWarning(
                            Obj,
                            QObject::tr("Wrong selection"),
                            QObject::tr("Tangent constraint at B-spline knot is only supported "
                                        "with lines!"));
                        return;
                    }
                }
                else {
                    Gui::TranslatedUserWarning(
                        Obj,
                        QObject::tr("Wrong selection"),
                        QObject::tr("Cannot add a tangency constraint at an unconnected point!"));
                    return;
                }
            }

            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (isBsplinePole(geom2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            if (!substituteConstraintCombinations(Obj, GeoId1, GeoId2)) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Tangent',%d,%d,%d))",
                                      GeoId1,
                                      static_cast<int>(PosId1),
                                      GeoId2);
                commitCommand();
                tryAutoRecompute(Obj);

                getSelection().clearSelection();
            }
            return;
        }
        else if (isEdge(GeoId1, PosId1)
                 && isEdge(GeoId2, PosId2)) {// simple tangency between GeoId1 and GeoId2

            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (isBsplinePole(geom1) || isBsplinePole(geom2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            // check if as a consequence of this command undesirable combinations of constraints
            // would arise and substitute them with more appropriate counterparts, examples:
            // - coincidence + tangency on edge
            // - point on object + tangency on edge
            if (substituteConstraintCombinations(Obj, GeoId1, GeoId2)) {
                return;
            }

            if (geom1 && geom2 && (isEllipse(*geom1) || isEllipse(*geom2))) {
                if (! isEllipse(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the ellipse
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isEllipse(*geom2) || isArcOfEllipse(*geom2) || isCircle(*geom2) || isArcOfCircle(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToEllipseviaNewPoint(Obj,
                                                    static_cast<const Part::GeomEllipse*>(geom1),
                                                    geom2,
                                                    GeoId1,
                                                    GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfHyperbola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfHyperbolaviaNewPoint(Obj,
                                            static_cast<const Part::GeomArcOfHyperbola*>(geom2),
                                            geom1,
                                            GeoId2,
                                            GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfParabola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                            Obj,
                                            static_cast<const Part::GeomArcOfParabola*>(geom2),
                                            geom1,
                                            GeoId2,
                                            GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isArcOfEllipse(*geom1) || isArcOfEllipse(*geom2))) {
                if (! isArcOfEllipse(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the arc of ellipse
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isArcOfHyperbola(*geom2) || isArcOfEllipse(*geom2)
                    || isCircle(*geom2) || isArcOfCircle(*geom2) || isLineSegment(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfEllipseviaNewPoint(
                                            Obj,
                                            static_cast<const Part::GeomArcOfEllipse*>(geom1),
                                            geom2,
                                            GeoId1,
                                            GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfParabola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                            Obj,
                                            static_cast<const Part::GeomArcOfParabola*>(geom2),
                                            geom1,
                                            GeoId2,
                                            GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isArcOfHyperbola(*geom1) || isArcOfHyperbola(*geom2))) {
                if (! isArcOfHyperbola(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the arc of hyperbola
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isArcOfHyperbola(*geom2) || isArcOfEllipse(*geom2) || isCircle(*geom2)
                    || isArcOfCircle(*geom2) || isLineSegment(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfHyperbolaviaNewPoint(
                                            Obj,
                                            static_cast<const Part::GeomArcOfHyperbola*>(geom1),
                                            geom2,
                                            GeoId1,
                                            GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfParabola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfParabola*>(geom2),
                                                geom1,
                                                GeoId2,
                                                GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isArcOfParabola(*geom1) || isArcOfParabola(*geom2))) {
                if (! isArcOfParabola(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the arc of hyperbola
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isArcOfParabola(*geom2) || isArcOfHyperbola(*geom2)
                    || isArcOfEllipse(*geom2) || isCircle(*geom2)
                    || isArcOfCircle(*geom2) || isLineSegment(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfParabola*>(geom1),
                                                geom2,
                                                GeoId1,
                                                GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isBSplineCurve(*geom1) || isBSplineCurve(*geom2))) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Only tangent-via-point is supported with a B-spline."));
                getSelection().clearSelection();
                return;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Tangent',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
    }

    return;
}

void CmdSketcherConstrainTangent::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef, GeoId3 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1 = Sketcher::PointPos::none, PosId2 = Sketcher::PointPos::none,
                       PosId3 = Sketcher::PointPos::none;

    switch (seqIndex) {
        case 0:// {SelEdge, SelEdgeOrAxis}
        case 1:// {SelEdgeOrAxis, SelEdge}
        case 2:// {SelEdge, SelExternalEdge}
        case 3:// {SelExternalEdge, SelEdge}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;

            // check if the edge already has a Block constraint
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (isBsplinePole(geom1) || isBsplinePole(geom2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            // check if as a consequence of this command undesirable combinations of constraints
            // would arise and substitute them with more appropriate counterparts, examples:
            // - coincidence + tangency on edge
            // - point on object + tangency on edge
            if (substituteConstraintCombinations(Obj, GeoId1, GeoId2)) {
                return;
            }

            if (geom1 && geom2 && (isEllipse(*geom1) || isEllipse(*geom2))) {
                if (! isEllipse(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the ellipse
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isEllipse(*geom2) || isArcOfEllipse(*geom2)
                    || isCircle(*geom2) || isArcOfCircle(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToEllipseviaNewPoint(Obj,
                                                static_cast<const Part::GeomEllipse*>(geom1),
                                                geom2,
                                                GeoId1,
                                                GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfHyperbola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfHyperbolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfHyperbola*>(geom2),
                                                geom1,
                                                GeoId2,
                                                GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfParabola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfParabola*>(geom2),
                                                geom1,
                                                GeoId2,
                                                GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isArcOfHyperbola(*geom1) || isArcOfHyperbola(*geom2))) {
                if (! isArcOfHyperbola(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the arc of hyperbola
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isArcOfHyperbola(*geom2) || isArcOfEllipse(*geom2) || isCircle(*geom2)
                   || isArcOfCircle(*geom2) || isLineSegment(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfHyperbolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfHyperbola*>(geom1),
                                                geom2,
                                                GeoId1,
                                                GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
                else if (isArcOfParabola(*geom2)) {
                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfParabola*>(geom2),
                                                geom1,
                                                GeoId2,
                                                GeoId1),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }
            else if (geom1 && geom2 && (isArcOfParabola(*geom1) || isArcOfParabola(*geom2))) {
                if (! isArcOfParabola(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                }

                // GeoId1 is the arc of hyperbola
                geom1 = Obj->getGeometry(GeoId1);
                geom2 = Obj->getGeometry(GeoId2);

                if (isArcOfParabola(*geom2) || isArcOfHyperbola(*geom2) || isArcOfEllipse(*geom2)
                   || isCircle(*geom2) || isArcOfCircle(*geom2) || isLineSegment(*geom2)) {

                    openCommand(
                        QT_TRANSLATE_NOOP("Command", "Add tangent constraint point"));
                    closeAndRecompute(currentTransactionID,
                                      !makeTangentToArcOfParabolaviaNewPoint(
                                                Obj,
                                                static_cast<const Part::GeomArcOfParabola*>(geom1),
                                                geom2,
                                                GeoId1,
                                                GeoId2),
                                      Obj);

                    getSelection().clearSelection();
                    return;
                }
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Tangent',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            commitCommand();
            tryAutoRecompute(Obj);

            return;
        }
        case 4:// {SelVertexOrRoot, SelEdge, SelEdgeOrAxis}
        case 5:// {SelVertexOrRoot, SelEdgeOrAxis, SelEdge}
        case 6:// {SelVertexOrRoot, SelEdge, SelExternalEdge}
        case 7:// {SelVertexOrRoot, SelExternalEdge, SelEdge}
        {
            // let's sink the point to be GeoId3.
            GeoId1 = selSeq.at(1).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(0).GeoId;
            PosId3 = selSeq.at(0).PosId;

            break;
        }
        case 8: // {SelEdge, SelVertexOrRoot, SelEdgeOrAxis}
        case 9: // {SelEdgeOrAxis, SelVertexOrRoot, SelEdge}
        case 10:// {SelEdge, SelVertexOrRoot, SelExternalEdge}
        case 11:// {SelExternalEdge, SelVertexOrRoot, SelEdge}
        {
            // let's sink the point to be GeoId3.
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(1).GeoId;
            PosId3 = selSeq.at(1).PosId;

            break;
        }
        case 12:// {SelVertexOrRoot, SelVertex}
        {
            // Different notation than the previous places
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;
            PosId1 = selSeq.at(0).PosId;
            PosId2 = selSeq.at(1).PosId;

            // check if the edge already has a Block constraint
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            if (isSimpleVertex(Obj, GeoId1, PosId1) || isSimpleVertex(Obj, GeoId2, PosId2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a tangency constraint at an unconnected point!"));
                return;
            }

            // This code supports simple B-spline endpoint tangency to any other geometric curve
            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

            if (geom1 && geom2 && (isBSplineCurve(*geom1) || isBSplineCurve(*geom2))) {
                if (! isBSplineCurve(*geom1)) {
                    std::swap(GeoId1, GeoId2);
                    std::swap(PosId1, PosId2);
                }
                // GeoId1 is the B-spline now
            }// end of code supports simple B-spline endpoint tangency

            openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Tangent',%d,%d,%d,%d))",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId2,
                                  static_cast<int>(PosId2));
            commitCommand();
            tryAutoRecompute(Obj);

            getSelection().clearSelection();
            return;
        }
        default:
            return;
    }

    if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

        // check if the edge already has a Block constraint
        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            showNoConstraintBetweenFixedGeometry(Obj);
            return;
        }

        if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select an edge that is not a B-spline weight."));
            return;
        }

        openCommand(QT_TRANSLATE_NOOP("Command", "Add tangent constraint"));

        bool safe = addConstraintSafely(Obj, [&]() {
            // add missing point-on-object constraints
            if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                if (!(geom1 && isBSplineCurve(*geom1))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId1);
                }
            }

            if (!IsPointAlreadyOnCurve(GeoId2, GeoId3, PosId3, Obj)) {
                const Part::Geometry *geom2 = Obj->getGeometry(GeoId2);
                if (!(geom2 && isBSplineCurve(*geom2))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId2);
                }
            }

            if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
                // FIXME: it's a good idea to add a check if the sketch is solved
                const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
                if (!(geom1 && isBSplineCurve(*geom1))) {
                    Gui::cmdAppObjectArgs(
                        Obj,
                        "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                        GeoId3,
                        static_cast<int>(PosId3),
                        GeoId1);
                }
            }

            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('TangentViaPoint',%d,%d,%d,%d))",
                GeoId1,
                GeoId2,
                GeoId3,
                static_cast<int>(PosId3));

            removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);
        });

        if (!safe) {
            abortCommand();
            return;
        }
        commitCommand();
        tryAutoRecompute(Obj);

        getSelection().clearSelection();

        return;
    }
}

// ======================================================================================

class CmdSketcherConstrainEqual: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainEqual();
    ~CmdSketcherConstrainEqual() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainEqual";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainEqual::CmdSketcherConstrainEqual()
    : CmdSketcherConstraint("Sketcher_ConstrainEqual")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Equal Constraint");
    sToolTipText =
        QT_TR_NOOP("Constrains the selected edges or circles to be equal");
    sWhatsThis = "Sketcher_ConstrainEqual";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_EqualLength";
    sAccel = "E";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge}};// Only option for equal constraint
}

void CmdSketcherConstrainEqual::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
            getSelection().clearSelection();
        }
        else {
            Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select two edges from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    // go through the selected subelements

    if (SubNames.size() < 2) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select at least two lines from the sketch."));
        return;
    }

    std::vector<int> ids;
    bool lineSel = false, arcSel = false, circSel = false, ellipsSel = false, arcEllipsSel = false,
         hasAlreadyExternal = false;
    bool hyperbSel = false, parabSel = false, weightSel = false;

    for (auto& subname : SubNames) {

        int GeoId;
        Sketcher::PointPos PosId;
        getIdsFromName(subname, Obj, GeoId, PosId);

        if (!isEdge(GeoId, PosId)) {
            Gui::TranslatedUserWarning(Obj,
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select two or more compatible edges."));
            return;
        }
        else if (GeoId == Sketcher::GeoEnum::HAxis || GeoId == Sketcher::GeoEnum::VAxis) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Sketch axes cannot be used in equality constraints."));
            return;
        }
        else if (isPointOrSegmentFixed(Obj, GeoId)) {

            if (hasAlreadyExternal) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }
            else {
                hasAlreadyExternal = true;
            }
        }

        const Part::Geometry* geo = Obj->getGeometry(GeoId);

        if (isBSplineCurve(*geo)) {
            // unsupported as they are generally hereogeneus shapes
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Equality for B-spline edge currently unsupported."));
            return;
        }

        if (isLineSegment(*geo)) {
            lineSel = true;
        }
        else if (isArcOfCircle(*geo)) {
            arcSel = true;
        }
        else if (isCircle(*geo)) {
            if (isBsplinePole(geo)) {
                weightSel = true;
            }
            else {
                circSel = true;
            }
        }
        else if (isEllipse(*geo)) {
            ellipsSel = true;
        }
        else if (isArcOfEllipse(*geo)) {
            arcEllipsSel = true;
        }
        else if (isArcOfHyperbola(*geo)) {
            hyperbSel = true;
        }
        else if (isArcOfParabola(*geo)) {
            parabSel = true;
        }
        else {
            Gui::TranslatedUserWarning(Obj,
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select two or more edges of similar type."));
            return;
        }

        ids.push_back(GeoId);
    }

    // Check for heterogeneous groups in selection
    if ((lineSel
         && ((arcSel || circSel) || (ellipsSel || arcEllipsSel) || hyperbSel || parabSel || weightSel))
        || ((arcSel || circSel) && ((ellipsSel || arcEllipsSel) || hyperbSel || parabSel || weightSel))
        || ((ellipsSel || arcEllipsSel) && (hyperbSel || parabSel || weightSel))
        || (hyperbSel && (parabSel || weightSel)) || (parabSel && weightSel)) {

        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select two or more edges of similar type."));
        return;
    }

    // undo command open
    openCommand(QT_TRANSLATE_NOOP("Command", "Add equality constraint"));
    for (int i = 0; i < int(ids.size() - 1); i++) {
        Gui::cmdAppObjectArgs(selection[0].getObject(),
                              "addConstraint(Sketcher.Constraint('Equal',%d,%d))",
                              ids[i],
                              ids[i + 1]);
    }
    // finish the transaction and update
    commitCommand();
    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
}

void CmdSketcherConstrainEqual::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef;

    switch (seqIndex) {
        case 0:// {SelEdge, SelEdge}
        case 1:// {SelEdge, SelExternalEdge}
        case 2:// {SelExternalEdge, SelEdge}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;

            // check if the edge already has a Block constraint
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            const Part::Geometry* geo1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geo2 = Obj->getGeometry(GeoId2);

            if ((isLineSegment(*geo1) && ! isLineSegment(*geo2))
                || (isArcOfHyperbola(*geo1) && ! isArcOfHyperbola(*geo2))
                || (isArcOfParabola(*geo1) && ! isArcOfParabola(*geo2))
                || (isBsplinePole(geo1) && !isBsplinePole(geo2))
                || ((isCircle(*geo1) || isArcOfCircle(*geo1)) && !(isCircle(*geo2) || isArcOfCircle(*geo2)))
                || ((isEllipse(*geo1) || isArcOfEllipse(*geo1)) && !(isEllipse(*geo2) || isArcOfEllipse(*geo2)))) {

                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select two or more edges of similar type."));
                return;
            }

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add equality constraint"));
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Equal',%d,%d))",
                                  GeoId1,
                                  GeoId2);
            // finish the transaction and update
            commitCommand();
            tryAutoRecompute(Obj);

            return;
        }
        default:
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainSymmetric: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainSymmetric();
    ~CmdSketcherConstrainSymmetric() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainSymmetric";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;

private:
    bool hasEndpoints(const Part::Geometry& geom)
    {
        return isLineSegment(geom) || isArcOfCircle(geom) || isArcOfEllipse(geom)
               || isArcOfHyperbola(geom) || isArcOfParabola(geom)
               || (isBSplineCurve(geom) && !isPeriodicBSplineCurve(geom));
    }
};

CmdSketcherConstrainSymmetric::CmdSketcherConstrainSymmetric()
    : CmdSketcherConstraint("Sketcher_ConstrainSymmetric")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Symmetric Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements to be symmetric");
    sWhatsThis = "Sketcher_ConstrainSymmetric";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Symmetric";
    sAccel = "S";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge, SelVertexOrRoot},
                           {SelExternalEdge, SelVertex},
                           {SelVertexOrRoot, SelEdge, SelVertexOrRoot},
                           {SelVertexOrRoot, SelExternalEdge, SelVertexOrRoot},
                           {SelVertex, SelEdgeOrAxis, SelVertex},
                           {SelVertexOrRoot, SelVertexOrRoot, SelEdge},
                           {SelVertexOrRoot, SelVertexOrRoot, SelExternalEdge},
                           {SelVertex, SelVertex, SelEdgeOrAxis},
                           {SelVertexOrRoot, SelVertexOrRoot, SelVertexOrRoot},
                           {SelEdge, SelEdgeOrAxis},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge},
                           {SelEdgeOrAxis, SelEdge}};
}

void CmdSketcherConstrainSymmetric::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(this));
            getSelection().clearSelection();
        }
        else {
            Gui::TranslatedUserWarning(
                getActiveGuiDocument()->getDocument(),
                QObject::tr("Wrong selection"),
                QObject::tr("Select two points and a symmetry line, "
                            "two points and a symmetry point, "
                            "an element and a symmetry line "
                            "or an element and a symmetry point from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.size() != 3 && SubNames.size() != 2) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select two points and a symmetry line, "
                                               "two points and a symmetry point, "
                                               "an element and a symmetry line "
                                               "or an element and a symmetry point from the sketch."));
        return;
    }

    int GeoId1, GeoId2, GeoId3;
    Sketcher::PointPos PosId1, PosId2, PosId3;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);

    if (SubNames.size() == 2) {
        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            showNoConstraintBetweenFixedGeometry(Obj);
            return;
        }
        if (isVertex(GeoId1, PosId1) && isEdge(GeoId2, PosId2)) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
        }
        if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2)) {
            if (GeoId1 == Sketcher::GeoEnum::HAxis || GeoId1 == Sketcher::GeoEnum::VAxis) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
            }
        }
        if (isEdge(GeoId1, PosId1) && (isVertex(GeoId2, PosId2) || isEdge(GeoId2, PosId2))) {
            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            if (!hasEndpoints(*geom1)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "because the first selected element has no endpoints. "
                                "Select a line or an open curve instead."));
                return;
            }

            const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);
            if (isEdge(GeoId2, PosId2) && !isLineSegment(*geom2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "because the second selected element is not a line. "
                                "Select a line or an axis instead."));
                return;
            }

            if (GeoId1 == GeoId2
                && (isEdge(GeoId2, PosId2)
                    || (isVertex(GeoId2, PosId2)
                        && (PosId2 == Sketcher::PointPos::start || PosId2 == Sketcher::PointPos::end)) )) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "between an element and its end points!"));
                return;
            }

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
            Gui::cmdAppObjectArgs(
                selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d,%d))",
                GeoId1,
                static_cast<int>(Sketcher::PointPos::start),
                GeoId1,
                static_cast<int>(Sketcher::PointPos::end),
                GeoId2,
                static_cast<int>(PosId2));

            // finish the transaction and update
            commitCommand();
            tryAutoRecompute(Obj);

            // clear the selection (convenience)
            getSelection().clearSelection();
            return;
        }

        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select two points and a symmetry line, "
                                               "two points and a symmetry point, "
                                               "an element and a symmetry line "
                                               "or an element and a symmetry point from the sketch."));
        return;
    }

    getIdsFromName(SubNames[2], Obj, GeoId3, PosId3);

    if (isEdge(GeoId1, PosId1) && isVertex(GeoId3, PosId3)) {
        std::swap(GeoId1, GeoId3);
        std::swap(PosId1, PosId3);
    }
    else if (isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {
        std::swap(GeoId2, GeoId3);
        std::swap(PosId2, PosId3);
    }

    if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
        showNoConstraintBetweenFixedGeometry(Obj);
        return;
    }

    if (isVertex(GeoId1, PosId1) && isVertex(GeoId2, PosId2)) {
        if (isEdge(GeoId3, PosId3)) {
            const Part::Geometry* geom = Obj->getGeometry(GeoId3);

            if (isLineSegment(*geom)) {
                if (GeoId1 == GeoId2 && GeoId2 == GeoId3) {
                    Gui::TranslatedUserWarning(Obj,
                                               QObject::tr("Wrong selection"),
                                               QObject::tr("Cannot add a symmetry constraint "
                                                           "between a line and its end points!"));
                    return;
                }

                // undo command open
                openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
                Gui::cmdAppObjectArgs(
                    selection[0].getObject(),
                    "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d))",
                    GeoId1,
                    static_cast<int>(PosId1),
                    GeoId2,
                    static_cast<int>(PosId2),
                    GeoId3);

                // finish the transaction and update
                commitCommand();
                tryAutoRecompute(Obj);

                // clear the selection (convenience)
                getSelection().clearSelection();
                return;
            }
        }
        else if (isVertex(GeoId3, PosId3)) {
            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
            Gui::cmdAppObjectArgs(
                selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d,%d))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                GeoId3,
                static_cast<int>(PosId3));

            // finish the transaction and update
            commitCommand();
            tryAutoRecompute(Obj);

            // clear the selection (convenience)
            getSelection().clearSelection();
            return;
        }
    }

    Gui::TranslatedUserWarning(Obj,
                               QObject::tr("Wrong selection"),
                               QObject::tr("Select two points and a symmetry line, "
                                           "two points and a symmetry point, "
                                           "an element and a symmetry line "
                                           "or an element and a symmetry point from the sketch."));
}

void CmdSketcherConstrainSymmetric::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef, GeoId3 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1 = Sketcher::PointPos::none, PosId2 = Sketcher::PointPos::none,
                       PosId3 = Sketcher::PointPos::none;

    switch (seqIndex) {
        case 0:// {SelEdge, SelVertexOrRoot}
        case 1:// {SelExternalEdge, SelVertex}
        {
            GeoId1 = GeoId2 = selSeq.at(0).GeoId;
            GeoId3 = selSeq.at(1).GeoId;
            PosId1 = Sketcher::PointPos::start;
            PosId2 = Sketcher::PointPos::end;
            PosId3 = selSeq.at(1).PosId;

            if (GeoId1 == GeoId3
                && (PosId2 == Sketcher::PointPos::start || PosId2 == Sketcher::PointPos::end)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "between an element and its end points!"));
                return;
            }

            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            const Part::Geometry* geom = Obj->getGeometry(GeoId1);
            if (!hasEndpoints(*geom)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "because the first selected element has no endpoints. "
                                "Select a line or an open curve instead."));
                return;
            }
            break;
        }
        case 2:// {SelVertexOrRoot, SelEdge, SelVertexOrRoot}
        case 3:// {SelVertexOrRoot, SelExternalEdge, SelVertexOrRoot}
        case 4:// {SelVertex, SelEdgeOrAxis, SelVertex}
        case 5:// {SelVertexOrRoot, SelVertexOrRoot, SelEdge}
        case 6:// {SelVertexOrRoot, SelVertexOrRoot, SelExternalEdge}
        case 7:// {SelVertex, SelVertex, SelEdgeOrAxis}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(1).GeoId;
            PosId1 = selSeq.at(0).PosId;
            PosId2 = selSeq.at(2).PosId;
            PosId3 = selSeq.at(1).PosId;

            if (isEdge(GeoId1, PosId1) && isVertex(GeoId3, PosId3)) {
                std::swap(GeoId1, GeoId3);
                std::swap(PosId1, PosId3);
            }
            else if (isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {
                std::swap(GeoId2, GeoId3);
                std::swap(PosId2, PosId3);
            }

            if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            const Part::Geometry* geom = Obj->getGeometry(GeoId3);

            if (isLineSegment(*geom)) {
                if (GeoId1 == GeoId2 && GeoId2 == GeoId3) {
                    Gui::TranslatedUserWarning(Obj,
                                               QObject::tr("Wrong selection"),
                                               QObject::tr("Cannot add a symmetry constraint "
                                                           "between a line and its end points."));
                    return;
                }

                // undo command open
                openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
                Gui::cmdAppObjectArgs(
                    Obj,
                    "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d))",
                    GeoId1,
                    static_cast<int>(PosId1),
                    GeoId2,
                    static_cast<int>(PosId2),
                    GeoId3);

                // finish the transaction and update
                commitCommand();
                tryAutoRecompute(Obj);
            }
            else {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select two points and a symmetry line, "
                                "two points and a symmetry point, "
                                "an element and a symmetry line "
                                "or an element and a symmetry point from the sketch."));
            }
            return;
        }
        case 8:// {SelVertexOrRoot, SelVertexOrRoot, SelVertexOrRoot}
        {
            // Simple point + point + point symmetry
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;
            GeoId3 = selSeq.at(2).GeoId;
            PosId1 = selSeq.at(0).PosId;
            PosId2 = selSeq.at(1).PosId;
            PosId3 = selSeq.at(2).PosId;

            if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d,%d))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                GeoId3,
                static_cast<int>(PosId3));

            // finish the transaction and update
            commitCommand();
            tryAutoRecompute(Obj);
            getSelection().clearSelection();
            return;
        }
        case 9:// {SelEdge, SelEdgeOrAxis}
        case 10:// {SelEdge, SelExternalEdge}
        case 11:// {SelExternalEdge, SelEdge}
        case 12:// {SelEdgeOrAxis, SelEdge}
        {
            if (selSeq.at(0).GeoId == Sketcher::GeoEnum::HAxis || selSeq.at(0).GeoId == Sketcher::GeoEnum::VAxis) {
                GeoId1 = GeoId2 = selSeq.at(1).GeoId;
                GeoId3 = selSeq.at(0).GeoId;
            } else {
                GeoId1 = GeoId2 = selSeq.at(0).GeoId;
                GeoId3 = selSeq.at(1).GeoId;
            }
            PosId1 = Sketcher::PointPos::start;
            PosId2 = Sketcher::PointPos::end;
            PosId3 = Sketcher::PointPos::none;

            if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
                showNoConstraintBetweenFixedGeometry(Obj);
                return;
            }

            if (GeoId1 == GeoId3) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "between an element and itself."));
                return;
            }

            const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
            if (!hasEndpoints(*geom1)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "because the first selected element has no endpoints. "
                                "Select a line or an open curve instead."));
                return;
            }
            const Part::Geometry* geom3 = Obj->getGeometry(GeoId3);
            if (!isLineSegment(*geom3)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add a symmetry constraint "
                                "because the second selected element is not a line. "
                                "Select a line or an axis instead."));
                return;
            }
            break;
        }
        default:
            break;
    }

    // undo command open
    openCommand(QT_TRANSLATE_NOOP("Command", "Add symmetric constraint"));
    Gui::cmdAppObjectArgs(Obj,
                          "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d,%d))",
                          GeoId1,
                          static_cast<int>(PosId1),
                          GeoId2,
                          static_cast<int>(PosId2),
                          GeoId3,
                          static_cast<int>(PosId3));

    // finish the transaction and update
    commitCommand();

    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
    return;
}

// ======================================================================================

DEF_STD_CMD_A(CmdSketcherConstrainSnellsLaw)

CmdSketcherConstrainSnellsLaw::CmdSketcherConstrainSnellsLaw()
    : Command("Sketcher_ConstrainSnellsLaw")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Refraction Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements based on the refraction law (Snell's Law)");
    sWhatsThis = "Sketcher_ConstrainSnellsLaw";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_SnellsLaw";
    sAccel = "K, W";
    eType = ForEdit;
}

void CmdSketcherConstrainSnellsLaw::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        QString strHelp = QObject::tr("Select two endpoints of lines to act as rays, "
                                      "and an edge representing a boundary. "
                                      "The first selected point corresponds "
                                      "to index n1, second to n2, "
                                      "and the value sets the ratio n2/n1.",
                                      "Constraint_SnellsLaw");

        const char dmbg[] = "Constraint_SnellsLaw";

        QString strError = QObject::tr("Selected objects are not just geometry "
                                       "from one sketch.",
                                       dmbg);

        strError.append(strHelp);
        Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                   QObject::tr("Wrong selection"),
                                   std::move(strError));
    }

    // get the needed lists and objects
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());
    const std::vector<std::string>& SubNames = selection[0].getSubNames();

    if (SubNames.size() != 3) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Number of selected objects is not 3"));
        return;
    }

    int GeoId1, GeoId2, GeoId3;
    Sketcher::PointPos PosId1, PosId2, PosId3;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);
    getIdsFromName(SubNames[2], Obj, GeoId3, PosId3);

    // sink the edge to be the last item
    if (isEdge(GeoId1, PosId1)) {
        std::swap(GeoId1, GeoId2);
        std::swap(PosId1, PosId2);
    }
    if (isEdge(GeoId2, PosId2)) {
        std::swap(GeoId2, GeoId3);
        std::swap(PosId2, PosId3);
    }

    // a bunch of validity checks
    if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Cannot create constraint with external geometry only."));
        return;
    }

    if (!(isVertex(GeoId1, PosId1) && !isSimpleVertex(Obj, GeoId1, PosId1)
          && isVertex(GeoId2, PosId2) && !isSimpleVertex(Obj, GeoId2, PosId2)
          && isEdge(GeoId3, PosId3))) {

        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Incompatible geometry is selected."));
        return;
    }

    const Part::Geometry* geo = Obj->getGeometry(GeoId3);

    // if (geo && isBSplineCurve(*geo)) {
    //     // unsupported until normal to B-spline at any point implemented.
    //     Gui::TranslatedUserWarning(
    //         Obj,
    //         QObject::tr("Wrong selection"),
    //         QObject::tr("SnellsLaw on B-spline edge is currently unsupported."));
    //     return;
    // }

    if (isBsplinePole(geo)) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select an edge that is not a B-spline weight."));
        return;
    }

    double n2divn1 = 0;

    // the essence.
    // Unlike other constraints, we'll ask for a value immediately.
    QDialog dlg(Gui::getMainWindow());
    Ui::InsertDatum ui_Datum;
    ui_Datum.setupUi(&dlg);
    dlg.setWindowTitle(EditDatumDialog::tr("Refractive Index Ratio"));
    ui_Datum.label->setText(EditDatumDialog::tr("Ratio n2/n1:"));
    Base::Quantity init_val;
    init_val.setUnit(Base::Unit());
    init_val.setValue(0.0);

    ui_Datum.labelEdit->setValue(init_val);
    ui_Datum.labelEdit->setParamGrpPath(
        QByteArray("User parameter:BaseApp/History/SketcherRefrIndexRatio"));
    ui_Datum.labelEdit->setEntryName(QByteArray("DatumValue"));
    ui_Datum.labelEdit->setToLastUsedValue();
    ui_Datum.labelEdit->selectNumber();
    ui_Datum.labelEdit->setSingleStep(0.05);
    // Unable to bind, because the constraint does not yet exist

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    ui_Datum.labelEdit->pushToHistory();

    Base::Quantity newQuant = ui_Datum.labelEdit->value();
    n2divn1 = newQuant.getValue();

    // add constraint
    openCommand(QT_TRANSLATE_NOOP("Command", "Add Snell's law constraint"));

    bool safe = addConstraintSafely(Obj, [&]() {
        if (!IsPointAlreadyOnCurve(GeoId2, GeoId1, PosId1, Obj)) {
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Coincident',%d,%d,%d,%d))",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId2,
                                  static_cast<int>(PosId2));
        }

        if (!IsPointAlreadyOnCurve(GeoId3, GeoId1, PosId1, Obj)) {
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId3);
        }

        Gui::cmdAppObjectArgs(
            selection[0].getObject(),
            "addConstraint(Sketcher.Constraint('SnellsLaw',%d,%d,%d,%d,%d,%.12f))",
            GeoId1,
            static_cast<int>(PosId1),
            GeoId2,
            static_cast<int>(PosId2),
            GeoId3,
            n2divn1);

        /*if (allexternal || constraintCreationMode==Reference) { // it is a constraint on a
        external line, make it non-driving const std::vector<Sketcher::Constraint *> &ConStr =
        Obj->Constraints.getValues();

            Gui::cmdAppObjectArgs(selection[0].getObject(),"setDriving(%i,%s)",
                ConStr.size()-1,"False");
        }*/
    });

    if (!safe) {
        abortCommand();
        return;
    }
    commitCommand();
    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
}

bool CmdSketcherConstrainSnellsLaw::isActive()
{
    return isCreateConstraintActive(getActiveGuiDocument());
}

// ======================================================================================

DEF_STD_CMD_A(CmdSketcherConstrainGroup)

CmdSketcherConstrainGroup::CmdSketcherConstrainGroup()
    : Command("Sketcher_ConstrainGroup")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Group Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected geometries together as a single entity."
        "The position and size of the grouped geometries can be defined by constraining the construction line that is generated."
        "Constraints applied to grouped edges are ignored as long as the Group constraint is here.");
    sWhatsThis = "Sketcher_ConstrainGroup";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Group";
    sAccel = "K, G";
    eType = ForEdit;
}

void CmdSketcherConstrainGroup::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        const char dmbg[] = "Constraint_Group";

        QString strError = QObject::tr("Selected objects are not just geometry "
                                       "from one sketch.",
                                       dmbg);

        Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                   QObject::tr("Wrong selection"),
                                   std::move(strError));
    }

    // get the needed lists and objects
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());
    const std::vector<std::string>& SubNames = selection[0].getSubNames();

    if (SubNames.empty()) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("No geometries selected"));
        return;
    }

    std::vector<Sketcher::GeoElementId> elts;

    for (auto& subName : SubNames) {
        int geoId;
        Sketcher::PointPos posId;
        getIdsFromName(subName, Obj, geoId, posId);

        bool alreadyAdded = std::any_of(elts.begin(), elts.end(),
                                [geoId](const Sketcher::GeoElementId& elem) {
                                    return elem.GeoId == geoId;
                                });

        if (geoId < 0 || alreadyAdded || Obj->getGeometryFacade(geoId)->isInternalAligned()) {
            continue;
        }

        elts.push_back(Sketcher::GeoElementId(geoId, Sketcher::PointPos::none));
    }

    if (elts.size() < 2) {
        Base::Console().warning("Cannot create group : minimum 2 geometries must be selected.\n");
        return;
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Add Group constraint"));

    if (!addListConstraint(Obj, elts, "Group")) {
        abortCommand();
        return;
    }

    tryAutoRecompute(Obj);

    commitCommand();

    getSelection().clearSelection();
}

/**
 * @brief Escapes a string for safe embedding within a single-quoted Python string literal.
 *
 * This function handles backslashes and single quotes.
 *
 * @param input The raw string to escape.
 * @return A new string with special characters escaped.
 */
std::string SketcherGui::escapeForPython(const std::string& input)
{
    std::string result;
    // Pre-allocating can be a small optimization if strings are long
    result.reserve(input.length());

    for (char c : input) {
        if (c == '\\') {
            result += "\\\\";
        } else if (c == '\'') {
            result += "\\'";
        } else {
            result += c;
        }
    }
    return result;
}

bool SketcherGui::addListConstraint(Sketcher::SketchObject* Obj,
                       std::vector<Sketcher::GeoElementId>& elts,
                       const std::string& constraintType,
                       Base::Vector2d frame_p1,
                       Base::Vector2d frame_p2,
                       bool isTextHeight,
                       const std::string& text,
                       const std::string& font)
{

    std::vector<int> geoIdsWithInternalGeos;
    // The lambda defines the condition for REMOVAL.
    // It returns 'true' if an element should be erased.
    auto new_end = std::remove_if(elts.begin(), elts.end(),
        [&](const Sketcher::GeoElementId& element) -> bool {
            int geoId = element.GeoId;

            // Condition 1: Check for invalid or already-aligned geometries.
            // If true, this element should be removed.
            if (geoId < 0 || Obj->getGeometryFacade(geoId)->isInternalAligned()) {
                return true; // Mark for removal
            }

            // Condition 2: Check for internal geometries that need cleanup later.
            // This does not mark the element for removal, but collects its ID.
            const Part::Geometry* geo = Obj->getGeometry(geoId);
            if (Obj->hasInternalGeometry(geo)) {
                // Collect the ID for later processing.
                geoIdsWithInternalGeos.push_back(geoId);
            }

            // If we reached here, the element is valid and should be kept.
            return false; // Do not remove
    });

    // Actually erase the elements that were moved to the end.
    elts.erase(new_end, elts.end());

    if (elts.size() < 2) {
        Base::Console().warning("Cannot create %s constraint: minimum 2 geometries.\n", constraintType.c_str());
        return false;
    }

    if ((frame_p1 - frame_p2).Length() < Precision::Confusion()) {
        // --- 1. Calculate Bounding Box ---
        Bnd_Box totalBBox;
        for (const auto& element : elts) {
            const Part::Geometry* geo = Obj->getGeometry(element.GeoId);
            if (geo) {
                BRepBndLib::Add(geo->toShape(), totalBBox, false);
            }
        }

        if (!totalBBox.HasFinitePart()) {
            Base::Console().warning("Cannot create %s constraint: bounding box is infinite\n", constraintType.c_str());
            return false;
        }

        gp_Pnt min_pnt = totalBBox.CornerMin();
        gp_Pnt max_pnt = totalBBox.CornerMax();

        // --- 2. Define and create the Construction Line "Frame" ---
        frame_p1 = Base::Vector2d(min_pnt.X(), min_pnt.Y());
        frame_p2 = Base::Vector2d(min_pnt.X(), max_pnt.Y());
    }

    Gui::cmdAppObjectArgs(Obj,
                          "addGeometry(Part.LineSegment(App.Vector(%f,%f,0), App.Vector(%f,%f,0)), True)",
                          frame_p1.x, frame_p1.y, frame_p2.x, frame_p2.y);

    int frameGeoId = Obj->getHighestCurveIndex();

    // --- 3. Prepend the Frame to the Element List ---
    elts.insert(elts.begin(), Sketcher::GeoElementId(frameGeoId, Sketcher::PointPos::none));

    // --- 4. Create the Python list of elements as a string ---
    std::stringstream elements_list_ss;
    elements_list_ss << "[";
    if (!elts.empty()) {
        for (size_t i = 0; i < elts.size() - 1; ++i) {
            elements_list_ss << elts[i].GeoId << ", " << elts[i].posIdAsInt() << ", ";
        }
        elements_list_ss << elts.back().GeoId << ", " << elts.back().posIdAsInt();
    }
    elements_list_ss << "]";
    std::string elements_list_string = elements_list_ss.str();

    // --- 5. Add the appropriate constraint via Python command ---
    if (constraintType == "Group") {
        Gui::cmdAppObjectArgs(
            Obj,
            "addConstraint(Sketcher.Constraint('Group', %s))",
            elements_list_string.c_str());
    }
    else if (constraintType == "Text") {
        std::string escaped_text = escapeForPython(text);
        std::string escaped_font = escapeForPython(font);
        Gui::cmdAppObjectArgs(
            Obj,
            "addConstraint(Sketcher.Constraint('Text', %s, '%s', '%s', %s))",
            elements_list_string.c_str(),
            escaped_text.c_str(),
            escaped_font.c_str(),
            isTextHeight ? "True" : "False");
    }
    else {
        Base::Console().error("Unsupported list constraint type: %s\n", constraintType.c_str());
        return false;
    }

    // We remove the internal alignment of the geometries that were grouped.
    std::sort(geoIdsWithInternalGeos.begin(), geoIdsWithInternalGeos.end(), std::greater<>());
    for (auto& geoId : geoIdsWithInternalGeos) {
        Obj->deleteUnusedInternalGeometry(geoId);
    }

    return true;
}

bool CmdSketcherConstrainGroup::isActive()
{
    return isCreateConstraintActive(getActiveGuiDocument());
}

// ======================================================================================
DEF_STD_CMD_A(CmdSketcherChangeDimensionConstraint)

CmdSketcherChangeDimensionConstraint::CmdSketcherChangeDimensionConstraint()
    : Command("Sketcher_ChangeDimensionConstraint")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Edit Value");
    sToolTipText = QT_TR_NOOP("Edits the value of a dimensional constraint");
    sWhatsThis = "Sketcher_ChangeDimensionConstraint";
    sStatusTip = sToolTipText;
    eType = ForEdit;
}

void CmdSketcherChangeDimensionConstraint::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    auto getDimConstraint = []() {
        std::vector<Gui::SelectionObject> selection{getSelection().getSelectionEx()};
        if (selection.size() != 1 || selection[0].getSubNames().size() != 1) {
            throw Base::RuntimeError();
        }

        if (auto sketch = dynamic_cast<Sketcher::SketchObject*>(selection[0].getObject())) {
            std::string subName = selection[0].getSubNames().at(0);
            if (subName.size() > 10 && subName.substr(0, 10) == "Constraint") {
                int ConstrId = Sketcher::PropertyConstraintList::getIndexFromConstraintName(subName);
                return std::make_tuple(sketch, ConstrId);
            }
        }

        throw Base::RuntimeError();
    };

    try {
        auto value = getDimConstraint();
        int tid = openActiveDocumentCommand(QT_TRANSLATE_NOOP("Command", "Modify sketch constraints"));
        EditDatumDialog(tid, std::get<0>(value), std::get<1>(value)).exec(false);
    }
    catch (const Base::RuntimeError&) {
        Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                   QObject::tr("Wrong selection"),
                                   QObject::tr("Select one dimensional constraint from the sketch."));
    }
}

bool CmdSketcherChangeDimensionConstraint::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// ======================================================================================
/*** Creation Mode / Toggle to or from Reference ***/
DEF_STD_CMD_AU(CmdSketcherToggleDrivingConstraint)

CmdSketcherToggleDrivingConstraint::CmdSketcherToggleDrivingConstraint()
    : Command("Sketcher_ToggleDrivingConstraint")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Toggle Driving/Reference Constraints");
    sToolTipText = QT_TR_NOOP("Toggles between driving and reference mode of the selected constraints and commands");
    sWhatsThis = "Sketcher_ToggleDrivingConstraint";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_ToggleConstraint";
    sAccel = "K, X";
    eType = ForEdit;

    // list of toggle driving constraint commands
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainLock");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainDistance");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainDistanceX");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainDistanceY");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainRadius");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainDiameter");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainRadiam");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainAngle");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_CompConstrainRadDia");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_Dimension");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_CompDimensionTools");
    rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ToggleDrivingConstraint");
    // rcCmdMgr.addCommandMode("ToggleDrivingConstraint", "Sketcher_ConstrainSnellsLaw");
}

void CmdSketcherToggleDrivingConstraint::updateAction(int mode)
{
    auto act = getAction();
    if (act) {
        switch (static_cast<ConstraintCreationMode>(mode)) {
        case ConstraintCreationMode::Driving:
            act->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_ToggleConstraint"));
            break;
        case ConstraintCreationMode::Reference:
            act->setIcon(Gui::BitmapFactory().iconFromTheme("Sketcher_ToggleConstraint_Driven"));
            break;
        }
    }
}

void CmdSketcherToggleDrivingConstraint::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    bool modeChange = true;

    std::vector<Gui::SelectionObject> selection;

    if (Gui::Selection().countObjectsOfType<Sketcher::SketchObject>() > 0) {
        // Now we check whether we have a constraint selected or not.

        // get the selection
        selection = getSelection().getSelectionEx();

        // only one sketch with its subelements are allowed to be selected
        if (selection.size() != 1
            || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
            Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select constraints from the sketch."));
            return;
        }

        Sketcher::SketchObject* Obj =
            static_cast<Sketcher::SketchObject*>(selection[0].getObject());

        // get the needed lists and objects
        const std::vector<std::string>& SubNames = selection[0].getSubNames();
        if (SubNames.empty()) {
            Gui::TranslatedUserWarning(Obj,
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select constraints from the sketch."));
            return;
        }

        for (auto& subname : SubNames) {
            // see if we have constraints, if we do it is not a mode change, but a toggle.
            if (subname.size() > 10 && subname.substr(0, 10) == "Constraint") {
                modeChange = false;
            }
        }
    }

    if (modeChange) {
        // Here starts the code for mode change
        Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

        if (constraintCreationMode == Driving) {
            constraintCreationMode = Reference;
        }
        else {
            constraintCreationMode = Driving;
        }

        rcCmdMgr.updateCommands("ToggleDrivingConstraint",
                                static_cast<int>(constraintCreationMode));
    }
    else// toggle the selected constraint(s)
    {
        Sketcher::SketchObject* Obj =
            static_cast<Sketcher::SketchObject*>(selection[0].getObject());

        // get the needed lists and objects
        const std::vector<std::string>& SubNames = selection[0].getSubNames();
        if (SubNames.empty()) {
            Gui::TranslatedUserWarning(Obj,
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select constraints from the sketch."));
            return;
        }

        // undo command open
        openCommand(QT_TRANSLATE_NOOP("Command", "Toggle constraint to driving/reference"));

        int successful = SubNames.size();
        // go through the selected subelements
        for (auto& subname : SubNames) {
            // only handle constraints
            if (subname.size() > 10 && subname.substr(0, 10) == "Constraint") {
                int ConstrId = Sketcher::PropertyConstraintList::getIndexFromConstraintName(subname);
                try {
                    // issue the actual commands to toggle
                    Gui::cmdAppObjectArgs(selection[0].getObject(), "toggleDriving(%d)", ConstrId);
                }
                catch (const Base::Exception&) {
                    successful--;
                }
            }
        }

        if (successful > 0) {
            commitCommand();
        }
        else {
            abortCommand();
        }

        tryAutoRecompute(Obj);

        // clear the selection (convenience)
        getSelection().clearSelection();
    }
}

bool CmdSketcherToggleDrivingConstraint::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

DEF_STD_CMD_A(CmdSketcherToggleActiveConstraint)

CmdSketcherToggleActiveConstraint::CmdSketcherToggleActiveConstraint()
    : Command("Sketcher_ToggleActiveConstraint")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Toggle Constraints");
    sToolTipText = QT_TR_NOOP("Toggles the state of the selected constraints");
    sWhatsThis = "Sketcher_ToggleActiveConstraint";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_ToggleActiveConstraint";
    sAccel = "K, Z";
    eType = ForEdit;
}

void CmdSketcherToggleActiveConstraint::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    std::vector<Gui::SelectionObject> selection;

    if (Gui::Selection().countObjectsOfType<Sketcher::SketchObject>() > 0) {
        // Now we check whether we have a constraint selected or not.

        // get the selection
        selection = getSelection().getSelectionEx();

        // only one sketch with its subelements are allowed to be selected
        if (selection.size() != 1
            || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
            Gui::TranslatedUserWarning(getActiveGuiDocument()->getDocument(),
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select constraints from the sketch."));
            return;
        }

        Sketcher::SketchObject* Obj =
            static_cast<Sketcher::SketchObject*>(selection[0].getObject());

        // get the needed lists and objects
        const std::vector<std::string>& SubNames = selection[0].getSubNames();
        if (SubNames.empty()) {
            Gui::TranslatedUserWarning(Obj,
                                       QObject::tr("Wrong selection"),
                                       QObject::tr("Select constraints from the sketch."));
            return;
        }

        // undo command open
        openCommand(QT_TRANSLATE_NOOP("Command", "Activate/Deactivate constraints"));

        int successful = SubNames.size();

        for (auto& subname : SubNames) {

            if (subname.size() > 10 && subname.substr(0, 10) == "Constraint") {
                int ConstrId = Sketcher::PropertyConstraintList::getIndexFromConstraintName(subname);
                try {
                    // issue the actual commands to toggle
                    Gui::cmdAppObjectArgs(selection[0].getObject(), "toggleActive(%d)", ConstrId);
                }
                catch (const Base::Exception&) {
                    successful--;
                }
            }
        }

        if (successful > 0) {
            commitCommand();
        }
        else {
            abortCommand();
        }

        tryAutoRecompute(Obj);

        // clear the selection (convenience)
        getSelection().clearSelection();
    }
}

bool CmdSketcherToggleActiveConstraint::isActive()
{
    return isCreateConstraintActive(getActiveGuiDocument());
}

void CreateSketcherCommandsConstraints()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    addPositionConstraintCommands(rcCmdMgr);
    rcCmdMgr.addCommand(new CmdSketcherCompHorizontalVertical());
    addDimensionCommand(rcCmdMgr);
    rcCmdMgr.addCommand(new CmdSketcherConstrainParallel());
    rcCmdMgr.addCommand(new CmdSketcherConstrainPerpendicular());
    rcCmdMgr.addCommand(new CmdSketcherConstrainTangent());
    addDimensionalConstraintCommands(rcCmdMgr);
    rcCmdMgr.addCommand(new CmdSketcherConstrainEqual());
    rcCmdMgr.addCommand(new CmdSketcherConstrainSymmetric());
    rcCmdMgr.addCommand(new CmdSketcherConstrainSnellsLaw());
    rcCmdMgr.addCommand(new CmdSketcherConstrainGroup());
    rcCmdMgr.addCommand(new CmdSketcherChangeDimensionConstraint());
    rcCmdMgr.addCommand(new CmdSketcherToggleDrivingConstraint());
    rcCmdMgr.addCommand(new CmdSketcherToggleActiveConstraint());
    rcCmdMgr.addCommand(new CmdSketcherCompDimensionTools());
    rcCmdMgr.addCommand(new CmdSketcherCompConstrainTools());
    rcCmdMgr.addCommand(new CmdSketcherCompToggleConstraints());
}
// clang-format on
