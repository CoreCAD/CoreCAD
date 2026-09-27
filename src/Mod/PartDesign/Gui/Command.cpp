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


FC_LOG_LEVEL_INIT("PartDesign", true, true)

using namespace std;

// Gate for commands whose body comes from the selection (selectedBody): available whenever
// the document has a Body to work on.
static bool hasAnyBody()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    return doc && !doc->getObjectsOfType(PartDesign::Body::getClassTypeId()).empty();
}

static bool hasAnySketch()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    return doc && !doc->getObjectsOfType(Part::Part2DObject::getClassTypeId()).empty();
}

// A Python list literal of element names, escaped so a quote in a name cannot break the command.
static std::string pythonNameList(const std::vector<std::string>& names)
{
    std::string list = "[";
    for (const auto& name : names) {
        list += "'" + Base::Tools::escapeEncodeString(name) + "',";
    }
    return list + "]";
}

static std::vector<std::string> allEdgeNames(const Part::TopoShape& shape)
{
    std::vector<std::string> names;
    const int count = shape.countSubElements("Edge");
    for (int i = 1; i <= count; ++i) {
        names.push_back("Edge" + std::to_string(i));
    }
    return names;
}

static void copyAppearance(App::DocumentObject* to, App::DocumentObject* from)
{
    for (const char* attr :
         {"ShapeAppearance", "LineColor", "PointColor", "Transparency", "DisplayMode"}) {
        Gui::Command::copyVisual(to, attr, from);
    }
}

// Cruth #130/#132: the selection decides the body a dress-up or pattern extends: the body of
// the selected geometry, else the sole body, else the chooser. There is no active body.
static PartDesign::Body* selectedBody(Gui::Command* cmd)
{
    return PartDesignGui::resolveTargetBody(cmd);
}

// Cruth §8.5/§4.6 spawn-vs-extend decision (GUI entry point).
//
// PURE query wrapper over PartDesign::Body::resolveBaseBody — the shared App-layer
// service the Python API (PartDesign.resolveBaseBody) also calls. Neither path has
// a side effect: they resolve the anchor chain and nothing more (P8 UI/API
// equivalence). The GUI's only addition is the human-facing ambiguity warning.
//
// Returns the resolved base Body, or nullptr. nullptr with @p abort == false means
// "the anchor chain reached no Body": the caller proceeds and the feature's undo
// transaction auto-spawns one (#17 — creation must live inside that transaction so a
// cancelled feature leaks no stray Body). @p abort == true means the chain is
// ambiguous and the command must stop.
static PartDesign::Body* decideBaseBody(Part::Part2DObject* sketch, bool& abort)
{
    abort = false;
    bool ambiguous = false;
    PartDesign::Body* body = PartDesign::Body::resolveBaseBody(sketch, ambiguous);
    if (ambiguous) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Ambiguous anchor"),
            QObject::tr(
                "This sketch's attachment chain reaches more than one Body. "
                "Pick a single Body explicitly before continuing."
            )
        );
        abort = true;
    }
    return body;
}

// Cruth §8.5: makeProfileFeature reads its profile from the selection, so a sketch resolved
// any other way is put there.
static void selectResolvedSketch(Part::Part2DObject* sketch)
{
    if (!sketch) {
        return;
    }
    Gui::Selection().clearSelection();
    Gui::Selection().addSelection(sketch->getDocument()->getName(), sketch->getNameInDocument());
}

// CoreCAD §8.5: pick the sketch the user means to operate on.
//
// Rules:
// - exactly one Part2DObject in the current selection → use it,
// - nothing selected, exactly one sketch in the document → use it,
// - anything else → warn and return nullptr.
//
// Every non-null result is pushed back into the selection (selectResolvedSketch) so the
// downstream feature-creation path uses precisely this sketch and never re-prompts.
static Part::Part2DObject* resolveSketchFromSelection(Gui::Command* cmd, App::Document* doc)
{
    auto selected = cmd->getSelection().getObjectsOfType(Part::Part2DObject::getClassTypeId());
    if (selected.size() == 1) {
        return static_cast<Part::Part2DObject*>(selected.front());
    }
    if (selected.size() > 1) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Multiple sketches selected"),
            QObject::tr("Select exactly one sketch to extrude.")
        );
        return nullptr;
    }

    auto inDoc = doc->getObjectsOfType(Part::Part2DObject::getClassTypeId());
    if (inDoc.size() == 1) {
        auto* only = static_cast<Part::Part2DObject*>(inDoc.front());
        selectResolvedSketch(only);
        return only;
    }

    // Cruth §8.5: several sketches, none selected — open the de-owned chooser so the
    // user can pick one (each labelled with where its anchor chain lands). Cancel
    // returns nullptr and the command aborts silently.
    std::vector<Part::Part2DObject*> candidates;
    candidates.reserve(inDoc.size());
    for (auto* obj : inDoc) {
        candidates.push_back(static_cast<Part::Part2DObject*>(obj));
    }
    Part::Part2DObject* picked = PartDesignGui::pickSketch(candidates);
    selectResolvedSketch(picked);
    return picked;
}

// Cruth §8.5/§4.6: resolve the base Body for a NEW sketch-based solid feature by walking
// the selected profile's anchor chain — the replacement for the legacy getBody(true)
// active-body requirement. Shared by every solid-producing command so they all decide
// spawn-vs-extend the same way (P8 UI consistency).
//
// Returns false (after showing the appropriate message) when there is no usable sketch or
// the anchor chain is ambiguous. On success @p body is the Body to extend, or nullptr for
// the auto-spawn case — makeProfileFeature() then spawns the Body inside the feature's
// undo transaction (#17), so a cancelled feature leaks nothing.
static bool resolveBaseBodyForNewFeature(Gui::Command* cmd, PartDesign::Body*& body)
{
    body = nullptr;
    auto* doc = cmd->getDocument();
    if (!doc) {
        return false;
    }

    // Cruth §4.6: a solid feature needs a profile to work on, not an active Body.
    if (doc->getObjectsOfType(Part::Part2DObject::getClassTypeId()).empty()) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("No sketch to work on"),
            QObject::tr("No sketch is available in the document")
        );
        return false;
    }

    // Cruth §8.5: the sketch's anchor chain decides spawn-vs-extend. Active-body session
    // state is no longer consulted.
    auto* sketch = resolveSketchFromSelection(cmd, doc);
    if (!sketch) {
        return false;
    }

    bool abort = false;
    body = decideBaseBody(sketch, abort);
    return !abort;
}

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
// Common utility functions for all features creating solids
//===========================================================================

static void finishFeature(
    const Gui::Command* cmd,
    App::DocumentObject* feature,
    App::DocumentObject* prevSolidFeature = nullptr,
    const bool hidePrevSolid = true,
    const bool updateDocument = true
)
{
    // The body the feature landed in, which for a fresh profile is a newly spawned one.
    PartDesign::Body* body
        = PartDesignGui::getBodyFor(prevSolidFeature ? prevSolidFeature : feature, false);

    if (hidePrevSolid && prevSolidFeature) {
        FCMD_OBJ_HIDE(prevSolidFeature);
    }

    if (updateDocument) {
        cmd->updateActive();
    }

    auto base = dynamic_cast<PartDesign::Feature*>(feature);
    if (base) {
        base = dynamic_cast<PartDesign::Feature*>(base->getBaseObject(true));
    }
    App::DocumentObject* looksLike = base ? static_cast<App::DocumentObject*>(base) : body;

    // Before setEdit, so the 'Shape preview' mode is not overridden (#0003621).
    if (looksLike) {
        copyAppearance(feature, looksLike);
    }

    PartDesignGui::setEdit(feature, body);
    cmd->doCommand(cmd->Gui, "Gui.Selection.clearSelection()");
}

// Opens the undo step and creates the feature in @p body, or aborts the step and returns null.
static App::DocumentObject* startFeature(Gui::Command* cmd, PartDesign::Body* body, const char* type)
{
    const std::string featureType = std::string("PartDesign::") + type;
    auto* feature = PartDesignGui::createFeature(
        body,
        featureType.c_str(),
        cmd->getUniqueObjectName(type, body)
    );
    if (!feature) {
        cmd->abortCommand();
    }
    return feature;
}

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

static void warnWrongSelection(const QString& text)
{
    QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"), text);
}

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

    rcCmdMgr.addCommand(new CmdPartDesignPad());
    rcCmdMgr.addCommand(new CmdPartDesignPocket());
    rcCmdMgr.addCommand(new CmdPartDesignHole());
    rcCmdMgr.addCommand(new CmdPartDesignRevolution());
    rcCmdMgr.addCommand(new CmdPartDesignGroove());
    rcCmdMgr.addCommand(new CmdPartDesignAdditivePipe);
    rcCmdMgr.addCommand(new CmdPartDesignSubtractivePipe);
    rcCmdMgr.addCommand(new CmdPartDesignAdditiveLoft);
    rcCmdMgr.addCommand(new CmdPartDesignSubtractiveLoft);
    rcCmdMgr.addCommand(new CmdPartDesignAdditiveHelix);
    rcCmdMgr.addCommand(new CmdPartDesignSubtractiveHelix);

    rcCmdMgr.addCommand(new CmdPartDesignFillet());
    rcCmdMgr.addCommand(new CmdPartDesignDraft());
    rcCmdMgr.addCommand(new CmdPartDesignChamfer());
    rcCmdMgr.addCommand(new CmdPartDesignThickness());

    rcCmdMgr.addCommand(new CmdPartDesignMirrored());
    rcCmdMgr.addCommand(new CmdPartDesignLinearPattern());
    rcCmdMgr.addCommand(new CmdPartDesignPolarPattern());
    // rcCmdMgr.addCommand(new CmdPartDesignScaled());
    rcCmdMgr.addCommand(new CmdPartDesignMultiTransform());

    rcCmdMgr.addCommand(new CmdPartDesignBoolean());
    rcCmdMgr.addCommand(new CmdPartDesignCompSketches());
}
