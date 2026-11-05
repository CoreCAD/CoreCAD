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


#include <Base/PlacementPy.h>
#include <App/DocumentObjectPy.h>

#include "ViewProviderJoint.h"

// inclusion of the generated files (generated out of ViewProviderJoint.pyi)
#include "ViewProviderJointPy.h"
#include "ViewProviderJointPy.cpp"

using namespace AssemblyGui;

// returns a string which represents the object e.g. when printed in python
std::string ViewProviderJointPy::representation() const
{
    std::stringstream str;
    str << "<Joint view provider object at " << getViewProviderJointPtr() << ">";

    return str.str();
}

PyObject* ViewProviderJointPy::redrawMarkers(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    getViewProviderJointPtr()->redrawMarkers();

    Py_Return;
}

PyObject* ViewProviderJointPy::showPreviewJcs(PyObject* args)
{
    PyObject* placementPy = nullptr;
    PyObject* refPy = nullptr;
    if (!PyArg_ParseTuple(args, "O!O", &Base::PlacementPy::Type, &placementPy, &refPy)) {
        return nullptr;
    }

    App::DocumentObject* refObj = nullptr;
    std::vector<std::string> subs;
    try {
        // A reference reaches here the way the rest of the workbench passes one
        // around: [object, [subelement, ...]].
        Py::Sequence ref(refPy);
        if (ref.size() != 2) {
            PyErr_SetString(PyExc_ValueError, "a reference is [object, [subelement, ...]]");
            return nullptr;
        }

        Py::Object objPy {ref[0]};
        if (!PyObject_TypeCheck(objPy.ptr(), &App::DocumentObjectPy::Type)) {
            PyErr_SetString(PyExc_TypeError, "the first item of a reference is an object");
            return nullptr;
        }
        refObj = static_cast<App::DocumentObjectPy*>(objPy.ptr())->getDocumentObjectPtr();

        Py::Object subsObj {ref[1]};
        Py::Sequence subsPy {subsObj};
        for (const auto& sub : subsPy) {
            subs.emplace_back(Py::String(Py::Object(sub)).as_std_string("utf-8"));
        }
    }
    catch (const Py::Exception&) {
        return nullptr;
    }

    getViewProviderJointPtr()->showPreviewJcs(
        *static_cast<Base::PlacementPy*>(placementPy)->getPlacementPtr(),
        refObj,
        subs
    );

    Py_Return;
}

PyObject* ViewProviderJointPy::hidePreviewJcs(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    getViewProviderJointPtr()->hidePreviewJcs();

    Py_Return;
}

PyObject* ViewProviderJointPy::setPickableState(PyObject* args)
{
    PyObject* state = nullptr;
    if (!PyArg_ParseTuple(args, "O!", &PyBool_Type, &state)) {
        return nullptr;
    }

    getViewProviderJointPtr()->setPickableState(Base::asBoolean(state));

    Py_Return;
}

PyObject* ViewProviderJointPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int ViewProviderJointPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}
