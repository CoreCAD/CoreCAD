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
// Common functions for all Transformed features
//===========================================================================

using ConfigureTransformed
    = std::function<void(App::DocumentObject* feature, const std::vector<App::DocumentObject*>& originals)>;

// The selected features are the originals; with none, the pattern repeats the whole shape.
static void makeTransformed(
    Gui::Command* cmd,
    PartDesign::Body* body,
    const char* type,
    const ConfigureTransformed& configure
)
{
    const std::vector<App::DocumentObject*> originals = cmd->getSelection().getObjectsOfType(
        PartDesign::Feature::getClassTypeId()
    );
    for (auto* original : originals) {
        if (PartDesignGui::getBodyFor(original, false) != body) {
            warnWrongSelection(QObject::tr("Select features from a single body."));
            return;
        }
    }

    cmd->openCommand((std::string("Make ") + type).c_str());
    App::DocumentObject* feature = startFeature(cmd, body, type);
    if (!feature) {
        return;
    }
    Gui::Command::updateActive();
    if (originals.empty()) {
        FCMD_OBJ_CMD(feature, "TransformMode = \"Whole shape\"");
    }
    else {
        FCMD_OBJ_CMD(feature, "Originals = " << PartDesignGui::buildLinkListPythonStr(originals));
    }
    configure(feature, originals);
    finishFeature(cmd, feature);
}

static Part::Part2DObject* sketchOfFirst(const std::vector<App::DocumentObject*>& originals)
{
    auto* profileBased = originals.empty()
        ? nullptr
        : freecad_cast<PartDesign::ProfileBased*>(originals.front());
    return profileBased ? profileBased->getVerifiedSketch(/*silent=*/true) : nullptr;
}

//===========================================================================
// PartDesign_Mirrored
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignMirrored)

CmdPartDesignMirrored::CmdPartDesignMirrored()
    : Command("PartDesign_Mirrored")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Mirror");
    sToolTipText = QT_TR_NOOP("Mirrors the selected features or active body");
    sWhatsThis = "PartDesign_Mirrored";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Mirrored";
}

void CmdPartDesignMirrored::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    makeTransformed(this, body, "Mirrored", [body](auto* feature, const auto& originals) {
        if (Part::Part2DObject* sketch = sketchOfFirst(originals)) {
            FCMD_OBJ_CMD(feature, "MirrorPlane = (" << getObjectCmd(sketch) << ", ['V_Axis'])");
        }
        else {
            FCMD_OBJ_CMD(
                feature,
                "MirrorPlane = (" << getObjectCmd(body->getOrigin()->getXY()) << ", [''])"
            );
        }
    });
}

bool CmdPartDesignMirrored::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_LinearPattern
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignLinearPattern)

CmdPartDesignLinearPattern::CmdPartDesignLinearPattern()
    : Command("PartDesign_LinearPattern")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Linear Pattern");
    sToolTipText = QT_TR_NOOP(
        "Duplicates the selected features or the active body in a linear pattern"
    );
    sWhatsThis = "PartDesign_LinearPattern";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_LinearPattern";
}

void CmdPartDesignLinearPattern::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    makeTransformed(this, body, "LinearPattern", [body](auto* feature, const auto& originals) {
        if (Part::Part2DObject* sketch = sketchOfFirst(originals)) {
            FCMD_OBJ_CMD(feature, "Direction = (" << getObjectCmd(sketch) << ", ['H_Axis'])");
            FCMD_OBJ_CMD(feature, "Direction2 = (" << getObjectCmd(sketch) << ", ['V_Axis'])");
        }
        else {
            FCMD_OBJ_CMD(feature, "Direction = (" << getObjectCmd(body->getOrigin()->getX()) << ",[''])");
        }
        FCMD_OBJ_CMD(feature, "Length = 100");
        FCMD_OBJ_CMD(feature, "Occurrences = 2");
    });
}

bool CmdPartDesignLinearPattern::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_PolarPattern
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignPolarPattern)

CmdPartDesignPolarPattern::CmdPartDesignPolarPattern()
    : Command("PartDesign_PolarPattern")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Polar Pattern");
    sToolTipText = QT_TR_NOOP(
        "Duplicates the selected features or the active body in a circular pattern"
    );
    sWhatsThis = "PartDesign_PolarPattern";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_PolarPattern";
}

void CmdPartDesignPolarPattern::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    makeTransformed(this, body, "PolarPattern", [body](auto* feature, const auto& originals) {
        if (Part::Part2DObject* sketch = sketchOfFirst(originals)) {
            FCMD_OBJ_CMD(feature, "Axis = (" << getObjectCmd(sketch) << ",['N_Axis'])");
        }
        else {
            FCMD_OBJ_CMD(feature, "Axis = (" << getObjectCmd(body->getOrigin()->getZ()) << ",[''])");
        }
        FCMD_OBJ_CMD(feature, "Angle = 360");
        FCMD_OBJ_CMD(feature, "Occurrences = 2");
    });
}

bool CmdPartDesignPolarPattern::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_MultiTransform
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignMultiTransform)

CmdPartDesignMultiTransform::CmdPartDesignMultiTransform()
    : Command("PartDesign_MultiTransform")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Multi-Transform");
    sToolTipText = QT_TR_NOOP(
        "Applies multiple transformations to the selected features or active body"
    );
    sWhatsThis = "PartDesign_MultiTransform";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_MultiTransform";
}

void CmdPartDesignMultiTransform::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* pcActiveBody = selectedBody(this);

    if (!pcActiveBody) {
        return;
    }

    // A selected pattern is converted into a MultiTransform; one is never nested in another.
    std::vector<App::DocumentObject*> features = getSelection().getObjectsOfType(
        PartDesign::Transformed::getClassTypeId()
    );
    std::erase_if(features, [](App::DocumentObject* f) {
        return f->isDerivedFrom<PartDesign::MultiTransform>();
    });
    if (features.empty()) {
        makeTransformed(this, pcActiveBody, "MultiTransform", [](auto*, const auto&) {});
        return;
    }

    // Only the first selected pattern is converted.
    PartDesign::Transformed* trFeat = static_cast<PartDesign::Transformed*>(features.front());

    // Move the insert point back one feature
    App::DocumentObject* oldTip = pcActiveBody->shownStep();
    App::DocumentObject* prevFeature = pcActiveBody->getPrevSolidFeature(trFeat);
    Gui::Selection().clearSelection();
    if (prevFeature) {
        Gui::Selection().addSelection(
            prevFeature->getDocument()->getName(),
            prevFeature->getNameInDocument()
        );
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Convert to Multi-Transform feature"));

    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    rcCmdMgr.runCommandByName("PartDesign_MoveTip");

    // Built ahead of the pattern; listing the pattern then takes it off the chain.
    App::DocumentObject* Feat = startFeature(this, pcActiveBody, "MultiTransform");
    if (!Feat) {
        return;
    }
    auto objCmd = getObjectCmd(trFeat);
    FCMD_OBJ_CMD(Feat, "Originals = " << objCmd << ".Originals");
    FCMD_OBJ_CMD(Feat, "TransformMode = " << objCmd << ".TransformMode");
    FCMD_OBJ_CMD(Feat, "Transformations = [" << objCmd << "]");

    FCMD_OBJ_CMD(trFeat, "Originals = []");

    finishFeature(this, Feat);

    // Restore the insert point; when the pattern was the Tip, the MultiTransform now is.
    if (oldTip != trFeat) {
        Gui::Selection().clearSelection();
        Gui::Selection().addSelection(oldTip->getDocument()->getName(), oldTip->getNameInDocument());
        rcCmdMgr.runCommandByName("PartDesign_MoveTip");
        Gui::Selection().clearSelection();
    }
}

bool CmdPartDesignMultiTransform::isActive()
{
    return hasAnyBody();
}

void CreatePartDesignTransformedCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartDesignMirrored());
    rcCmdMgr.addCommand(new CmdPartDesignLinearPattern());
    rcCmdMgr.addCommand(new CmdPartDesignPolarPattern());
    rcCmdMgr.addCommand(new CmdPartDesignMultiTransform());
}
