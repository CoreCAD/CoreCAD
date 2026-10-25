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

// ============================================================================
bool canHorVerBlock(Sketcher::SketchObject* Obj, int geoId)
{
    const std::vector<Sketcher::Constraint*>& vals = Obj->Constraints.getValues();

    // check if the edge already has a Horizontal/Vertical/Block constraint
    for (auto& constr : vals) {
        if (constr->Type == Sketcher::Horizontal && constr->First == geoId
            && constr->FirstPos == Sketcher::PointPos::none) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Double constraint"),
                QObject::tr("The selected edge already has a horizontal constraint!"));
            return false;
        }
        if (constr->Type == Sketcher::Vertical && constr->First == geoId
            && constr->FirstPos == Sketcher::PointPos::none) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Impossible constraint"),
                QObject::tr("The selected edge already has a vertical constraint!"));
            return false;
        }
        // check if the edge already has a Block constraint
        if (constr->Type == Sketcher::Block && constr->First == geoId
            && constr->FirstPos == Sketcher::PointPos::none) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Impossible constraint"),
                QObject::tr("The selected edge already has a block constraint!"));
            return false;
        }
    }
    return true;
}

void horVerActivated(CmdSketcherConstraint* cmd, std::string type)
{
    // get the selection
    std::vector<Gui::SelectionObject> selection = Gui::Command::getSelection().getSelectionEx();

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() != 1
        || !selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool constraintMode = hGrp->GetBool("ContinuousConstraintMode", true);

        if (constraintMode) {
            ActivateHandler(cmd->getActiveGuiDocument(), std::make_unique<DrawSketchHandlerGenConstraint>(cmd));
            Gui::Command::getSelection().clearSelection();
        }
        else {
            Gui::TranslatedUserWarning(cmd->getActiveGuiDocument(),
                QObject::tr("Wrong selection"),
                QObject::tr("Select an edge from the sketch."));
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    std::vector<int> edgegeoids;
    std::vector<int> pointgeoids;
    std::vector<Sketcher::PointPos> pointpos;

    int fixedpoints = 0;

    for (auto& name : SubNames) {
        int GeoId;
        Sketcher::PointPos PosId;
        getIdsFromName(name, Obj, GeoId, PosId);

        if (isEdge(GeoId, PosId)) {// it is an edge
            const Part::Geometry* geo = Obj->getGeometry(GeoId);

            if (!isLineSegment(*geo)) {
                Gui::TranslatedUserWarning(Obj,
                    QObject::tr("Impossible constraint"),
                    QObject::tr("The selected edge is not a line segment."));
                return;
            }

            if (canHorVerBlock(Obj, GeoId)) {
                edgegeoids.push_back(GeoId);
            }
        }
        else if (isVertex(GeoId, PosId)) {
            // can be a point, a construction point, an external point or root

            if (isPointOrSegmentFixed(Obj, GeoId)) {
                fixedpoints++;
            }

            pointgeoids.push_back(GeoId);
            pointpos.push_back(PosId);
        }
    }

    if (edgegeoids.empty() && pointgeoids.size() < 2) {
        Gui::TranslatedUserWarning(
            Obj,
            QObject::tr("Impossible constraint"),
            QObject::tr("The selected items cannot be constrained horizontally or vertically!"));
        return;
    }

    // if there is at least one edge selected, ignore the point alignment functionality
    if (!edgegeoids.empty()) {
        // undo command open
        const char* cmdName = type == "Horizontal" ? "Add horizontal constraint" : type == "Vertical" ? "Add vertical constraint" : "Add horizontal/vertical constraint";
        cmd->openCommand(QT_TRANSLATE_NOOP("Command", cmdName));
        for (auto& geoId : edgegeoids) {
            std::string typeToApply = type;
            if (type == "HorVer") {
                const Part::Geometry* geo = Obj->getGeometry(geoId);
                auto* line = static_cast<const Part::GeomLineSegment*>(geo);
                double angle = toVector2d(line->getEndPoint() - line->getStartPoint()).Angle();
                typeToApply = fabs(sin(angle)) < fabs(cos(angle)) ? "Horizontal" : "Vertical";
            }

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('%s',%d))",
                typeToApply,
                geoId);
        }
    }
    else if (fixedpoints <= 1) {// pointgeoids
        // undo command open
        const char* cmdName = type == "Horizontal" ? "Add horizontal alignment" : type == "Vertical" ? "Add vertical alignment" : "Add horizontal/vertical alignment";
        cmd->openCommand(QT_TRANSLATE_NOOP("Command", cmdName));
        std::vector<int>::iterator it;
        std::vector<Sketcher::PointPos>::iterator itp;
        for (it = pointgeoids.begin(), itp = pointpos.begin();
            it != std::prev(pointgeoids.end()) && itp != std::prev(pointpos.end());
            it++, itp++) {

            std::string typeToApply = type;
            if (type == "HorVer") {
                auto point1 = Obj->getPoint(*it, *itp);
                auto point2 = Obj->getPoint(*std::next(it), *std::next(itp));
                double angle = toVector2d(point2 - point1).Angle();
                typeToApply = fabs(sin(angle)) < fabs(cos(angle)) ? "Horizontal" : "Vertical";
            }

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                "addConstraint(Sketcher.Constraint('%s',%d,%d,%d,%d))",
                typeToApply,
                *it,
                static_cast<int>(*itp),
                *std::next(it),
                static_cast<int>(*std::next(itp)));
        }
    }
    else {// vertex mode, fixedpoints > 1
        Gui::TranslatedUserWarning(Obj,
            QObject::tr("Impossible constraint"),
            QObject::tr("There are more than one fixed points selected. "
                "Select a maximum of one fixed point!"));
        return;
    }
    // finish the transaction and update
    cmd->commitCommand();

    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    Gui::Command::getSelection().clearSelection();
}

void horVerApplyConstraint(CmdSketcherConstraint* cmd, std::string type, std::vector<SelIdPair>& selSeq, int seqIndex)
{
    auto* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(cmd->getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    switch (seqIndex) {
    case 0:// {Edge}
    {
        if (selSeq.empty()) {
            return;
        }

        int CrvId = selSeq.front().GeoId;
        if (CrvId != -1) {
            const Part::Geometry* geo = Obj->getGeometry(CrvId);

            if (!isLineSegment(*geo)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Impossible constraint"),
                    QObject::tr("The selected edge is not a line segment."));
                return;
            }

            // check if the edge already has a Horizontal/Vertical/Block constraint
            if (!canHorVerBlock(Obj, CrvId)) {
                return;
            }

            std::string typeToApply = type;
            if (type == "HorVer") {
                const Part::Geometry* geo = Obj->getGeometry(CrvId);
                auto* line = static_cast<const Part::GeomLineSegment*>(geo);
                double angle = toVector2d(line->getEndPoint() - line->getStartPoint()).Angle();
                typeToApply = fabs(sin(angle)) < fabs(cos(angle)) ? "Horizontal" : "Vertical";
            }

            const char* cmdName = typeToApply == "Horizontal" ? "Add horizontal constraint" : "Add vertical constraint";
            cmd->openCommand(QT_TRANSLATE_NOOP("Command", cmdName));

            // issue the actual commands to create the constraint
            Gui::cmdAppObjectArgs(sketchgui->getObject(),
                "addConstraint(Sketcher.Constraint('%s',%d))",
                typeToApply,
                CrvId);
            // finish the transaction and update
            cmd->commitCommand();

            tryAutoRecompute(Obj);
        }

        break;
    }

    case 1:// {SelVertex, SelVertexOrRoot}
    case 2:// {SelRoot, SelVertex}
    {
        int GeoId1, GeoId2;
        Sketcher::PointPos PosId1, PosId2;
        GeoId1 = selSeq.at(0).GeoId;
        GeoId2 = selSeq.at(1).GeoId;
        PosId1 = selSeq.at(0).PosId;
        PosId2 = selSeq.at(1).PosId;

        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            showNoConstraintBetweenFixedGeometry(Obj);
            return;
        }

        std::string typeToApply = type;
        if (type == "HorVer") {
            auto point1 = Obj->getPoint(GeoId1, PosId1);
            auto point2 = Obj->getPoint(GeoId2, PosId2);
            double angle = toVector2d(point2 - point1).Angle();
            typeToApply = fabs(sin(angle)) < fabs(cos(angle)) ? "Horizontal" : "Vertical";
        }

        // undo command open
        const char* cmdName = type == "Horizontal" ? "Add horizontal alignment" : "Add vertical alignment";
        cmd->openCommand(QT_TRANSLATE_NOOP("Command", cmdName));

        // issue the actual commands to create the constraint
        Gui::cmdAppObjectArgs(sketchgui->getObject(),
            "addConstraint(Sketcher.Constraint('%s',%d,%d,%d,%d))",
            typeToApply,
            GeoId1,
            static_cast<int>(PosId1),
            GeoId2,
            static_cast<int>(PosId2));
        // finish the transaction and update
        cmd->commitCommand();

        tryAutoRecompute(Obj);

        break;
    }
    }
}

class CmdSketcherConstrainHorVer : public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainHorVer();
    ~CmdSketcherConstrainHorVer() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainHorVer";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainHorVer::CmdSketcherConstrainHorVer()
    : CmdSketcherConstraint("Sketcher_ConstrainHorVer")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Horizontal/Vertical Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements either horizontally or vertically, based on their closest alignment");
    sWhatsThis = "Sketcher_ConstrainHorVer";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_HorVer";
    sAccel = "A";
    eType = ForEdit;

    allowedSelSequences = { {SelEdge}, {SelVertexOrRoot, SelVertexOrRoot} };
}

void CmdSketcherConstrainHorVer::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    horVerActivated(this, "HorVer");
}

void CmdSketcherConstrainHorVer::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    horVerApplyConstraint(this, "HorVer", selSeq, seqIndex);
}


// ============================================================================

class CmdSketcherConstrainHorizontal: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainHorizontal();
    ~CmdSketcherConstrainHorizontal() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainHorizontal";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainHorizontal::CmdSketcherConstrainHorizontal()
    : CmdSketcherConstraint("Sketcher_ConstrainHorizontal")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Horizontal Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements horizontally");
    sWhatsThis = "Sketcher_ConstrainHorizontal";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Horizontal";
    sAccel = "H";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}, {SelVertexOrRoot, SelVertexOrRoot}};
}

void CmdSketcherConstrainHorizontal::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    horVerActivated(this, "Horizontal");
}

void CmdSketcherConstrainHorizontal::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    horVerApplyConstraint(this, "Horizontal", selSeq, seqIndex);
}

// ================================================================================

class CmdSketcherConstrainVertical: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainVertical();
    ~CmdSketcherConstrainVertical() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainVertical";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainVertical::CmdSketcherConstrainVertical()
    : CmdSketcherConstraint("Sketcher_ConstrainVertical")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Vertical Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements vertically");
    sWhatsThis = "Sketcher_ConstrainVertical";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Vertical";
    sAccel = "V";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}, {SelVertexOrRoot, SelVertexOrRoot}};
}

void CmdSketcherConstrainVertical::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    horVerActivated(this, "Vertical");
}

void CmdSketcherConstrainVertical::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    horVerApplyConstraint(this, "Vertical", selSeq, seqIndex);
}

// ======================================================================================

class CmdSketcherConstrainLock: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainLock();
    ~CmdSketcherConstrainLock() override
    {}
    void updateAction(int mode) override;
    const char* className() const override
    {
        return "CmdSketcherConstrainLock";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainLock::CmdSketcherConstrainLock()
    : CmdSketcherConstraint("Sketcher_ConstrainLock")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Lock Position");
    sToolTipText = QT_TR_NOOP("Constrains the selected vertices by adding horizontal and vertical distance constraints");
    sWhatsThis = "Sketcher_ConstrainLock";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Lock";
    sAccel = "K, L";
    eType = ForEdit;

    allowedSelSequences = {{SelVertex}};
}

void CmdSketcherConstrainLock::activated(int iMsg)
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

    std::vector<int> GeoId;
    std::vector<Sketcher::PointPos> PosId;

    for (std::vector<std::string>::const_iterator it = SubNames.begin(); it != SubNames.end();
         ++it) {
        int GeoIdt;
        Sketcher::PointPos PosIdt;
        getIdsFromName((*it), Obj, GeoIdt, PosIdt);
        GeoId.push_back(GeoIdt);
        PosId.push_back(PosIdt);

        if ((it != std::prev(SubNames.end())
             && (isEdge(GeoIdt, PosIdt) || (GeoIdt < 0 && GeoIdt >= Sketcher::GeoEnum::VAxis)))
            || (it == std::prev(SubNames.end()) && isEdge(GeoIdt, PosIdt))) {
            if (selection.size() == 1) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select one vertex from the sketch other than the origin."));
            }
            else {
                Gui::TranslatedUserWarning(Obj,
                                           QObject::tr("Wrong selection"),
                                           QObject::tr("Select only vertices from the sketch. The "
                                                       "last selected vertex may be the origin."));
            }
            // clear the selection (convenience)
            getSelection().clearSelection();
            return;
        }
    }

    int lastconstraintindex = Obj->Constraints.getSize() - 1;

    if (GeoId.size() == 1) {// absolute mode
        // check if the edge already has a Block constraint
        bool edgeisblocked = false;

        if (isPointOrSegmentFixed(Obj, GeoId[0])) {
            edgeisblocked = true;
        }

        Base::Vector3d pnt = Obj->getPoint(GeoId[0], PosId[0]);

        // undo command open
        openCommand(QT_TRANSLATE_NOOP("Command", "Add 'Lock' constraint"));
        Gui::cmdAppObjectArgs(selection[0].getObject(),
                              "addConstraint(Sketcher.Constraint('DistanceX',%d,%d,%f))",
                              GeoId[0],
                              static_cast<int>(PosId[0]),
                              pnt.x);
        Gui::cmdAppObjectArgs(selection[0].getObject(),
                              "addConstraint(Sketcher.Constraint('DistanceY',%d,%d,%f))",
                              GeoId[0],
                              static_cast<int>(PosId[0]),
                              pnt.y);

        lastconstraintindex += 2;

        if (edgeisblocked || GeoId[0] <= Sketcher::GeoEnum::RefExt
            || constraintCreationMode == Reference) {
            // it is a constraint on a external line, make it non-driving

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "setDriving(%d,%s)",
                                  lastconstraintindex - 1,
                                  "False");

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "setDriving(%d,%s)",
                                  lastconstraintindex,
                                  "False");
        }
    }
    else {
        std::vector<int>::const_iterator itg;
        std::vector<Sketcher::PointPos>::const_iterator itp;

        Base::Vector3d pntr = Obj->getPoint(GeoId.back(), PosId.back());

        // check if the edge already has a Block constraint
        bool refpointfixed = false;

        if (isPointOrSegmentFixed(Obj, GeoId.back())) {
            refpointfixed = true;
        }

        for (itg = GeoId.begin(), itp = PosId.begin();
             itg != std::prev(GeoId.end()) && itp != std::prev(PosId.end());
             ++itp, ++itg) {
            bool pointfixed = false;

            if (isPointOrSegmentFixed(Obj, *itg)) {
                pointfixed = true;
            }

            Base::Vector3d pnt = Obj->getPoint(*itg, *itp);

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add relative 'Lock' constraint"));
            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('DistanceX',%d,%d,%d,%d,%f))",
                                  *itg,
                                  static_cast<int>(*itp),
                                  GeoId.back(),
                                  static_cast<int>(PosId.back()),
                                  pntr.x - pnt.x);

            Gui::cmdAppObjectArgs(selection[0].getObject(),
                                  "addConstraint(Sketcher.Constraint('DistanceY',%d,%d,%d,%d,%f))",
                                  *itg,
                                  static_cast<int>(*itp),
                                  GeoId.back(),
                                  static_cast<int>(PosId.back()),
                                  pntr.y - pnt.y);
            lastconstraintindex += 2;

            if ((refpointfixed && pointfixed) || constraintCreationMode == Reference) {
                // it is a constraint on a external line, make it non-driving

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "setDriving(%d,%s)",
                                      lastconstraintindex - 1,
                                      "False");

                Gui::cmdAppObjectArgs(selection[0].getObject(),
                                      "setDriving(%d,%s)",
                                      lastconstraintindex,
                                      "False");
            }
        }
    }

    // finish the transaction and update
    commitCommand();

    tryAutoRecompute(Obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
}

void CmdSketcherConstrainLock::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    switch (seqIndex) {
        case 0:// {Vertex}
            // Create the constraints
            SketcherGui::ViewProviderSketch* sketchgui =
                static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
            Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

            // check if the edge already has a Block constraint
            bool pointfixed = false;

            if (selSeq.empty()) {
                return;
            }

            if (isPointOrSegmentFixed(Obj, selSeq.front().GeoId)) {
                pointfixed = true;
            }

            Base::Vector3d pnt = Obj->getPoint(selSeq.front().GeoId, selSeq.front().PosId);

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add fixed constraint"));
            Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                  "addConstraint(Sketcher.Constraint('DistanceX', %d, %d, %f))",
                                  selSeq.front().GeoId,
                                  static_cast<int>(selSeq.front().PosId),
                                  pnt.x);
            Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                  "addConstraint(Sketcher.Constraint('DistanceY', %d, %d, %f))",
                                  selSeq.front().GeoId,
                                  static_cast<int>(selSeq.front().PosId),
                                  pnt.y);

            if (pointfixed || constraintCreationMode == Reference) {
                // it is a constraint on a external line, make it non-driving
                const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

                Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                      "setDriving(%d, %s)",
                                      ConStr.size() - 2,
                                      "False");

                Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                      "setDriving(%d, %s)",
                                      ConStr.size() - 1,
                                      "False");
            }

            // finish the transaction and update
            commitCommand();

            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Mod/Sketcher");
            bool autoRecompute = hGrp->GetBool("AutoRecompute", false);

            if (autoRecompute) {
                Gui::Command::updateActive();
            }
            break;
    }
}

void CmdSketcherConstrainLock::updateAction(int mode)
{
    switch (mode) {
        case Reference:
            if (getAction()) {
                getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Lock_Driven"));
            }
            break;
        case Driving:
            if (getAction()) {
                getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Lock"));
            }
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainBlock: public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainBlock();
    ~CmdSketcherConstrainBlock() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainBlock";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainBlock::CmdSketcherConstrainBlock()
    : CmdSketcherConstraint("Sketcher_ConstrainBlock")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Block Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected edges as fixed");
    sWhatsThis = "Sketcher_ConstrainBlock";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Block";
    sAccel = "K, B";
    eType = ForEdit;

    allowedSelSequences = {{SelEdge}};
}

void CmdSketcherConstrainBlock::activated(int iMsg)
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

    // Check that the solver does not report redundant/conflicting constraints
    if (Obj->getLastSolverStatus() != GCS::Success || Obj->getLastHasConflicts()
        || Obj->getLastHasRedundancies()) {
        Gui::TranslatedUserWarning(Obj,
                                   QObject::tr("Wrong solver status"),
                                   QObject::tr("A block constraint cannot be added "
                                               "if the sketch is unsolved "
                                               "or there are redundant and "
                                               "conflicting constraints."));
        return;
    }

    std::vector<int> GeoId;
    const std::vector<Sketcher::Constraint*>& vals = Obj->Constraints.getValues();

    for (auto& subname : SubNames) {
        int GeoIdt;
        Sketcher::PointPos PosIdt;
        getIdsFromName(subname, Obj, GeoIdt, PosIdt);

        if (isVertex(GeoIdt, PosIdt) || GeoIdt < 0) {
            if (selection.size() == 1) {
                Gui::TranslatedUserWarning(Obj,
                                           QObject::tr("Wrong selection"),
                                           QObject::tr("Select one edge from the sketch."));
            }
            else {
                Gui::TranslatedUserWarning(Obj,
                                           QObject::tr("Wrong selection"),
                                           QObject::tr("Select only edges from the sketch."));
            }
            // clear the selection
            getSelection().clearSelection();
            return;
        }

        // check if the edge already has a Block constraint
        if (checkConstraint(vals, Sketcher::Block, GeoIdt, Sketcher::PointPos::none)) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Double constraint"),
                QObject::tr("The selected edge already has a block constraint!"));
            return;
        }

        GeoId.push_back(GeoIdt);
    }

    for (std::vector<int>::iterator itg = GeoId.begin(); itg != GeoId.end(); ++itg) {
        // undo command open
        openCommand(QT_TRANSLATE_NOOP("Command", "Add Block constraint"));

        bool safe = addConstraintSafely(Obj, [&]() {
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Block',%d))", (*itg));
        });

        if (!safe) {
            abortCommand();
            return;
        }
        commitCommand();
        tryAutoRecompute(Obj);
    }

    // clear the selection (convenience)
    getSelection().clearSelection();
}

void CmdSketcherConstrainBlock::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    switch (seqIndex) {
        case 0:// {Edge}
        {
            // Create the constraints
            SketcherGui::ViewProviderSketch* sketchgui =
                static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());

            auto Obj = sketchgui->getObject<Sketcher::SketchObject>();

            // check if the edge already has a Block constraint
            const std::vector<Sketcher::Constraint*>& vals = Obj->Constraints.getValues();

            if (selSeq.empty()) {
                return;
            }

            if (checkConstraint(vals,
                                Sketcher::Block,
                                selSeq.front().GeoId,
                                Sketcher::PointPos::none)) {
                Gui::TranslatedUserWarning(
                    Obj,
                    QObject::tr("Double constraint"),
                    QObject::tr("The selected edge already has a block constraint!"));
                return;
            }

            // undo command open
            openCommand(QT_TRANSLATE_NOOP("Command", "Add block constraint"));

            bool safe = addConstraintSafely(Obj, [&]() {
                Gui::cmdAppObjectArgs(sketchgui->getObject(),
                                      "addConstraint(Sketcher.Constraint('Block',%d))",
                                      selSeq.front().GeoId);
            });

            if (!safe) {
                abortCommand();
                return;
            }

            commitCommand();
            tryAutoRecompute(Obj);
        } break;
        default:
            break;
    }
}

// ======================================================================================

class CmdSketcherConstrainCoincidentUnified : public CmdSketcherConstraint
{
public:
    CmdSketcherConstrainCoincidentUnified(const char* initName = "Sketcher_ConstrainCoincidentUnified");
    ~CmdSketcherConstrainCoincidentUnified() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainCoincidentUnified";
    }

protected:
    enum class CoincicenceType {
        Coincident,
        PointOnObject,
        Both
    };

    void activated(int iMsg) override;
    void onActivated(CoincicenceType type);
    void activatedCoincident(SketchObject* obj, std::vector<SelIdPair> points, std::vector<SelIdPair> curves);
    void activatedPointOnObject(SketchObject* obj, std::vector<SelIdPair> points, std::vector<SelIdPair> curves);

    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
    void applyConstraintCoincident(std::vector<SelIdPair>& selSeq, int seqIndex);
    void applyConstraintPointOnObject(std::vector<SelIdPair>& selSeq, int seqIndex);

    // returns true if a substitution took place
    static bool substituteConstraintCombinationsPointOnObject(SketchObject* Obj, int GeoId1, PointPos PosId1, int GeoId2);
    static bool substituteConstraintCombinationsCoincident(SketchObject* Obj, int GeoId1, PointPos PosId1, int GeoId2, PointPos PosId2);

    bool isCoincidentSelectionValid(SketchObject* obj, int GeoId1, PointPos PosId1, int GeoId2, PointPos PosId2);
};

CmdSketcherConstrainCoincidentUnified::CmdSketcherConstrainCoincidentUnified(const char* initName)
    : CmdSketcherConstraint(initName)
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Coincident Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements to be coincident");
    sWhatsThis = "Sketcher_ConstrainCoincidentUnified";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Coincident";

    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Constraints");
    sAccel = hGrp->GetBool("UnifiedCoincident", true) ? "C" :"C,O";

    eType = ForEdit;

    allowedSelSequences = {{SelVertex, SelEdgeOrAxis},
                           {SelRoot, SelEdge},
                           {SelVertex, SelExternalEdge},
                           {SelEdge, SelVertexOrRoot},
                           {SelEdgeOrAxis, SelVertex},
                           {SelExternalEdge, SelVertex},

                           {SelVertexOrRoot, SelVertexOrRoot},
                           {SelEdge, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge} };
}

bool CmdSketcherConstrainCoincidentUnified::substituteConstraintCombinationsPointOnObject(SketchObject* Obj, int GeoId1, PointPos PosId1, int GeoId2)
{
    const std::vector<Constraint*>& cvals = Obj->Constraints.getValues();

    int cid = 0;
    for (std::vector<Constraint*>::const_iterator it = cvals.begin(); it != cvals.end();
        ++it, ++cid) {
        if ((*it)->Type == Sketcher::Tangent && (*it)->FirstPos == Sketcher::PointPos::none
            && (*it)->SecondPos == Sketcher::PointPos::none && (*it)->Third == GeoEnum::GeoUndef
            && (((*it)->First == GeoId1 && (*it)->Second == GeoId2)
                || ((*it)->Second == GeoId1 && (*it)->First == GeoId2))
            && (PosId1 == Sketcher::PointPos::start
                || PosId1 == Sketcher::PointPos::end)) {

            // NOTE: This function does not either open or commit a command as it is used for group
            // addition it relies on such infrastructure being provided by the caller.

            Gui::cmdAppObjectArgs(Obj, "delConstraint(%d)", cid);

            doEndpointToEdgeTangency(Obj, GeoId1, PosId1, GeoId2);

            notifyConstraintSubstitutions(
                QObject::tr("Endpoint to edge tangency was applied instead."));

            getSelection().clearSelection();
            return true;
        }
    }

    return false;
}

bool CmdSketcherConstrainCoincidentUnified::substituteConstraintCombinationsCoincident(SketchObject* Obj, int GeoId1, PointPos PosId1, int GeoId2, PointPos PosId2)
{
    // checks for direct and indirect coincidence constraints
    bool constraintExists = Obj->arePointsCoincident(GeoId1, PosId1, GeoId2, PosId2);

    const std::vector<Constraint*>& cvals = Obj->Constraints.getValues();

    // NOTE: This function does not either open or commit a command as it is used for group addition
    // it relies on such infrastructure being provided by the caller.

    int j = 0;
    for (std::vector<Constraint*>::const_iterator it = cvals.begin(); it != cvals.end();
        ++it, ++j) {
        if ((*it)->Type == Sketcher::Tangent && (*it)->Third == GeoEnum::GeoUndef
            && (((*it)->First == GeoId1 && (*it)->Second == GeoId2)
                || ((*it)->Second == GeoId1 && (*it)->First == GeoId2))) {
            if (!(PosId1 == Sketcher::PointPos::start
                  || PosId1 == Sketcher::PointPos::end)
                || !(PosId2 == Sketcher::PointPos::start
                     || PosId2 == Sketcher::PointPos::end)) {
                continue;
            }
            if ((*it)->FirstPos == Sketcher::PointPos::none
                && (*it)->SecondPos == Sketcher::PointPos::none) {

                if (constraintExists) {
                    // try to remove any pre-existing direct coincident constraints
                    Gui::cmdAppObjectArgs(Obj,
                        "delConstraintOnPoint(%d,%d)",
                        GeoId1,
                        static_cast<int>(PosId1));
                }

                Gui::cmdAppObjectArgs(Obj, "delConstraint(%d)", j);

                doEndpointTangency(Obj, GeoId1, GeoId2, PosId1, PosId2);

                notifyConstraintSubstitutions(
                    QObject::tr("Endpoint to endpoint tangency was applied instead."));

                getSelection().clearSelection();
                return true;
            }
            else if (isBsplineKnot(Obj, GeoId1) != isBsplineKnot(Obj, GeoId2)) {
                // Replace with knot-to-endpoint tangency

                if (isBsplineKnot(Obj, GeoId2)) {
                    std::swap(GeoId1, GeoId2);
                    std::swap(PosId1, PosId2);
                }

                // if a similar tangency already exists this must result in bad constraints
                if ((*it)->SecondPos == Sketcher::PointPos::none) {
                    Gui::cmdAppObjectArgs(Obj, "delConstraint(%d)", j);

                    doEndpointTangency(Obj, GeoId1, GeoId2, PosId1, PosId2);

                    notifyConstraintSubstitutions(
                        QObject::tr("B-spline knot to endpoint tangency was applied instead."));

                    getSelection().clearSelection();
                    return true;
                }
            }
        }
    }

    return false;
}

void CmdSketcherConstrainCoincidentUnified::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    onActivated(CoincicenceType::Both);
}

void CmdSketcherConstrainCoincidentUnified::onActivated(CoincicenceType type)
{
    QString errorMess;
    if (type == CoincicenceType::Coincident) {
        errorMess = QObject::tr("Select either several points, or several conics for concentricity.");
    }
    else if (type == CoincicenceType::PointOnObject) {
        errorMess = QObject::tr("Select either one point and several curves, or one curve and several points");
    }
    else {
        errorMess = QObject::tr("Select either one point and several curves or one curve and several"
            " points for pointOnObject, or several points for coincidence, or several conics for concentricity.");
    }

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
            Gui::TranslatedUserWarning(getActiveGuiDocument(), QObject::tr("Wrong selection"), errorMess);
        }
        return;
    }

    // get the needed lists and objects
    const std::vector<std::string>& SubNames = selection[0].getSubNames();
    auto* Obj = static_cast<Sketcher::SketchObject*>(selection[0].getObject());

    // count curves and points
    std::vector<SelIdPair> points;
    std::vector<SelIdPair> curves;
    for (std::size_t i = 0; i < SubNames.size(); i++) {
        SelIdPair id;
        getIdsFromName(SubNames[i], Obj, id.GeoId, id.PosId);
        if (isEdge(id.GeoId, id.PosId)) {
            curves.push_back(id);
        }
        if (isVertex(id.GeoId, id.PosId)) {
            points.push_back(id);
        }
    }

    if (type != CoincicenceType::Coincident && ((points.size() == 1 && !curves.empty()) || (!points.empty() && curves.size() == 1))) {
        activatedPointOnObject(Obj, points, curves);
    }
    else if (type != CoincicenceType::PointOnObject && ((!points.empty() && curves.empty()) || (points.empty() && !curves.empty()))) {
        activatedCoincident(Obj, points, curves);
    }
    else {
        Gui::TranslatedUserWarning(Obj, QObject::tr("Wrong selection"), errorMess);
    }
}

void CmdSketcherConstrainCoincidentUnified::activatedPointOnObject(SketchObject* obj, std::vector<SelIdPair> points, std::vector<SelIdPair> curves)
{
    openCommand(QT_TRANSLATE_NOOP("Command", "Add point on object constraint"));
    int cnt = 0;
    for (std::size_t iPnt = 0; iPnt < points.size(); iPnt++) {
        for (std::size_t iCrv = 0; iCrv < curves.size(); iCrv++) {
            if (areBothPointsOrSegmentsFixed(obj, points[iPnt].GeoId, curves[iCrv].GeoId)) {
                showNoConstraintBetweenFixedGeometry(obj);
                continue;
            }
            if (points[iPnt].GeoId == curves[iCrv].GeoId) {
                continue;// constraining a point of an element onto the element is a bad idea...
            }

            const Part::Geometry* geom = obj->getGeometry(curves[iCrv].GeoId);

            if (geom && isBsplinePole(geom)) {
                Gui::TranslatedUserWarning(
                    obj,
                    QObject::tr("Wrong selection"),
                    QObject::tr("Select an edge that is not a B-spline weight."));
                abortCommand();
                return;
            }

            if (substituteConstraintCombinationsPointOnObject(obj,
                points[iPnt].GeoId,
                points[iPnt].PosId,
                curves[iCrv].GeoId)) {
                cnt++;
                continue;
            }

            cnt++;
            Gui::cmdAppObjectArgs(
                obj,
                "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                points[iPnt].GeoId,
                static_cast<int>(points[iPnt].PosId),
                curves[iCrv].GeoId);
        }
    }
    if (cnt) {
        commitCommand();
        getSelection().clearSelection();
    }
    else {
        abortCommand();
        Gui::TranslatedUserWarning(obj,
            QObject::tr("Wrong selection"),
            QObject::tr("None of the selected points were constrained onto the respective curves, because they are part of the same element, they are both external geometry, or the edge is not eligible."));
    }
}

void CmdSketcherConstrainCoincidentUnified::activatedCoincident(SketchObject* obj, std::vector<SelIdPair> points, std::vector<SelIdPair> curves)
{
    bool allConicsEdges = true;// If user selects only conics (circle, ellipse, arc, arcOfEllipse)
                               // then we make concentric constraint.
    for (auto& curve : curves) {
        if (!isGeoConcentricCompatible(obj->getGeometry(curve.GeoId))) {
            allConicsEdges = false;
        }

        if (!allConicsEdges) {
            Gui::TranslatedUserWarning(
                obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select two or more vertices from the sketch for a coincident "
                    "constraint, or two or more circles, ellipses, arcs or arcs of ellipse "
                    "for a concentric constraint."));
            return;
        }
        curve.PosId = Sketcher::PointPos::mid;
    }

    std::vector<SelIdPair> vecOfSelIdToUse = curves.empty() ? points : curves;

    int GeoId1 = vecOfSelIdToUse[0].GeoId;
    Sketcher::PointPos PosId1 = vecOfSelIdToUse[0].PosId;

    // undo command open
    bool constraintsAdded = false;
    openCommand(QT_TRANSLATE_NOOP("Command", "Add coincident constraint"));

    for (std::size_t i = 1; i < vecOfSelIdToUse.size(); i++) {
        int GeoId2 = vecOfSelIdToUse[i].GeoId;
        Sketcher::PointPos PosId2 = vecOfSelIdToUse[i].PosId;

        // check if the edge already has a Block constraint
        if (areBothPointsOrSegmentsFixed(obj, GeoId1, GeoId2)) {
            showNoConstraintBetweenFixedGeometry(obj);
            return;
        }

        // check if as a consequence of this command undesirable combinations of constraints would
        // arise and substitute them with more appropriate counterparts, examples:
        // - coincidence + tangency on edge
        // - point on object + tangency on edge
        if (substituteConstraintCombinationsCoincident(obj, GeoId1, PosId1, GeoId2, PosId2)) {
            constraintsAdded = true;
            break;
        }

        if (isCoincidentSelectionValid(obj, GeoId1, PosId1, GeoId2, PosId2)) {
            constraintsAdded = true;
            Gui::cmdAppObjectArgs(obj,
                "addConstraint(Sketcher.Constraint('Coincident',%d,%d,%d,%d))",
                GeoId1,
                static_cast<int>(PosId1),
                GeoId2,
                static_cast<int>(PosId2));
        }
    }

    // finish or abort the transaction and update
    if (constraintsAdded) {
        commitCommand();
    }
    else {
        abortCommand();
    }

    tryAutoRecompute(obj);

    // clear the selection (convenience)
    getSelection().clearSelection();
}

void CmdSketcherConstrainCoincidentUnified::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    switch (seqIndex) {
    case 0:// {SelVertex, SelEdgeOrAxis}
    case 1:// {SelRoot, SelEdge}
    case 2:// {SelVertex, SelExternalEdge}
    case 3:// {SelEdge, SelVertexOrRoot}
    case 4:// {SelEdgeOrAxis, SelVertex}
    case 5:// {SelExternalEdge, SelVertex}
        applyConstraintPointOnObject(selSeq, seqIndex);
        break;
    case 6:// {SelVertexOrRoot, SelVertexOrRoot}
    case 7:// {SelEdge, SelEdge}
    case 8:// {SelEdge, SelExternalEdge}
    case 9:// {SelExternalEdge, SelEdge}
        seqIndex -= 6;
        applyConstraintCoincident(selSeq, seqIndex);
        break;
    default:
        return;
    }
}

void CmdSketcherConstrainCoincidentUnified::applyConstraintPointOnObject(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    int GeoIdVt, GeoIdCrv;
    Sketcher::PointPos PosIdVt;

    switch (seqIndex) {
    case 0:// {SelVertex, SelEdgeOrAxis}
    case 1:// {SelRoot, SelEdge}
    case 2:// {SelVertex, SelExternalEdge}
        GeoIdVt = selSeq.at(0).GeoId;
        GeoIdCrv = selSeq.at(1).GeoId;
        PosIdVt = selSeq.at(0).PosId;

        break;
    case 3:// {SelEdge, SelVertexOrRoot}
    case 4:// {SelEdgeOrAxis, SelVertex}
    case 5:// {SelExternalEdge, SelVertex}
        GeoIdVt = selSeq.at(1).GeoId;
        GeoIdCrv = selSeq.at(0).GeoId;
        PosIdVt = selSeq.at(1).PosId;

        break;
    default:
        return;
    }

    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    openCommand(QT_TRANSLATE_NOOP("Command", "Add point on object constraint"));
    bool allOK = true;

    if (areBothPointsOrSegmentsFixed(Obj, GeoIdVt, GeoIdCrv)) {
        showNoConstraintBetweenFixedGeometry(Obj);
        allOK = false;
    }
    if (GeoIdVt == GeoIdCrv) {
        allOK = false;// constraining a point of an element onto the element is a bad idea...
    }

    const Part::Geometry* geom = Obj->getGeometry(GeoIdCrv);

    if (geom && isBsplinePole(geom)) {
        Gui::TranslatedUserWarning(Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("Select an edge that is not a B-spline weight."));
        abortCommand();

        return;
    }

    if (allOK) {
        if (!substituteConstraintCombinationsPointOnObject(Obj, GeoIdVt, PosIdVt, GeoIdCrv)) {
            Gui::cmdAppObjectArgs(sketchgui->getObject(),
                "addConstraint(Sketcher.Constraint('PointOnObject',%d,%d,%d))",
                GeoIdVt,
                static_cast<int>(PosIdVt),
                GeoIdCrv);
        }

        commitCommand();
        tryAutoRecompute(Obj);
    }
    else {
        abortCommand();
        Gui::TranslatedUserWarning(Obj,
            QObject::tr("Wrong selection"),
            QObject::tr("None of the selected points "
                "were constrained onto the respective curves, "
                "either because they are parts of the same element, "
                "or because they are both external geometry."));
    }
}

void CmdSketcherConstrainCoincidentUnified::applyConstraintCoincident(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    SketcherGui::ViewProviderSketch* sketchgui =
        static_cast<SketcherGui::ViewProviderSketch*>(getActiveGuiDocument()->getInEdit());
    Sketcher::SketchObject* Obj = sketchgui->getSketchObject();

    int GeoId1 = selSeq.at(0).GeoId, GeoId2 = selSeq.at(1).GeoId;
    Sketcher::PointPos PosId1 = selSeq.at(0).PosId, PosId2 = selSeq.at(1).PosId;

    switch (seqIndex) {
    case 0:// {SelVertexOrRoot, SelVertexOrRoot}
        // Nothing specific.
        break;
    case 1:// {SelEdge, SelEdge}
    case 2:// {SelEdge, SelExternalEdge}
    case 3:// {SelExternalEdge, SelEdge}
        // Concentric for circles, ellipse, arc, arcofEllipse only.
        if (!isGeoConcentricCompatible(Obj->getGeometry(GeoId1))
            || !isGeoConcentricCompatible(Obj->getGeometry(GeoId2))) {
            Gui::TranslatedUserWarning(
                Obj,
                QObject::tr("Wrong selection"),
                QObject::tr(
                    "Select two vertices from the sketch for a coincident constraint, or two "
                    "circles, ellipses, arcs or arcs of ellipse for a concentric constraint."));
            return;
        }
        PosId1 = Sketcher::PointPos::mid;
        PosId2 = Sketcher::PointPos::mid;
        break;
    }

    // check if the edge already has a Block constraint
    if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
        showNoConstraintBetweenFixedGeometry(Obj);
        return;
    }

    // undo command open
    openCommand(QT_TRANSLATE_NOOP("Command", "Add coincident constraint"));

    if (substituteConstraintCombinationsCoincident(Obj, GeoId1, PosId1, GeoId2, PosId2)) {}
    else if (isCoincidentSelectionValid(Obj, GeoId1, PosId1, GeoId2, PosId2)) {
        Gui::cmdAppObjectArgs(sketchgui->getObject(),
            "addConstraint(Sketcher.Constraint('Coincident', %d, %d, %d, %d))",
            GeoId1,
            static_cast<int>(PosId1),
            GeoId2,
            static_cast<int>(PosId2));
    }
    else {
        abortCommand();
        return;
    }
    commitCommand();
    tryAutoRecompute(Obj);
}

bool CmdSketcherConstrainCoincidentUnified::isCoincidentSelectionValid(SketchObject* obj, int GeoId1, PointPos PosId1, int GeoId2, PointPos PosId2)
{
    // check if this coincidence is already enforced (even indirectly)
    bool constraintExists = obj->arePointsCoincident(GeoId1, PosId1, GeoId2, PosId2);
    bool sameGeo = GeoId1 == GeoId2;

    const Part::Geometry* geo = obj->getGeometry(GeoId1);
    bool isBSpline = geo && geo->is<Part::GeomBSplineCurve>();

    return !constraintExists && (!sameGeo || isBSpline);
}

// ======================================================================================

class CmdSketcherConstrainCoincident: public CmdSketcherConstrainCoincidentUnified
{
public:
    CmdSketcherConstrainCoincident();
    ~CmdSketcherConstrainCoincident() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainCoincident";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainCoincident::CmdSketcherConstrainCoincident()
    : CmdSketcherConstrainCoincidentUnified("Sketcher_ConstrainCoincident")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Coincident Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected elements to be coincident");
    sWhatsThis = "Sketcher_ConstrainCoincident";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_PointOnPoint";
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Constraints");
    sAccel = hGrp->GetBool("UnifiedCoincident", true) ? "C,C" : "C";
    eType = ForEdit;

    allowedSelSequences = {{SelVertexOrRoot, SelVertexOrRoot},
                           {SelEdge, SelEdge},
                           {SelEdge, SelExternalEdge},
                           {SelExternalEdge, SelEdge}};
}

void CmdSketcherConstrainCoincident::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    onActivated(CoincicenceType::Coincident);
}

void CmdSketcherConstrainCoincident::applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex)
{
    applyConstraintCoincident(selSeq, seqIndex);
}

// ======================================================================================

class CmdSketcherConstrainPointOnObject: public CmdSketcherConstrainCoincidentUnified
{
public:
    CmdSketcherConstrainPointOnObject();
    ~CmdSketcherConstrainPointOnObject() override
    {}
    const char* className() const override
    {
        return "CmdSketcherConstrainPointOnObject";
    }

protected:
    void activated(int iMsg) override;
    void applyConstraint(std::vector<SelIdPair>& selSeq, int seqIndex) override;
};

CmdSketcherConstrainPointOnObject::CmdSketcherConstrainPointOnObject()
    : CmdSketcherConstrainCoincidentUnified("Sketcher_ConstrainPointOnObject")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Point-On-Object Constraint");
    sToolTipText = QT_TR_NOOP("Constrains the selected point onto the selected object");
    sWhatsThis = "Sketcher_ConstrainPointOnObject";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_PointOnObject";
    sAccel = "O";
    eType = ForEdit;

    allowedSelSequences = {{SelVertex, SelEdgeOrAxis},
                           {SelRoot, SelEdge},
                           {SelVertex, SelExternalEdge},
                           {SelEdge, SelVertexOrRoot},
                           {SelEdgeOrAxis, SelVertex},
                           {SelExternalEdge, SelVertex}};
}

void CmdSketcherConstrainPointOnObject::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    onActivated(CoincicenceType::PointOnObject);
}

void CmdSketcherConstrainPointOnObject::applyConstraint(std::vector<SelIdPair>& selSeq,
                                                        int seqIndex)
{
    applyConstraintPointOnObject(selSeq, seqIndex);
}

void SketcherGui::addPositionConstraintCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdSketcherConstrainHorVer());
    rcCmdMgr.addCommand(new CmdSketcherConstrainHorizontal());
    rcCmdMgr.addCommand(new CmdSketcherConstrainVertical());
    rcCmdMgr.addCommand(new CmdSketcherConstrainLock());
    rcCmdMgr.addCommand(new CmdSketcherConstrainBlock());
    rcCmdMgr.addCommand(new CmdSketcherConstrainCoincidentUnified());
    rcCmdMgr.addCommand(new CmdSketcherConstrainCoincident());
    rcCmdMgr.addCommand(new CmdSketcherConstrainPointOnObject());
}
