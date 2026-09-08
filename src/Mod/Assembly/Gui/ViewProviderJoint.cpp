// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   Cruth is free software: you can redistribute it and/or modify it       *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Cruth is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with Cruth. If not, see                                  *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <array>

#include <Inventor/nodes/SoSwitch.h>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/PropertyLinks.h>

#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>

#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/AssemblyUtils.h>
#include <Mod/Assembly/App/Joint.h>

#include "JcsMarker.h"
#include "ViewProviderJoint.h"
#include "ViewProviderJointPy.h"

using namespace AssemblyGui;

namespace
{
/// The icon of each joint kind, in the order Joint::JointTypeEnums declares them.
constexpr std::array<const char*, 13> JointIcons = {
    "Assembly_CreateJointFixed.svg",
    "Assembly_CreateJointRevolute.svg",
    "Assembly_CreateJointCylindrical.svg",
    "Assembly_CreateJointSlider.svg",
    "Assembly_CreateJointBall.svg",
    "Assembly_CreateJointDistance.svg",
    "Assembly_CreateJointParallel.svg",
    "Assembly_CreateJointPerpendicular.svg",
    "Assembly_CreateJointAngle.svg",
    "Assembly_CreateJointRackPinion.svg",
    "Assembly_CreateJointScrew.svg",
    "Assembly_CreateJointGears.svg",
    "Assembly_CreateJointPulleys.svg",
};
}  // namespace

PROPERTY_SOURCE(AssemblyGui::ViewProviderJoint, Gui::ViewProviderDocumentObject)

ViewProviderJoint::ViewProviderJoint()
    : jcs1(std::make_unique<JcsMarker>())
    , jcs2(std::make_unique<JcsMarker>())
    , jcsPreview(std::make_unique<JcsMarker>())
{
    Gui::ViewProviderSuppressibleExtension::initExtension(this);

    pcSelectionRoot = new Gui::SoFCSelection();
    pcSelectionRoot->ref();
    pcSelectionRoot->addChild(jcs1->node());
    pcSelectionRoot->addChild(jcs2->node());
    pcSelectionRoot->addChild(jcsPreview->node());
}

ViewProviderJoint::~ViewProviderJoint()
{
    pcSelectionRoot->unref();
}

void ViewProviderJoint::attach(App::DocumentObject* obj)
{
    Gui::ViewProviderDocumentObject::attach(obj);

    addDisplayMaskMode(pcSelectionRoot, "Wireframe");

    pcSelectionRoot->objectName = obj->getNameInDocument();
    pcSelectionRoot->documentName = obj->getDocument()->getName();
    pcSelectionRoot->subElementName = "Main";

    redrawMarkers();
}

void ViewProviderJoint::setDisplayMode(const char* ModeName)
{
    if (strcmp("Wireframe", ModeName) == 0) {
        setDisplayMaskMode("Wireframe");
    }
    Gui::ViewProviderDocumentObject::setDisplayMode(ModeName);
}

std::vector<std::string> ViewProviderJoint::getDisplayModes() const
{
    return {"Wireframe"};
}

void ViewProviderJoint::updateData(const App::Property* prop)
{
    auto* joint = dynamic_cast<Assembly::Joint*>(getObject());
    if (joint) {
        if (prop == &joint->Placement1) {
            redrawMarker(*jcs1, "Placement1", "Reference1");
        }
        else if (prop == &joint->Placement2) {
            redrawMarker(*jcs2, "Placement2", "Reference2");
        }
        else if (prop == &joint->JointType) {
            // A revolute joint and a slider do not look alike in the tree.
            signalChangeIcon();
        }
    }

    Gui::ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderJoint::redrawMarkers()
{
    redrawMarker(*jcs1, "Placement1", "Reference1");
    redrawMarker(*jcs2, "Placement2", "Reference2");
}

void ViewProviderJoint::redrawMarker(JcsMarker& marker, const char* placementName, const char* referenceName)
{
    auto* joint = getObject();
    if (!joint) {
        return;
    }

    auto* ref = dynamic_cast<App::PropertyXLinkSub*>(joint->getPropertyByName(referenceName));
    if (!Assembly::isRefValid(ref)) {
        // A frame with nothing to sit on is not drawn: it would otherwise be left
        // hanging at the world origin when a reference breaks.
        marker.hide();
        return;
    }

    Base::Placement placement = App::GeoFeature::getPlacementFromProp(joint, placementName);
    marker.show(App::GeoFeature::getGlobalPlacement(nullptr, ref) * placement);
}

void ViewProviderJoint::showPreviewJcs(
    const Base::Placement& placement,
    App::DocumentObject* refObj,
    const std::vector<std::string>& subs
)
{
    if (!refObj || subs.empty()) {
        jcsPreview->hide();
        return;
    }

    Base::Placement global = App::GeoFeature::getGlobalPlacement(nullptr, refObj, subs.front());
    jcsPreview->show(global * placement);
}

void ViewProviderJoint::hidePreviewJcs()
{
    jcsPreview->hide();
}

void ViewProviderJoint::setPickableState(bool state)
{
    jcs1->setPickable(state);
    jcs2->setPickable(state);
    jcsPreview->setPickable(state);
}

QIcon ViewProviderJoint::getIcon() const
{
    auto* joint = dynamic_cast<Assembly::Joint*>(getObject());
    if (joint) {
        long type = joint->JointType.getValue();
        if (type >= 0 && type < static_cast<long>(JointIcons.size())) {
            return Gui::BitmapFactory().pixmap(JointIcons.at(type));
        }
    }

    return Gui::BitmapFactory().pixmap("Assembly_CreateJoint.svg");
}

QIcon ViewProviderJoint::mergeColorfulOverlayIcons(const QIcon& orig) const
{
    QIcon icon = orig;

    auto* joint = getObject();
    auto* assembly = joint ? Assembly::getOwningAssembly(joint) : nullptr;
    if (assembly) {
        auto* part = Assembly::getMovingPartFromRef(joint, "Reference1");
        if (part && !assembly->isPartConnected(part)) {
            // A joint whose part nothing else holds says so on its own icon.
            static const QSize overlaySize {10, 10};
            QPixmap overlay = Gui::BitmapFactory().pixmapFromSvg("Part_Detached", overlaySize);
            if (!overlay.isNull()) {
                icon = Gui::BitmapFactoryInst::mergePixmap(
                    icon,
                    overlay,
                    Gui::BitmapFactoryInst::BottomLeft
                );
            }
        }
    }

    return Gui::ViewProviderDocumentObject::mergeColorfulOverlayIcons(icon);
}

bool ViewProviderJoint::doubleClicked()
{
    auto* joint = getObject();
    if (!joint) {
        return false;
    }

    std::string objName = joint->getNameInDocument();
    std::string docName = joint->getDocument()->getName();

    std::string cmd = "import JointObject\n"
                      "JointObject.editJoint(App.getDocument('"
        + docName + "').getObject('" + objName + "'))";

    Gui::Command::runCommand(Gui::Command::Gui, cmd.c_str());

    return true;
}

PyObject* ViewProviderJoint::getPyObject()
{
    if (!pyViewObject) {
        pyViewObject = new ViewProviderJointPy(this);
    }
    pyViewObject->IncRef();
    return pyViewObject;
}
