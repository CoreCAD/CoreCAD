// SPDX-License-Identifier: LGPL-2.1-or-later

/**************************************************************************
 *   Copyright (c) 2022 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *   Copyright (c) 2026 Cruth contributors                                 *
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

#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <boost/signals2.hpp>
#include <map>
#include <string>
#include <vector>
#include <QApplication>
#include <QMessageBox>


#include "SketchWorkflow.h"
#include "Utils.h"
#include "ViewProviderBody.h"
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/ShapeBinder.h>
#include <Mod/Part/App/AttachExtension.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Part/App/Attacher.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Sketcher/Gui/ViewProviderSketch.h>

#include <App/Document.h>
#include <App/Link.h>
#include <App/Origin.h>
#include <App/Datums.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/ViewProviderCoordinateSystem.h>
#include <Gui/Selection/SelectionFilter.h>

using namespace PartDesignGui;

namespace
{
struct RejectException
{
};

struct WrongSelectionException
{
};

struct WrongSupportException
{
};

struct SupportNotPlanarException
{
};

class SupportFaceValidator
{
public:
    explicit SupportFaceValidator(Gui::SelectionObject faceSelection)
        : faceSelection(faceSelection)
    {}

    void handleSelectedBody(PartDesign::Body* activeBody)
    {
        App::DocumentObject* object = faceSelection.getObject();
        std::vector<std::string> elements = faceSelection.getSubNames();

        // In case the selected face belongs to the body then it means its
        // Display Mode Body is set to Tip. But the body face is not allowed
        // to be used as support because otherwise it would cause a cyclic
        // dependency. So, instead we use the tip object as reference.
        // https://forum.freecad.org/viewtopic.php?f=3&t=37448
        if (object == activeBody) {
            App::DocumentObject* tip = activeBody->Tip.getValue();
            if (tip && Part::hasShape(tip) && elements.size() == 1) {
                Gui::SelectionChanges msg;
                msg.pDocName = faceSelection.getDocName();
                msg.pObjectName = tip->getNameInDocument();
                msg.pSubName = elements[0].c_str();
                msg.pTypeName = tip->getTypeId().getName();

                faceSelection = Gui::SelectionObject {msg};

                // automatically switch to 'Through' mode
                setThroughModeOfBody(activeBody);
            }
        }
    }

    void throwIfInvalid()
    {
        App::DocumentObject* object = faceSelection.getObject();
        std::vector<std::string> elements = faceSelection.getSubNames();

        Part::ShapeFeature* partobject = dynamic_cast<Part::ShapeFeature*>(object);
        if (!partobject) {
            throw WrongSelectionException();
        }

        if (elements.size() != 1) {
            throw WrongSelectionException();
        }

        // get the selected sub shape (a Face)
        const Part::TopoShape& shape = partobject->Shape.getValue();
        Part::TopoShape subshape(shape.getSubShape(elements[0].c_str()));
        if (subshape.isNull()) {
            throw WrongSupportException();
        }

        if (!subshape.isPlanar(Attacher::AttachEnginePlane::planarPrecision())) {
            throw SupportNotPlanarException();
        }
    }

    std::string getSupport() const
    {
        return faceSelection.getAsPropertyLinkSubString();
    }

    App::DocumentObject* getObject() const
    {
        return faceSelection.getObject();
    }

private:
    void setThroughModeOfBody(PartDesign::Body* activeBody)
    {
        // automatically switch to 'Through' mode
        PartDesignGui::ViewProviderBody* vpBody = dynamic_cast<PartDesignGui::ViewProviderBody*>(
            Gui::Application::Instance->getViewProvider(activeBody)
        );
        if (vpBody) {
            vpBody->DisplayModeBody.setValue("Through");
        }
    }

private:
    mutable Gui::SelectionObject faceSelection;
};

class SupportPlaneValidator
{
public:
    explicit SupportPlaneValidator(Gui::SelectionObject faceSelection)
        : faceSelection(faceSelection)
    {}

    std::string getSupport() const
    {
        return faceSelection.getAsPropertyLinkSubString();
    }

    App::DocumentObject* getObject() const
    {
        return faceSelection.getObject();
    }

private:
    mutable Gui::SelectionObject faceSelection;
};

class SketchPreselection
{
public:
    SketchPreselection(
        Gui::Document* guidocument,
        PartDesign::Body* activeBody,
        std::tuple<Gui::SelectionFilter, Gui::SelectionFilter, Gui::SelectionFilter> filter
    )
        : guidocument(guidocument)
        , activeBody(activeBody)
        , faceFilter(std::get<0>(filter))
        , planeFilter(std::get<1>(filter))
        , sketchFilter(std::get<2>(filter))
    {}

    bool matches()
    {
        return faceFilter.match() || planeFilter.match() || sketchFilter.match();
    }

    // True only when a single planar face or datum plane is selected (not a sketch).
    // Used for the fast-path that skips the attachment dialog.
    bool isSingleFaceOrPlane()
    {
        return (faceFilter.match() || planeFilter.match()) && !sketchFilter.match();
    }

    std::string getSupport() const
    {
        return supportString;
    }

    void createSupport()
    {
        createBodyOrThrow();

        if (faceFilter.match()) {
            Gui::SelectionObject faceSelObject = faceFilter.Result[0][0];
            SupportFaceValidator validator {faceSelObject};
            validator.handleSelectedBody(activeBody);
            validator.throwIfInvalid();
            supportString = validator.getSupport();

            // Guard: if activeBody is a transitive dependency of the support object,
            // splicing the sketch into that Body's pipeline creates a DAG cycle. Cruth
            // feature-first: rather than manufacture an empty Body to hold the sketch,
            // detach and let the sketch be born free at document level — the Pad anchor
            // walk decides which Body (new or existing) claims it later.
            App::DocumentObject* supportObj = validator.getObject();
            if (activeBody && activeBody->getInListEx(true).count(supportObj)) {
                activeBody = nullptr;
            }
        }
        else if (planeFilter.match()) {
            SupportPlaneValidator validator(planeFilter.Result[0][0]);
            supportString = validator.getSupport();
        }
        else {
            // For a sketch, the support is the object itself with no sub-element.
            Gui::SelectionObject sketchSelObject = sketchFilter.Result[0][0];
            supportString = sketchSelObject.getAsPropertyLinkSubString();
        }
        // CoreCAD Phase 2: cross-Body face references are valid. No ShapeBinder copy needed.
    }

    void createSketchOnSupport(const std::string& supportString)
    {
        // create Sketch on Face or Plane
        App::Document* appdocument = guidocument->getDocument();
        std::string FeatName = appdocument->getUniqueObjectName("Sketch");

        guidocument->openCommand(QT_TRANSLATE_NOOP("Command", "Sketch on Face"));
        App::DocumentObject* Feat = nullptr;
        if (activeBody) {
            Feat = PartDesignGui::createFeature(activeBody, "Sketcher::SketchObject", FeatName);
        }
        else {
            // Born free: the document is the container (ARCHITECTURE §7.1).
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.getDocument('%s').addObject('Sketcher::SketchObject','%s')",
                appdocument->getName(),
                FeatName.c_str()
            );
            Feat = appdocument->getObject(FeatName.c_str());
        }
        if (!Feat) {
            throw RejectException();
        }
        FCMD_OBJ_CMD(Feat, "AttachmentSupport = " << supportString);
        if (sketchFilter.match()) {
            FCMD_OBJ_CMD(
                Feat,
                "MapMode = '" << Attacher::AttachEngine::getModeName(Attacher::mmObjectXY) << "'"
            );
        }
        else {  // For Face or Plane
            FCMD_OBJ_CMD(
                Feat,
                "MapMode = '" << Attacher::AttachEngine::getModeName(Attacher::mmFlatFace) << "'"
            );
        }
        Gui::Command::updateActive();
        PartDesignGui::setEdit(Feat, activeBody);
    }

private:
    void createBodyOrThrow()
    {
        if (!activeBody) {
            activeBody = PartDesignGui::getBody(/* messageIfNot = */ true);
            if (!activeBody) {
                throw RejectException();
            }
        }
    }

private:
    Gui::Document* guidocument;
    PartDesign::Body* activeBody;
    Gui::SelectionFilter faceFilter;
    Gui::SelectionFilter planeFilter;
    Gui::SelectionFilter sketchFilter;
    std::string supportString;
};

class SketchRequestSelection
{
public:
    SketchRequestSelection(Gui::Document* guidocument, PartDesign::Body* activeBody)
        : guidocument(guidocument)
        , activeBody(activeBody)
    {}

    void findSupport()
    {
        try {
            // Start command early, so undo will undo any Body creation
            guidocument->openCommand(QT_TRANSLATE_NOOP("Command", "New Sketch"));
            tryFindSupport();
        }
        catch (const RejectException&) {
            guidocument->abortCommand();
            throw;
        }
    }

private:
    void tryFindSupport()
    {
        // CoreCAD POC: no Body is created here. The sketch is born free and the
        // Pad anchor walk later decides which Body (new or existing) owns it.
        createSketchAndShowAttachment();
    }

    void setOriginTemporaryVisibility()
    {
        // Cruth substrate flip: the base planes belong to the shared document-level
        // Origin, not to any Body, so highlight them for picking whether or not a Body
        // is active. On an empty doc / no-active-body the planes are the ONLY thing the
        // user can attach a sketch to (no solid faces yet); the old !activeBody
        // early-return left the attachment dialog with nothing selectable ("no picker").
        auto* origin = PartDesign::Body::findDocumentOrigin(guidocument->getDocument());
        if (!origin) {
            return;
        }
        auto* vpo = dynamic_cast<Gui::ViewProviderCoordinateSystem*>(
            Gui::Application::Instance->getViewProvider(origin)
        );
        if (vpo) {
            vpo->setTemporaryVisibility(Gui::DatumElement::Planes | Gui::DatumElement::Axes);
            vpo->setPlaneLabelVisibility(true);
        }
    }

    void createSketchAndShowAttachment()
    {
        setOriginTemporaryVisibility();

        // Capture selection before clearing it to pre-populate the attachment dialog.
        // This mirrors UnifiedDatumCommand: use attacher to find the best fit mode.
        App::PropertyLinkSubList support;
        Gui::Selection().getAsPropertyLinkSubList(support);
        if (activeBody) {
            support.removeValue(activeBody);
        }

        // Don't pre-populate when the selection contains sketches. A sketch selected
        // from prior work should not automatically become the attachment reference —
        // the user can choose a face or plane in the dialog.
        bool hasSketch = std::ranges::any_of(support.getValues(), [](App::DocumentObject* obj) {
            return obj && obj->isDerivedFrom<Part::Part2DObject>();
        });

        // Create sketch. CoreCAD POC: when there is an active Body, nest the sketch
        // in it (legacy behaviour); otherwise create it free at document level and,
        // if a Part workspace is active, add it there. The Pad anchor walk decides
        // Body ownership later.
        App::Document* doc = guidocument->getDocument();
        std::string FeatName = doc->getUniqueObjectName("Sketch");
        if (activeBody) {
            PartDesignGui::createFeature(activeBody, "Sketcher::SketchObject", FeatName);
        }
        else {
            // No active body: the bodyless sketch lives directly in the document (the
            // document is the container, ARCHITECTURE §7.1). No App::Part to file it into.
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.activeDocument().addObject('Sketcher::SketchObject','%s')",
                FeatName.c_str()
            );
        }
        auto sketch = doc->getObject(FeatName.c_str());

        if (!hasSketch && support.getSize() > 0) {
            if (auto* pcAttach = sketch->getExtensionByType<Part::AttachExtension>()) {
                pcAttach->attacher().setReferences(support);
                Attacher::SuggestResult sugr;
                pcAttach->attacher().suggestMapModes(sugr);
                if (sugr.message == Attacher::SuggestResult::srOK) {
                    FCMD_OBJ_CMD(sketch, "AttachmentSupport = " << support.getPyReprString());
                    FCMD_OBJ_CMD(
                        sketch,
                        "MapMode = '" << Attacher::AttachEngine::getModeName(sugr.bestFitMode) << "'"
                    );
                    Gui::Command::updateActive();
                }
            }
        }

        PartDesign::Body* partDesignBody = activeBody;
        auto onAccept = [partDesignBody, sketch, doc]() {
            resetOriginVisibility(doc);

            Gui::Selection().clearSelection();

            PartDesignGui::setEdit(sketch, partDesignBody);
        };
        auto onReject = [doc]() {
            resetOriginVisibility(doc);
        };

        Gui::Selection().clearSelection();

        // Open attachment dialog
        auto* vps = dynamic_cast<SketcherGui::ViewProviderSketch*>(
            Gui::Application::Instance->getViewProvider(sketch)
        );
        vps->showAttachmentEditor(onAccept, onReject);
    }

    static void resetOriginVisibility(App::Document* doc)
    {
        // Must match the Origin made visible in setOriginTemporaryVisibility() — the
        // shared document-level one — and, like it, run whether or not a Body is active.
        auto* origin = PartDesign::Body::findDocumentOrigin(doc);
        if (!origin) {
            return;
        }
        auto* vpo = dynamic_cast<Gui::ViewProviderCoordinateSystem*>(
            Gui::Application::Instance->getViewProvider(origin)
        );
        if (vpo) {
            vpo->resetTemporaryVisibility();
            vpo->resetTemporarySize();
            vpo->setPlaneLabelVisibility(false);
        }
    }

private:
    Gui::Document* guidocument;
    PartDesign::Body* activeBody;
};

}  // namespace

SketchWorkflow::SketchWorkflow(Gui::Document* document)
    : guidocument(document)
{
    appdocument = guidocument->getDocument();
}

void SketchWorkflow::createSketch()
{
    try {
        tryCreateSketch();
    }
    catch (const RejectException&) {
    }
    catch (const WrongSelectionException&) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Several sub-elements selected"),
            QObject::tr("Select a single face as support for a sketch!")
        );
    }
    catch (const WrongSupportException&) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("No support face selected"),
            QObject::tr("Select a face as support for a sketch!")
        );
    }
    catch (const SupportNotPlanarException&) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("No planar support"),
            QObject::tr("Need a planar face as support for a sketch!")
        );
    }
}

void SketchWorkflow::tryCreateSketch()
{
    auto result = shouldCreateBody();
    auto shouldMakeBody = std::get<0>(result);
    activeBody = std::get<1>(result);
    if (shouldAbort(shouldMakeBody)) {
        return;
    }

    // CoreCAD POC: route every PartDesign sketch creation through the attachment
    // dialog by default. The Phase 4 attempt at this default was reverted because
    // of a "body disappears" UX glitch — re-evaluate under the auto-spawn flow.
    bool useAttachment = App::GetApplication()
                             .GetParameterGroupByPath(
                                 "User parameter:BaseApp/Preferences/Mod/PartDesign"
                             )
                             ->GetBool("NewSketchUseAttachmentDialog", true);

    bool shiftHeld = QApplication::queryKeyboardModifiers() & Qt::ShiftModifier;

    auto filters = getFilters();
    SketchPreselection sketchOnFace {guidocument, activeBody, filters};

    // Fast path: single face or datum plane, preference off, Shift not held.
    // If the face turns out to be non-planar or otherwise invalid, fall through
    // to the attachment dialog instead of showing an error.
    // A selected sketch, multiple references, no selection, Shift, or preference on
    // all go through the attachment dialog.
    // CoreCAD POC: the fast path requires a Body to host the sketch and is not
    // null-safe, so skip it entirely when no Body is active. Sketches are now
    // born free (see shouldCreateBody/createSketchAndShowAttachment).
    if (activeBody && !useAttachment && !shiftHeld && sketchOnFace.isSingleFaceOrPlane()) {
        try {
            sketchOnFace.createSupport();
            sketchOnFace.createSketchOnSupport(sketchOnFace.getSupport());
            return;
        }
        catch (const WrongSupportException&) {
            // Fall through to attachment dialog
        }
        catch (const WrongSelectionException&) {
            // Fall through to attachment dialog
        }
        catch (const SupportNotPlanarException&) {
            // Fall through to attachment dialog
        }
    }

    SketchRequestSelection requestSelection {guidocument, activeBody};
    requestSelection.findSupport();
}

std::tuple<bool, PartDesign::Body*> SketchWorkflow::shouldCreateBody()
{
    // CoreCAD POC: sketches are now born free. Sketch creation never spawns a
    // Body and never shows the legacy DlgActiveBody modal — the Pad anchor walk
    // is the single thing that decides Body spawn-vs-extend. We simply report
    // the active Body if there is one (it may be null).
    // If we are inside a link, we still need to use its placement.
    // CoreCAD POC: autoActivate is OFF here. We deliberately do NOT let getBody
    // auto-activate the lone Body of a single-Body document — otherwise every
    // sketch would nest into that Body and a second independent Body could never
    // be started. A Body is used only when one is genuinely active; with no
    // active Body the sketch is born free and Pad's anchor walk owns the spawn.
    //
    // (issue #12) A de-owned Body carries no frame of its own, so the former "sketch inside a
    // Link: copy the Link's placement onto the Body" step is gone — a Body has no Placement to
    // write. When editing inside a Link, the sketch resolves its own world position through its
    // attachment and the Link's global placement, not through a mutated Body frame.
    PartDesign::Body* pdBody = PartDesignGui::getBody(
        /* messageIfNot = */ false,
        /* autoActivate = */ false,
        /* assertModern = */ true
    );

    return std::make_tuple(false, pdBody);
}

bool SketchWorkflow::shouldAbort(bool) const
{
    // CoreCAD POC: a missing Body no longer aborts sketch creation.
    return false;
}

std::tuple<Gui::SelectionFilter, Gui::SelectionFilter, Gui::SelectionFilter> SketchWorkflow::getFilters() const
{
    // Hint:
    // The behaviour of this command has changed with respect to a selected sketch:
    // It doesn't try any more to edit a selected sketch but always tries to create
    // a new sketch.
    // See https://forum.freecad.org/viewtopic.php?f=3&t=44070

    Gui::SelectionFilter FaceFilter("SELECT Part::Feature SUBELEMENT Face COUNT 1");
    Gui::SelectionFilter PlaneFilter("SELECT App::Plane COUNT 1", activeBody);
    Gui::SelectionFilter PlaneFilter2("SELECT Part::DatumPlane COUNT 1", activeBody);
    Gui::SelectionFilter SketchFilter("SELECT Part::Part2DObject COUNT 1", activeBody);

    if (PlaneFilter2.match()) {
        PlaneFilter = PlaneFilter2;
    }

    return std::make_tuple(FaceFilter, PlaneFilter, SketchFilter);
}
