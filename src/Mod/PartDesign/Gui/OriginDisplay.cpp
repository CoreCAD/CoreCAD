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

#include <App/Origin.h>
#include <Gui/Application.h>
#include <Mod/PartDesign/App/Body.h>

#include "OriginDisplay.h"

using namespace PartDesignGui;

static Gui::ViewProviderCoordinateSystem* viewOf(App::DocumentObject* origin)
{
    return dynamic_cast<Gui::ViewProviderCoordinateSystem*>(
        Gui::Application::Instance->getViewProvider(origin)
    );
}

OriginDisplay::~OriginDisplay()
{
    hide();
}

void OriginDisplay::show(const App::DocumentObject* feature, Gui::DatumElements elements)
{
    App::Origin* origin = feature ? PartDesign::Body::findDocumentOrigin(feature->getDocument())
                                  : nullptr;
    if (auto* view = origin ? viewOf(origin) : nullptr) {
        view->setTemporaryVisibility(elements);
        shown = App::DocumentObjectT(origin);
    }
}

void OriginDisplay::hide()
{
    App::DocumentObject* origin = shown.getObject();
    shown = App::DocumentObjectT();
    if (auto* view = origin ? viewOf(origin) : nullptr) {
        view->resetTemporaryVisibility();
    }
}
