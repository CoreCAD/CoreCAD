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

#include <Base/Placement.h>

class SoSwitch;
class SoTransform;
class SoPickStyle;

namespace AssemblyGui
{

/**
 * The little coordinate frame drawn where a joint meets a component: a translucent
 * disc for the joint plane and three coloured axis stubs.
 *
 * Ported from the Python SoSwitchMarker, which was a pivy subclass of SoSwitch. It
 * needs to be no such thing: it owns a switch, it is not one. The axis colours come
 * from the same View preferences the rest of the application uses for X, Y and Z.
 */
class AssemblyGuiExport JcsMarker
{
public:
    JcsMarker();
    ~JcsMarker();

    JcsMarker(const JcsMarker&) = delete;
    JcsMarker& operator=(const JcsMarker&) = delete;

    /// The node to hang under a view provider's display mode.
    SoSwitch* node() const
    {
        return root;
    }

    /// Show the marker at a world placement, or hide it.
    void show(const Base::Placement& worldPlacement);
    void hide();

    /// Whether the marker can be picked in the 3D view.
    void setPickable(bool pickable);

private:
    SoSwitch* root;
    SoTransform* transform;
    SoPickStyle* pick;
};

}  // namespace AssemblyGui
