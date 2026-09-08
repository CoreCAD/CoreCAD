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
#include <App/GroupExtension.h>
#include <App/PropertyUnits.h>
#include <App/PropertyStandard.h>

#include <vector>


namespace Assembly
{

class AssemblyObject;
class Motion;

/**
 * A kinematic simulation of an assembly: the integration settings, plus the
 * motions that drive it.
 *
 * This is the typed replacement for the former Python `Simulation` proxy (an
 * App::FeaturePython carrying its whole meaning in an opaque Proxy). The solver
 * already had to reach into that proxy's properties by name and cast blindly;
 * with a real type it reads them directly, and the document recipe can state
 * what a simulation actually says.
 *
 * The former property names carried a sort-order prefix (aTimeStart, bTimeEnd,
 * cTimeStepOutput, fGlobalErrorTolerance, jFramesPerSecond). Those letters were a
 * property-editor ordering trick leaking into the data model, and they would have
 * been written into every recipe; the typed properties are named for what they
 * mean.
 */
class AssemblyExport Simulation: public App::DocumentObject, public App::GroupExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::Simulation);

public:
    Simulation();
    ~Simulation() override;

    /// Time the simulation starts at.
    App::PropertyTime TimeStart;
    /// Time the simulation ends at.
    App::PropertyTime TimeEnd;
    /// Interval between the frames the solver reports.
    App::PropertyTime TimeStepOutput;
    /// Integration global error tolerance.
    App::PropertyFloat GlobalErrorTolerance;
    /// Playback rate for the animation of the computed frames.
    App::PropertyInteger FramesPerSecond;

    App::DocumentObjectExecReturn* execute() override;

    /// The assembly this simulation belongs to, or nullptr if it belongs to none.
    AssemblyObject* getAssembly() const;

    /// The motions driving this simulation, in order.
    std::vector<Motion*> getMotions() const;

    PyObject* getPyObject() override;

    const char* getViewProviderName() const override
    {
        return "AssemblyGui::ViewProviderSimulation";
    }
};


}  // namespace Assembly
