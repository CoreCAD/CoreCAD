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
// PartDesign_Clone
//===========================================================================

DEF_STD_CMD_A(CmdPartDesignClone)

CmdPartDesignClone::CmdPartDesignClone()
    : Command("PartDesign_Clone")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Clone");
    sToolTipText = QT_TR_NOOP("Copies a solid object parametrically as the base feature of a new body");
    sWhatsThis = "PartDesign_Clone";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Clone";
}

void CmdPartDesignClone::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> objs = getSelection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::erase_if(objs, [](App::DocumentObject* o) { return !Part::hasShape(o); });

    if (objs.size() == 1) {
        // Cruth §4.6: the clone still lands in a body of its own, but the body is not minted
        // ahead of it. The user creates a feature; the body is the system's accounting of the
        // solid that results. The spawn happens INSIDE the transaction opened here, so
        // cancelling the clone removes the body with it and leaves no empty Body behind.
        openCommand(QT_TRANSLATE_NOOP("Command", "Create Clone"));

        auto obj = objs[0];
        auto objCmd = getObjectCmd(obj);
        std::string cloneName = getUniqueObjectName("Clone", obj);

        auto* bodyObj = PartDesign::Body::spawnAutoBody(obj->getDocument());
        if (!bodyObj) {
            abortCommand();
            return;
        }

        // createFeature births the clone at document level, then splices it into the Body's
        // pipeline (Tip + BaseFeature chain). A de-owned Body has no Group to write.
        auto cloneObj = PartDesignGui::createFeature(bodyObj, "PartDesign::FeatureBase", cloneName);
        if (!cloneObj) {
            abortCommand();
            return;
        }

        // The clone's own link to the object it copies. Cruth Amendment 4: feature geometry is
        // world-frame and position belongs only to anchors, so there is no Placement to copy —
        // the clone is coincident with its source without one. (The old code copied Placement and
        // un-hid it to make the clone independently movable; that property no longer exists on
        // Part::Feature, so those two lines had been throwing since Amendment 4 merged.)
        Gui::cmdAppObject(cloneObj, std::stringstream() << "BaseFeature = " << objCmd);

        updateActive();
        copyAppearance(cloneObj, obj);
        commitCommand();
    }
}

bool CmdPartDesignClone::isActive()
{
    return getSelection().countObjectsOfType<Part::ShapeFeature>() == 1;
}

//===========================================================================
// PartDesign_Sketch
//===========================================================================

/* Sketch commands =======================================================*/
DEF_STD_CMD_A(CmdPartDesignNewSketch)

CmdPartDesignNewSketch::CmdPartDesignNewSketch()
    : Command("PartDesign_NewSketch")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("New Sketch");
    sToolTipText = QT_TR_NOOP("Creates a new sketch");
    sWhatsThis = "PartDesign_NewSketch";
    sStatusTip = sToolTipText;
    sPixmap = "Sketcher_NewSketch";
}


void CmdPartDesignNewSketch::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesignGui::SketchWorkflow creator(getActiveGuiDocument());
    creator.createSketch();
}

bool CmdPartDesignNewSketch::isActive()
{
    // CoreCAD §4.6: sketch creation is available whenever a document is open;
    // a Body is no longer a precondition.
    return getActiveGuiDocument() != nullptr;
}
//===========================================================================
// PartDesign_Boolean
//===========================================================================

/* Boolean commands =======================================================*/

// Cruth Amendment 5 §8.3 / Clause 5.3 — the "Apply to: A / B / Both" prompt. Given the bodies a
// single tool reaches (always including the resolved target, the user's explicit choice), let the
// user choose which to cut. One checkbox per body, all checked by default; returns the chosen
// bodies in the same order, or an empty vector if the user cancels. The reach set is computed once,
// at creation time — the choice is then resolved to sibling features and never re-queried
// (Clause 5.3).
static std::vector<PartDesign::Body*> chooseBodiesToAffect(const std::vector<PartDesign::Body*>& reached)
{
    QDialog dlg(Gui::getMainWindow());
    dlg.setWindowTitle(QObject::tr("Apply to multiple bodies"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(
        new QLabel(QObject::tr("This cut reaches %1 bodies. Apply it to:").arg(reached.size()), &dlg)
    );

    std::vector<QCheckBox*> boxes;
    boxes.reserve(reached.size());
    for (auto* body : reached) {
        auto* box = new QCheckBox(QString::fromUtf8(body->Label.getValue()), &dlg);
        box->setChecked(true);
        layout->addWidget(box);
        boxes.push_back(box);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    std::vector<PartDesign::Body*> chosen;
    if (dlg.exec() != QDialog::Accepted) {
        return chosen;  // cancelled
    }
    for (std::size_t i = 0; i < reached.size(); ++i) {
        if (boxes[i]->isChecked()) {
            chosen.push_back(reached[i]);
        }
    }
    return chosen;
}

DEF_STD_CMD_A(CmdPartDesignBoolean)

CmdPartDesignBoolean::CmdPartDesignBoolean()
    : Command("PartDesign_Boolean")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Boolean Operation");
    sToolTipText = QT_TR_NOOP(
        "Applies boolean operations with the selected objects as tools on the remaining body"
    );
    sWhatsThis = "PartDesign_Boolean";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Boolean";
}


void CmdPartDesignBoolean::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // Cruth §8.5/§4.6: a Boolean is told its target — the selection names the tools, and the
    // target is the body they leave over (asked for when that is ambiguous), never whichever
    // body happens to be active.
    PartDesign::Body* pcTargetBody = PartDesignGui::resolveBooleanTarget(this);
    if (!pcTargetBody) {
        return;
    }

    // The selection names the tools. Read it directly: a Body is no longer a Part::Feature, so a
    // "SELECT Part::Feature" filter silently dropped every tree-picked body.
    std::vector<App::DocumentObject*> selected;
    for (auto* obj : getSelection().getObjectsOfType(App::DocumentObject::getClassTypeId())) {
        if (obj->isDerivedFrom<PartDesign::Body>() || obj->isDerivedFrom<Part::Feature>()) {
            selected.push_back(obj);
        }
    }

    // Cruth Amendment 5 §8.3 — multi-body scope. If the selection resolves to exactly one tool
    // body, ask which bodies its cut reaches (tool ∩ Body ≠ ∅); when it reaches more than just the
    // target, prompt "Apply to: A / B / Both" and fan the cut out to one sibling per chosen
    // body (each advancing its own chain, sharing the one tool). Anything else falls through to the
    // classic single-Boolean-on-target path below.
    if (!selected.empty()) {
        std::vector<PartDesign::Body*> toolBodies;
        for (App::DocumentObject* obj : selected) {
            // The selection may be a Body itself (tree pick), or a feature whose Body is
            // *derived* by walking its BaseShape chain (findBodyOf) — never a stored edge.
            auto* b = freecad_cast<PartDesign::Body*>(obj);
            if (!b) {
                b = PartDesignGui::getBodyFor(obj, /*messageIfNot=*/false);
            }
            if (b && b != pcTargetBody
                && std::find(toolBodies.begin(), toolBodies.end(), b) == toolBodies.end()) {
                toolBodies.push_back(b);
            }
        }
        if (toolBodies.size() == 1) {
            PartDesign::Body* tool = toolBodies.front();
            // Cruth §3.3: a Body holds no stored geometry; derive it from the Tip.
            const Part::TopoShape toolShape = tool->derivedTipShape();
            // Candidate targets: the resolved target (explicit intent) plus every other body the
            // tool reaches. Order: target first, then document order.
            std::vector<PartDesign::Body*> reached {pcTargetBody};
            for (auto* obj : getDocument()->getObjectsOfType(PartDesign::Body::getClassTypeId())) {
                auto* cand = static_cast<PartDesign::Body*>(obj);
                if (cand != pcTargetBody && cand != tool
                    && PartDesign::Body::toolReaches(toolShape, cand->derivedTipShape())) {
                    reached.push_back(cand);
                }
            }
            if (reached.size() > 1) {
                std::vector<PartDesign::Body*> chosen = chooseBodiesToAffect(reached);
                if (chosen.empty()) {
                    return;  // user cancelled the gesture
                }
                openCommand(QT_TRANSLATE_NOOP("Command", "Cut through bodies"));
                PartDesign::Body::spawnScopeSiblings(tool, chosen, "Cut");
                commitCommand();
                updateActive();
                doCommand(Gui, "Gui.Selection.clearSelection()");
                return;
            }
        }
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Create Boolean"));
    App::DocumentObject* Feat = startFeature(this, pcTargetBody, "Boolean");
    if (!Feat) {
        return;
    }

    // If we don't add an object to the boolean group then don't update the body
    // as otherwise this will fail and it will be marked as invalid
    bool updateDocument = false;
    if (!selected.empty()) {
        std::vector<App::DocumentObject*> bodies;
        for (App::DocumentObject* obj : selected) {
            // Anything in the target itself (the Body, or one of its features) is not a tool.
            if (obj != pcTargetBody
                && PartDesignGui::getBodyFor(obj, /*messageIfNot=*/false) != pcTargetBody
                && std::find(bodies.begin(), bodies.end(), obj) == bodies.end()) {
                bodies.push_back(obj);
            }
        }
        if (!bodies.empty()) {
            updateDocument = true;
            std::string bodyString = PartDesignGui::buildLinkListPythonStr(bodies);
            FCMD_OBJ_CMD(Feat, "Tools = " << bodyString);
        }
    }

    finishFeature(this, Feat, nullptr, false, updateDocument);
}

bool CmdPartDesignBoolean::isActive()
{
    return hasActiveDocument() && !Gui::Control().activeDialog();
}

// Command group for datums =============================================

class CmdPartDesignCompSketches: public Gui::GroupCommand
{
public:
    CmdPartDesignCompSketches()
        : GroupCommand("PartDesign_CompSketches")
    {
        sAppModule = "PartDesign";
        sGroup = "PartDesign";
        sMenuText = QT_TR_NOOP("Create Datum");
        sToolTipText = QT_TR_NOOP("Creates a datum object or local coordinate system");
        sWhatsThis = "PartDesign_CompDatums";
        sStatusTip = sToolTipText;
        eType = ForEdit;

        setCheckable(false);
        setRememberLast(false);

        addCommand("PartDesign_NewSketch");
        addCommand("Sketcher_MapSketch");
        addCommand("Sketcher_EditSketch");
    }

    const char* className() const override
    {
        return "CmdPartDesignCompSketches";
    }

    bool isActive() override
    {
        return (hasActiveDocument() && !Gui::Control().activeDialog());
    }
};

//===========================================================================
// Initialization
//===========================================================================

void CreatePartDesignCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdPartDesignClone());

    rcCmdMgr.addCommand(new CmdPartDesignNewSketch());

    CreatePartDesignProfileBasedCommands(rcCmdMgr);

    CreatePartDesignDressUpCommands(rcCmdMgr);

    CreatePartDesignTransformedCommands(rcCmdMgr);

    rcCmdMgr.addCommand(new CmdPartDesignBoolean());
    rcCmdMgr.addCommand(new CmdPartDesignCompSketches());
}
