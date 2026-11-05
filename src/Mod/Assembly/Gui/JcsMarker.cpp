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

#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>

#include <App/Application.h>
#include <Base/Color.h>
#include <Base/Parameter.h>
#include <Base/Rotation.h>

#include <Gui/Inventor/SoAxisCrossKit.h>

#include "JcsMarker.h"

using namespace AssemblyGui;

namespace
{
/// How far the marker is blown up relative to its unit-sized geometry.
constexpr float MarkerScaleFactor = 40.0F;
constexpr float AxisThickness = 3.0F;
/// Radius of the joint-plane disc, and how many segments approximate it.
constexpr double PlaneRadius = 0.4;
constexpr int PlaneSegments = 15;

Base::Color axisColor(const char* name, uint32_t fallback)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View"
    );

    Base::Color color;
    color.setPackedValue(hGrp->GetUnsigned(name, fallback));
    return color;
}

/// One axis stub, scaled to stay the same size on screen.
Gui::SoShapeScale* axisNode(
    const SbVec3f& from,
    const SbVec3f& to,
    const Base::Color& color,
    SoDrawStyle* style
)
{
    auto* coords = new SoCoordinate3();
    coords->point.set1Value(0, from);
    coords->point.set1Value(1, to);

    auto* line = new SoLineSet();
    line->numVertices.setValue(2);

    auto* baseColor = new SoBaseColor();
    baseColor->rgb.setValue(color.r, color.g, color.b);

    auto* axis = new SoAnnotation();
    axis->addChild(style);
    axis->addChild(baseColor);
    axis->addChild(coords);
    axis->addChild(line);

    auto* scale = new Gui::SoShapeScale();
    scale->setPart("shape", axis);
    scale->scaleFactor = MarkerScaleFactor;

    return scale;
}

/// The translucent disc standing for the joint plane.
Gui::SoShapeScale* planeNode()
{
    auto* coords = new SoCoordinate3();
    for (int i = 0; i < PlaneSegments; ++i) {
        double angle = static_cast<double>(i) / PlaneSegments * 2.0 * std::numbers::pi;
        coords->point.set1Value(
            i,
            static_cast<float>(std::cos(angle) * PlaneRadius),
            static_cast<float>(std::sin(angle) * PlaneRadius),
            0.0F
        );
    }

    auto* face = new SoFaceSet();
    face->numVertices.setValue(PlaneSegments);

    auto* style = new SoDrawStyle();
    style->style = SoDrawStyle::FILLED;

    auto* material = new SoMaterial();
    material->diffuseColor.setValue(0.5F, 0.5F, 0.5F);
    material->ambientColor.setValue(0.5F, 0.5F, 0.5F);
    material->specularColor.setValue(0.5F, 0.5F, 0.5F);
    material->emissiveColor.setValue(0.5F, 0.5F, 0.5F);
    material->transparency.setValue(0.3F);

    auto* plane = new SoAnnotation();
    plane->addChild(style);
    plane->addChild(material);
    plane->addChild(coords);
    plane->addChild(face);

    auto* scale = new Gui::SoShapeScale();
    scale->setPart("shape", plane);
    scale->scaleFactor = MarkerScaleFactor;

    return scale;
}
}  // namespace

JcsMarker::JcsMarker()
{
    root = new SoSwitch();
    root->ref();
    root->whichChild = SO_SWITCH_NONE;

    transform = new SoTransform();
    pick = new SoPickStyle();
    setPickable(true);

    auto* lineStyle = new SoDrawStyle();
    lineStyle->style = SoDrawStyle::LINES;
    lineStyle->lineWidth = AxisThickness;

    auto* jcs = new SoAnnotation();
    jcs->addChild(transform);
    jcs->addChild(pick);
    jcs->addChild(planeNode());
    // X and Y are drawn as outer stubs and Z whole, so the frame reads as a plane
    // with a normal rather than as three equal axes.
    jcs->addChild(axisNode({0.5F, 0, 0}, {1, 0, 0}, axisColor("AxisXColor", 0xCC333300), lineStyle));
    jcs->addChild(axisNode({0, 0.5F, 0}, {0, 1, 0}, axisColor("AxisYColor", 0x33CC3300), lineStyle));
    jcs->addChild(axisNode({0, 0, 0}, {0, 0, 1}, axisColor("AxisZColor", 0x3333CC00), lineStyle));

    root->addChild(jcs);
}

JcsMarker::~JcsMarker()
{
    root->unref();
}

void JcsMarker::show(const Base::Placement& worldPlacement)
{
    const Base::Vector3d& pos = worldPlacement.getPosition();
    transform->translation
        .setValue(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));

    double q0 {};
    double q1 {};
    double q2 {};
    double q3 {};
    worldPlacement.getRotation().getValue(q0, q1, q2, q3);
    transform->rotation.setValue(
        static_cast<float>(q0),
        static_cast<float>(q1),
        static_cast<float>(q2),
        static_cast<float>(q3)
    );

    root->whichChild = SO_SWITCH_ALL;
}

void JcsMarker::hide()
{
    root->whichChild = SO_SWITCH_NONE;
}

void JcsMarker::setPickable(bool pickable)
{
    pick->style.setValue(pickable ? SoPickStyle::SHAPE_ON_TOP : SoPickStyle::UNPICKABLE);
}
