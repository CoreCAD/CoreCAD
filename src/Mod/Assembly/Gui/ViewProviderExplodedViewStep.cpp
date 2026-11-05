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


#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Color.h>
#include <Base/Parameter.h>

#include <Gui/BitmapFactory.h>

#include "ViewProviderExplodedViewStep.h"
#include "ViewProviderExplodedViewStepPy.h"

using namespace AssemblyGui;

namespace
{
/// Dash pattern of an explosion line: the same 0xF0F0 the Python view provider used.
constexpr unsigned short ExplosionLinePattern = 0xF0F0;

ParameterGrp::handle assemblyPreferences()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Assembly"
    );
}
}  // namespace

PROPERTY_SOURCE(AssemblyGui::ViewProviderExplodedViewStep, Gui::ViewProviderDocumentObject)

ViewProviderExplodedViewStep::ViewProviderExplodedViewStep()
{
    pcSelectionRoot = new Gui::SoFCSelection();
    pcSelectionRoot->ref();

    pcLineGroup = new SoSeparator();
    pcLineGroup->ref();
    pcSelectionRoot->addChild(pcLineGroup);
}

ViewProviderExplodedViewStep::~ViewProviderExplodedViewStep()
{
    pcLineGroup->unref();
    pcSelectionRoot->unref();
}

void ViewProviderExplodedViewStep::attach(App::DocumentObject* obj)
{
    Gui::ViewProviderDocumentObject::attach(obj);

    addDisplayMaskMode(pcSelectionRoot, "Wireframe");

    pcSelectionRoot->objectName = obj->getNameInDocument();
    pcSelectionRoot->documentName = obj->getDocument()->getName();
    pcSelectionRoot->subElementName = "Main";
}

void ViewProviderExplodedViewStep::setDisplayMode(const char* ModeName)
{
    if (strcmp("Wireframe", ModeName) == 0) {
        setDisplayMaskMode("Wireframe");
    }
    Gui::ViewProviderDocumentObject::setDisplayMode(ModeName);
}

std::vector<std::string> ViewProviderExplodedViewStep::getDisplayModes() const
{
    return {"Wireframe"};
}

QIcon ViewProviderExplodedViewStep::getIcon() const
{
    return Gui::BitmapFactory().pixmap("button_add_all.svg");
}

void ViewProviderExplodedViewStep::redrawLines(const std::vector<Assembly::ExplosionLine>& lines)
{
    pcLineGroup->removeAllChildren();

    // Read on every redraw rather than cached at attach: a preference changed mid
    // session then shows on the next move instead of the next document.
    ParameterGrp::handle hGrp = assemblyPreferences();

    auto* drawStyle = new SoDrawStyle();
    drawStyle->style = SoDrawStyle::LINES;
    drawStyle->lineWidth = static_cast<float>(hGrp->GetInt("StepLineThickness", 3));
    drawStyle->linePattern = ExplosionLinePattern;

    Base::Color lineColor;
    lineColor.setPackedValue(hGrp->GetUnsigned("StepLineColor", 0xCC333300));

    auto* baseColor = new SoBaseColor();
    baseColor->rgb.setValue(lineColor.r, lineColor.g, lineColor.b);

    for (const auto& [start, end] : lines) {
        auto* coords = new SoCoordinate3();
        coords->point.set1Value(
            0,
            static_cast<float>(start.x),
            static_cast<float>(start.y),
            static_cast<float>(start.z)
        );
        coords->point.set1Value(
            1,
            static_cast<float>(end.x),
            static_cast<float>(end.y),
            static_cast<float>(end.z)
        );

        auto* lineSet = new SoLineSet();
        lineSet->numVertices.setValue(2);

        auto* lineSep = new SoSeparator();
        lineSep->addChild(drawStyle);
        lineSep->addChild(baseColor);
        lineSep->addChild(coords);
        lineSep->addChild(lineSet);

        pcLineGroup->addChild(lineSep);
    }
}

PyObject* ViewProviderExplodedViewStep::getPyObject()
{
    if (!pyViewObject) {
        pyViewObject = new ViewProviderExplodedViewStepPy(this);
    }
    pyViewObject->IncRef();
    return pyViewObject;
}
