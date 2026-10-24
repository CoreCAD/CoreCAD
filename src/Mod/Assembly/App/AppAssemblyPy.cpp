// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2023 Ondsel <development@ondsel.com>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <App/DocumentObject.h>
#include <App/DocumentObjectPy.h>
#include <Base/Interpreter.h>
#include <Base/PlacementPy.h>
#include <Base/Tools.h>

#include "AssemblyUtils.h"


namespace Assembly
{
class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("AssemblyApp")
    {
        add_varargs_method(
            "findPlacement",
            &Module::findPlacement,
            "findPlacement(ref, ignoreVertex=False) -> Placement\n\n"
            "Local coordinate system a joint connector sits at, for a raw reference of\n"
            "the form [obj, [subElement, vtx]]. Object-relative placement."
        );
        initialize("This module is the Assembly module.");  // register with Python
    }

private:
    // Raw-reference entry point onto Assembly::findPlacement, so Python callers that hold
    // a [obj, subs] reference (interactive JCS preview, exploded-view drag) share the one
    // C++ geometry implementation instead of a Python copy.
    Py::Object findPlacement(const Py::Tuple& args)
    {
        PyObject* pyRef = nullptr;
        PyObject* pyIgnore = Py_False;
        if (!PyArg_ParseTuple(args.ptr(), "O|O!", &pyRef, &PyBool_Type, &pyIgnore)) {
            throw Py::Exception();
        }

        Py::Sequence ref(pyRef);
        if (ref.size() < 2) {
            throw Py::ValueError("findPlacement: ref must be [obj, [subElements]]");
        }

        App::DocumentObject* obj = nullptr;
        Py::Object pyObj = ref.getItem(0);
        if (PyObject_TypeCheck(pyObj.ptr(), &App::DocumentObjectPy::Type)) {
            obj = static_cast<App::DocumentObjectPy*>(pyObj.ptr())->getDocumentObjectPtr();
        }
        else if (!pyObj.isNone()) {
            throw Py::TypeError("findPlacement: ref[0] must be a DocumentObject");
        }

        std::vector<std::string> subs;
        Py::Sequence subSeq(ref.getItem(1));
        for (Py::Sequence::size_type i = 0; i < subSeq.size(); ++i) {
            subs.emplace_back(Py::String(subSeq.getItem(i)).as_std_string("utf-8"));
        }

        const bool ignoreVertex = PyObject_IsTrue(pyIgnore) == 1;
        Base::Placement plc = Assembly::findPlacement(obj, subs, ignoreVertex);
        return Py::asObject(new Base::PlacementPy(new Base::Placement(plc)));
    }
};

PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}

}  // namespace Assembly
