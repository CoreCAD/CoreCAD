// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/PyObjectBase.h>

#include "SketchObject.h"
#include "SketchObjectPy.h"
#include "SketchObjectSF.h"
#include "SketchRecipe.h"


namespace Sketcher
{
class Module: public Py::ExtensionModule<Module>
{
public:
    Module()
        : Py::ExtensionModule<Module>("Sketcher")
    {
        add_varargs_method("open", &Module::open);
        add_varargs_method("insert", &Module::insert);
        add_varargs_method(
            "mergeReport",
            &Module::mergeReport,
            "mergeReport(ancestor, branchA, branchB) -> str\n\n"
            "Merge three sketches — a common ancestor and two edited copies of it — and return a\n"
            "plain-language report of the outcome: what combined, what value conflicts need a\n"
            "choice, what constraints were dropped or need a decision, and whether the merged\n"
            "sketch still solves. Exploratory surfacing of the recipe-merge engine; the three\n"
            "sketches must share the ancestor's durable identity (a reload/copy, not a re-draw)."
        );
        initialize("This module is the Sketcher module.");  // register with Python
    }

    ~Module() override
    {}

private:
    Py::Object open(const Py::Tuple& args)
    {
        char* Name;
        if (!PyArg_ParseTuple(args.ptr(), "et", "utf-8", &Name)) {
            throw Py::Exception();
        }
        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        Base::FileInfo file(EncodedName.c_str());

        // extract extension
        if (file.extension().empty()) {
            throw Py::RuntimeError("No file extension");
        }

        throw Py::RuntimeError("Unknown file extension");
        // return Py::None();
    }

    Py::Object insert(const Py::Tuple& args)
    {
        char* Name;
        const char* DocName;
        if (!PyArg_ParseTuple(args.ptr(), "ets", "utf-8", &Name, &DocName)) {
            throw Py::Exception();
        }
        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        try {
            Base::FileInfo file(EncodedName.c_str());

            // extract extension
            if (file.extension().empty()) {
                throw Py::RuntimeError("No file extension");
            }

            App::Document* pcDoc = App::GetApplication().getDocument(DocName);
            if (!pcDoc) {
                pcDoc = App::GetApplication().newDocument(DocName);
            }

            if (file.hasExtension("skf")) {
                auto filename = file.fileNamePure();
                auto* pcFeature = pcDoc->addObject<Sketcher::SketchObjectSF>(filename.c_str());
                pcFeature->SketchFlatFile.setValue(EncodedName.c_str());

                pcDoc->recompute();
            }
            else {
                throw Py::RuntimeError("Unknown file extension");
            }
        }
        catch (const Base::Exception& e) {
            throw Py::RuntimeError(e.what());
        }
        return Py::None();
    }

    Py::Object mergeReport(const Py::Tuple& args)
    {
        PyObject* ancObj = nullptr;
        PyObject* aObj = nullptr;
        PyObject* bObj = nullptr;
        if (!PyArg_ParseTuple(
                args.ptr(),
                "O!O!O!",
                &SketchObjectPy::Type,
                &ancObj,
                &SketchObjectPy::Type,
                &aObj,
                &SketchObjectPy::Type,
                &bObj
            )) {
            throw Py::Exception();
        }

        SketchObject* ancestor = static_cast<SketchObjectPy*>(ancObj)->getSketchObjectPtr();
        SketchObject* branchA = static_cast<SketchObjectPy*>(aObj)->getSketchObjectPtr();
        SketchObject* branchB = static_cast<SketchObjectPy*>(bObj)->getSketchObjectPtr();

        try {
            MergeReport report = mergeSketches(*ancestor, *branchA, *branchB);
            return Py::String(formatMergeReport(report));
        }
        catch (const Base::Exception& e) {
            throw Py::RuntimeError(e.what());
        }
    }
};

/// @cond DOXERR
PyObject* initModule()
{
    return Base::Interpreter().addModule(new Module);
}
/// @endcond

}  // namespace Sketcher
