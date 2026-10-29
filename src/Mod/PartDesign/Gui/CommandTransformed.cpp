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
#include <Standard_Failure.hxx>
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
#include <Mod/PartDesign/App/FeatureRevolution.h>
#include <Mod/PartDesign/App/FeatureTransformed.h>
#include <Mod/PartDesign/App/FeatureDressUp.h>
#include <Mod/PartDesign/App/FeatureCurvePattern.h>
#include <Mod/PartDesign/App/FeatureFillPattern.h>
#include <Mod/PartDesign/App/FeaturePointPattern.h>

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
// PartDesign_PointPattern
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignPointPattern)

CmdPartDesignPointPattern::CmdPartDesignPointPattern()
    : Command("PartDesign_PointPattern")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Point Pattern");
    sToolTipText = QT_TR_NOOP(
        "Duplicates the selected features or the active body onto each point of a sketch"
    );
    sWhatsThis = "PartDesign_PointPattern";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_PointPattern";
}

// A sketch selected with the features places the copies; without one, the newest sketch
// that has points does.
static App::DocumentObject* sketchOfPoints(Gui::Command* cmd, App::Document* doc)
{
    for (auto* obj : cmd->getSelection().getObjectsOfType(Part::Part2DObject::getClassTypeId())) {
        if (!PartDesign::PointPattern::pointsOf(static_cast<Part::Part2DObject*>(obj)).empty()) {
            return obj;
        }
    }
    const std::vector<App::DocumentObject*> objects = doc->getObjects();
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        auto* sketch = freecad_cast<Part::Part2DObject*>(*it);
        if (sketch && !PartDesign::PointPattern::pointsOf(sketch).empty()) {
            return sketch;
        }
    }
    return nullptr;
}

void CmdPartDesignPointPattern::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    App::DocumentObject* points = sketchOfPoints(this, body->getDocument());
    makeTransformed(this, body, "PointPattern", [points](auto* feature, const auto&) {
        if (points) {
            FCMD_OBJ_CMD(feature, "Points = " << getObjectCmd(points));
        }
    });
}

bool CmdPartDesignPointPattern::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_CurvePattern
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignCurvePattern)

CmdPartDesignCurvePattern::CmdPartDesignCurvePattern()
    : Command("PartDesign_CurvePattern")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Curve Pattern");
    sToolTipText = QT_TR_NOOP("Duplicates the selected features or the active body along a curve");
    sWhatsThis = "PartDesign_CurvePattern";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_CurvePattern";
}

static bool isProfile(const App::DocumentObject* sketch)
{
    return std::ranges::any_of(sketch->getInList(), [sketch](App::DocumentObject* user) {
        auto* profileBased = freecad_cast<PartDesign::ProfileBased*>(user);
        return profileBased && profileBased->Profile.getValue() == sketch;
    });
}

// Edges or a sketch selected with the features make the path; without them, the newest
// sketch that is one curve and not a feature's profile does.
static std::pair<App::DocumentObject*, std::vector<std::string>> pathOf(
    Gui::Command* cmd,
    App::Document* doc
)
{
    for (auto& selected : cmd->getSelection().getSelectionEx()) {
        std::vector<std::string> edges;
        for (const std::string& sub : selected.getSubNames()) {
            if (sub.compare(0, 4, "Edge") == 0) {
                edges.push_back(sub);
            }
        }
        if (!edges.empty()) {
            return {selected.getObject(), edges};
        }
        App::DocumentObject* obj = selected.getObject();
        if (obj->isDerivedFrom<Part::Part2DObject>() && PartDesign::CurvePattern::isPath(obj)) {
            return {obj, {}};
        }
    }
    const std::vector<App::DocumentObject*> objects = doc->getObjects();
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if ((*it)->isDerivedFrom<Part::Part2DObject>() && !isProfile(*it)
            && PartDesign::CurvePattern::isPath(*it)) {
            return {*it, {}};
        }
    }
    return {nullptr, {}};
}

void CmdPartDesignCurvePattern::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    const auto [path, edges] = pathOf(this, body->getDocument());
    makeTransformed(this, body, "CurvePattern", [path, edges](auto* feature, const auto&) {
        FCMD_OBJ_CMD(feature, "Occurrences = 3");
        if (!path) {
            return;
        }
        FCMD_OBJ_CMD(
            feature,
            "Path = (" << getObjectCmd(path) << ", "
                       << PartDesignGui::buildLinkSubPythonStr(path, edges) << ")"
        );
        try {
            const double ahead = static_cast<PartDesign::CurvePattern*>(feature)->lengthAhead();
            FCMD_OBJ_CMD(feature, "Length = " << ahead);
        }
        catch (const Base::Exception&) {
        }
        catch (const Standard_Failure&) {
        }
    });
}

bool CmdPartDesignCurvePattern::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_FillPattern
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignFillPattern)

CmdPartDesignFillPattern::CmdPartDesignFillPattern()
    : Command("PartDesign_FillPattern")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Fill Pattern");
    sToolTipText = QT_TR_NOOP(
        "Fills a flat region with copies of the selected features or the active body"
    );
    sWhatsThis = "PartDesign_FillPattern";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_FillPattern";
}

// Faces or a sketch selected with the features make the boundary; without them, the newest
// sketch that outlines a region and is not a feature's profile does.
static std::pair<App::DocumentObject*, std::vector<std::string>> boundaryOf(
    Gui::Command* cmd,
    App::Document* doc
)
{
    for (auto& selected : cmd->getSelection().getSelectionEx()) {
        std::vector<std::string> faces;
        for (const std::string& sub : selected.getSubNames()) {
            if (sub.compare(0, 4, "Face") == 0) {
                faces.push_back(sub);
            }
        }
        if (!faces.empty()) {
            return {selected.getObject(), faces};
        }
        App::DocumentObject* obj = selected.getObject();
        if (obj->isDerivedFrom<Part::Part2DObject>() && PartDesign::FillPattern::isBoundary(obj)) {
            return {obj, {}};
        }
    }
    const std::vector<App::DocumentObject*> objects = doc->getObjects();
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
        if ((*it)->isDerivedFrom<Part::Part2DObject>() && !isProfile(*it)
            && PartDesign::FillPattern::isBoundary(*it)) {
            return {*it, {}};
        }
    }
    return {nullptr, {}};
}

void CmdPartDesignFillPattern::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* body = selectedBody(this);
    if (!body) {
        return;
    }
    const auto [boundary, faces] = boundaryOf(this, body->getDocument());
    makeTransformed(this, body, "FillPattern", [boundary, faces](auto* feature, const auto&) {
        if (boundary) {
            FCMD_OBJ_CMD(
                feature,
                "Boundary = (" << getObjectCmd(boundary) << ", "
                               << PartDesignGui::buildLinkSubPythonStr(boundary, faces) << ")"
            );
        }
    });
}

bool CmdPartDesignFillPattern::isActive()
{
    return hasAnyBody();
}

void CreatePartDesignTransformedCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartDesignMirrored());
    rcCmdMgr.addCommand(new CmdPartDesignLinearPattern());
    rcCmdMgr.addCommand(new CmdPartDesignPolarPattern());
    rcCmdMgr.addCommand(new CmdPartDesignPointPattern());
    rcCmdMgr.addCommand(new CmdPartDesignCurvePattern());
    rcCmdMgr.addCommand(new CmdPartDesignFillPattern());
}
