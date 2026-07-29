// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <QMenu>

#include <algorithm>
#include <set>
#include <vector>

#include <App/Document.h>
#include <App/Origin.h>
#include <App/Part.h>
#include <App/VarSet.h>
#include <Base/Console.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/MDIView.h>
#include <Gui/Selection/SoFCUnifiedSelection.h>
#include <Gui/ViewProviderDatum.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>
#include <Mod/PartDesign/App/FeatureBase.h>
#include <Mod/PartDesign/App/ShapeBinder.h>

#include "ViewProviderBody.h"
#include "Utils.h"
#include "ViewProvider.h"


using namespace PartDesignGui;
namespace sp = std::placeholders;

const char* PartDesignGui::ViewProviderBody::BodyModeEnum[] = {"Through", "Tip", nullptr};

PROPERTY_SOURCE(PartDesignGui::ViewProviderBody, PartGui::ViewProviderPart)

ViewProviderBody::ViewProviderBody()
{
    ADD_PROPERTY(DisplayModeBody, ((long)0));
    DisplayModeBody.setEnums(BodyModeEnum);

    sPixmap = "PartDesign_Body.svg";

    // Own the scene nodes that the retired Gui OriginGroup extension used to provide
    // (Cruth §11 step 5e). pcBodyChildren is both the "Group"/Through display-mask node
    // and the 3D child root; front/back are the annotation separators.
    pcBodyChildren = new Gui::SoFCSelectionRoot;
    pcBodyChildren->ref();
    pcBodyFront = new SoSeparator();
    pcBodyFront->ref();
    pcBodyBack = new SoSeparator();
    pcBodyBack->ref();
}

ViewProviderBody::~ViewProviderBody()
{
    pcBodyChildren->unref();
    pcBodyFront->unref();
    pcBodyBack->unref();
}

SoGroup* ViewProviderBody::getChildRoot() const
{
    return pcBodyChildren;
}

SoSeparator* ViewProviderBody::getFrontRoot() const
{
    return pcBodyFront;
}

SoSeparator* ViewProviderBody::getBackRoot() const
{
    return pcBodyBack;
}

void ViewProviderBody::attach(App::DocumentObject* pcFeat)
{
    // call parent attach method
    ViewProviderPart::attach(pcFeat);

    // Register the "Group" display-mask mode against our own child-root node (formerly
    // done by ViewProviderGeoFeatureGroupExtension::extensionAttach). onChanged() switches
    // to this mode for "Through" body display; Document::handleChildren3D parents the
    // pipeline features under the same node.
    addDisplayMaskMode(pcBodyChildren, "Group");

    // set default display mode
    onChanged(&DisplayModeBody);

    if (App::Document* doc = pcFeat->getDocument()) {
        m_RecomputedConn = doc->signalRecomputed.connect(
            [this](const App::Document& doc, const std::vector<App::DocumentObject*>& recomputedObjs) {
                this->afterRecompute(doc, recomputedObjs);
            }
        );
    }
    m_ChangedConn = Gui::Application::Instance->signalChangedObject.connect(
        [this](const Gui::ViewProvider& vp, const App::Property& prop) {
            this->onChangedObject(vp, prop);
        }
    );
}

void ViewProviderBody::onChangedObject(const Gui::ViewProvider& vp, const App::Property& prop)
{
    static const std::unordered_set<std::string> watchedProps {"Visibility"};
    if (!watchedProps.contains(prop.getName())) {
        return;
    }
    auto* vpd = dynamic_cast<const Gui::ViewProviderDocumentObject*>(&vp);
    if (!vpd) {
        return;
    }
    auto* changedObj = vpd->getObject();
    if (!changedObj) {
        return;
    }

    auto* body = this->getObject<PartDesign::Body>();
    if (!body) {
        return;
    }
    const auto& features = body->getFullModel();
    bool isRelevantChange = (changedObj == body)
        || (std::ranges::find(features, changedObj) != features.end());

    if (isRelevantChange) {
        refreshOverlays();
    }
}

void ViewProviderBody::afterRecompute(
    const App::Document& /* doc */,
    const std::vector<App::DocumentObject*>& recomputedObjs
)
{
    refreshOverlays();

    // Cruth §3.3: a Body's displayed geometry is DERIVED from its Tip (getRenderedShape ->
    // getSubObject -> derivedTipShape), not stored in a Shape property, so no "Shape"
    // property change fires the base rebuild any more. Drive the rebuild from recompute
    // instead: when this Body recomputed (it links its Tip, so a change to the tip feature
    // recomputes it), its derived shape may have changed — refresh the Coin visual, mirroring
    // the base's visibility guard so a hidden Body defers until it is shown again.
    auto* body = getObject<PartDesign::Body>();
    if (!body) {
        return;
    }
    if (std::ranges::find(recomputedObjs, static_cast<App::DocumentObject*>(body))
        != recomputedObjs.end()) {
        if (isUpdateForced() || Visibility.getValue()) {
            updateVisual();
        }
        else {
            VisualTouched = true;
        }
    }
}

void ViewProviderBody::refreshOverlays()
{
    auto* body = getObject<PartDesign::Body>();
    if (!body) {
        return;
    }
    for (auto* obj : body->getFullModel()) {
        Gui::ViewProvider* vpBase = Gui::Application::Instance->getViewProvider(obj);
        if (auto* vpPartDesign = dynamic_cast<PartDesignGui::ViewProvider*>(vpBase)) {
            vpPartDesign->updateOverlay();
        }
    }
}

// TODO on activating the body switch to the "Through" mode (2015-09-05, Fat-Zer)
// TODO different icon in tree if mode is Through (2015-09-05, Fat-Zer)
// TODO drag&drop (2015-09-05, Fat-Zer)
// TODO Add activate () call (2015-09-08, Fat-Zer)

void ViewProviderBody::setDisplayMode(const char* ModeName)
{

    // if we show "Through" we must avoid to set the display mask modes, as this would result
    // in going into "tip" mode. When through is chosen the child features are displayed, and all
    // we need to ensure is that the display mode change is propagated to them from within the
    // onChanged() method.
    if (DisplayModeBody.getValue() == 1) {
        PartGui::ViewProviderPartExt::setDisplayMode(ModeName);
    }
}

void ViewProviderBody::setOverrideMode(const std::string& mode)
{

    // if we are in through mode, we need to ensure that the override mode is not set for the body
    //(as this would result in "tip" mode), it is enough when the children are set to the correct
    // override mode.

    if (DisplayModeBody.getValue() != 0) {
        Gui::ViewProvider::setOverrideMode(mode);
    }
    else {
        overrideMode = mode;

        // Propagate the override mode to child features.
        // When the Body is an external link, the global viewport loop
        // won't reach these children automatically.
        if (pcObject && !isRestoring()) {
            Gui::Document* gdoc = Gui::Application::Instance->getDocument(pcObject->getDocument());
            if (gdoc) {
                PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
                auto features = body->getFullModel();
                for (auto feature : features) {
                    if (feature && feature->isDerivedFrom<PartDesign::Feature>()) {
                        if (Gui::ViewProvider* vp = gdoc->getViewProvider(feature)) {
                            vp->setOverrideMode(mode);
                        }
                    }
                }
                if (App::DocumentObject* base = body->BaseFeature.getValue()) {
                    if (Gui::ViewProvider* vp = gdoc->getViewProvider(base)) {
                        vp->setOverrideMode(mode);
                    }
                }
            }
        }
    }
}

void ViewProviderBody::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    Q_UNUSED(receiver);
    Q_UNUSED(member);
    Gui::ActionFunction* func = new Gui::ActionFunction(menu);

    QAction* act = menu->addAction(tr("Active Body"));
    act->setCheckable(true);
    act->setChecked(isActiveBody());
    func->trigger(act, [this]() { this->toggleActiveBody(); });

    Gui::ViewProviderGeometryObject::setupContextMenu(menu, receiver, member);  // clazy:exclude=skipped-base-method
}

bool ViewProviderBody::isActiveBody()
{
    auto activeDoc = Gui::Application::Instance->activeDocument();
    if (!activeDoc) {
        activeDoc = getDocument();
    }
    auto activeView = activeDoc->setActiveView(this);
    if (!activeView) {
        return false;
    }

    if (activeView->isActiveObject(getObject(), PDBODYKEY)) {
        return true;
    }
    else {
        return false;
    }
}

void ViewProviderBody::toggleActiveBody()
{
    if (isActiveBody()) {
        // active body double-clicked. Deactivate.
        Gui::Command::doCommand(
            Gui::Command::Gui,
            "Gui.ActiveDocument.ActiveView.setActiveObject('%s', None)",
            PDBODYKEY
        );
    }
    else {

        // assure the PartDesign workbench
        if (App::GetApplication()
                .GetUserParameter()
                .GetGroup("BaseApp")
                ->GetGroup("Preferences")
                ->GetGroup("Mod/PartDesign")
                ->GetBool("SwitchToWB", true)) {
            Gui::Command::assureWorkbench("PartDesignWorkbench");
        }

        // and set correct active objects
        auto* part = App::Part::getPartOfObject(getObject());
        if (part && !isActiveBody()) {
            Gui::Command::doCommand(
                Gui::Command::Gui,
                "Gui.ActiveDocument.ActiveView.setActiveObject('%s',%s)",
                PARTKEY,
                Gui::Command::getObjectCmd(part).c_str()
            );
        }

        Gui::Command::doCommand(
            Gui::Command::Gui,
            "Gui.ActiveDocument.ActiveView.setActiveObject('%s',%s)",
            PDBODYKEY,
            Gui::Command::getObjectCmd(getObject()).c_str()
        );
    }
}

bool ViewProviderBody::doubleClicked()
{
    toggleActiveBody();
    return true;
}

// TODO To be deleted (2015-09-08, Fat-Zer)
// void ViewProviderBody::updateTree()
//{
//    if (ActiveGuiDoc == NULL) return;
//
//    // Highlight active body and all its features
//    //Base::Console().error("ViewProviderBody::updateTree()\n");
//    PartDesign::Body* body = getObject<PartDesign::Body>();
//    bool active = body->IsActive.getValue();
//    //Base::Console().error("Body is %s\n", active ? "active" : "inactive");
//    ActiveGuiDoc->signalHighlightObject(*this, Gui::Blue, active);
//    std::vector<App::DocumentObject*> features = body->getFullModel();
//    bool highlight = true;
//    App::DocumentObject* tip = body->Tip.getValue();
//    for (std::vector<App::DocumentObject*>::const_iterator f = features.begin(); f !=
//    features.end(); f++) {
//        //Base::Console().error("Highlighting %s: %s\n", (*f)->getNameInDocument(), highlight ?
//        "true" : "false"); Gui::ViewProviderDocumentObject* vp =
//        dynamic_cast<Gui::ViewProviderDocumentObject*>(Gui::Application::Instance->getViewProvider(*f));
//        if (vp != NULL)
//            ActiveGuiDoc->signalHighlightObject(*vp, Gui::LightBlue, active ? highlight : false);
//        if (highlight && (tip == *f))
//            highlight = false;
//    }
//}

bool ViewProviderBody::onDelete(const std::vector<std::string>&)
{
    // TODO May be do it conditionally? (2015-09-05, Fat-Zer)
    FCMD_OBJ_CMD(getObject(), "removeObjectsFromDocument()");
    return true;
}

void ViewProviderBody::updateData(const App::Property* prop)
{
    PartDesign::Body* body = getObject<PartDesign::Body>();

    if (prop == &body->BaseFeature || prop == &body->Tip) {
        // ensure all model features are in visual body mode. Membership now changes via the
        // BaseFeature chain / Tip, not a Group edit (Cruth §11 step 5e).
        setVisualBodyMode(true);
    }

    if (prop == &body->Tip) {
        // We changed Tip
        App::DocumentObject* tip = body->Tip.getValue();

        auto features = body->getFullModel();

        // restore icons
        for (auto feature : features) {
            Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(feature);
            if (vp && vp->isDerivedFrom<PartDesignGui::ViewProvider>()) {
                static_cast<PartDesignGui::ViewProvider*>(vp)->setTipIcon(feature == tip);
            }
        }
    }

    if (prop == &body->Tip || prop == &body->TipComponentId) {
        applyMultiOutputDisplay();
    }

    PartGui::ViewProviderPart::updateData(prop);
}

void ViewProviderBody::finishRestoring()
{
    PartGui::ViewProviderPart::finishRestoring();
    // Re-open of a multi-output document: TipComponentId is loaded but the feature-
    // hide step is skipped during restore, so apply it once restore has settled.
    applyMultiOutputDisplay();
}

namespace
{
// A multi-output Body IS the representation of its Tip, so no feature feeding that Tip
// may draw independently. Walk the Tip's dependency chain (backward via getOutList) and
// hide every body-content feature in it — solid features (the shared pattern and its
// upstream Pad/base features) and their profile sketches — so only the Bodies' own
// component shapes show. Datums/origins are left alone: they are reference geometry the
// user may want visible. Without this, e.g. the base Pad of a multi-output pattern keeps
// drawing at instance 0's location, leaving a phantom solid with no Body (Cruth: a Body
// is the representation of its Tip, §4.6/§5.5).
void hideTipChain(App::DocumentObject* tip)
{
    if (!tip) {
        return;
    }
    const Base::Type sketchType = Base::Type::fromName("Sketcher::SketchObject");
    std::set<App::DocumentObject*> seen;
    std::vector<App::DocumentObject*> stack {tip};
    while (!stack.empty()) {
        App::DocumentObject* obj = stack.back();
        stack.pop_back();
        if (!obj || !seen.insert(obj).second) {
            continue;
        }
        const bool isSketch = !sketchType.isBad() && obj->isDerivedFrom(sketchType);
        if (PartDesign::Body::isSolidFeature(obj) || isSketch) {
            if (Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(obj)) {
                vp->setVisible(false);
            }
        }
        for (App::DocumentObject* dep : obj->getOutList()) {
            stack.push_back(dep);
        }
    }
}
}  // namespace

void ViewProviderBody::applyMultiOutputDisplay()
{
    PartDesign::Body* body = getObject<PartDesign::Body>();
    if (!body) {
        return;
    }

    if (!body->TipComponentId.getStrValue().empty()) {
        // Multi-output: force "Tip" mode so this Body draws its own component shape,
        // and hide the shared feature so its full multi-solid shape does not also draw.
        if (DisplayModeBody.getValue() != 1) {
            DisplayModeBody.setValue(static_cast<long>(1));
        }
        if (!isRestoring()) {
            hideTipChain(body->Tip.getValue());
        }
        return;
    }

    // Collapsed back to a single component: undo only what the multi-output path forced.
    // An ordinary single-component Body is already in "Through" mode (0), so guarding on
    // mode == 1 keeps this a no-op for it and avoids clobbering normal display.
    if (DisplayModeBody.getValue() == 1) {
        DisplayModeBody.setValue(static_cast<long>(0));  // back to "Through"
        if (!isRestoring()) {
            if (App::DocumentObject* tip = body->Tip.getValue()) {
                if (Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(tip)) {
                    vp->setVisible(true);  // re-show the previously hidden shared feature
                }
            }
        }
    }
}

void ViewProviderBody::onChanged(const App::Property* prop)
{

    if (prop == &DisplayModeBody) {
        auto body = getObject<PartDesign::Body>();

        if (DisplayModeBody.getValue() == 0) {
            // if we are in an override mode we need to make sure to come out, because
            // otherwise the maskmode is blocked and won't go into "through"
            if (getOverrideMode() != "As Is") {
                auto mode = getOverrideMode();
                ViewProvider::setOverrideMode("As Is");
                overrideMode = mode;
            }
            setDisplayMaskMode("Group");
            if (body) {
                body->setShowTip(false);
            }
        }
        else {
            if (body) {
                body->setShowTip(true);
            }
            if (getOverrideMode() == "As Is") {
                setDisplayMaskMode(DisplayMode.getValueAsString());
            }
            else {
                Base::Console().message("Set override mode: %s\n", getOverrideMode().c_str());
                setDisplayMaskMode(getOverrideMode().c_str());
            }
        }

        // #0002559: Body becomes visible upon changing DisplayModeBody
        Visibility.touch();
    }
    else {
        unifyVisualProperty(prop);
    }

    // When changing transparency then adjust the ShapeAppearance inside onChanged()
    // of the base class but don't notify its container again. This breaks the chain of
    // notification and avoids the call of onChanged() with the ShapeAppearance as argument
    // This fixes issue https://github.com/FreeCAD/FreeCAD/issues/18075
    if (prop == &Transparency) {
        ShapeAppearance.enableNotify(false);
    }

    PartGui::ViewProviderPartExt::onChanged(prop);

    if (prop == &Transparency) {
        ShapeAppearance.enableNotify(true);
    }
}

void ViewProviderBody::unifyVisualProperty(const App::Property* prop)
{

    if (!pcObject || isRestoring()) {
        return;
    }

    if (prop == &Visibility || prop == &Selectable || prop == &DisplayModeBody
        || prop == &PointColorArray || prop == &ShowPlacement || prop == &LineColorArray) {
        return;
    }

    // Fixes issue 11197. In case of affected projects where the bounding box of a sub-feature
    // is shown allow it to hide it
    if (prop == &BoundingBox) {
        if (BoundingBox.getValue()) {
            return;
        }
    }

    Gui::Document* gdoc = Gui::Application::Instance->getDocument(pcObject->getDocument());

    PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
    auto features = body->getFullModel();
    for (auto feature : features) {

        if (!feature->isDerivedFrom<PartDesign::Feature>()) {
            continue;
        }

        // copy over the properties data
        if (Gui::ViewProvider* vp = gdoc->getViewProvider(feature)) {
            if (auto fprop = vp->getPropertyByName(prop->getName())) {
                fprop->Paste(*prop);
            }
        }
    }
}

std::map<std::string, Base::Color> ViewProviderBody::getElementColors(const char* element) const
{
    // A PartDesign Body doesn't really have element colors on its own: it's a sort of container,
    // and its subshapes are the ones that have actual colors. If you query a body's ViewProvider
    // for its element colors, what you are really asking for is the element colors of its tip.
    PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
    if (App::DocumentObject* tip = body->Tip.getValue()) {
        Gui::Document* guiDoc = Gui::Application::Instance->getDocument(tip->getDocument());
        Gui::ViewProvider* vp = guiDoc->getViewProvider(tip);
        return vp->getElementColors(element);
    }
    return ViewProviderPart::getElementColors(element);
}


std::vector<App::DocumentObject*> ViewProviderBody::pipelineChain() const
{
    // Walk the BaseFeature chain backward from the Tip (tip-first), guarding
    // against cycles, then reverse to base -> tip pipeline order. This is the
    // source-of-truth flip: the chain *is* the pipeline (ARCHITECTURE.md §3.3),
    // independent of Group membership.
    std::vector<App::DocumentObject*> chain;  // tip -> base
    std::set<App::DocumentObject*> onChain;
    auto* body = getObject<PartDesign::Body>();
    for (App::DocumentObject* feat = body ? body->Tip.getValue() : nullptr; feat;) {
        if (!onChain.insert(feat).second) {
            break;  // cycle guard
        }
        chain.push_back(feat);
        auto* pdFeat = freecad_cast<PartDesign::Feature*>(feat);
        feat = pdFeat ? pdFeat->BaseFeature.getValue() : nullptr;
    }
    std::reverse(chain.begin(), chain.end());  // base -> tip (pipeline order)
    return chain;
}

std::vector<App::DocumentObject*> ViewProviderBody::claimChildren() const
{
    auto* body = getObject<PartDesign::Body>();
    if (!body) {
        // Degenerate case (no Body object): nothing to claim.
        return {};
    }

    // 1. Derive the ordered solid pipeline from the BaseFeature chain.
    std::vector<App::DocumentObject*> chain = pipelineChain();  // base -> tip
    std::set<App::DocumentObject*> onChain(chain.begin(), chain.end());

    // 2. Collect objects claimed by features (so profiles/sketches nest under
    //    their feature instead of appearing at body level). Both the chain
    //    features and any remaining Group members are potential claimers.
    const std::vector<App::DocumentObject*> groupMembers = body->getFullModel();
    std::set<App::DocumentObject*> claimed;
    auto collectClaimed = [&](App::DocumentObject* obj) {
        if (!obj) {
            return;
        }
        Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider(obj);
        if (!vp || vp == this) {
            return;
        }
        for (auto* child : vp->claimChildren()) {
            if (child) {
                claimed.insert(child);
            }
        }
    };
    for (auto* obj : chain) {
        collectClaimed(obj);
    }
    for (auto* obj : groupMembers) {
        collectClaimed(obj);
    }

    // 3. Assemble the result: pipeline first in chain order, then auxiliary
    //    Group members that are not on the chain (Origin is handled separately,
    //    below; datums and unconsumed sketches land here). Skip anything nested
    //    under a feature.
    std::vector<App::DocumentObject*> result;
    std::set<App::DocumentObject*> emitted;
    auto emit = [&](App::DocumentObject* obj) {
        if (!obj || !obj->isAttachedToDocument() || claimed.contains(obj)) {
            return;
        }
        if (emitted.insert(obj).second) {
            result.push_back(obj);
        }
    };
    for (auto* obj : chain) {
        emit(obj);
    }
    for (auto* obj : groupMembers) {
        if (!onChain.contains(obj)) {
            emit(obj);
        }
    }

    // 4. The world frame is NOT claimed here. It is owned by the document, shared by
    //    every body, so claiming it would both hide it from the document root and make
    //    it a child of every body at once. It belongs at root, listed once.
    return result;
}


std::vector<App::DocumentObject*> ViewProviderBody::claimChildren3D() const
{
    auto* body = getObject<PartDesign::Body>();
    if (!body) {
        // Degenerate case (no Body object): nothing to claim.
        return {};
    }

    // A Body parents its SOLIDS in 3D, and nothing else.
    //
    // The solid pipeline is what the Body's shape IS, so nesting those nodes is honest:
    // it is what makes a clicked face arrive as "Body.Pad.Face6", the selection path the
    // App layer resolves and the one we want.
    //
    // The loose members (profile sketches, datums, shapebinders — Body::getFullModel's
    // second group) are INPUTS the Body references, never parts of its shape: P1 (inputs
    // are referenced, not consumed, not owned) and P3 (references, not ownership). Parenting
    // them here issued an address the model never agreed to honour — a click on a visible
    // sketch edge resolved to "Body.Prof.Edge1", which Body::getSubObject cannot answer,
    // so the pick failed even though the element existed and the click was correct. Their
    // address is simply themselves, and that resolves.
    //
    // This is the single-consumer half of the rule Gui::Document already enforces centrally
    // for shared inputs (contestedChildren3D): an input claimed by two consumers sits at the
    // scene root. One rule instead of two — an input is never parented by a consumer, whether
    // it has one or several. Note this is the SCENE half only: the tree (claimChildren) still
    // lists loose members under the Body, which is a display default, not a model fact.
    std::vector<App::DocumentObject*> result;
    std::set<App::DocumentObject*> seen;
    for (auto* feat : pipelineChain()) {
        if (feat && feat->isAttachedToDocument() && seen.insert(feat).second) {
            result.push_back(feat);
        }
    }

    // The world frame is not parented here either: it is document-owned and already sits
    // at the scene-graph root, at identity. Parenting it under each body would give the
    // same nodes several parents for no gain.
    return result;
}


void ViewProviderBody::setVisualBodyMode(bool bodymode)
{

    Gui::Document* gdoc = Gui::Application::Instance->getDocument(pcObject->getDocument());

    PartDesign::Body* body = static_cast<PartDesign::Body*>(getObject());
    auto features = body->getFullModel();
    for (auto feature : features) {

        if (!feature->isDerivedFrom<PartDesign::Feature>()) {
            continue;
        }

        auto* vp = static_cast<PartDesignGui::ViewProvider*>(gdoc->getViewProvider(feature));
        if (vp) {
            vp->setBodyMode(bodymode);
        }
    }
}

std::vector<std::string> ViewProviderBody::getDisplayModes() const
{

    // The user-facing display modes are the inherited Part modes only. "Through"/"Tip" are
    // driven by the DisplayModeBody enum, not this list. The retired OriginGroup extension
    // used to inject a leading "Group" mode that we erased here; with the extension gone it is
    // no longer present, so nothing needs removing.
    return ViewProviderPart::getDisplayModes();
}

PartDesign::Feature* ViewProviderBody::getShownFeature() const
{
    auto body = static_cast<PartDesign::Body*>(getObject());
    auto features = body->getFullModel();

    for (auto feature : features) {
        if (!feature->isDerivedFrom<PartDesign::Feature>()) {
            continue;
        }

        if (feature->Visibility.getValue()) {
            return static_cast<PartDesign::Feature*>(feature);
        }
    }

    return nullptr;
}

Gui::ViewProvider* ViewProviderBody::getShownViewProvider() const
{
    if (const auto* feature = getShownFeature()) {
        return Gui::Application::Instance->getViewProvider(feature);
    }

    return nullptr;
}

bool ViewProviderBody::canDropObjects() const
{
    // if the BaseFeature property is marked as hidden or read-only then
    // it's not allowed to modify it.
    auto* body = getObject<PartDesign::Body>();
    if (body->BaseFeature.testStatus(App::Property::Status::Hidden)
        || body->BaseFeature.testStatus(App::Property::Status::ReadOnly)) {
        return false;
    }
    return true;
}

bool ViewProviderBody::canDropObject(App::DocumentObject* obj) const
{
    if (obj->isDerivedFrom<App::VarSet>()) {
        return true;
    }
    else if (obj->isDerivedFrom<App::DatumElement>()) {
        // accept only datums that are not part of a LCS.
        auto* lcs = static_cast<App::DatumElement*>(obj)->getLCS();
        return !lcs;
    }
    else if (obj->isDerivedFrom<App::LocalCoordinateSystem>()) {
        return !obj->isDerivedFrom<App::Origin>();
    }
    else if (obj->isDerivedFrom<PartDesign::SubShapeBinder>()) {
        return true;
    }
    else if (obj->isDerivedFrom<Part::Part2DObject>()) {
        return true;
    }
    else if (!obj->isDerivedFrom<Part::ShapeFeature>()) {
        return false;
    }
    else if (PartDesign::Body::inAnyBody(obj)) {
        return false;
    }
    else if (obj->isDerivedFrom(Part::BodyBase::getClassTypeId())) {
        return false;
    }

    return true;
}

void ViewProviderBody::dropObject(App::DocumentObject* obj)
{
    auto* body = getObject<PartDesign::Body>();
    if (obj->isDerivedFrom<Part::Part2DObject>() || obj->isDerivedFrom<App::DatumElement>()
        || obj->isDerivedFrom<App::LocalCoordinateSystem>()) {
        body->addFeature(obj);
    }
    else if (PartDesign::Body::isAllowed(obj) && PartDesignGui::isFeatureMovable(obj)) {
        std::vector<App::DocumentObject*> move;
        move.push_back(obj);
        std::vector<App::DocumentObject*> deps = PartDesignGui::collectMovableDependencies(move);
        move.insert(std::end(move), std::begin(deps), std::end(deps));

        // The body we are moving these features OUT of, so we detach them (heal its
        // chain, retreat its Tip, retire it if it empties) before re-homing into the
        // drop target. This legitimately wants the body object — it is a real body
        // operation, and it matches the source lookup in Body::moveFeatureToBody.
        // KNOWN GAP (blocked-by #33/#34): isFeatureMovable only permits dragging a
        // chain-base feature, but a MULTI-OUTPUT base backs N bodies; first-match
        // detaches from only one. Left first-match until the "act on all N bodies a
        // feature backs" semantics are settled — same first-match as today, no regression.
        PartDesign::Body* source = PartDesign::Body::findBodyOf(obj);
        if (source) {
            source->removeFeatures(move);
        }
        try {
            body->addFeatures(move);
        }
        catch (const Base::Exception& e) {
            e.reportException();
        }
    }
    else if (!body->BaseFeature.getValue()) {
        body->BaseFeature.setValue(obj);
    }

    App::Document* doc = body->getDocument();
    doc->recompute();

    // check if a proxy object has been created for the base feature
    std::vector<App::DocumentObject*> links = body->getFullModel();
    for (auto it : links) {
        if (it->isDerivedFrom<PartDesign::FeatureBase>()) {
            PartDesign::FeatureBase* base = static_cast<PartDesign::FeatureBase*>(it);
            if (base && base->BaseFeature.getValue() == obj) {
                Gui::Application::Instance->hideViewProvider(obj);
                break;
            }
        }
    }
}

bool ViewProviderBody::canDragObjectToTarget(App::DocumentObject* obj, App::DocumentObject* target) const
{
    if (obj->isDerivedFrom<PartDesign::Feature>()) {
        return target && target->is<PartDesign::Body>();
    }

    return ViewProviderPart::canDragObjectToTarget(obj, target);
}

void ViewProviderBody::show()
{
    // Call the base version first to ensure normal behavior
    PartGui::ViewProviderPart::show();

    auto* body = static_cast<PartDesign::Body*>(getObject());

    auto tip = body->Tip.getValue();
    if (!tip || tip->Visibility.getValue()) {
        return;
    }

    auto features = body->getFullModel();
    if (features.empty()) {
        return;
    }

    bool foundVisible = false;
    for (const auto feature : features) {
        if (!feature) {
            continue;
        }

        auto vp = Gui::Application::Instance->getViewProvider(feature);
        if (!vp) {
            continue;
        }

        if (vp->isDerivedFrom(PartDesignGui::ViewProvider::getClassTypeId())) {
            if (feature->Visibility.getValue()) {
                foundVisible = true;
                break;
            }
        }
    }

    if (!foundVisible) {
        tip->Visibility.setValue(true);
    }
}
