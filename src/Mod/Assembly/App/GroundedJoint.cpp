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

#include "GroundedJoint.h"
#include "GroundedJointPy.h"

using namespace Assembly;


PROPERTY_SOURCE(Assembly::GroundedJoint, App::DocumentObject)


GroundedJoint::GroundedJoint()
{
    ADD_PROPERTY_TYPE(ObjectToGround, (nullptr), "Ground", App::Prop_None, "The object to ground");
}

GroundedJoint::~GroundedJoint() = default;

App::DocumentObjectExecReturn* GroundedJoint::execute()
{
    // Grounding is enforced by the AssemblyObject solve, which reads this
    // joint's ObjectToGround link by name; per-object recompute is a no-op.
    return App::DocumentObject::StdReturn;
}

PyObject* GroundedJoint::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new GroundedJointPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
