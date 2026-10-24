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


#include <cmath>
#include <numbers>
#include <vector>

#include <Inventor/VRMLnodes/SoVRMLBillboard.h>
#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTransform.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <Base/Color.h>
#include <Base/Parameter.h>

#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Inventor/SoAxisCrossKit.h>
#include <Gui/ViewProvider.h>

#include <Mod/Assembly/App/GroundedJoint.h>

#include "ViewProviderGroundedJoint.h"

using namespace AssemblyGui;

namespace
{
/// How far the padlock is blown up relative to its unit-sized geometry.
constexpr float LockScaleFactor = 3.0F;

/// The body of the padlock.
SoAnnotation* lockBody()
{
    auto* coords = new SoCoordinate3();
    coords->point.set1Value(0, -5, -4, 0);
    coords->point.set1Value(1, 5, -4, 0);
    coords->point.set1Value(2, 5, 4, 0);
    coords->point.set1Value(3, -5, 4, 0);

    auto* face = new SoFaceSet();
    face->numVertices.setValue(4);

    auto* body = new SoAnnotation();
    body->addChild(coords);
    body->addChild(face);

    return body;
}

/// The shackle: a half ring, drawn as one band from an outer arc back along an
/// inner one.
SoAnnotation* lockShackle()
{
    constexpr double centerY = 4.0;
    constexpr double outerRadius = 4.0;
    constexpr double innerRadius = outerRadius * 0.7;
    constexpr int stepDegrees = 5;

    std::vector<SbVec3f> points;
    for (int angle = 0; angle <= 180; angle += stepDegrees) {
        double rad = angle * std::numbers::pi / 180.0;
        points.emplace_back(
            static_cast<float>(std::cos(rad) * outerRadius),
            static_cast<float>(centerY + std::sin(rad) * outerRadius),
            0.0F
        );
    }
    for (int angle = 181; angle > 0; angle -= stepDegrees) {
        double rad = angle * std::numbers::pi / 180.0;
        points.emplace_back(
            static_cast<float>(std::cos(rad) * innerRadius),
            static_cast<float>(centerY + std::sin(rad) * innerRadius),
            0.0F
        );
    }

    auto* coords = new SoCoordinate3();
    for (std::size_t i = 0; i < points.size(); ++i) {
        coords->point.set1Value(static_cast<int>(i), points[i]);
    }

    auto* hints = new SoShapeHints();
    hints->faceType = SoShapeHints::UNKNOWN_FACE_TYPE;

    auto* face = new SoFaceSet();
    face->numVertices.setValue(static_cast<int>(points.size()));

    auto* shackle = new SoAnnotation();
    shackle->addChild(hints);
    shackle->addChild(coords);
    shackle->addChild(face);

    return shackle;
}
}  // namespace

PROPERTY_SOURCE(AssemblyGui::ViewProviderGroundedJoint, Gui::ViewProviderDocumentObject)

ViewProviderGroundedJoint::ViewProviderGroundedJoint()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Assembly"
    );
    Base::Color lockColor;
    lockColor.setPackedValue(hGrp->GetUnsigned("AssemblyConstraints", 0xCC333300));

    auto* color = new SoBaseColor();
    color->rgb.setValue(lockColor.r, lockColor.g, lockColor.b);

    auto* lockpad = new SoSeparator();
    lockpad->addChild(color);
    lockpad->addChild(lockBody());
    lockpad->addChild(lockShackle());

    // Always facing the camera, and always the same size on screen: the padlock is
    // a label on a component, not a thing in the model.
    auto* billboard = new SoVRMLBillboard();
    billboard->addChild(lockpad);

    auto* scale = new Gui::SoShapeScale();
    scale->setPart("shape", billboard);
    scale->scaleFactor = LockScaleFactor;

    auto* pick = new SoPickStyle();
    pick->style.setValue(SoPickStyle::SHAPE_ON_TOP);

    pcTransform = new SoTransform();

    pcLockRoot = new SoSeparator();
    pcLockRoot->ref();
    pcLockRoot->addChild(pcTransform);
    pcLockRoot->addChild(pick);
    pcLockRoot->addChild(scale);
}

ViewProviderGroundedJoint::~ViewProviderGroundedJoint()
{
    pcLockRoot->unref();
}

void ViewProviderGroundedJoint::attach(App::DocumentObject* obj)
{
    Gui::ViewProviderDocumentObject::attach(obj);

    addDisplayMaskMode(pcLockRoot, "Wireframe");

    updateLockPosition();
}

void ViewProviderGroundedJoint::setDisplayMode(const char* ModeName)
{
    if (strcmp("Wireframe", ModeName) == 0) {
        setDisplayMaskMode("Wireframe");
    }
    Gui::ViewProviderDocumentObject::setDisplayMode(ModeName);
}

std::vector<std::string> ViewProviderGroundedJoint::getDisplayModes() const
{
    return {"Wireframe"};
}

void ViewProviderGroundedJoint::updateData(const App::Property* prop)
{
    auto* joint = dynamic_cast<Assembly::GroundedJoint*>(getObject());
    if (joint && prop == &joint->ObjectToGround) {
        updateLockPosition();
    }

    Gui::ViewProviderDocumentObject::updateData(prop);
}

void ViewProviderGroundedJoint::updateLockPosition()
{
    auto* joint = dynamic_cast<Assembly::GroundedJoint*>(getObject());
    if (!joint) {
        return;
    }

    auto* grounded = joint->ObjectToGround.getValue();
    if (!grounded) {
        return;
    }

    // The padlock sits at the middle of what it holds. Asking the component's own
    // view provider is what puts it in the middle of what is actually drawn, which
    // is what the padlock labels.
    Base::Vector3d position = App::GeoFeature::getGlobalPlacement(grounded).getPosition();
    if (auto* vp = Gui::Application::Instance->getViewProvider(grounded)) {
        Base::BoundBox3d bbox = vp->getBoundingBox();
        if (bbox.IsValid()) {
            position = bbox.GetCenter();
        }
    }

    pcTransform->translation.setValue(
        static_cast<float>(position.x),
        static_cast<float>(position.y),
        static_cast<float>(position.z)
    );
}

QIcon ViewProviderGroundedJoint::getIcon() const
{
    return Gui::BitmapFactory().pixmap("Assembly_ToggleGrounded.svg");
}
