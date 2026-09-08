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

#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>


namespace Assembly
{

class AssemblyObject;
class Simulation;

/**
 * A driver applied to one joint for the duration of a simulation: a formula
 * giving that joint's angle or displacement as a function of time.
 *
 * This is the typed replacement for the former Python `Motion` proxy. The solver
 * already read its three properties by name and cast blindly, and a motion could
 * only find the simulation it belonged to by asking every object above it whether
 * its Python proxy happened to own a method called "setMotionsChangedCallback" --
 * a duck-type that would have matched anything.
 */
class AssemblyExport Motion: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::Motion);

public:
    Motion();
    ~Motion() override;

    /// The joint this motion drives.
    App::PropertyXLinkSubHidden Joint;
    /// The motion itself as a function of time, for example "1.0*time".
    App::PropertyString Formula;
    /// "Angular" (a rotation about the joint axis) or "Linear" (a displacement along it).
    App::PropertyEnumeration MotionType;

    App::DocumentObjectExecReturn* execute() override;

    /// The joint named by Joint, resolved. Nullptr if the reference is broken.
    App::DocumentObject* getJoint() const;

    /// True when this motion rotates rather than translates.
    bool isAngular() const;

    /// The simulation this motion belongs to, or nullptr if it belongs to none.
    Simulation* getSimulation() const;

    /// The assembly this motion belongs to, or nullptr if it belongs to none.
    AssemblyObject* getAssembly() const;

    PyObject* getPyObject() override;

    /// Hosts the (still-Python) ViewProviderMotion over the FeaturePython view
    /// provider shell, as Joint does. Ported to C++ with the view provider slice.
    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderFeaturePython";
    }

private:
    static const char* MotionTypeEnums[];
};


}  // namespace Assembly
