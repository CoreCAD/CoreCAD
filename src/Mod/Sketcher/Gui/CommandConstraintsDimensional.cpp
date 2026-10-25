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

// clang-format off
using namespace std;
using namespace SketcherGui;
using namespace Sketcher;

// ======================================================================================

class CmdSketcherConstrainDistance : public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainDistance();
    ~CmdSketcherConstrainDistance() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainDistance";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainDistance::CmdSketcherConstrainDistance()
    : CmdSketcherConstraint("Sketcher_ConstrainDistance")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Distance Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the vertical distance between two points, or from a point to the origin if one is selected");
    sWhatsThis = "Sketcher_ConstrainDistance";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Length";
    sAccel = "K, D";
    eType = ForEdit;

    allowedSelSequences = {{SelVertexOrRoot, SelVertexOrRoot},
                           {SelEdge},
                           {SelExternalEdge},
                           {SelVertex, SelEdgeOrAxis},
                           {SelRoot, SelEdge},
                           {SelVertexOrRoot, SelExternalEdge},
                           {SelEdge, SelEdge}};
}

void CmdSketcherConstrainDistance::activated(int iMsg)
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
            Gui::TranslatedUserWarning(getActiveGuiDocument(),
                QObject::tr("Wrong selection"),
                QObject::tr("Select vertices from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty() || SubNames.size() > 2) {
        Gui::TranslatedUserWarning(Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select exactly one line or one point and one line "
                "or two points from the sketch."));
        return;
    }

    int GeoId1, GeoId2 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1, PosId2 = Sketcher::PointPos::none;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    if (SubNames.size() == 2) {
        getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);
    }

    bool arebothpointsorsegmentsfixed = areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

    if (isVertex(GeoId1, PosId1)
        && (GeoId2 == Sketcher::GeoEnum::VAxis || GeoId2 == Sketcher::GeoEnum::HAxis)) {
        std::swap(GeoId1, GeoId2);
        std::swap(PosId1, PosId2);
    }

    if ((isVertex(GeoId1, PosId1) || GeoId1 == Sketcher::GeoEnum::VAxis
        || GeoId1 == Sketcher::GeoEnum::HAxis)
        && isVertex(GeoId2, PosId2)) {// point to point distance

        Base::Vector3d pnt2 = Obj->getPoint(GeoId2, PosId2);

        if (GeoId1 == Sketcher::GeoEnum::HAxis && PosId1 == Sketcher::PointPos::none) {
            PosId1 = Sketcher::PointPos::start;

            openCommand(
                QT_TRANSLATE_NOOP("Command", "Add distance from horizontal axis constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('DistanceY',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                pnt2.y);
        }
        else if (GeoId1 == Sketcher::GeoEnum::VAxis && PosId1 == Sketcher::PointPos::none) {
            PosId1 = Sketcher::PointPos::start;

            openCommand(QT_TRANSLATE_NOOP("Command", "Add distance from vertical axis constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('DistanceX',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                pnt2.x);
        }
        else {
            Base::Vector3d pnt1 = Obj->getPoint(GeoId1, PosId1);

            openCommand(QT_TRANSLATE_NOOP("Command", "Add point to point distance constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                (pnt2 - pnt1).Length());
        }

        if (arebothpointsorsegmentsfixed || constraintCreationMode == Reference) {
            // it is a constraint on a external line, make it non-driving
            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "setDriving(%d,%s)",
                ConStr.size() - 1,
                "False");
            finishDatumConstraint(this, Obj, false);
        }
        else {
            finishDatumConstraint(this, Obj, true);
        }
        return;
    }
    else if ((isVertex(GeoId1, PosId1) && isEdge(GeoId2, PosId2))
        || (isEdge(GeoId1, PosId1) && isVertex(GeoId2, PosId2))) {// point to line distance
        if (isVertex(GeoId2, PosId2)) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
        }
        Base::Vector3d pnt = Obj->getPoint(GeoId1, PosId1);
        const Part::Geometry* geom = Obj->getGeometry(GeoId2);

        if (isLineSegment(*geom)) {
            auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
            Base::Vector3d pnt1 = lineSeg->getStartPoint();
            Base::Vector3d pnt2 = lineSeg->getEndPoint();
            Base::Vector3d d = pnt2 - pnt1;
            double ActDist =
                std::abs(-pnt.x * d.y + pnt.y * d.x + pnt1.x * pnt2.y - pnt2.x * pnt1.y)
                / d.Length();

            openCommand(QT_TRANSLATE_NOOP("Command", "Add point to line distance constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                ActDist);

            if (arebothpointsorsegmentsfixed
                || constraintCreationMode
                == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                    "setDriving(%d,%s)",
                    ConStr.size() - 1,
                    "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
        else if (isCircleOrArc(*geom)) {    // point to circle distance
            auto [radius, center] = getRadiusCenterCircleArc(geom);
            Base::Vector3d d = center - pnt;
            double ActDist = std::abs(d.Length() - radius);

            openCommand(QT_TRANSLATE_NOOP("Command", "Add point to circle distance constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                ActDist);

            if (arebothpointsorsegmentsfixed
                || constraintCreationMode
                == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                    "setDriving(%d,%s)",
                    ConStr.size() - 1,
                    "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
    }
    else if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2)) {
        const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
        const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

        if(isCircleOrArc(*geom1) && isCircleOrArc(*geom2)) {

            auto [radius1, center1] = getRadiusCenterCircleArc(geom1);
            auto [radius2, center2] = getRadiusCenterCircleArc(geom2);

            double ActDist = 0.0;

            Base::Vector3d intercenter = center1 - center2;
            double intercenterdistance = intercenter.Length();

            if (intercenterdistance >= radius1 && intercenterdistance >= radius2) {

                ActDist = intercenterdistance - radius1 - radius2;
            }
            else {
                double bigradius = std::max(radius1, radius2);
                double smallradius = std::min(radius1, radius2);

                ActDist = bigradius - smallradius - intercenterdistance;
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add circle to circle distance constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('Distance',%d,%d,%f))",
                                  GeoId1,
                                  GeoId2,
                                  ActDist);

            if (arebothpointsorsegmentsfixed
                || constraintCreationMode
                    == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "setDriving(%d,%s)",
                                      ConStr.size() - 1,
                                      "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
        else if ((isCircleOrArc(*geom1) && isLineSegment(*geom2))
            || (isLineSegment(*geom1) && isCircleOrArc(*geom2))) {// circle to line distance

            if (isLineSegment(*geom1)) {
                std::swap(geom1, geom2);// Assume circle is first
                std::swap(GeoId1, GeoId2);
            }

            auto [radius, center] = getRadiusCenterCircleArc(geom1);

            auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom2);
            Base::Vector3d pnt1 = lineSeg->getStartPoint();
            Base::Vector3d pnt2 = lineSeg->getEndPoint();
            Base::Vector3d d = pnt2 - pnt1;
            double ActDist =
                std::abs(-center.x * d.y + center.y * d.x + pnt1.x * pnt2.y - pnt2.x * pnt1.y)
                / d.Length()
                - radius;

            openCommand(QT_TRANSLATE_NOOP("Command", "Add circle to line distance constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%f)) ",
                GeoId1,
                GeoId2,
                ActDist);

            if (arebothpointsorsegmentsfixed
                || constraintCreationMode
                == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                    "setDriving(%i,%s)",
                    ConStr.size() - 1,
                    "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
        else {
            Gui::TranslatedNotification(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Cannot add a length constraint on this selection!"));
            return;
        }
    }
    else if (isEdge(GeoId1, PosId1)) {// line length
        if (GeoId1 < 0 && GeoId1 >= Sketcher::GeoEnum::VAxis) {
            Gui::TranslatedUserWarning(Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Cannot add a length constraint on an axis!"));
            return;
        }

        arebothpointsorsegmentsfixed = isPointOrSegmentFixed(Obj, GeoId1);

        const Part::Geometry* geom = Obj->getGeometry(GeoId1);

        if (isLineSegment(*geom)) {
            auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
            double ActLength = (lineSeg->getEndPoint() - lineSeg->getStartPoint()).Length();

            openCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%f))",
                GeoId1,
                ActLength);

            // it is a constraint on a external line, make it non-driving
            if (arebothpointsorsegmentsfixed || GeoId1 <= Sketcher::GeoEnum::RefExt
                || constraintCreationMode == Reference) {
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                    "setDriving(%d,%s)",
                    ConStr.size() - 1,
                    "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
        else if (isArcOfCircle(*geom)) {
            auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
            double ActLength = arc->getAngle(false) * arc->getRadius();

            openCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('Distance',%d,%f))",
                GeoId1,
                ActLength);

            // it is a constraint on a external line, make it non-driving
            if (arebothpointsorsegmentsfixed || GeoId1 <= Sketcher::GeoEnum::RefExt
                || constraintCreationMode == Reference) {
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                    "setDriving(%d,%s)",
                    ConStr.size() - 1,
                    "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
    }

    Gui::TranslatedUserWarning(Obj,
        QObject::tr("Wrong selection"),
        QObject::tr("Select exactly one line or one point and one line or "
            "two points or two circles from the sketch."));
    return;
}

void CmdSketcherConstrainDistance::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1 = Sketcher::PointPos::none, PosId2 = Sketcher::PointPos::none;

    bool arebothpointsorsegmentsfixed = areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

    switch (seqIndex) {
    case 0:// {SelVertexOrRoot, SelVertexOrRoot}
    {
        GeoId1 = selSeq.at(0).GeoId;
        GeoId2 = selSeq.at(1).GeoId;
        PosId1 = selSeq.at(0).PosId;
        PosId2 = selSeq.at(1).PosId;

        Base::Vector3d pnt2 = Obj->getPoint(GeoId2, PosId2);

        if (GeoId1 == Sketcher::GeoEnum::HAxis && PosId1 == Sketcher::PointPos::none) {
            PosId1 = Sketcher::PointPos::start;

            openCommand(
                QT_TRANSLATE_NOOP("Command", "Add distance from horizontal axis constraint"));
            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('DistanceY',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                pnt2.y);
        }
        else if (GeoId1 == Sketcher::GeoEnum::VAxis && PosId1 == Sketcher::PointPos::none) {
            PosId1 = Sketcher::PointPos::start;

            openCommand(
                QT_TRANSLATE_NOOP("Command", "Add distance from vertical axis constraint"));
            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('DistanceX',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                pnt2.x);
        }
        else {
            Base::Vector3d pnt1 = Obj->getPoint(GeoId1, PosId1);

            openCommand(QT_TRANSLATE_NOOP("Command", "Add point to point distance constraint"));
            Gui::cmdAppObjectArgs(
                Obj,
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2),
                (pnt2 - pnt1).Length());
        }

        if (arebothpointsorsegmentsfixed || constraintCreationMode == Reference) {
            // it is a constraint on a external line, make it non-driving
            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
            finishDatumConstraint(this, Obj, false);
        }
        else {
            finishDatumConstraint(this, Obj, true);
        }

        return;
    }
    case 1:// {SelEdge}
    case 2:// {SelExternalEdge}
    {
        GeoId1 = selSeq.at(0).GeoId;

        arebothpointsorsegmentsfixed = isPointOrSegmentFixed(Obj, GeoId1);

        const Part::Geometry* geom = Obj->getGeometry(GeoId1);

        if (isLineSegment(*geom) || isArcOfCircle(*geom)) {
            double ActLength = 0.;
            if (isLineSegment(*geom)) {
                auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
                ActLength = (lineSeg->getEndPoint() - lineSeg->getStartPoint()).Length();
            }
            else if (isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                ActLength = arc->getAngle(false) * arc->getRadius();
            }

            openCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            Gui::cmdAppObjectArgs(Obj,
                "addConstraint(Sketcher.Constraint('Distance',%d,%f))",
                GeoId1,
                ActLength);

            if (arebothpointsorsegmentsfixed || GeoId1 <= Sketcher::GeoEnum::RefExt
                || constraintCreationMode == Reference) {
                // it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }
        }
        else if (isCircle(*geom)) {
            // allow this selection but do nothing as it needs 2 circles or 1 circle and 1 line
        }
        else {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("This constraint does not make sense for non-linear curves."));
        }

        return;
    }
    case 3:// {SelVertex, SelEdgeOrAxis}
    case 4:// {SelRoot, SelEdge}
    case 5:// {SelVertexOrRoot, SelExternalEdge}
    {
        GeoId1 = selSeq.at(0).GeoId;
        GeoId2 = selSeq.at(1).GeoId;
        PosId1 = selSeq.at(0).PosId;

        Base::Vector3d pnt = Obj->getPoint(GeoId1, PosId1);
        const Part::Geometry* geom = Obj->getGeometry(GeoId2);

        if (isLineSegment(*geom)) {
            auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
            Base::Vector3d pnt1 = lineSeg->getStartPoint();
            Base::Vector3d pnt2 = lineSeg->getEndPoint();
            Base::Vector3d d = pnt2 - pnt1;
            double ActDist =
                std::abs(-pnt.x * d.y + pnt.y * d.x + pnt1.x * pnt2.y - pnt2.x * pnt1.y)
                / d.Length();

            openCommand(QT_TRANSLATE_NOOP("Command", "Add point to line distance constraint"));
            Gui::cmdAppObjectArgs(Obj,
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%f))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                ActDist);

            if (arebothpointsorsegmentsfixed || constraintCreationMode == Reference) {
                // it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }
        }

        return;
    }
    case 6:// {SelEdge, SelEdge}
    {
        GeoId1 = selSeq.at(0).GeoId;
        GeoId2 = selSeq.at(1).GeoId;
        const Part::Geometry* geom1 = Obj->getGeometry(GeoId1);
        const Part::Geometry* geom2 = Obj->getGeometry(GeoId2);

        if (isCircle(*geom1) && isCircle(*geom2)) {// circle to circle distance
            auto circleSeg1 = static_cast<const Part::GeomCircle*>(geom1);
            double radius1 = circleSeg1->getRadius();
            Base::Vector3d center1 = circleSeg1->getCenter();

            auto circleSeg2 = static_cast<const Part::GeomCircle*>(geom2);
            double radius2 = circleSeg2->getRadius();
            Base::Vector3d center2 = circleSeg2->getCenter();

            double ActDist = 0.;

            Base::Vector3d intercenter = center1 - center2;
            double intercenterdistance = intercenter.Length();

            if (intercenterdistance >= radius1 && intercenterdistance >= radius2) {

                ActDist = intercenterdistance - radius1 - radius2;
            }
            else {
                double bigradius = std::max(radius1, radius2);
                double smallradius = std::min(radius1, radius2);

                ActDist = bigradius - smallradius - intercenterdistance;
            }

            openCommand(
                QT_TRANSLATE_NOOP("Command", "Add circle to circle distance constraint"));
            Gui::cmdAppObjectArgs(Obj,
                "addConstraint(Sketcher.Constraint('Distance',%d,%d,%f))",
                GeoId1,
                GeoId2,
                ActDist);

            if (arebothpointsorsegmentsfixed
                || constraintCreationMode
                == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
        else {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select exactly one line or one point and one line or two points "
                    "or two circles from the sketch."));
        }
    }
    default:
        break;
    }
}

void CmdSketcherConstrainDistance::updateAction(int mode)
{
    switch (mode) {
    case Reference:
        if (getAction()) {
            getAction()->setIcon(
                Gui::BitmapFactory().iconFromTheme("Constraint_Length_Driven"));
        }
        break;
    case Driving:
        if (getAction()) {
            getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Length"));
        }
        break;
    }
}

// ======================================================================================

class CmdSketcherConstrainAxisDistance: public CmdSketcherConstraint
{
public:
    explicit CmdSketcherConstrainAxisDistance(bool vertical);
    ~CmdSketcherConstrainAxisDistance() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return vertical ? "CmdSketcherConstrainDistanceY" : "CmdSketcherConstrainDistanceX";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;

private:
    double along(const Base::Vector3d& pnt) const
    {
        return vertical ? pnt.y : pnt.x;
    }
    const char* typeName() const
    {
        return vertical ? "DistanceY" : "DistanceX";
    }
    const char* pointToPointCommand() const
    {
        return vertical
            ? QT_TRANSLATE_NOOP("Command", "Add point to point vertical distance constraint")
            : QT_TRANSLATE_NOOP("Command", "Add point to point horizontal distance constraint");
    }
    void addPointToPoint(Sketcher::SketchObject* Obj,
                         int GeoId1,
                         Sketcher::PointPos PosId1,
                         int GeoId2,
                         Sketcher::PointPos PosId2,
                         bool fixed);
    void finishAdded(Sketcher::SketchObject* Obj, bool fixed);

    bool vertical;
};

CmdSketcherConstrainAxisDistance::CmdSketcherConstrainAxisDistance(bool vertical)
    : CmdSketcherConstraint(vertical ? "Sketcher_ConstrainDistanceY"
                                     : "Sketcher_ConstrainDistanceX")
    , vertical(vertical)
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    if (vertical) {
        sMenuText = QT_TRANSLATE_NOOP("CmdSketcherConstrainDistanceY", "Vertical Dimension");
        sToolTipText = QT_TRANSLATE_NOOP("CmdSketcherConstrainDistanceY", "Constrains the vertical distance between two points, or from a point to the origin if only one is selected");
        sWhatsThis = "Sketcher_ConstrainDistanceY";
        sPixmap = "Constraint_VerticalDistance";
        sAccel = "I";
    }
    else {
        sMenuText = QT_TRANSLATE_NOOP("CmdSketcherConstrainDistanceX", "Horizontal Dimension");
        sToolTipText = QT_TRANSLATE_NOOP("CmdSketcherConstrainDistanceX", "Constrains the horizontal distance between two points, or from a point to the origin if only one is selected");
        sWhatsThis = "Sketcher_ConstrainDistanceX";
        sPixmap = "Constraint_HorizontalDistance";
        sAccel = "L";
    }
    sStatusTip = sToolTipText;
    eType = ForEdit;

    // Can't do single vertex because its a prefix for 2 vertices
    allowedSelSequences = {{SelVertexOrRoot, SelVertexOrRoot},
                           {SelEdge},
                           {SelExternalEdge}};
}

void CmdSketcherConstrainAxisDistance::activated(int iMsg)
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
                                       QObject::tr("Select the right things from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty() || SubNames.size() > 2) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select exactly one line or up to two points from the sketch."));
        return;
    }

    int GeoId1, GeoId2 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1, PosId2 = Sketcher::PointPos::none;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    if (SubNames.size() == 2) {
        getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);
    }

    bool arebothpointsorsegmentsfixed = areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

    if (GeoId2 == Sketcher::GeoEnum::HAxis || GeoId2 == Sketcher::GeoEnum::VAxis) {
        std::swap(GeoId1, GeoId2);
        std::swap(PosId1, PosId2);
    }

    // the axis along the measured direction is rejected; the other one stands for the origin
    const int alongAxis = vertical ? Sketcher::GeoEnum::VAxis : Sketcher::GeoEnum::HAxis;
    const int acrossAxis = vertical ? Sketcher::GeoEnum::HAxis : Sketcher::GeoEnum::VAxis;
    if (GeoId1 == alongAxis && PosId1 == Sketcher::PointPos::none) {
        GeoId1 = GeoEnum::GeoUndef;
    }
    else if (GeoId1 == acrossAxis && PosId1 == Sketcher::PointPos::none) {
        GeoId1 = Sketcher::GeoEnum::HAxis;
        PosId1 = Sketcher::PointPos::start;
    }

    if (isEdge(GeoId1, PosId1) && GeoId2 == GeoEnum::GeoUndef) {
        if (GeoId1 < 0 && GeoId1 >= Sketcher::GeoEnum::VAxis) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                vertical ? QObject::tr("Cannot add a vertical length constraint on an axis!")
                         : QObject::tr("Cannot add a horizontal length constraint on an axis!"));
            return;
        }

        arebothpointsorsegmentsfixed = isPointOrSegmentFixed(Obj, GeoId1);

        const Part::Geometry* geom = Obj->getGeometry(GeoId1);

        if (isLineSegment(*geom)) {
            // convert to as if two endpoints of the line have been selected
            PosId1 = Sketcher::PointPos::start;
            GeoId2 = GeoId1;
            PosId2 = Sketcher::PointPos::end;
        }
    }
    if (isVertex(GeoId1, PosId1) && isVertex(GeoId2, PosId2)) {
        addPointToPoint(Obj, GeoId1, PosId1, GeoId2, PosId2, arebothpointsorsegmentsfixed);
        return;
    }
    else if (isVertex(GeoId1, PosId1) && GeoId2 == GeoEnum::GeoUndef) {
        // point on a fixed coordinate
        if (GeoId1 < 0 && GeoId1 >= Sketcher::GeoEnum::VAxis) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                vertical
                    ? QObject::tr("Cannot add a fixed y-coordinate constraint on the origin point!")
                    : QObject::tr("Cannot add a fixed x-coordinate constraint on the origin point!"));
            return;
        }

        double ActCoord = along(Obj->getPoint(GeoId1, PosId1));

        openCommand(vertical ? QT_TRANSLATE_NOOP("Command", "Add fixed y-coordinate constraint")
                             : QT_TRANSLATE_NOOP("Command", "Add fixed x-coordinate constraint"));
        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('%s',%d,%d,%f))",
                              typeName(),
                              GeoId1,
                              static_cast<int>(PosId1),
                              ActCoord);

        finishAdded(Obj, isPointOrSegmentFixed(Obj, GeoId1));
        return;
    }

    Gui::TranslatedUserWarning(
        Obj,
        QObject::tr("Wrong selection"),
        QObject::tr("Select exactly one line or up to two points from the sketch."));
}

void CmdSketcherConstrainAxisDistance::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1 = Sketcher::PointPos::none, PosId2 = Sketcher::PointPos::none;

    switch (seqIndex) {
        case 0:// {SelVertexOrRoot, SelVertexOrRoot}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;
            PosId1 = selSeq.at(0).PosId;
            PosId2 = selSeq.at(1).PosId;
            break;
        }
        case 1:// {SelEdge}
        case 2:// {SelExternalEdge}
        {
            GeoId1 = GeoId2 = selSeq.at(0).GeoId;
            PosId1 = Sketcher::PointPos::start;
            PosId2 = Sketcher::PointPos::end;

            const Part::Geometry* geom = Obj->getGeometry(GeoId1);

            if (! isLineSegment(*geom)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr(
                        "This constraint only makes sense on a line segment or a pair of points."));
                return;
            }

            break;
        }
        default:
            break;
    }

    addPointToPoint(Obj,
                    GeoId1,
                    PosId1,
                    GeoId2,
                    PosId2,
                    areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2));
}

void CmdSketcherConstrainAxisDistance::addPointToPoint(Sketcher::SketchObject* Obj,
                                                       int GeoId1,
                                                       Sketcher::PointPos PosId1,
                                                       int GeoId2,
                                                       Sketcher::PointPos PosId2,
                                                       bool fixed)
{
    double ActLength = along(Obj->getPoint(GeoId2, PosId2)) - along(Obj->getPoint(GeoId1, PosId1));

    // negative sign avoidance: swap the points to make value positive
    if (ActLength < -Precision::Confusion()) {
        std::swap(GeoId1, GeoId2);
        std::swap(PosId1, PosId2);
        ActLength = -ActLength;
    }

    openCommand(pointToPointCommand());
    Gui::cmdAppObjectArgs(Obj,
                          "addConstraint(Sketcher.Constraint('%s',%d,%d,%d,%d,%f))",
                          typeName(),
                          GeoId1,
                          static_cast<int>(PosId1),
                          GeoId2,
                          static_cast<int>(PosId2),
                          ActLength);

    finishAdded(Obj, fixed);
}

void CmdSketcherConstrainAxisDistance::finishAdded(Sketcher::SketchObject* Obj, bool fixed)
{
    if (fixed || constraintCreationMode == Reference) {
        // it is a constraint on a external line, make it non-driving
        const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

        Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
        finishDatumConstraint(this, Obj, false);
    }
    else {
        finishDatumConstraint(this, Obj, true);
    }
}

void CmdSketcherConstrainAxisDistance::updateAction(int mode)
{
    if (!getAction()) {
        return;
    }
    const char* icon = vertical ? "Constraint_VerticalDistance" : "Constraint_HorizontalDistance";
    switch (mode) {
        case Reference:
            getAction()->setIcon(
                Gui::BitmapFactory().iconFromTheme((std::string(icon) + "_Driven").c_str()));
            break;
        case Driving:
            getAction()->setIcon(Gui::BitmapFactory().iconFromTheme(icon));
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainRadius: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainRadius();
    ~CmdSketcherConstrainRadius() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainRadius";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainRadius::CmdSketcherConstrainRadius()
    : CmdSketcherConstraint("Sketcher_ConstrainRadius")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Radius Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the radius of the selected circle or arc");
    sWhatsThis = "Sketcher_ConstrainRadius";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Radius";
    sAccel = "K, R";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}, {SelExternalEdge}};
}

void CmdSketcherConstrainRadius::activated(int iMsg)
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
                                       QObject::tr("Select the right things from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    // check for which selected geometry the constraint can be applied
    std::vector<std::pair<int, double>> geoIdRadiusMap;
    std::vector<std::pair<int, double>> externalGeoIdRadiusMap;

    bool poles = false;
    bool nonpoles = false;

    for (auto& subname : SubNames) {
        bool issegmentfixed = false;
        int GeoId;

        if (subname.size() > 4 && subname.substr(0, 4) == "Edge") {
            GeoId = std::atoi(subname.substr(4, 4000).c_str()) - 1;
            issegmentfixed = isPointOrSegmentFixed(Obj, GeoId);
        }
        else if (subname.size() > 4 && subname.substr(0, 12) == "ExternalEdge") {
            GeoId = -std::atoi(subname.substr(12, 4000).c_str()) - 2;
            issegmentfixed = true;
        }
        else {
            continue;
        }

        const Part::Geometry* geom = Obj->getGeometry(GeoId);

        if (geom && isArcOfCircle(*geom)) {
            auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
            double radius = arc->getRadius();

            if (issegmentfixed) {
                externalGeoIdRadiusMap.emplace_back(GeoId, radius);
            }
            else {
                geoIdRadiusMap.emplace_back(GeoId, radius);
            }

            nonpoles = true;
        }
        else if (geom && isCircle(*geom)) {
            auto circle = static_cast<const Part::GeomCircle*>(geom);
            double radius = circle->getRadius();

            if (issegmentfixed) {
                externalGeoIdRadiusMap.emplace_back(GeoId, radius);
            }
            else {
                geoIdRadiusMap.emplace_back(GeoId, radius);
            }

            if (isBsplinePole(geom)) {
                poles = true;
            }
            else {
                nonpoles = true;
            }
        }
    }

    if (geoIdRadiusMap.empty() && externalGeoIdRadiusMap.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    if (poles && nonpoles) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select either only one or more B-spline poles or only one or more arcs or "
                        "circles from the sketch, but not mixed."));
        return;
    }

    bool commitNeeded = false;
    bool updateNeeded = false;
    bool commandopened = false;

    if (!externalGeoIdRadiusMap.empty()) {
        // Create the non-driving radius constraints now
        openCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
        commandopened = true;
        unsigned int constrSize = 0;

        for (std::vector<std::pair<int, double>>::iterator it = externalGeoIdRadiusMap.begin();
             it != externalGeoIdRadiusMap.end();
             ++it) {

            if (nonpoles) {
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                      it->first,
                                      it->second);
            }
            else {
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                      it->first,
                                      it->second);
            }

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            constrSize = ConStr.size();

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "setDriving(%d,%s)",
                                  constrSize - 1,
                                  "False");
        }

        finishDatumConstraint(this, Obj, false, externalGeoIdRadiusMap.size());

        commitNeeded = true;
        updateNeeded = true;
    }

    if (!geoIdRadiusMap.empty()) {
        if (geoIdRadiusMap.size() > 1 && constraintCreationMode == Driving) {

            int refGeoId = geoIdRadiusMap.front().first;
            double radius = geoIdRadiusMap.front().second;

            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
            }

            // Add the equality constraints
            for (std::vector<std::pair<int, double>>::iterator it = geoIdRadiusMap.begin() + 1;
                 it != geoIdRadiusMap.end();
                 ++it) {
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Equal',%d,%d))",
                                      refGeoId,
                                      it->first);
            }

            if (nonpoles) {
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                      refGeoId,
                                      radius);
            }
            else {
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                      refGeoId,
                                      radius);
            }
        }
        else {
            // Create the radius constraints now
            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
            }
            for (std::vector<std::pair<int, double>>::iterator it = geoIdRadiusMap.begin();
                 it != geoIdRadiusMap.end();
                 ++it) {
                if (nonpoles) {
                    Gui::cmdAppObjectArgs(selection[0].getObject(),
                                          "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                          it->first,
                                          it->second);
                }
                else {
                    Gui::cmdAppObjectArgs(selection[0].getObject(),
                                          "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                          it->first,
                                          it->second);
                }

                if (constraintCreationMode == Reference) {
                    const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();
                    Gui::cmdAppObjectArgs(selection[0].getObject(),
                                          "setDriving(%d,%s)",
                                          ConStr.size() - 1,
                                          "False");
                }
            }
        }

        finishDatumConstraint(this, Obj, constraintCreationMode == Driving);

        // updateActive();
        getSelection().clearSelection();
    }

    if (commitNeeded) {
        commitCommand();
    }

    if (updateNeeded) {
        tryAutoRecomputeIfNotSolve(Obj);// we have to update the solver after this aborted addition.
    }
}

void CmdSketcherConstrainRadius::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId = selSeq.at(0).GeoId;
    double radius = 0.0;

    bool updateNeeded = false;

    switch (seqIndex) {
        case 0:// {SelEdge}
        case 1:// {SelExternalEdge}
        {
            const Part::Geometry* geom = Obj->getGeometry(GeoId);

            if (geom && isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                radius = arc->getRadius();
            }
            else if (geom && isCircle(*geom)) {
                auto circle = static_cast<const Part::GeomCircle*>(geom);
                radius = circle->getRadius();
            }
            else {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Constraint only applies to arcs or circles."));
                return;
            }

            // Create the radius constraint now
            openCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));

            bool ispole = isBsplinePole(geom);

            if (ispole) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                      GeoId,
                                      radius);
            }
            else {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                      GeoId,
                                      radius);
            }

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            bool fixed = isPointOrSegmentFixed(Obj, GeoId);
            if (fixed || constraintCreationMode == Reference) {
                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");

                updateNeeded = true;// We do need to update the solver DoF after setting the
                                    // constraint driving.
            }

            finishDatumConstraint(this, Obj, constraintCreationMode == Driving && !fixed);

            // updateActive();
            getSelection().clearSelection();

            commitCommand();

            if (updateNeeded) {
                tryAutoRecomputeIfNotSolve(
                    Obj);// we have to update the solver after this aborted addition.
            }
        }
    }
}

void CmdSketcherConstrainRadius::updateAction(int mode)
{
    switch (mode) {
        case Reference:
            if (getAction()) {
                getAction()->setIcon(
                    Gui::BitmapFactory().iconFromTheme("Constraint_Radius_Driven"));
            }
            break;
        case Driving:
            if (getAction()) {
                getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius"));
            }
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainDiameter: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainDiameter();
    ~CmdSketcherConstrainDiameter() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainDiameter";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainDiameter::CmdSketcherConstrainDiameter()
    : CmdSketcherConstraint("Sketcher_ConstrainDiameter")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Diameter Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the diameter of the selected circle or arc");
    sWhatsThis = "Sketcher_ConstrainDiameter";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Diameter";
    sAccel = "K, O";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}, {SelExternalEdge}};
}

void CmdSketcherConstrainDiameter::activated(int iMsg)
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
                                       QObject::tr("Select the right things from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    // check for which selected geometry the constraint can be applied
    std::vector<std::pair<int, double>> geoIdDiameterMap;
    std::vector<std::pair<int, double>> externalGeoIdDiameterMap;

    for (auto& subname : SubNames) {
        bool issegmentfixed = false;
        int GeoId;

        if (subname.size() > 4 && subname.substr(0, 4) == "Edge") {
            GeoId = std::atoi(subname.substr(4, 4000).c_str()) - 1;
            issegmentfixed = isPointOrSegmentFixed(Obj, GeoId);
        }
        else if (subname.size() > 4 && subname.substr(0, 12) == "ExternalEdge") {
            GeoId = -std::atoi(subname.substr(12, 4000).c_str()) - 2;
            issegmentfixed = true;
        }
        else {
            continue;
        }

        const Part::Geometry* geom = Obj->getGeometry(GeoId);

        if (geom && isArcOfCircle(*geom)) {
            auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
            double radius = arc->getRadius();

            if (issegmentfixed) {
                externalGeoIdDiameterMap.emplace_back(GeoId, 2 * radius);
            }
            else {
                geoIdDiameterMap.emplace_back(GeoId, 2 * radius);
            }
        }
        else if (geom && isCircle(*geom)) {
            auto circle = static_cast<const Part::GeomCircle*>(geom);
            double radius = circle->getRadius();

            if (isBsplinePole(geom)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));

                continue;
            }

            if (issegmentfixed) {
                externalGeoIdDiameterMap.emplace_back(GeoId, 2 * radius);
            }
            else {
                geoIdDiameterMap.emplace_back(GeoId, 2 * radius);
            }
        }
    }

    if (geoIdDiameterMap.empty() && externalGeoIdDiameterMap.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    bool commitNeeded = false;
    bool updateNeeded = false;
    bool commandopened = false;

    if (!externalGeoIdDiameterMap.empty()) {
        // Create the non-driving radius constraints now
        openCommand(QT_TRANSLATE_NOOP("Command", "Add diameter constraint"));
        commandopened = true;
        unsigned int constrSize = 0;

        for (std::vector<std::pair<int, double>>::iterator it = externalGeoIdDiameterMap.begin();
             it != externalGeoIdDiameterMap.end();
             ++it) {
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                  it->first,
                                  it->second);

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            constrSize = ConStr.size();

            Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", constrSize - 1, "False");
        }

        finishDatumConstraint(this, Obj, false, externalGeoIdDiameterMap.size());

        commitNeeded = true;
        updateNeeded = true;
    }

    if (!geoIdDiameterMap.empty()) {
        if (geoIdDiameterMap.size() > 1 && constraintCreationMode == Driving) {

            int refGeoId = geoIdDiameterMap.front().first;
            double diameter = geoIdDiameterMap.front().second;

            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add diameter constraint"));
            }

            // Add the equality constraints
            for (std::vector<std::pair<int, double>>::iterator it = geoIdDiameterMap.begin() + 1;
                 it != geoIdDiameterMap.end();
                 ++it) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Equal',%d,%d))",
                                      refGeoId,
                                      it->first);
            }

            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                  refGeoId,
                                  diameter);
        }
        else {
            // Create the diameter constraints now
            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add diameter constraint"));
            }
            for (std::vector<std::pair<int, double>>::iterator it = geoIdDiameterMap.begin();
                 it != geoIdDiameterMap.end();
                 ++it) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                      it->first,
                                      it->second);

                if (constraintCreationMode == Reference) {

                    const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                    Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                }
            }
        }

        finishDatumConstraint(this, Obj, constraintCreationMode == Driving);

        // updateActive();
        getSelection().clearSelection();
    }

    if (commitNeeded) {
        commitCommand();
    }

    if (updateNeeded) {
        tryAutoRecomputeIfNotSolve(Obj);// we have to update the solver after this aborted addition.
    }
}

void CmdSketcherConstrainDiameter::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId = selSeq.at(0).GeoId;
    double diameter = 0.0;

    bool updateNeeded = false;

    switch (seqIndex) {
        case 0:// {SelEdge}
        case 1:// {SelExternalEdge}
        {
            const Part::Geometry* geom = Obj->getGeometry(GeoId);

            if (geom && isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                diameter = 2 * arc->getRadius();
            }
            else if (geom && isCircle(*geom)) {
                auto circle = static_cast<const Part::GeomCircle*>(geom);
                diameter = 2 * circle->getRadius();
            }
            else {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Constraint only applies to arcs or circles."));
                return;
            }

            if (isBsplinePole(geom)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            // Create the diameter constraint now
            openCommand(QT_TRANSLATE_NOOP("Command", "Add diameter constraint"));
            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                  GeoId,
                                  diameter);

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            bool fixed = isPointOrSegmentFixed(Obj, GeoId);
            if (fixed || constraintCreationMode == Reference) {
                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                updateNeeded = true;// We do need to update the solver DoF after setting the
                                    // constraint driving.
            }

            finishDatumConstraint(this, Obj, constraintCreationMode == Driving && !fixed);

            // updateActive();
            getSelection().clearSelection();

            commitCommand();

            if (updateNeeded) {
                tryAutoRecomputeIfNotSolve(
                    Obj);// we have to update the solver after this aborted addition.
            }
        }
    }
}

void CmdSketcherConstrainDiameter::updateAction(int mode)
{
    switch (mode) {
        case Reference:
            if (getAction()) {
                getAction()->setIcon(
                    Gui::BitmapFactory().iconFromTheme("Constraint_Diameter_Driven"));
            }
            break;
        case Driving:
            if (getAction()) {
                getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter"));
            }
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainRadiam: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainRadiam();
    ~CmdSketcherConstrainRadiam() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainRadiam";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainRadiam::CmdSketcherConstrainRadiam()
    : CmdSketcherConstraint("Sketcher_ConstrainRadiam")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Radius/Diameter Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the radius of the selected arc or the diameter of the selected circle");
    sWhatsThis = "Sketcher_ConstrainRadiam";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Radiam";
    sAccel = "K, S";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}, {SelExternalEdge}};
}

void CmdSketcherConstrainRadiam::activated(int iMsg)
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
                                       QObject::tr("Select the right things from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    // check for which selected geometry the constraint can be applied
    std::vector<std::pair<int, double>> geoIdRadiamMap;
    std::vector<std::pair<int, double>> externalGeoIdRadiamMap;

    bool poles = false;
    bool nonpoles = false;

    for (auto& subname : SubNames) {
        bool issegmentfixed = false;
        int GeoId;

        if (subname.size() > 4 && subname.substr(0, 4) == "Edge") {
            GeoId = std::atoi(subname.substr(4, 4000).c_str()) - 1;
            issegmentfixed = isPointOrSegmentFixed(Obj, GeoId);
        }
        else if (subname.size() > 4 && subname.substr(0, 12) == "ExternalEdge") {
            GeoId = -std::atoi(subname.substr(12, 4000).c_str()) - 2;
            issegmentfixed = true;
        }
        else {
            continue;
        }

        const Part::Geometry* geom = Obj->getGeometry(GeoId);
        double radius;

        if (geom && isArcOfCircle(*geom)) {
            auto arcir = static_cast<const Part::GeomArcOfCircle*>(geom);
            radius = arcir->getRadius();
            nonpoles = true;
        }
        else if (geom && isCircle(*geom)) {
            auto arcir = static_cast<const Part::GeomCircle*>(geom);
            radius = arcir->getRadius();
            if (isBsplinePole(geom)) {
                poles = true;
            }
            else {
                nonpoles = true;
            }
        }
        else {
            continue;
        }

        if (issegmentfixed) {
            externalGeoIdRadiamMap.emplace_back(GeoId, radius);
        }
        else {
            geoIdRadiamMap.emplace_back(GeoId, radius);
        }
    }

    if (geoIdRadiamMap.empty() && externalGeoIdRadiamMap.empty()) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select one or more arcs or circles from the sketch."));
        return;
    }

    if (poles && nonpoles) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select either only one or more B-spline poles or only one or more arcs or "
                        "circles from the sketch, but not mixed."));
        return;
    }

    bool commitNeeded = false;
    bool updateNeeded = false;
    bool commandopened = false;

    if (!externalGeoIdRadiamMap.empty()) {
        // Create the non-driving radiam constraints now
        openCommand(QT_TRANSLATE_NOOP("Command", "Add radiam constraint"));
        commandopened = true;
        unsigned int constrSize = 0;

        for (std::vector<std::pair<int, double>>::iterator it = externalGeoIdRadiamMap.begin();
             it != externalGeoIdRadiamMap.end();
             ++it) {
            if (isArcOfCircle(*(Obj->getGeometry(it->first)))) {
                if (nonpoles) {
                    Gui::cmdAppObjectArgs(Obj,
                                          "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                          it->first,
                                          it->second);
                }
                else {
                    Gui::cmdAppObjectArgs(Obj,
                                          "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                          it->first,
                                          it->second);
                }
            }
            else {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                      it->first,
                                      it->second * 2);
            }

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            constrSize = ConStr.size();

            Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", constrSize - 1, "False");
        }

        finishDatumConstraint(this, Obj, false, externalGeoIdRadiamMap.size());

        commitNeeded = true;
        updateNeeded = true;
    }

    if (!geoIdRadiamMap.empty()) {
        if (geoIdRadiamMap.size() > 1 && constraintCreationMode == Driving) {

            int refGeoId = geoIdRadiamMap.front().first;
            double radiam = geoIdRadiamMap.front().second;

            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add radiam constraint"));
            }

            // Add the equality constraints
            for (std::vector<std::pair<int, double>>::iterator it = geoIdRadiamMap.begin() + 1;
                 it != geoIdRadiamMap.end();
                 ++it) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Equal',%d,%d))",
                                      refGeoId,
                                      it->first);
            }

            if (poles) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                      refGeoId,
                                      radiam);
            }
            else if (isCircle(*(Obj->getGeometry(refGeoId)))) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                      refGeoId,
                                      radiam * 2);
            }
            else {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                      refGeoId,
                                      radiam);
            }
        }
        else {
            // Create the radiam constraints now
            if (!commandopened) {
                openCommand(QT_TRANSLATE_NOOP("Command", "Add radiam constraint"));
            }
            for (std::vector<std::pair<int, double>>::iterator it = geoIdRadiamMap.begin();
                 it != geoIdRadiamMap.end();
                 ++it) {
                if (poles) {
                    Gui::cmdAppObjectArgs(Obj,
                                          "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                          it->first,
                                          it->second);
                }
                else if (isCircle(*(Obj->getGeometry(it->first)))){
                    Gui::cmdAppObjectArgs(Obj,
                                          "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                          it->first,
                                          it->second * 2);
                }
                else {
                    Gui::cmdAppObjectArgs(Obj,
                                          "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                          it->first,
                                          it->second);
                }

                if (constraintCreationMode == Reference) {

                    const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                    Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                }
            }
        }

        finishDatumConstraint(this, Obj, constraintCreationMode == Driving);

        // updateActive();
        getSelection().clearSelection();
    }

    if (commitNeeded) {
        commitCommand();
    }

    if (updateNeeded) {
        tryAutoRecomputeIfNotSolve(Obj);// we have to update the solver after this aborted addition.
    }
}

void CmdSketcherConstrainRadiam::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId = selSeq.at(0).GeoId;
    double radiam = 0.0;

    bool updateNeeded = false;

    bool isCircleGeom = false;
    bool isPole = false;

    switch (seqIndex) {
        case 0:// {SelEdge}
        case 1:// {SelExternalEdge}
        {
            const Part::Geometry* geom = Obj->getGeometry(GeoId);

            if (geom && isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                radiam = arc->getRadius();
            }
            else if (geom && isCircle(*geom)) {
                auto circle = static_cast<const Part::GeomCircle*>(geom);
                radiam = circle->getRadius();
                isCircleGeom= true;
                if (isBsplinePole(geom)) {
                    isPole = true;
                }
            }
            else {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Constraint only applies to arcs or circles."));
                return;
            }

            // Create the radiam constraint now
            openCommand(QT_TRANSLATE_NOOP("Command", "Add radiam constraint"));

            if (isPole) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Weight',%d,%f))",
                                      GeoId,
                                      radiam);
            }
            else if (isCircleGeom) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Diameter',%d,%f))",
                                      GeoId,
                                      radiam * 2);
            }
            else {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Radius',%d,%f))",
                                      GeoId,
                                      radiam);
            }

            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            bool fixed = isPointOrSegmentFixed(Obj, GeoId);
            if (fixed || constraintCreationMode == Reference) {
                Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
                updateNeeded = true;// We do need to update the solver DoF after setting the
                                    // constraint driving.
            }

            finishDatumConstraint(this, Obj, constraintCreationMode == Driving && !fixed);

            // updateActive();
            getSelection().clearSelection();

            commitCommand();

            if (updateNeeded) {
                tryAutoRecomputeIfNotSolve(
                    Obj);// we have to update the solver after this aborted addition.
            }
        }
    }
}

void CmdSketcherConstrainRadiam::updateAction(int mode)
{
    switch (mode) {
        case Reference:
            if (getAction()) {
                getAction()->setIcon(
                    Gui::BitmapFactory().iconFromTheme("Constraint_Radiam_Driven"));
            }
            break;
        case Driving:
            if (getAction()) {
                getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam"));
            }
            break;
    }
}

// ======================================================================================

DEF_STD_CMD_ACLU(CmdSketcherCompConstrainRadDia)

CmdSketcherCompConstrainRadDia::CmdSketcherCompConstrainRadDia()
    : Command("Sketcher_CompConstrainRadDia")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Radius/Diameter Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the radius or diameter of an arc or a circle");
    sWhatsThis = "Sketcher_CompConstrainRadDia";
    sStatusTip = sToolTipText;
    sAccel = "R";
    eType = ForEdit;
}

void CmdSketcherCompConstrainRadDia::activated(int iMsg)
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    if (iMsg == 0) {
        rcCmdMgr.runCommandByName("Sketcher_ConstrainRadius");
    }
    else if (iMsg == 1) {
        rcCmdMgr.runCommandByName("Sketcher_ConstrainDiameter");
    }
    else if (iMsg == 2) {
        rcCmdMgr.runCommandByName("Sketcher_ConstrainRadiam");
    }
    else {
        return;
    }

    // Save new choice as default
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher");
    hGrp->SetInt("CurRadDiaCons", iMsg);

    // Since the default icon is reset when enabling/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action* CmdSketcherCompConstrainRadDia::createAction()
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    QAction* arc1 = pcAction->addAction(QString());
    arc1->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius"));
    QAction* arc2 = pcAction->addAction(QString());
    arc2->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter"));
    QAction* arc3 = pcAction->addAction(QString());
    arc3->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam"));

    _pcAction = pcAction;
    languageChange();

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher");
    int curRadDiaCons = hGrp->GetInt("CurRadDiaCons", 2);

    switch (curRadDiaCons) {
        case 0:
            pcAction->setIcon(arc1->icon());
            break;
        case 1:
            pcAction->setIcon(arc2->icon());
            break;
        default:
            pcAction->setIcon(arc3->icon());
            curRadDiaCons = 2;
    }
    pcAction->setProperty("defaultAction", QVariant(curRadDiaCons));
    pcAction->setShortcut(QString::fromLatin1(getAccel()));

    return pcAction;
}

void CmdSketcherCompConstrainRadDia::updateAction(int mode)
{
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(getAction());
    if (!pcAction) {
        return;
    }

    QList<QAction*> a = pcAction->actions();
    int index = pcAction->property("defaultAction").toInt();
    switch (mode) {
        case Reference:
            a[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius_Driven"));
            a[1]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter_Driven"));
            a[2]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam_Driven"));
            getAction()->setIcon(a[index]->icon());
            break;
        case Driving:
            a[0]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radius"));
            a[1]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Diameter"));
            a[2]->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Radiam"));
            getAction()->setIcon(a[index]->icon());
            break;
    }
}

void CmdSketcherCompConstrainRadDia::languageChange()
{
    Command::languageChange();

    if (!_pcAction) {
        return;
    }
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    QAction* arc1 = a[0];
    arc1->setText(QApplication::translate("CmdSketcherCompConstrainRadDia", "Constrain radius"));
    arc1->setToolTip(QApplication::translate("Sketcher_ConstrainRadius",
                                             "Fix the radius of an arc or a circle"));
    arc1->setStatusTip(QApplication::translate("Sketcher_ConstrainRadius",
                                               "Fix the radius of an arc or a circle"));
    QAction* arc2 = a[1];
    arc2->setText(QApplication::translate("CmdSketcherCompConstrainRadDia", "Constrain diameter"));
    arc2->setToolTip(QApplication::translate("Sketcher_ConstrainDiameter",
                                             "Fix the diameter of a circle or an arc"));
    arc2->setStatusTip(QApplication::translate("Sketcher_ConstrainDiameter",
                                               "Fix the diameter of a circle or an arc"));
    QAction* arc3 = a[2];
    arc3->setText(QApplication::translate("CmdSketcherCompConstrainRadDia",
                                          "Constrain auto radius/diameter"));
    arc3->setToolTip(QApplication::translate("Sketcher_ConstrainRadiam",
                                             "Fix the radius/diameter of an arc or a circle"));
    arc3->setStatusTip(QApplication::translate("Sketcher_ConstrainRadiam",
                                               "Fix the radius/diameter of an arc or a circle"));
}

bool CmdSketcherCompConstrainRadDia::isActive()
{
    return isCommandActive(getActiveGuiDocument());
}

// ======================================================================================

class CmdSketcherConstrainAngle: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainAngle();
    ~CmdSketcherConstrainAngle() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainAngle";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainAngle::CmdSketcherConstrainAngle()
    : CmdSketcherConstraint("Sketcher_ConstrainAngle")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Angle Dimension");
    sToolTipText = QT_TR_NOOP("Constrains the angle between two straight lines or between one line and the X-axis of the sketch if only one is selected");
    sWhatsThis = "Sketcher_ConstrainAngle";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_InternalAngle";
    sAccel = "K, A";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge, SelEdgeOrAxis},
                           {SelEdgeOrAxis, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge},
                           {SelExternalEdge, SelExternalEdge},
                           {SelEdge, SelVertexOrRoot, SelEdgeOrAxis},
                           {SelEdgeOrAxis, SelVertexOrRoot, SelEdge},
                           {SelEdge, SelVertexOrRoot, SelExternalEdge},
                           {SelExternalEdge, SelVertexOrRoot, SelEdge},
                           {SelExternalEdge, SelVertexOrRoot, SelExternalEdge},
                           {SelVertexOrRoot, SelEdge, SelEdgeOrAxis},
                           {SelVertexOrRoot, SelEdgeOrAxis, SelEdge},
                           {SelVertexOrRoot, SelEdge, SelExternalEdge},
                           {SelVertexOrRoot, SelExternalEdge, SelEdge},
                           {SelVertexOrRoot, SelExternalEdge, SelExternalEdge}};
}

void CmdSketcherConstrainAngle::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // TODO: comprehensive messages, like in CmdSketcherConstrainTangent
    //  get the selection
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
                                       QObject::tr("Select the right things from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    if (SubNames.empty() || SubNames.size() > 3) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Wrong selection"),
            QObject::tr(
                "Select one or two lines from the sketch. Or select two edges and a point."));
        return;
    }

    int GeoId1, GeoId2 = GeoEnum::GeoUndef, GeoId3 = GeoEnum::GeoUndef;
    Sketcher::PointPos PosId1, PosId2 = Sketcher::PointPos::none, PosId3 = Sketcher::PointPos::none;
    getIdsFromName(SubNames[0], Obj, GeoId1, PosId1);
    if (SubNames.size() > 1) {
        getIdsFromName(SubNames[1], Obj, GeoId2, PosId2);
    }
    if (SubNames.size() > 2) {
        getIdsFromName(SubNames[2], Obj, GeoId3, PosId3);
    }

    if (SubNames.size() == 3) {// standalone implementation of angle-via-point

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

        bool bothexternal = areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

        if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

            if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                return;
            }

            double ActAngle = 0.0;

            openCommand(QT_TRANSLATE_NOOP("Command", "Add angle constraint"));

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

            // assuming point-on-curves have been solved, calculate the angle.
            // DeepSOIC: this may be slow, but I wanted to reuse the conversion
            // from Geometry to GCS shapes that is done in Sketch
            Base::Vector3d p = Obj->getPoint(GeoId3, PosId3);
            ActAngle = Obj->calculateAngleViaPoint(GeoId1, GeoId2, p.x, p.y);

            // negative constraint value avoidance
            if (ActAngle < -Precision::Angular()) {
                std::swap(GeoId1, GeoId2);
                std::swap(PosId1, PosId2);
                ActAngle = -ActAngle;
            }

            Gui::cmdAppObjectArgs(
                selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('AngleViaPoint',%d,%d,%d,%d,%f))",
                GeoId1,
                GeoId2,
                GeoId3,
                static_cast<int>(PosId3),
                ActAngle);

            removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);

            if (bothexternal
                || constraintCreationMode
                    == Reference) {// it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "setDriving(%d,%s)",
                                      ConStr.size() - 1,
                                      "False");
                finishDatumConstraint(this, Obj, false);
            }
            else {
                finishDatumConstraint(this, Obj, true);
            }

            return;
        }
    }
    else if (SubNames.size() < 3) {

        if (isVertex(GeoId1, PosId1) && isEdge(GeoId2, PosId2)) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
        }

        if (isBsplinePole(Obj, GeoId1)
            || (GeoId2 != GeoEnum::GeoUndef && isBsplinePole(Obj, GeoId2))) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select an edge that is not a B-spline weight."));
            return;
        }

        if (isEdge(GeoId2, PosId2)) {// line to line angle
            makeAngleBetweenTwoLines(Obj, this, GeoId1, GeoId2);
            return;
        }
        else if (isEdge(GeoId1, PosId1)) {// line angle or arc angle
            if (GeoId1 < 0 && GeoId1 >= Sketcher::GeoEnum::VAxis) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Cannot add an angle constraint on an axis!"));
                return;
            }

            const Part::Geometry* geom = Obj->getGeometry(GeoId1);

            if (isLineSegment(*geom)) {
                auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
                Base::Vector3d dir = lineSeg->getEndPoint() - lineSeg->getStartPoint();
                double ActAngle = atan2(dir.y, dir.x);

                openCommand(QT_TRANSLATE_NOOP("Command", "Add angle constraint"));
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Angle',%d,%f))",
                                      GeoId1,
                                      ActAngle);

                if (GeoId1 <= Sketcher::GeoEnum::RefExt || constraintCreationMode == Reference) {
                    // it is a constraint on a external line, make it non-driving
                    const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                    Gui::cmdAppObjectArgs(selection[0].getObject(),
                                          "setDriving(%d,%s)",
                                          ConStr.size() - 1,
                                          "False");
                    finishDatumConstraint(this, Obj, false);
                }
                else {
                    finishDatumConstraint(this, Obj, true);
                }

                return;
            }
            else if (isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                double angle = arc->getAngle(/*EmulateCCWXY=*/true);

                openCommand(QT_TRANSLATE_NOOP("Command", "Add angle constraint"));
                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "addConstraint(Sketcher.Constraint('Angle',%d,%f))",
                                      GeoId1,
                                      angle);

                if (GeoId1 <= Sketcher::GeoEnum::RefExt || constraintCreationMode == Reference) {
                    // it is a constraint on a external line, make it non-driving
                    const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                    Gui::cmdAppObjectArgs(selection[0].getObject(),
                                          "setDriving(%d,%s)",
                                          ConStr.size() - 1,
                                          "False");
                    finishDatumConstraint(this, Obj, false);
                }
                else {
                    finishDatumConstraint(this, Obj, true);
                }

                return;
            }
        }
    }

    Gui::TranslatedUserWarning(
        Obj,
        QObject::tr("Wrong selection"),
        QObject::tr("Select one or two lines from the sketch. Or select two edges and a point."));
    return;
}

void CmdSketcherConstrainAngle::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
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
        case 4:// {SelExternalEdge, SelExternalEdge}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(1).GeoId;

            makeAngleBetweenTwoLines(Obj, this, GeoId1, GeoId2);
            return;
        }
        case 5:// {SelEdge, SelVertexOrRoot, SelEdgeOrAxis}
        case 6:// {SelEdgeOrAxis, SelVertexOrRoot, SelEdge}
        case 7:// {SelEdge, SelVertexOrRoot, SelExternalEdge}
        case 8:// {SelExternalEdge, SelVertexOrRoot, SelEdge}
        case 9:// {SelExternalEdge, SelVertexOrRoot, SelExternalEdge}
        {
            GeoId1 = selSeq.at(0).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(1).GeoId;
            PosId3 = selSeq.at(1).PosId;
            break;
        }
        case 10:// {SelVertexOrRoot, SelEdge, SelEdgeOrAxis}
        case 11:// {SelVertexOrRoot, SelEdgeOrAxis, SelEdge}
        case 12:// {SelVertexOrRoot, SelEdge, SelExternalEdge}
        case 13:// {SelVertexOrRoot, SelExternalEdge, SelEdge}
        case 14:// {SelVertexOrRoot, SelExternalEdge, SelExternalEdge}
        {
            GeoId1 = selSeq.at(1).GeoId;
            GeoId2 = selSeq.at(2).GeoId;
            GeoId3 = selSeq.at(0).GeoId;
            PosId3 = selSeq.at(0).PosId;
            break;
        }
    }

    bool bothexternal = areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

    if (isEdge(GeoId1, PosId1) && isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {

        if (isBsplinePole(Obj, GeoId1) || isBsplinePole(Obj, GeoId2)) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select an edge that is not a B-spline weight."));
            return;
        }

        double ActAngle = 0.0;

        openCommand(QT_TRANSLATE_NOOP("Command", "Add angle constraint"));

        // add missing point-on-object constraints
        if (!IsPointAlreadyOnCurve(GeoId1, GeoId3, PosId3, Obj)) {
            const Part::Geometry *geom1 = Obj->getGeometry(GeoId1);
            if (!(geom1 && isBSplineCurve(*geom1))) {
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                      GeoId3,
                                      static_cast<int>(PosId3),
                                      GeoId1);
            }
        }
        if (!IsPointAlreadyOnCurve(GeoId2, GeoId3, PosId3, Obj)) {
            const Part::Geometry *geom2 = Obj->getGeometry(GeoId2);
            if (!(geom2 && isBSplineCurve(*geom2))) {
                Gui::cmdAppObjectArgs(Obj,
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
                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                                      GeoId3,
                                      static_cast<int>(PosId3),
                                      GeoId1);
            }
        }

        // assuming point-on-curves have been solved, calculate the angle.
        // DeepSOIC: this may be slow, but I wanted to reuse the conversion
        // from Geometry to GCS shapes that is done in Sketch
        Base::Vector3d p = Obj->getPoint(GeoId3, PosId3);
        ActAngle = Obj->calculateAngleViaPoint(GeoId1, GeoId2, p.x, p.y);

        // negative constraint value avoidance
        if (ActAngle < -Precision::Angular()) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
            ActAngle = -ActAngle;
        }

        Gui::cmdAppObjectArgs(Obj,
                              "addConstraint(Sketcher.Constraint('AngleViaPoint',%d,%d,%d,%d,%f))",
                              GeoId1,
                              GeoId2,
                              GeoId3,
                              static_cast<int>(PosId3),
                              ActAngle);

        removeRedundantPointOnObject(Obj, GeoId1, GeoId2, GeoId3);

        if (bothexternal || constraintCreationMode == Reference) {
            // it is a constraint on a external line, make it non-driving
            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

            Gui::cmdAppObjectArgs(Obj, "setDriving(%d,%s)", ConStr.size() - 1, "False");
            finishDatumConstraint(this, Obj, false);
        }
        else {
            finishDatumConstraint(this, Obj, true);
        }

        return;
    }
}

void CmdSketcherConstrainAngle::updateAction(int mode)
{
    switch (mode) {
        case Reference:
            if (getAction()) {
                getAction()->setIcon(
                    Gui::BitmapFactory().iconFromTheme("Constraint_InternalAngle_Driven"));
            }
            break;
        case Driving:
            if (getAction()) {
                getAction()->setIcon(
                    Gui::BitmapFactory().iconFromTheme("Constraint_InternalAngle"));
            }
            break;
    }
}


void SketcherGui::addDimensionalConstraintCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdSketcherConstrainDistance());
    rcCmdMgr.addCommand(new CmdSketcherConstrainAxisDistance(false));
    rcCmdMgr.addCommand(new CmdSketcherConstrainAxisDistance(true));
    rcCmdMgr.addCommand(new CmdSketcherConstrainRadius());
    rcCmdMgr.addCommand(new CmdSketcherConstrainDiameter());
    rcCmdMgr.addCommand(new CmdSketcherConstrainRadiam());
    rcCmdMgr.addCommand(new CmdSketcherCompConstrainRadDia());
    rcCmdMgr.addCommand(new CmdSketcherConstrainAngle());
}
