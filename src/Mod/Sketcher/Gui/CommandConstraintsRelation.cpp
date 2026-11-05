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

void SketcherGui::addRelationConstraintCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdSketcherConstrainParallel());
    rcCmdMgr.addCommand(new CmdSketcherConstrainEqual());
    rcCmdMgr.addCommand(new CmdSketcherConstrainSymmetric());
    rcCmdMgr.addCommand(new CmdSketcherConstrainSnellsLaw());
}
