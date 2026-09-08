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
#include <Mod/Part/App/TopoShapePy.h>

#include "AssemblyObject.h"
#include "ExplodedView.h"

// inclusion of the generated files (generated out of ExplodedView.pyi)
#include "ExplodedViewPy.h"
#include "ExplodedViewPy.cpp"

using namespace Assembly;

namespace
{
/// Render explosion lines as a Python list of [start, end] vector pairs, the shape
/// the (still-Python) task panel and view provider already expect.
PyObject* toPyLines(const std::vector<ExplosionLine>& lines)
{
    Py::List result;
    for (const auto& [start, end] : lines) {
        Py::List pair;
        pair.append(Py::asObject(new Base::VectorPy(start)));
        pair.append(Py::asObject(new Base::VectorPy(end)));
        result.append(pair);
    }
    return Py::new_reference_to(result);
}
}  // namespace

// returns a string which represents the object e.g. when printed in python
std::string ExplodedViewPy::representation() const
{
    return {"<Assembly ExplodedView>"};
}

PyObject* ExplodedViewPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int ExplodedViewPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}

PyObject* ExplodedViewPy::getAssembly(PyObject* args) const
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    App::DocumentObject* assembly = getExplodedViewPtr()->getAssembly();
    if (!assembly) {
        Py_RETURN_NONE;
    }
    return assembly->getPyObject();
}

PyObject* ExplodedViewPy::applyMoves(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    return toPyLines(getExplodedViewPtr()->applyMoves());
}

PyObject* ExplodedViewPy::explodeTemporarily(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    getExplodedViewPtr()->explodeTemporarily();
    Py_RETURN_NONE;
}

PyObject* ExplodedViewPy::restoreAssembly(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }
    getExplodedViewPtr()->restoreAssembly();
    Py_RETURN_NONE;
}

PyObject* ExplodedViewPy::saveAssemblyAndExplode(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    const Part::TopoShape shape = getExplodedViewPtr()->saveAssemblyAndExplode();
    if (shape.isNull()) {
        Py_RETURN_NONE;
    }
    return new Part::TopoShapePy(new Part::TopoShape(shape));
}

PyObject* ExplodedViewPy::getExplodedShape(PyObject* args) const
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    const Part::TopoShape shape = getExplodedViewPtr()->getExplodedShape();
    if (shape.isNull()) {
        Py_RETURN_NONE;
    }
    return new Part::TopoShapePy(new Part::TopoShape(shape));
}
