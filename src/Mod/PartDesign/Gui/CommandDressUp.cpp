// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
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


#include <algorithm>
#include <optional>

#include <BRepAdaptor_Surface.hxx>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <TopoDS.hxx>


#include <App/Origin.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/CommandT.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionObject.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureGroove.h>
#include <Mod/PartDesign/App/FeatureMultiTransform.h>
#include <Mod/PartDesign/App/FeatureRevolution.h>
#include <Mod/PartDesign/App/FeatureTransformed.h>
#include <Mod/PartDesign/App/FeatureDressUp.h>

#include "ReferenceSelection.h"
#include "SketchPickDialog.h"
#include "SketchWorkflow.h"
#include "Utils.h"
#include "ViewProvider.h"

#include "CommandSupport.h"


FC_LOG_LEVEL_INIT("PartDesign", true, true)

using namespace std;
using namespace PartDesignGui::CommandSupport;

//===========================================================================
// Common utility functions for Dressup features
//===========================================================================

struct DressupKind
{
    const char* type;     // after "PartDesign::"; also names the feature and its undo step
    bool edgesByDefault;  // a pick of the whole shape means every edge
    bool (*keeps)(const Part::TopoShape& shape, const std::string& element) = nullptr;
};

struct DressupPick
{
    Part::ShapeFeature* base = nullptr;
    std::vector<std::string> elements;
    bool allEdges = false;
};

// #136: a pattern copy is drawn through its Body, so a pick names the Body's own elements.
// A step names what it builds on against the shown step, so each pick is translated there.
static std::optional<std::vector<std::string>> shownElementsOf(
    PartDesign::Body* body,
    std::vector<std::string> picked,
    bool edgesByDefault
)
{
    if (picked.empty() && edgesByDefault) {
        picked = allEdgeNames(body->Shape.getShape());
    }
    std::vector<std::string> onShown;
    for (const auto& element : picked) {
        std::string tipElement = body->shownSubElement(element.c_str());
        if (tipElement.empty()) {
            warnWrongSelection(
                QObject::tr("%1 is not part of this body's last feature.")
                    .arg(QString::fromStdString(element))
            );
            return std::nullopt;
        }
        onShown.push_back(std::move(tipElement));
    }
    return onShown;
}

// With nothing picked, the dress-up works on the selected body's last step. The user's
// selection is left as it is when the pick is refused.
static std::optional<DressupPick> pickForDressup(Gui::Command* cmd, const DressupKind& kind)
{
    std::vector<Gui::SelectionObject> selection = cmd->getSelection().getSelectionEx();
    if (selection.size() > 1) {
        warnWrongSelection(QObject::tr("Select an edge, face, or body from a single body."));
        return std::nullopt;
    }

    PartDesign::Body* body = selectedBody(cmd);
    if (!body) {
        return std::nullopt;
    }
    if (selection.empty()) {
        return DressupPick {.base = static_cast<Part::ShapeFeature*>(body->shownStep()), .elements = {}};
    }

    App::DocumentObject* picked = selection.front().getObject();
    std::vector<std::string> elements = selection.front().getSubNames();
    if (picked == body && body->shownStep()) {
        auto onShown = shownElementsOf(body, elements, kind.edgesByDefault);
        if (!onShown) {
            return std::nullopt;
        }
        picked = body->shownStep();
        elements = std::move(*onShown);
    }
    if (PartDesignGui::getBodyFor(picked, false) != body
        && !PartDesign::Body::backsBody(picked, body)) {
        warnWrongSelection(QObject::tr("Select an edge, face, or body from a body."));
        return std::nullopt;
    }
    if (!Part::hasShape(picked)) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong object type"),
            QObject::tr("%1 works only on parts.").arg(QString::fromLatin1(kind.type))
        );
        return std::nullopt;
    }

    DressupPick pick {.base = static_cast<Part::ShapeFeature*>(picked), .elements = std::move(elements)};
    const Part::TopoShape& shape = pick.base->Shape.getShape();
    if (shape.getShape().IsNull()) {
        warnWrongSelection(QObject::tr("Shape of the selected part is empty"));
        return std::nullopt;
    }
    if (pick.elements.empty() && kind.edgesByDefault) {
        pick.allEdges = true;
        pick.elements = allEdgeNames(shape);
    }
    return pick;
}

static void finishDressupFeature(Gui::Command* cmd, const char* type, const DressupPick& pick)
{
    // A base feature may back several bodies (Cruth §4.7); the picked element decides which
    // one the dress-up extends, falling back to the first when that is ambiguous.
    PartDesign::Body* body = nullptr;
    if (!pick.elements.empty()) {
        try {
            body = PartDesign::Body::bodyOf(pick.base, pick.elements.front().c_str());
        }
        catch (const Base::Exception&) {
            body = nullptr;
        }
    }
    if (!body) {
        body = PartDesignGui::getBodyFor(pick.base, false);
    }
    if (!body) {
        return;
    }

    cmd->openCommand((std::string("Make ") + type).c_str());
    App::DocumentObject* feature = startFeature(cmd, body, type);
    if (!feature) {
        return;
    }
    FCMD_OBJ_CMD(
        feature,
        "Base = (" << Gui::Command::getObjectCmd(pick.base) << ", " << pythonNameList(pick.elements)
                   << ")"
    );
    if (pick.allEdges) {
        FCMD_OBJ_CMD(feature, "UseAllEdges = True");
    }
    finishFeature(cmd, feature, pick.base);

    // A failed dress-up (a fillet too large for its edge) would otherwise leave nothing shown.
    App::DocumentObject* base = static_cast<PartDesign::DressUp*>(feature)->Base.getValue();
    auto* view = base
        ? dynamic_cast<PartDesignGui::ViewProvider*>(Gui::Application::Instance->getViewProvider(base))
        : nullptr;
    if (view && feature->isError()) {
        view->Visibility.setValue(true);
    }
}

static void makeDressup(Gui::Command* cmd, const DressupKind& kind)
{
    std::optional<DressupPick> pick = pickForDressup(cmd, kind);
    if (!pick) {
        return;
    }
    if (kind.keeps) {
        const Part::TopoShape& shape = pick->base->Shape.getShape();
        std::erase_if(pick->elements, [&](const std::string& element) {
            return !kind.keeps(shape, element);
        });
    }
    finishDressupFeature(cmd, kind.type, *pick);
}

static bool isFace(const Part::TopoShape& /*shape*/, const std::string& element)
{
    return element.starts_with("Face");
}

static bool isDraftableFace(const Part::TopoShape& shape, const std::string& element)
{
    if (!isFace(shape, element)) {
        return false;
    }
    BRepAdaptor_Surface surface(TopoDS::Face(shape.getSubShape(element.c_str())));
    const GeomAbs_SurfaceType type = surface.GetType();
    return type == GeomAbs_Plane || type == GeomAbs_Cylinder || type == GeomAbs_Cone;
}

//===========================================================================
// PartDesign_Fillet
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignFillet)

CmdPartDesignFillet::CmdPartDesignFillet()
    : Command("PartDesign_Fillet")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Fillet");
    sToolTipText = QT_TR_NOOP("Applies a fillet to the selected edges or faces");
    sWhatsThis = "PartDesign_Fillet";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Fillet";
}

void CmdPartDesignFillet::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeDressup(this, {.type = "Fillet", .edgesByDefault = true});
}

bool CmdPartDesignFillet::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_Chamfer
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignChamfer)

CmdPartDesignChamfer::CmdPartDesignChamfer()
    : Command("PartDesign_Chamfer")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Chamfer");
    sToolTipText = QT_TR_NOOP("Applies a chamfer to the selected edges or faces");
    sWhatsThis = "PartDesign_Chamfer";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Chamfer";
}

void CmdPartDesignChamfer::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeDressup(this, {.type = "Chamfer", .edgesByDefault = true});
}

bool CmdPartDesignChamfer::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_Draft
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignDraft)

CmdPartDesignDraft::CmdPartDesignDraft()
    : Command("PartDesign_Draft")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Draft");
    sToolTipText = QT_TR_NOOP("Applies a draft to the selected faces");
    sWhatsThis = "PartDesign_Draft";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Draft";
}

void CmdPartDesignDraft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeDressup(this, {.type = "Draft", .edgesByDefault = false, .keeps = isDraftableFace});
}

bool CmdPartDesignDraft::isActive()
{
    return hasAnyBody();
}


//===========================================================================
// PartDesign_Thickness
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignThickness)

CmdPartDesignThickness::CmdPartDesignThickness()
    : Command("PartDesign_Thickness")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Thickness");
    sToolTipText = QT_TR_NOOP("Applies thickness and removes the selected faces");
    sWhatsThis = "PartDesign_Thickness";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Thickness";
}

void CmdPartDesignThickness::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeDressup(this, {.type = "Thickness", .edgesByDefault = false, .keeps = isFace});
}

bool CmdPartDesignThickness::isActive()
{
    return hasAnyBody();
}

void CreatePartDesignDressUpCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartDesignFillet());
    rcCmdMgr.addCommand(new CmdPartDesignDraft());
    rcCmdMgr.addCommand(new CmdPartDesignChamfer());
    rcCmdMgr.addCommand(new CmdPartDesignThickness());
}
