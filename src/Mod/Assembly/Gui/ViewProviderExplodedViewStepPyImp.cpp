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


#include <Base/GeometryPyCXX.h>
#include <Base/Vector3D.h>

#include <Mod/Assembly/App/ExplodedViewStep.h>

#include "ViewProviderExplodedViewStep.h"

// inclusion of the generated files (generated out of ViewProviderExplodedViewStep.pyi)
#include "ViewProviderExplodedViewStepPy.h"
#include "ViewProviderExplodedViewStepPy.cpp"

using namespace AssemblyGui;

// returns a string which represents the object e.g. when printed in python
std::string ViewProviderExplodedViewStepPy::representation() const
{
    std::stringstream str;
    str << "<Exploded view step view provider object at " << getViewProviderExplodedViewStepPtr()
        << ">";

    return str.str();
}

PyObject* ViewProviderExplodedViewStepPy::redrawLines(PyObject* args)
{
    PyObject* linesPy = nullptr;
    if (!PyArg_ParseTuple(args, "O", &linesPy)) {
        return nullptr;
    }

    std::vector<Assembly::ExplosionLine> lines;
    try {
        Py::Sequence sequence(linesPy);
        for (const auto& item : sequence) {
            Py::Sequence pair(item);
            if (pair.size() != 2) {
                PyErr_SetString(PyExc_ValueError, "a line is a pair of points");
                return nullptr;
            }
            lines.emplace_back(
                Base::getVectorFromTuple<double>(Py::Object(pair[0]).ptr()),
                Base::getVectorFromTuple<double>(Py::Object(pair[1]).ptr())
            );
        }
    }
    catch (const Py::Exception&) {
        return nullptr;
    }

    getViewProviderExplodedViewStepPtr()->redrawLines(lines);

    Py_Return;
}

PyObject* ViewProviderExplodedViewStepPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int ViewProviderExplodedViewStepPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}
