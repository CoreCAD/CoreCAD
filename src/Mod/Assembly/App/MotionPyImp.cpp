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

#include "AssemblyObject.h"
#include "Motion.h"
#include "Simulation.h"

// inclusion of the generated files (generated out of Motion.pyi)
#include "MotionPy.h"
#include "MotionPy.cpp"

using namespace Assembly;

// returns a string which represents the object e.g. when printed in python
std::string MotionPy::representation() const
{
    return {"<Assembly Motion>"};
}

PyObject* MotionPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int MotionPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}

PyObject* MotionPy::getSimulation(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    auto* simulation = getMotionPtr()->getSimulation();
    if (!simulation) {
        Py_RETURN_NONE;
    }

    return simulation->getPyObject();
}

PyObject* MotionPy::getAssembly(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    auto* assembly = getMotionPtr()->getAssembly();
    if (!assembly) {
        Py_RETURN_NONE;
    }

    return assembly->getPyObject();
}
