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

#include <Mod/Assembly/App/Motion.h>

#include "ViewProviderMotion.h"

using namespace AssemblyGui;

PROPERTY_SOURCE(AssemblyGui::ViewProviderMotion, Gui::ViewProviderDocumentObject)

ViewProviderMotion::ViewProviderMotion() = default;

ViewProviderMotion::~ViewProviderMotion() = default;

QIcon ViewProviderMotion::getIcon() const
{
    auto* motion = dynamic_cast<Assembly::Motion*>(getObject());
    if (motion && motion->isAngular()) {
        return Gui::BitmapFactory().pixmap("button_rotate.svg");
    }

    return Gui::BitmapFactory().pixmap("button_right.svg");
}

void ViewProviderMotion::updateData(const App::Property* prop)
{
    auto* motion = dynamic_cast<Assembly::Motion*>(getObject());
    if (motion && prop == &motion->MotionType) {
        // A turning motion and a sliding one do not look alike in the tree.
        signalChangeIcon();
    }

    Gui::ViewProviderDocumentObject::updateData(prop);
}

bool ViewProviderMotion::doubleClicked()
{
    auto* motion = dynamic_cast<Assembly::Motion*>(getObject());
    if (!motion || !motion->getAssembly()) {
        // A motion that belongs to no assembly has nothing to drive.
        return false;
    }

    std::string objName = motion->getNameInDocument();
    std::string docName = motion->getDocument()->getName();

    std::string cmd = "import CommandCreateSimulation\n"
                      "CommandCreateSimulation.editMotion(App.getDocument('"
        + docName + "').getObject('" + objName + "'))";

    Gui::Command::runCommand(Gui::Command::Gui, cmd.c_str());

    return true;
}
