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
    addRelationConstraintCommands(rcCmdMgr);
    addTangencyConstraintCommands(rcCmdMgr);
    addDimensionalConstraintCommands(rcCmdMgr);
    rcCmdMgr.addCommand(new CmdSketcherConstrainGroup());
    rcCmdMgr.addCommand(new CmdSketcherChangeDimensionConstraint());
    rcCmdMgr.addCommand(new CmdSketcherToggleDrivingConstraint());
    rcCmdMgr.addCommand(new CmdSketcherToggleActiveConstraint());
    rcCmdMgr.addCommand(new CmdSketcherCompDimensionTools());
    rcCmdMgr.addCommand(new CmdSketcherCompConstrainTools());
    rcCmdMgr.addCommand(new CmdSketcherCompToggleConstraints());
}
// clang-format on
