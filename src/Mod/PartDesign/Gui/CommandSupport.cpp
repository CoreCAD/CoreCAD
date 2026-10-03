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

namespace PartDesignGui::CommandSupport
{

// Gate for commands whose body comes from the selection (selectedBody): available whenever
// the document has a Body to work on.
bool hasAnyBody()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    return doc && !doc->getObjectsOfType(PartDesign::Body::getClassTypeId()).empty();
}

bool hasAnySketch()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    return doc && !doc->getObjectsOfType(Part::Part2DObject::getClassTypeId()).empty();
}

// A Python list literal of element names, escaped so a quote in a name cannot break the command.
std::string pythonNameList(const std::vector<std::string>& names)
{
    std::string list = "[";
    for (const auto& name : names) {
        list += "'" + Base::Tools::escapeEncodeString(name) + "',";
    }
    return list + "]";
}

std::vector<std::string> allEdgeNames(const Part::TopoShape& shape)
{
    std::vector<std::string> names;
    const int count = shape.countSubElements("Edge");
    for (int i = 1; i <= count; ++i) {
        names.push_back("Edge" + std::to_string(i));
    }
    return names;
}

void copyAppearance(App::DocumentObject* to, App::DocumentObject* from)
{
    for (const char* attr :
         {"ShapeAppearance", "LineColor", "PointColor", "Transparency", "DisplayMode"}) {
        Gui::Command::copyVisual(to, attr, from);
    }
}

// Cruth #130/#132: the selection decides the body a dress-up or pattern extends: the body of
// the selected geometry, else the sole body, else the chooser. There is no active body.
PartDesign::Body* selectedBody(Gui::Command* cmd)
{
    return PartDesignGui::resolveTargetBody(cmd);
}

namespace
{

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
PartDesign::Body* decideBaseBody(Part::Part2DObject* sketch, bool& abort)
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
void selectResolvedSketch(Part::Part2DObject* sketch)
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
Part::Part2DObject* resolveSketchFromSelection(Gui::Command* cmd, App::Document* doc)
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

}  // namespace

// Cruth §8.5/§4.6: resolve the base Body for a NEW sketch-based solid feature by walking
// the selected profile's anchor chain — the replacement for the legacy getBody(true)
// active-body requirement. Shared by every solid-producing command so they all decide
// spawn-vs-extend the same way (P8 UI consistency).
//
// Returns false (after showing the appropriate message) when there is no usable sketch or
// the anchor chain is ambiguous. On success @p body is the Body to extend, or nullptr for
// the auto-spawn case — makeProfileFeature() then spawns the Body inside the feature's
// undo transaction (#17), so a cancelled feature leaks nothing.
bool resolveBaseBodyForNewFeature(Gui::Command* cmd, PartDesign::Body*& body)
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
// Common utility functions for all features creating solids
//===========================================================================

void finishFeature(
    const Gui::Command* cmd,
    App::DocumentObject* feature,
    App::DocumentObject* prevSolidFeature,
    const bool hidePrevSolid,
    const bool updateDocument
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
App::DocumentObject* startFeature(Gui::Command* cmd, PartDesign::Body* body, const char* type)
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

void warnWrongSelection(const QString& text)
{
    QMessageBox::warning(Gui::getMainWindow(), QObject::tr("Wrong selection"), text);
}

}  // namespace PartDesignGui::CommandSupport
