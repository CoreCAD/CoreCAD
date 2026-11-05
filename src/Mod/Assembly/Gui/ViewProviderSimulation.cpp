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


#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/ViewProviderGroupExtension.h>

#include <Mod/Assembly/App/Simulation.h>

#include "ViewProviderSimulation.h"

using namespace AssemblyGui;

PROPERTY_SOURCE(AssemblyGui::ViewProviderSimulation, Gui::ViewProviderDocumentObject)

ViewProviderSimulation::ViewProviderSimulation()
{
    // The children of a simulation are the motions driving it; the group extension
    // claims them and deletes them with it.
    Gui::ViewProviderGroupExtension::initExtension(this);
}

ViewProviderSimulation::~ViewProviderSimulation() = default;

QIcon ViewProviderSimulation::getIcon() const
{
    return Gui::BitmapFactory().pixmap("Assembly_CreateSimulation.svg");
}

bool ViewProviderSimulation::doubleClicked()
{
    auto* simulation = dynamic_cast<Assembly::Simulation*>(getObject());
    if (!simulation || !simulation->getAssembly()) {
        // A simulation that belongs to no assembly has nothing to run.
        return false;
    }

    std::string objName = simulation->getNameInDocument();
    std::string docName = simulation->getDocument()->getName();

    std::string cmd = "import CommandCreateSimulation\n"
                      "CommandCreateSimulation.editSimulation(App.getDocument('"
        + docName + "').getObject('" + objName + "'))";

    Gui::Command::runCommand(Gui::Command::Gui, cmd.c_str());

    return true;
}
