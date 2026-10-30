// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License (LGPL)   *
 *   as published by the Free Software Foundation; either version 2 of     *
 *   the License, or (at your option) any later version.                   *
 *   for detail see the LICENCE text file.                                 *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with FreeCAD; if not, write to the Free Software        *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
 *   USA                                                                   *
 *                                                                         *
 ***************************************************************************/

#include <FCConfig.h>

#include <array>
#include <string_view>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/Parameter.h>
#include <Base/PyWrapParseTupleAndKeywords.h>
#include <Base/Sequencer.h>

#include "Application.h"
#include "ApplicationPy.h"
#include "DocumentPy.h"
#include "DocumentObserverPython.h"
#include "DocumentObjectPy.h"
#include "DocumentChanges.h"
#include "ObjectRecipe.h"
#include "Project.h"
#include "VersionHistory.h"
#include "RecoverySnapshot.h"
#include "UnreferencedFiles.h"


// using Base::GetConsole;
using namespace Base;
using namespace App;


namespace
{
/// One version's statement of a merged node, or None where that version has none.
Py::Object asSaid(const std::optional<RecipeNode>& node)
{
    if (!node) {
        return Py::None();
    }
    Py::Dict fields;
    for (const auto& [name, value] : node->fields) {
        fields.setItem(name, Py::String(value));
    }
    Py::List refs;
    for (const RecipeRef& ref : node->refs) {
        Py::Dict entry;
        entry.setItem("target", Py::String(ref.target));
        entry.setItem("element", Py::String(ref.element));
        entry.setItem("label", Py::String(ref.elementLabel));
        refs.append(entry);
    }
    Py::Dict said;
    said.setItem("type", Py::String(node->type));
    said.setItem("fields", fields);
    said.setItem("refs", refs);
    return said;
}

}  // namespace

PyObject* ApplicationPy::sMergeDocuments(PyObject* /*self*/, PyObject* args)
{
    PyObject* ancpy {};
    PyObject* apy {};
    PyObject* bpy {};
    int build = 0;
    if (!PyArg_ParseTuple(args,
                          "O!O!O!|p",
                          &App::DocumentPy::Type,
                          &ancpy,
                          &App::DocumentPy::Type,
                          &apy,
                          &App::DocumentPy::Type,
                          &bpy,
                          &build)) {
        return nullptr;
    }

    Document* ancestor = static_cast<App::DocumentPy*>(ancpy)->getDocumentPtr();
    Document* branchA = static_cast<App::DocumentPy*>(apy)->getDocumentPtr();
    Document* branchB = static_cast<App::DocumentPy*>(bpy)->getDocumentPtr();
    if (!ancestor || !branchA || !branchB) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid document");
        return nullptr;
    }

    PY_TRY
    {
        MergedDocument made;
        if (build != 0) {
            made = buildMergedDocument(*ancestor, *branchA, *branchB);
        }
        else {
            made.report = mergeDocuments(*ancestor, *branchA, *branchB);
        }
        const DocumentMergeReport& report = made.report;
        Py::List conflicts;
        for (const MergeConflict& conflict : report.conflicts) {
            Py::Dict entry;
            entry.setItem("kind",
                          Py::String(conflict.kind == MergeConflict::Kind::Value ? "value"
                                                                                 : "reference"));
            entry.setItem("object", Py::String(conflict.id));
            entry.setItem("type", Py::String(conflict.type));
            entry.setItem("detail", Py::String(conflict.detail));
            entry.setItem("ancestor", asSaid(conflict.ancestor));
            entry.setItem("branchA", asSaid(conflict.branchA));
            entry.setItem("branchB", asSaid(conflict.branchB));
            conflicts.append(entry);
        }
        Py::Dict answer;
        answer.setItem("text", Py::String(formatDocumentReport(report)));
        answer.setItem("conflicts", conflicts);
        Py::List overridden;
        for (const std::string& lost : report.overridden) {
            overridden.append(Py::String(lost));
        }
        answer.setItem("overridden", overridden);
        Py::List undetermined;
        for (const std::string& open : report.undetermined) {
            undetermined.append(Py::String(open));
        }
        answer.setItem("undetermined", undetermined);
        answer.setItem("complete", Py::Boolean(report.complete()));
        if (build != 0) {
            answer.setItem("document",
                           made.document != nullptr ? Py::asObject(made.document->getPyObject())
                                                    : Py::None());
            answer.setItem("refusal", Py::String(made.refusal));
            Py::List failing;
            for (const std::string& label : made.newlyFailing) {
                failing.append(Py::String(label));
            }
            answer.setItem("newlyFailing", failing);
            Py::List identity;
            for (const std::string& change : made.identityChanges) {
                identity.append(Py::String(change));
            }
            answer.setItem("identityChanges", identity);
        }
        return Py::new_reference_to(answer);
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sDiffDocuments(PyObject* /*self*/, PyObject* args)
{
    PyObject* beforePy {};
    PyObject* afterPy {};
    if (!PyArg_ParseTuple(args,
                          "O!O!",
                          &App::DocumentPy::Type,
                          &beforePy,
                          &App::DocumentPy::Type,
                          &afterPy)) {
        return nullptr;
    }

    Document* before = static_cast<App::DocumentPy*>(beforePy)->getDocumentPtr();
    Document* after = static_cast<App::DocumentPy*>(afterPy)->getDocumentPtr();
    if (!before || !after) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid document");
        return nullptr;
    }

    PY_TRY
    {
        const auto optionalText = [](const std::optional<std::string>& value) -> Py::Object {
            return value ? Py::Object(Py::String(*value)) : Py::None();
        };
        const auto kindName = [](RecipeChange::Kind kind) {
            switch (kind) {
                case RecipeChange::Kind::Added:
                    return "added";
                case RecipeChange::Kind::Removed:
                    return "removed";
                case RecipeChange::Kind::FieldChanged:
                    return "field";
                case RecipeChange::Kind::LinksChanged:
                    return "links";
                case RecipeChange::Kind::TypeChanged:
                    return "type";
            }
            return "";
        };

        Py::List list;
        for (const DocumentChange& change : diffDocuments(*before, *after)) {
            Py::Dict entry;
            entry.setItem("object", Py::String(change.objectId));
            entry.setItem("label", Py::String(change.label));
            entry.setItem("type", Py::String(change.objectType));
            entry.setItem("section", Py::String(change.section));
            entry.setItem("entity",
                          change.section.empty() ? Py::Object(Py::None())
                                                 : Py::Object(Py::String(change.change.id)));
            entry.setItem("kind", Py::String(kindName(change.change.kind)));
            entry.setItem("field", Py::String(change.change.field));
            entry.setItem("before", optionalText(change.change.before));
            entry.setItem("after", optionalText(change.change.after));
            entry.setItem("text", Py::String(change.text));
            list.append(entry);
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sFileVersions(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string file(path);
    PyMem_Free(path);

    PY_TRY
    {
        Py::List list;
        for (const FileVersion& version : fileVersions(file)) {
            Py::Dict entry;
            entry.setItem("commit", Py::String(version.commit));
            entry.setItem("author", Py::String(version.author));
            entry.setItem("time", Py::Long(static_cast<long long>(version.time)));
            entry.setItem("summary", Py::String(version.summary));
            list.append(entry);
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sOpenFileVersion(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    char* revision = nullptr;
    if (!PyArg_ParseTuple(args, "etet", "utf-8", &path, "utf-8", &revision)) {
        return nullptr;
    }
    const std::string file(path);
    const std::string at(revision);
    PyMem_Free(path);
    PyMem_Free(revision);

    PY_TRY
    {
        return openFileVersion(file, at)->getPyObject();
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sIsKeptInHistory(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string file(path);
    PyMem_Free(path);

    PY_TRY
    {
        return Py::new_reference_to(Py::Boolean(isKeptInHistory(file)));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sStartHistory(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string folder(path);
    PyMem_Free(path);

    PY_TRY
    {
        startHistory(folder);
        Py_Return;
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sCommitFile(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    char* message = nullptr;
    if (!PyArg_ParseTuple(args, "etet", "utf-8", &path, "utf-8", &message)) {
        return nullptr;
    }
    const std::string file(path);
    const std::string text(message);
    PyMem_Free(path);
    PyMem_Free(message);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(commitFile(file, text)));
    }
    PY_CATCH;
}
