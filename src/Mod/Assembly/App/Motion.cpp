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

#include <cstring>

#include <App/Document.h>

#include "AssemblyUtils.h"
#include "Motion.h"
#include "MotionPy.h"
#include "Simulation.h"

using namespace Assembly;


const char* Motion::MotionTypeEnums[] = {"Angular", "Linear", nullptr};

PROPERTY_SOURCE(Assembly::Motion, App::DocumentObject)

Motion::Motion()
{
    ADD_PROPERTY_TYPE(Joint, (nullptr), "Motion", App::Prop_None, "The joint that is moved by the motion");
    ADD_PROPERTY_TYPE(
        Formula,
        (""),
        "Motion",
        App::Prop_None,
        "This is the formula of the motion. For example '1.0*time'."
    );
    ADD_PROPERTY_TYPE(MotionType, (0L), "Motion", App::Prop_None, "The type of the motion");
    MotionType.setEnums(MotionTypeEnums);
}

Motion::~Motion() = default;

App::DocumentObjectExecReturn* Motion::execute()
{
    // A motion is an input to the solver, not a producer of anything in the document.
    return App::DocumentObject::StdReturn;
}

App::DocumentObject* Motion::getJoint() const
{
    return Joint.getValue();
}

bool Motion::isAngular() const
{
    return std::strcmp(MotionType.getValueAsString(), "Angular") == 0;
}

Simulation* Motion::getSimulation() const
{
    for (auto* obj : getInList()) {
        if (auto* simulation = freecad_cast<Simulation*>(obj)) {
            return simulation;
        }
    }

    return nullptr;
}

AssemblyObject* Motion::getAssembly() const
{
    return getOwningAssembly(this);
}

PyObject* Motion::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new MotionPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
