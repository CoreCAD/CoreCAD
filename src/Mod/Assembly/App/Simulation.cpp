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

#include "PreCompiled.h"

#include <App/Document.h>

#include "AssemblyUtils.h"
#include "Motion.h"
#include "Simulation.h"
#include "SimulationPy.h"

using namespace Assembly;

PROPERTY_SOURCE_WITH_EXTENSIONS(Assembly::Simulation, App::DocumentObject)

Simulation::Simulation()
{
    ADD_PROPERTY_TYPE(TimeStart, (0.0), "Simulation", App::Prop_None, "Simulation start time.");
    ADD_PROPERTY_TYPE(TimeEnd, (1.0), "Simulation", App::Prop_None, "Simulation end time.");
    ADD_PROPERTY_TYPE(
        TimeStepOutput,
        (1.0e-2),
        "Simulation",
        App::Prop_None,
        "Simulation time step for output."
    );
    ADD_PROPERTY_TYPE(
        GlobalErrorTolerance,
        (1.0e-6),
        "Simulation",
        App::Prop_None,
        "Integration global error tolerance."
    );
    ADD_PROPERTY_TYPE(
        FramesPerSecond,
        (30),
        "Simulation",
        App::Prop_None,
        "Frames per second of the animation playback."
    );

    App::GroupExtension::initExtension(this);
}

Simulation::~Simulation() = default;

App::DocumentObjectExecReturn* Simulation::execute()
{
    // A simulation holds settings and motions; the frames are computed on demand by
    // the assembly's solver and never stored. Nothing to recompute.
    return App::DocumentObject::StdReturn;
}

AssemblyObject* Simulation::getAssembly() const
{
    return getOwningAssembly(this);
}

std::vector<Motion*> Simulation::getMotions() const
{
    std::vector<Motion*> motions;

    for (auto* obj : Group.getValue()) {
        if (auto* motion = freecad_cast<Motion*>(obj)) {
            motions.push_back(motion);
        }
    }

    return motions;
}

PyObject* Simulation::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new SimulationPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
