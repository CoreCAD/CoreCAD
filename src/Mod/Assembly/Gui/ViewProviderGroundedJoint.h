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

#pragma once

#include <string>
#include <vector>

#include <Mod/Assembly/AssemblyGlobal.h>

#include <Gui/ViewProviderDocumentObject.h>

class SoSeparator;
class SoTransform;

namespace AssemblyGui
{

/**
 * The view provider of an Assembly::GroundedJoint: the padlock drawn on a component
 * that is held in place.
 *
 * The typed replacement for the former Python ViewProviderGroundedJoint proxy. The
 * padlock always faces the camera and keeps its size on screen, as before.
 *
 * It is placed on the component when the view provider is built and when the joint
 * is pointed at a different component. It does not follow a component that moves --
 * the same as the Python version, which watched for a "Placement" property the
 * grounded joint has never had.
 */
class AssemblyGuiExport ViewProviderGroundedJoint: public Gui::ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(AssemblyGui::ViewProviderGroundedJoint);

public:
    ViewProviderGroundedJoint();
    ~ViewProviderGroundedJoint() override;

    void attach(App::DocumentObject* obj) override;
    void setDisplayMode(const char* ModeName) override;
    std::vector<std::string> getDisplayModes() const override;

    void updateData(const App::Property* prop) override;

    QIcon getIcon() const override;

private:
    /// Put the padlock on the component this joint grounds.
    void updateLockPosition();

    SoSeparator* pcLockRoot;
    SoTransform* pcTransform;
};

}  // namespace AssemblyGui
