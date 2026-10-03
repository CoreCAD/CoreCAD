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
// Common utility functions for ProfileBased features
//===========================================================================

/**
 *  Partially pulled from Linkstage3 importExternalObjects for toponaming element map
 *  compatibility with sketches that contain point objects.  By adding an empty
 *  subobject when appropriate, we allow those sketches to be used as profiles without error.
 *
 * @param prop  The property ( generally a Profile link )
 * @param _sobjs    Subobjects to use
 * @return  True if elements were found
 */
bool importExternalElements(App::PropertyLinkSub& prop, std::vector<App::SubObjectT> _sobjs)
{
    if (!prop.getName() || !prop.getName()[0]) {
        FC_THROWM(Base::RuntimeError, "Invalid property");
    }
    auto editObj = freecad_cast<App::DocumentObject*>(prop.getContainer());
    if (!editObj) {
        FC_THROWM(Base::RuntimeError, "Editing object not found");
    }
    if (!PartDesign::Body::inAnyBody(editObj)) {
        FC_THROWM(Base::RuntimeError, "No body for editing object: " << editObj->getNameInDocument());
    }
    std::map<App::DocumentObject*, std::vector<std::string>> links;
    std::vector<App::SubObjectT> sobjs;
    auto docName = editObj->getDocument()->getName();
    auto inList = editObj->getInListEx(true);
    for (auto sobjT : _sobjs) {
        auto sobj = sobjT.getSubObject();
        if (sobj == editObj) {
            continue;
        }
        if (!sobj) {
            FC_THROWM(Base::RuntimeError, "Object not found: " << sobjT.getSubObjectFullName(docName));
        }
        if (inList.count(sobj)) {
            FC_THROWM(
                Base::RuntimeError,
                "Cyclic dependency on object " << sobjT.getSubObjectFullName(docName)
            );
        }
        sobjT.normalized();
        // An element picked on an object replaces a whole-object reference to it.
        auto& subs = links[sobj];
        std::string element = sobjT.getOldElementName();
        if (element.size()) {
            if (subs.size() == 1 && subs.front().empty()) {
                std::erase_if(sobjs, [sobj](const auto& s) { return s.getSubObject() == sobj; });
            }
        }
        else if (subs.size() > 0) {
            continue;
        }
        subs.push_back(std::move(element));
        sobjs.push_back(sobjT);
    }

    int import = 0;
    App::DocumentObject* obj = nullptr;
    std::vector<std::string> subs;
    for (const auto& sobjT : sobjs) {
        auto sobj = sobjT.getSubObject();
        if (!PartDesign::Body::sameBody(sobj, editObj)) {
            import = 1;
            break;
        }
        if (!obj) {
            obj = sobj;
        }
        else if (obj != sobj) {
            if (!import) {
                import = -1;
            }
            break;
        }
        subs.push_back(sobjT.getOldElementName());
    }
    if (!import) {
        if (subs.empty()) {
            subs.emplace_back();
        }
        if (obj == prop.getValue() && prop.getSubValues() == subs) {
            return false;
        }
        prop.setValue(obj, std::move(subs));
        return true;
    }
    return false;
}

static void setProfile(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements
)
{
    FCMD_OBJ_CMD(
        feature,
        "Profile = (" << Gui::Command::getObjectCmd(profile) << ", " << pythonNameList(elements) << ")"
    );
}

// A profile with elements is only those elements, so a whole sketch is set without them —
// unless the pick is a vertex, which makes a point section.
static void setSketchOrPointProfile(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements
)
{
    const bool pointPicked = !elements.empty() && elements.front().starts_with("Vertex");
    if (profile->isDerivedFrom<Part::Part2DObject>() && !pointPicked) {
        FCMD_OBJ_CMD(feature, "Profile = " << Gui::Command::getObjectCmd(profile));
    }
    else {
        setProfile(feature, profile, elements);
    }
}

using TakeSelection = void (*)(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements,
    const std::vector<Gui::SelectionObject>& selection
);

static void takeProfile(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements,
    const std::vector<Gui::SelectionObject>& /*selection*/
)
{
    setProfile(feature, profile, elements);
}

// Every pick after the profile is a section; a pick without elements is the whole object.
static void takeProfileAndSections(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements,
    const std::vector<Gui::SelectionObject>& selection
)
{
    setSketchOrPointProfile(feature, profile, elements);
    for (std::size_t i = 1; i < selection.size(); ++i) {
        std::vector<std::string> names = selection[i].getSubNames();
        if (names.empty()) {
            names.emplace_back();
        }
        FCMD_OBJ_CMD(
            feature,
            "Sections += [(" << Gui::Command::getObjectCmd(selection[i].getObject()) << ", "
                             << pythonNameList(names) << ")]"
        );
    }
}

// A second pick is the spine: a whole sketch, or the edges picked on it.
static void takeProfileAndSpine(
    App::DocumentObject* feature,
    App::DocumentObject* profile,
    const std::vector<std::string>& elements,
    const std::vector<Gui::SelectionObject>& selection
)
{
    setSketchOrPointProfile(feature, profile, elements);
    if (selection.size() != 2) {
        return;
    }
    const App::DocumentObject* spine = selection[1].getObject();
    std::vector<std::string> names = selection[1].getSubNames();
    if (spine->isDerivedFrom<Part::Part2DObject>() && names.empty()) {
        FCMD_OBJ_CMD(feature, "Spine = " << Gui::Command::getObjectCmd(spine));
        return;
    }
    std::erase_if(names, [](const std::string& name) {
        return name.find("Edge") == std::string::npos;
    });
    FCMD_OBJ_CMD(
        feature,
        "Spine = (" << Gui::Command::getObjectCmd(spine) << ", " << pythonNameList(names) << ")"
    );
}

struct ProfileKind
{
    const char* type;  // after "PartDesign::"; also names the feature and its undo step
    bool subtractive;
    std::function<void(Part::ShapeFeature* profile, App::DocumentObject* feature)> configure;
    TakeSelection takeSelection = takeProfile;
};

static void warnNothingToSubtractFrom()
{
    QMessageBox msgBox(Gui::getMainWindow());
    msgBox.setText(QObject::tr("Cannot use this command as there is no solid to subtract from."));
    msgBox.setInformativeText(
        QObject::tr("Ensure that the body contains a feature before attempting a subtractive command.")
    );
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);
    msgBox.exec();
}

// Cruth §8.3, the single-reach case: a cut whose profile anchors to no body cuts the one body
// the document holds.
static PartDesign::Body* soleBody(App::Document* doc)
{
    auto bodies = doc->getObjectsOfType(PartDesign::Body::getClassTypeId());
    return bodies.size() == 1 ? static_cast<PartDesign::Body*>(bodies.front()) : nullptr;
}

static void makeProfileFeature(Gui::Command* cmd, const ProfileKind& kind)
{
    // Null means the profile anchors to no body; one is spawned inside the undo step below,
    // so cancelling the feature removes it too (#17).
    PartDesign::Body* body = nullptr;
    if (!resolveBaseBodyForNewFeature(cmd, body)) {
        return;
    }
    if (kind.subtractive) {
        if (!body) {
            body = soleBody(cmd->getDocument());
        }
        if (!body || !body->isSolid()) {
            warnNothingToSubtractFrom();
            return;
        }
    }

    // resolveBaseBodyForNewFeature leaves the profile first in the selection.
    std::vector<Gui::SelectionObject> selection = cmd->getSelection().getSelectionEx();
    App::DocumentObject* profile = selection.empty() ? nullptr : selection.front().getObject();
    std::vector<std::string> elements = selection.empty() ? std::vector<std::string>()
                                                          : selection.front().getSubNames();
    if (!profile || (!profile->isDerivedFrom<Part::Part2DObject>() && elements.empty())) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("No sketch to work on"),
            QObject::tr("Select a sketch to use as the profile.")
        );
        return;
    }
    if (!Part::hasShape(profile)) {
        return;
    }

    // #0002760: recompute a broken profile now, so it still shows as broken if the user cancels.
    if (profile->isTouched()) {
        profile->recomputeFeature();
    }

    cmd->openCommand((std::string("Make ") + kind.type).c_str());
    if (!body) {
        body = PartDesign::Body::spawnAutoBody(cmd->getDocument());
        if (!body) {
            cmd->abortCommand();
            return;
        }
    }
    App::DocumentObject* feature = startFeature(cmd, body, kind.type);
    auto* profileBased = freecad_cast<PartDesign::ProfileBased*>(feature);
    if (!profileBased) {
        if (feature) {
            cmd->abortCommand();
        }
        return;
    }

    // A cross-body profile is skipped: importing finds nothing for it, and its cycle check
    // would trip on the new feature, which is already in the body's in-list.
    if (elements.empty() && PartDesign::Body::backsBody(profile, body)) {
        importExternalElements(profileBased->Profile, {profile});
        elements = profileBased->Profile.getSubValues();
    }
    kind.takeSelection(feature, profile, elements, selection);
    kind.configure(static_cast<Part::ShapeFeature*>(profile), feature);
}

static void finishProfileBased(
    const Gui::Command* cmd,
    const Part::ShapeFeature* profile,
    App::DocumentObject* feature
)
{
    if (profile->isDerivedFrom<Part::Part2DObject>()) {
        FCMD_OBJ_HIDE(profile);
    }
    finishFeature(cmd, feature);
}

// A sketch's own vertical axis; anything else has none, so the document origin's Y axis.
static void setVerticalReferenceAxis(App::DocumentObject* feature, Part::ShapeFeature* profile)
{
    if (profile->isDerivedFrom<Part::Part2DObject>()) {
        FCMD_OBJ_CMD(
            feature,
            "ReferenceAxis = (" << Gui::Command::getObjectCmd(profile) << ",['V_Axis'])"
        );
    }
    else if (App::Origin* origin = PartDesign::Body::findDocumentOrigin(feature->getDocument())) {
        FCMD_OBJ_CMD(
            feature,
            "ReferenceAxis = (" << Gui::Command::getObjectCmd(origin->getY()) << ",[''])"
        );
    }
}

static auto extrude(const Gui::Command* cmd, double length)
{
    return [cmd, length](Part::ShapeFeature* profile, App::DocumentObject* feature) {
        FCMD_OBJ_CMD(feature, "Length = " << length);
        Gui::Command::updateActive();
        if (profile->isDerivedFrom<Part::Part2DObject>()) {
            FCMD_OBJ_CMD(
                feature,
                "ReferenceAxis = (" << Gui::Command::getObjectCmd(profile) << ",['N_Axis'])"
            );
        }
        finishProfileBased(cmd, profile, feature);
    };
}

static auto sweep(const Gui::Command* cmd)
{
    return [cmd](Part::ShapeFeature* profile, App::DocumentObject* feature) {
        Gui::Command::updateActive();
        finishProfileBased(cmd, profile, feature);
    };
}

static auto helix(const Gui::Command* cmd)
{
    return [cmd](Part::ShapeFeature* profile, App::DocumentObject* feature) {
        // A helix with default values is often invalid until the user sets more of them.
        Base::ObjectStatusLocker<App::Document::Status, App::Document> guard(
            App::Document::IgnoreErrorOnRecompute,
            feature->getDocument(),
            true
        );
        Gui::Command::updateActive();
        setVerticalReferenceAxis(feature, profile);
        finishProfileBased(cmd, profile, feature);

        // A failed first build would otherwise leave nothing visible to edit against.
        if (!feature->isError()) {
            return;
        }
        App::DocumentObject* base = static_cast<PartDesign::Feature*>(feature)->BaseFeature.getValue();
        auto* view = base ? dynamic_cast<PartDesignGui::ViewProvider*>(
                                Gui::Application::Instance->getViewProvider(base)
                            )
                          : nullptr;
        if (view) {
            view->makeTemporaryVisible(true);
        }
    };
}

//===========================================================================
// PartDesign_Pad
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignPad)

CmdPartDesignPad::CmdPartDesignPad()
    : Command("PartDesign_Pad")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Pad");
    sToolTipText = QT_TR_NOOP("Extrudes the selected sketch or profile and adds it to the body");
    sWhatsThis = "PartDesign_Pad";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Pad";
}

void CmdPartDesignPad::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(this, {.type = "Pad", .subtractive = false, .configure = extrude(this, 10.0)});
}

bool CmdPartDesignPad::isActive()
{
    return hasAnySketch();
}

//===========================================================================
// PartDesign_Pocket
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignPocket)

CmdPartDesignPocket::CmdPartDesignPocket()
    : Command("PartDesign_Pocket")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Pocket");
    sToolTipText = QT_TR_NOOP("Extrudes the selected sketch or profile and removes it from the body");
    sWhatsThis = "PartDesign_Pocket";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Pocket";
}

void CmdPartDesignPocket::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(this, {.type = "Pocket", .subtractive = true, .configure = extrude(this, 5.0)});
}

bool CmdPartDesignPocket::isActive()
{
    return hasAnyBody();
}

//===========================================================================
// PartDesign_Hole
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignHole)

CmdPartDesignHole::CmdPartDesignHole()
    : Command("PartDesign_Hole")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Hole");
    sToolTipText
        = QT_TR_NOOP("Creates holes in the active body at the center points of circles or arcs of the selected sketch or profile");
    sWhatsThis = "PartDesign_Hole";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Hole";
}

void CmdPartDesignHole::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::Command* cmd = this;
    makeProfileFeature(
        this,
        {.type = "Hole",
         .subtractive = true,
         .configure = [cmd](Part::ShapeFeature* profile, App::DocumentObject* feature) {
             finishProfileBased(cmd, profile, feature);
         }}
    );
}

bool CmdPartDesignHole::isActive()
{
    return hasAnySketch();
}

//===========================================================================
// PartDesign_Revolution
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignRevolution)

CmdPartDesignRevolution::CmdPartDesignRevolution()
    : Command("PartDesign_Revolution")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Revolve");
    sToolTipText = QT_TR_NOOP(
        "Revolves the selected sketch or profile around a line or axis and adds it to the body"
    );
    sWhatsThis = "PartDesign_Revolution";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Revolution";
}

void CmdPartDesignRevolution::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::Command* cmd = this;
    auto configure = [cmd](Part::ShapeFeature* profile, App::DocumentObject* feature) {
        setVerticalReferenceAxis(feature, profile);
        FCMD_OBJ_CMD(feature, "Angle = 360.0");
        auto* revolution = dynamic_cast<PartDesign::Revolution*>(feature);
        if (revolution && revolution->suggestReversed()) {
            FCMD_OBJ_CMD(feature, "Reversed = 1");
        }
        finishProfileBased(cmd, profile, feature);
    };
    makeProfileFeature(this, {.type = "Revolution", .subtractive = false, .configure = configure});
}

bool CmdPartDesignRevolution::isActive()
{
    return hasAnySketch();
}

//===========================================================================
// PartDesign_Groove
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignGroove)

CmdPartDesignGroove::CmdPartDesignGroove()
    : Command("PartDesign_Groove")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Groove");
    sToolTipText = QT_TR_NOOP(
        "Revolves the sketch or profile around a line or axis and removes it from the body"
    );
    sWhatsThis = "PartDesign_Groove";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_Groove";
}

void CmdPartDesignGroove::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::Command* cmd = this;
    auto configure = [cmd](Part::ShapeFeature* profile, App::DocumentObject* feature) {
        setVerticalReferenceAxis(feature, profile);
        FCMD_OBJ_CMD(feature, "Angle = 360.0");
        try {
            // Throws when the axis is perpendicular to the sketch; the user can still fix it.
            auto* groove = dynamic_cast<PartDesign::Groove*>(feature);
            if (groove && groove->suggestReversed()) {
                FCMD_OBJ_CMD(feature, "Reversed = 1");
            }
        }
        catch (const Base::Exception& e) {
            e.reportException();
        }
        finishProfileBased(cmd, profile, feature);
    };
    makeProfileFeature(this, {.type = "Groove", .subtractive = true, .configure = configure});
}

bool CmdPartDesignGroove::isActive()
{
    return hasAnySketch();
}

//===========================================================================
// PartDesign_AdditivePipe
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignAdditivePipe)

CmdPartDesignAdditivePipe::CmdPartDesignAdditivePipe()
    : Command("PartDesign_AdditivePipe")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Additive Pipe");
    sToolTipText = QT_TR_NOOP(
        "Sweeps the selected sketch or profile along a path and adds it to the body"
    );
    sWhatsThis = "PartDesign_AdditivePipe";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_AdditivePipe";
}

void CmdPartDesignAdditivePipe::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(
        this,
        {.type = "AdditivePipe",
         .subtractive = false,
         .configure = sweep(this),
         .takeSelection = takeProfileAndSpine}
    );
}

bool CmdPartDesignAdditivePipe::isActive()
{
    return hasAnySketch();
}


//===========================================================================
// PartDesign_SubtractivePipe
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignSubtractivePipe)

CmdPartDesignSubtractivePipe::CmdPartDesignSubtractivePipe()
    : Command("PartDesign_SubtractivePipe")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Subtractive Pipe");
    sToolTipText = QT_TR_NOOP(
        "Sweeps the selected sketch or profile along a path and removes it from the body"
    );
    sWhatsThis = "PartDesign_SubtractivePipe";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_SubtractivePipe";
}

void CmdPartDesignSubtractivePipe::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(
        this,
        {.type = "SubtractivePipe",
         .subtractive = true,
         .configure = sweep(this),
         .takeSelection = takeProfileAndSpine}
    );
}

bool CmdPartDesignSubtractivePipe::isActive()
{
    return hasAnySketch();
}


//===========================================================================
// PartDesign_AdditiveLoft
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignAdditiveLoft)

CmdPartDesignAdditiveLoft::CmdPartDesignAdditiveLoft()
    : Command("PartDesign_AdditiveLoft")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Additive Loft");
    sToolTipText = QT_TR_NOOP(
        "Lofts the selected sketch or profile along a path and adds it to the body"
    );
    sWhatsThis = "PartDesign_AdditiveLoft";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_AdditiveLoft";
}

void CmdPartDesignAdditiveLoft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(
        this,
        {.type = "AdditiveLoft",
         .subtractive = false,
         .configure = sweep(this),
         .takeSelection = takeProfileAndSections}
    );
}

bool CmdPartDesignAdditiveLoft::isActive()
{
    return hasAnySketch();
}


//===========================================================================
// PartDesign_SubtractiveLoft
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignSubtractiveLoft)

CmdPartDesignSubtractiveLoft::CmdPartDesignSubtractiveLoft()
    : Command("PartDesign_SubtractiveLoft")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Subtractive Loft");
    sToolTipText = QT_TR_NOOP(
        "Lofts the selected sketch or profile along a path and removes it from the body"
    );
    sWhatsThis = "PartDesign_SubtractiveLoft";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_SubtractiveLoft";
}

void CmdPartDesignSubtractiveLoft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(
        this,
        {.type = "SubtractiveLoft",
         .subtractive = true,
         .configure = sweep(this),
         .takeSelection = takeProfileAndSections}
    );
}

bool CmdPartDesignSubtractiveLoft::isActive()
{
    return hasAnySketch();
}

//===========================================================================
// PartDesign_AdditiveHelix
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignAdditiveHelix)

CmdPartDesignAdditiveHelix::CmdPartDesignAdditiveHelix()
    : Command("PartDesign_AdditiveHelix")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Additive Helix");
    sToolTipText = QT_TR_NOOP(
        "Sweeps the selected sketch or profile along a helix and adds it to the body"
    );
    sWhatsThis = "PartDesign_AdditiveHelix";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_AdditiveHelix";
}

void CmdPartDesignAdditiveHelix::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(this, {.type = "AdditiveHelix", .subtractive = false, .configure = helix(this)});
}

bool CmdPartDesignAdditiveHelix::isActive()
{
    return hasAnySketch();
}


//===========================================================================
// PartDesign_SubtractiveHelix
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignSubtractiveHelix)

CmdPartDesignSubtractiveHelix::CmdPartDesignSubtractiveHelix()
    : Command("PartDesign_SubtractiveHelix")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Subtractive Helix");
    sToolTipText = QT_TR_NOOP(
        "Sweeps the selected sketch or profile along a helix and removes it from the body"
    );
    sWhatsThis = "PartDesign_SubtractiveHelix";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_SubtractiveHelix";
}

void CmdPartDesignSubtractiveHelix::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    makeProfileFeature(
        this,
        {.type = "SubtractiveHelix", .subtractive = true, .configure = helix(this)}
    );
}

bool CmdPartDesignSubtractiveHelix::isActive()
{
    return hasAnySketch();
}

void CreatePartDesignProfileBasedCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartDesignPad());
    rcCmdMgr.addCommand(new CmdPartDesignPocket());
    rcCmdMgr.addCommand(new CmdPartDesignHole());
    rcCmdMgr.addCommand(new CmdPartDesignRevolution());
    rcCmdMgr.addCommand(new CmdPartDesignGroove());
    rcCmdMgr.addCommand(new CmdPartDesignAdditivePipe());
    rcCmdMgr.addCommand(new CmdPartDesignSubtractivePipe());
    rcCmdMgr.addCommand(new CmdPartDesignAdditiveLoft());
    rcCmdMgr.addCommand(new CmdPartDesignSubtractiveLoft());
    rcCmdMgr.addCommand(new CmdPartDesignAdditiveHelix());
    rcCmdMgr.addCommand(new CmdPartDesignSubtractiveHelix());
}
