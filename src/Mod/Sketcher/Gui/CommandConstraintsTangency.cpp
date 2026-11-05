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
#include "CommandConstraintsCommon.h"

// clang-format off
using namespace std;
using namespace SketcherGui;
using namespace Sketcher;

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

void SketcherGui::addTangencyConstraintCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdSketcherConstrainPerpendicular());
    rcCmdMgr.addCommand(new CmdSketcherConstrainTangent());
}
