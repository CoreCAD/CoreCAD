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

#include <Base/VectorPy.h>

#include "ExplodedViewStep.h"

// inclusion of the generated files (generated out of ExplodedViewStep.pyi)
#include "ExplodedViewStepPy.h"
#include "ExplodedViewStepPy.cpp"

using namespace Assembly;

// returns a string which represents the object e.g. when printed in python
std::string ExplodedViewStepPy::representation() const
{
    return {"<Assembly ExplodedViewStep>"};
}

PyObject* ExplodedViewStepPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int ExplodedViewStepPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}

PyObject* ExplodedViewStepPy::applyStep(PyObject* args)
{
    PyObject* comPy = nullptr;
    double size = 100.0;

    if (!PyArg_ParseTuple(args, "|O!d", &Base::VectorPy::Type, &comPy, &size)) {
        return nullptr;
    }

    Base::Vector3d com;
    if (comPy) {
        com = static_cast<Base::VectorPy*>(comPy)->value();
    }

    Py::List result;
    for (const auto& [start, end] : getExplodedViewStepPtr()->applyStep(com, size)) {
        Py::List pair;
        pair.append(Py::asObject(new Base::VectorPy(start)));
        pair.append(Py::asObject(new Base::VectorPy(end)));
        result.append(pair);
    }

    return Py::new_reference_to(result);
}
