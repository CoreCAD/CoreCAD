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

#include <Mod/Assembly/AssemblyGlobal.h>

#include <Gui/ViewProviderDocumentObject.h>


namespace AssemblyGui
{

/**
 * The view provider of an Assembly::Motion.
 *
 * The typed replacement for the former Python ViewProviderMotion proxy. Its icon
 * says whether the motion turns something or slides it, so it follows MotionType:
 * the Python version only picked the icon when the tree happened to ask again.
 *
 * Editing opens the (Python) motion dialog, which is also what the simulation
 * panel opens when a motion in its list is double-clicked -- one entry point, not
 * a method reached through a view provider's proxy.
 */
class AssemblyGuiExport ViewProviderMotion: public Gui::ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(AssemblyGui::ViewProviderMotion);

public:
    ViewProviderMotion();
    ~ViewProviderMotion() override;

    QIcon getIcon() const override;

    bool doubleClicked() override;

    void updateData(const App::Property* prop) override;
};

}  // namespace AssemblyGui
