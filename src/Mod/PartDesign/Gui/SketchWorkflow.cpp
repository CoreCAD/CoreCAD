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
#include "TaskFeaturePick.h"
#include "Utils.h"
#include "ViewProviderBody.h"
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/DatumPlane.h>
#include <Mod/PartDesign/App/ShapeBinder.h>
#include <Mod/Part/App/AttachExtension.h>
#include <Mod/Part/App/Attacher.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Sketcher/Gui/ViewProviderSketch.h>

#include <App/Document.h>
#include <App/Link.h>
#include <App/Origin.h>
#include <App/Datums.h>
#include <App/Part.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/ViewParams.h>
#include <Gui/ViewProviderPlane.h>
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

struct MissingPlanesException
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
            if (tip && tip->isDerivedFrom<Part::Feature>() && elements.size() == 1) {
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

        Part::Feature* partobject = dynamic_cast<Part::Feature*>(object);
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
            // placing the sketch here creates a DAG cycle. Auto-create a new Body.
            App::DocumentObject* supportObj = validator.getObject();
            if (activeBody && activeBody->getInListEx(true).count(supportObj)) {
                App::Document* appdoc = guidocument->getDocument();
                activeBody = PartDesignGui::makeBody(appdoc);
                if (!activeBody) {
                    throw RejectException();
                }
                tryAddNewBodyToActivePart();
                Base::Console().message(
                    "New Body created: the active Body is a dependency of the selected face.\n"
                );
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
        auto Feat = PartDesignGui::createFeature(activeBody, "Sketcher::SketchObject", FeatName);
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
            if (activeBody) {
                tryAddNewBodyToActivePart();
            }
            else {
                throw RejectException();
            }
        }
    }

    void tryAddNewBodyToActivePart()
    {
        App::Part* activePart = PartDesignGui::getActivePart();
        if (activePart) {
            activePart->addObject(activeBody);
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

class PlaneFinder
{
public:
    PlaneFinder(App::Document* appdocument, PartDesign::Body* activeBody)
        : appdocument(appdocument)
        , activeBody(activeBody)
    {}

    std::vector<App::DocumentObject*> getPlanes() const
    {
        return planes;
    }

    std::vector<PartDesignGui::TaskFeaturePick::featureStatus> getStatus() const
    {
        return status;
    }

    unsigned countValidPlanes() const
    {
        return validPlaneCount;
    }

    void findBasePlanes()
    {
        try {
            tryFindBasePlanes();
        }
        catch (const Base::Exception& ex) {
            Base::Console().error("%s\n", ex.what());
        }
    }

    void findDatumPlanes()
    {
        App::GeoFeatureGroupExtension* geoGroup = getGroupExtensionOfBody();
        const std::vector<Base::Type> types
            = {PartDesign::Plane::getClassTypeId(), App::Plane::getClassTypeId()};
        auto datumPlanes = appdocument->getObjectsOfType(types);

        for (auto plane : datumPlanes) {
            if (std::find(planes.begin(), planes.end(), plane) != planes.end()) {
                continue;  // Skip if already in planes (for base planes)
            }

            planes.push_back(plane);
            // Check whether this plane belongs to the active body: a datum plane derives to the
            // body's Tip; a world-frame base plane lives in the single shared document Origin and
            // is valid for every body (Cruth §11 step 5e shared-Origin).
            if (PartDesign::Body::backsBody(plane, activeBody)
                || (plane->isDerivedFrom<App::DatumElement>()
                    && static_cast<App::DatumElement*>(plane)->isOriginFeature())) {
                if (!activeBody->isAfterInsertPoint(plane)) {
                    validPlaneCount++;
                    status.push_back(PartDesignGui::TaskFeaturePick::validFeature);
                }
                else {
                    status.push_back(PartDesignGui::TaskFeaturePick::afterTip);
                }
            }
            else {
                PartDesign::Body* planeBody = PartDesign::Body::findBodyOf(plane);
                if (planeBody) {
                    if ((geoGroup && geoGroup->hasObject(planeBody, true))
                        || !App::GeoFeatureGroupExtension::getGroupOfObject(planeBody)) {
                        status.push_back(PartDesignGui::TaskFeaturePick::otherBody);
                    }
                    else {
                        status.push_back(PartDesignGui::TaskFeaturePick::otherPart);
                    }
                }
                else {
                    if ((geoGroup && geoGroup->hasObject(plane, true))
                        || App::GeoFeatureGroupExtension::getGroupOfObject(plane)) {
                        status.push_back(PartDesignGui::TaskFeaturePick::otherPart);
                    }
                    else {
                        status.push_back(PartDesignGui::TaskFeaturePick::notInBody);
                    }
                }
            }
        }
    }

    void findShapeBinderPlanes()
    {

        // Collect also shape binders consisting of a single planar face
        auto shapeBinders(appdocument->getObjectsOfType(PartDesign::ShapeBinder::getClassTypeId()));
        auto binders(appdocument->getObjectsOfType(PartDesign::SubShapeBinder::getClassTypeId()));
        shapeBinders.insert(shapeBinders.end(), binders.begin(), binders.end());
        for (auto binder : shapeBinders) {
            // Check whether this plane belongs to the active body
            if (PartDesign::Body::backsBody(binder, activeBody)) {
                Part::TopoShape shape = static_cast<Part::Feature*>(binder)->Shape.getShape();
                if (shape.isPlanar()) {
                    if (!activeBody->isAfterInsertPoint(binder)) {
                        validPlaneCount++;
                        planes.push_back(binder);
                        status.push_back(PartDesignGui::TaskFeaturePick::validFeature);
                    }
                }
            }
        }
    }

private:
    void tryFindBasePlanes()
    {
        auto* origin = activeBody->getOrigin();
        for (auto plane : origin->planes()) {
            planes.push_back(plane);
            status.push_back(PartDesignGui::TaskFeaturePick::basePlane);
            validPlaneCount++;
        }
    }

    App::GeoFeatureGroupExtension* getGroupExtensionOfBody() const
    {
        App::GeoFeatureGroupExtension* geoGroup {nullptr};
        if (activeBody) {
            auto group(App::GeoFeatureGroupExtension::getGroupOfObject(activeBody));
            if (group) {
                geoGroup = group->getExtensionByType<App::GeoFeatureGroupExtension>();
            }
        }

        return geoGroup;
    }

private:
    App::Document* appdocument;
    PartDesign::Body* activeBody;
    unsigned validPlaneCount = 0;
    std::vector<App::DocumentObject*> planes;
    std::vector<PartDesignGui::TaskFeaturePick::featureStatus> status;
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
        catch (const MissingPlanesException&) {
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
        // Origin-plane highlighting is a non-critical visual aid. Without an
        // active Body there is no Origin to show; the user can still pick a
        // global plane in the attachment dialog.
        if (!activeBody) {
            return;
        }
        // Cruth substrate flip (Stage 3a-2b): highlight the shared document-level Origin's
        // planes, not the dormant per-body Origin. This is the coordinate root de-owned
        // features resolve against, so the plane the user clicks is the one the sketch
        // ends up attached to (no later relink needed for display consistency).
        auto* origin = activeBody->getDocumentOrigin();
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
            Gui::Command::doCommand(
                Gui::Command::Doc,
                "App.activeDocument().addObject('Sketcher::SketchObject','%s')",
                FeatName.c_str()
            );
            if (App::Part* activePart = PartDesignGui::getActivePart()) {
                Gui::Command::doCommand(
                    Gui::Command::Doc,
                    "%s.addObject(%s)",
                    Gui::Command::getObjectCmd(activePart).c_str(),
                    Gui::Command::getObjectCmd(doc->getObject(FeatName.c_str())).c_str()
                );
            }
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
        auto onAccept = [partDesignBody, sketch]() {
            resetOriginVisibility(partDesignBody);

            Gui::Selection().clearSelection();

            PartDesignGui::setEdit(sketch, partDesignBody);
        };
        auto onReject = [partDesignBody]() {
            resetOriginVisibility(partDesignBody);
        };

        Gui::Selection().clearSelection();

        // Open attachment dialog
        auto* vps = dynamic_cast<SketcherGui::ViewProviderSketch*>(
            Gui::Application::Instance->getViewProvider(sketch)
        );
        vps->showAttachmentEditor(onAccept, onReject);
    }

    static void resetOriginVisibility(PartDesign::Body* partDesignBody)
    {
        if (!partDesignBody) {
            return;
        }
        // Cruth substrate flip (Stage 3a-2b): must match the Origin made visible in
        // setOriginTemporaryVisibility() — the shared document-level one, not per-body.
        auto* origin = partDesignBody->getDocumentOrigin();
        auto* vpo = dynamic_cast<Gui::ViewProviderCoordinateSystem*>(
            Gui::Application::Instance->getViewProvider(origin)
        );
        if (vpo) {
            vpo->resetTemporaryVisibility();
            vpo->resetTemporarySize();
            vpo->setPlaneLabelVisibility(false);
        }
    }

    void findAndSelectPlane()
    {
        App::Document* appdocument = guidocument->getDocument();
        PlaneFinder planeFinder {appdocument, activeBody};

        planeFinder.findBasePlanes();
        planeFinder.findDatumPlanes();
        planeFinder.findShapeBinderPlanes();

        std::vector<App::DocumentObject*> planes = planeFinder.getPlanes();
        std::vector<PartDesignGui::TaskFeaturePick::featureStatus> status = planeFinder.getStatus();
        unsigned validPlaneCount = planeFinder.countValidPlanes();

        for (auto& plane : planes) {
            auto* planeViewProvider
                = Gui::Application::Instance->getViewProvider<Gui::ViewProviderPlane>(plane);

            // skip updating planes from coordinate systems
            if (!planeViewProvider || !planeViewProvider->getRole().empty()) {
                continue;
            }

            planeViewProvider->setLabelVisibility(true);
            planeViewProvider->setTemporaryScale(
                Gui::ViewParams::instance()->getDatumTemporaryScaleFactor()
            );
        }

        //
        // Lambda definitions
        //
        App::Document* documentOfBody = appdocument;
        PartDesign::Body* partDesignBody = activeBody;

        auto restorePlaneVisibility = [planes]() {
            for (auto& plane : planes) {
                auto* planeViewProvider
                    = Gui::Application::Instance->getViewProvider<Gui::ViewProviderPlane>(plane);
                if (!planeViewProvider) {
                    continue;
                }

                planeViewProvider->resetTemporarySize();
                planeViewProvider->setLabelVisibility(false);
            }
        };

        // Determines if user made a valid selection in dialog
        auto acceptFunction =
            [restorePlaneVisibility](const std::vector<App::DocumentObject*>& features) -> bool {
            restorePlaneVisibility();
            return !features.empty();
        };

        // Called by dialog when user hits "OK" and accepter returns true
        auto processFunction = [documentOfBody,
                                partDesignBody](const std::vector<App::DocumentObject*>& features) {
            SketchRequestSelection::createSketch(documentOfBody, partDesignBody, features);
        };

        // Called by dialog for "Cancel", or "OK" if accepter returns false
        std::string docname = documentOfBody->getName();
        auto rejectFunction = [docname, restorePlaneVisibility]() {
            restorePlaneVisibility();
            Gui::Document* document = Gui::Application::Instance->getDocument(docname.c_str());
            if (document) {
                document->abortCommand();
            }
        };

        //
        // End of lambda definitions
        //

        if (validPlaneCount == 0) {
            throw MissingPlanesException();
        }
        else if (validPlaneCount == 1) {
            processFunction(planes);
        }
        else if (validPlaneCount > 1) {
            checkForShownDialog();
            Gui::Selection().clearSelection();

            // Show dialog and let user pick plane
            Gui::Control().showDialog(new PartDesignGui::TaskDlgFeaturePick(
                planes,
                status,
                acceptFunction,
                processFunction,
                true,
                rejectFunction
            ));
        }
    }

    void checkForShownDialog()
    {
        App::Document* appdocument = guidocument->getDocument();
        Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog(appdocument);
        PartDesignGui::TaskDlgFeaturePick* pickDlg
            = qobject_cast<PartDesignGui::TaskDlgFeaturePick*>(dlg);
        if (dlg && !pickDlg) {
            QMessageBox msgBox(Gui::getMainWindow());
            msgBox.setText(QObject::tr("A dialog is already open in the task panel"));
            msgBox.setInformativeText(QObject::tr("Close this dialog?"));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::Yes);
            int ret = msgBox.exec();
            if (ret == QMessageBox::Yes) {
                Gui::Control().closeDialog();
            }
            else {
                throw RejectException();
            }
        }

        if (dlg) {
            Gui::Control().closeDialog();
        }
    }

    static void createSketch(
        App::Document* documentOfBody,
        PartDesign::Body* partDesignBody,
        const std::vector<App::DocumentObject*>& features
    )
    {
        // may happen when the user switched to an empty document while the
        // dialog is open
        if (features.empty()) {
            return;
        }
        std::string FeatName = documentOfBody->getUniqueObjectName("Sketch");
        auto* plane = static_cast<App::Plane*>(features.front());
        auto* lcs = plane->getLCS();

        std::string supportString;
        if (lcs) {
            supportString = Gui::Command::getObjectCmd(lcs, "(") + ",['"
                + plane->getNameInDocument() + "'])";
        }
        else {
            supportString = Gui::Command::getObjectCmd(plane, "(", ",[''])");
        }

        App::Document* doc = partDesignBody->getDocument();
        if (!doc->hasPendingTransaction()) {
            doc->openTransaction(QT_TRANSLATE_NOOP("Command", "New Sketch"));
        }

        auto Feat = PartDesignGui::createFeature(partDesignBody, "Sketcher::SketchObject", FeatName);
        FCMD_OBJ_CMD(Feat, "AttachmentSupport = " << supportString);
        FCMD_OBJ_CMD(
            Feat,
            "MapMode = '" << Attacher::AttachEngine::getModeName(Attacher::mmFlatFace) << "'"
        );
        Gui::Command::updateActive();  // Make sure the AttachmentSupport's Placement property is updated
        PartDesignGui::setEdit(Feat, partDesignBody);
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
    catch (const MissingPlanesException&) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("No valid planes in this document"),
            QObject::tr("Create a plane first or select a face to sketch on")
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
    App::DocumentObject* topParent = nullptr;
    PartDesign::Body* pdBody = PartDesignGui::getBody(
        /* messageIfNot = */ false,
        /* autoActivate = */ false,
        /* assertModern = */ true,
        &topParent
    );
    if (pdBody && topParent && topParent->isLink()) {
        auto* xLink = dynamic_cast<App::Link*>(topParent);
        pdBody->Placement.setValue(xLink->Placement.getValue());
    }

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
    Gui::SelectionFilter PlaneFilter2("SELECT PartDesign::Plane COUNT 1", activeBody);
    Gui::SelectionFilter SketchFilter("SELECT Part::Part2DObject COUNT 1", activeBody);

    if (PlaneFilter2.match()) {
        PlaneFilter = PlaneFilter2;
    }

    return std::make_tuple(FaceFilter, PlaneFilter, SketchFilter);
}
