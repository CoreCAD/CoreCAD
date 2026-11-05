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
/// One survey, said the way a script reads it. Both halves answer the same shape of question, so
/// they answer in the same shape.
Py::Dict asSaid(const ProjectSurvey& found)
{
    Py::List read;
    for (const std::string& recipe : found.recipesRead) {
        read.append(Py::String(recipe));
    }
    Py::List unreferenced;
    for (const UnreferencedFile& file : found.unreferenced) {
        Py::Tuple entry(2);
        entry.setItem(0, Py::String(file.path));
        entry.setItem(1, Py::Long(static_cast<unsigned long>(file.bytes)));
        unreferenced.append(entry);
    }

    Py::Dict answer;
    answer.setItem("recipesRead", read);
    answer.setItem("unreferenced", unreferenced);
    answer.setItem("bytes", Py::Long(static_cast<unsigned long>(found.bytes)));
    return answer;
}

/// The folder a survey was asked about, or nothing where the argument was not one.
bool folderAsked(PyObject* args, std::string& asked)
{
    char* folder {};
    if (!PyArg_ParseTuple(args, "et", "utf-8", &folder)) {
        return false;
    }
    asked = folder;
    PyMem_Free(folder);
    return true;
}
}  // namespace

PyObject* ApplicationPy::sSurveyProjectSourceMaterial(PyObject* /*self*/, PyObject* args)
{
    std::string asked;
    if (!folderAsked(args, asked)) {
        return nullptr;
    }
    PY_TRY
    {
        return Py::new_reference_to(asSaid(surveyProjectSourceMaterial(asked)));
    }
    PY_CATCH
}

PyObject* ApplicationPy::sSurveyProjectRebuildStore(PyObject* /*self*/, PyObject* args)
{
    std::string asked;
    if (!folderAsked(args, asked)) {
        return nullptr;
    }
    PY_TRY
    {
        return Py::new_reference_to(asSaid(surveyProjectRebuildStore(asked)));
    }
    PY_CATCH
}

namespace
{
/// What a discard did, said the way a script reads it.
Py::Dict asSaid(const Discarded& done)
{
    Py::List removed;
    for (const UnreferencedFile& file : done.removed) {
        Py::Tuple entry(2);
        entry.setItem(0, Py::String(file.path));
        entry.setItem(1, Py::Long(static_cast<unsigned long>(file.bytes)));
        removed.append(entry);
    }
    Py::List kept;
    for (const auto& [path, why] : done.kept) {
        Py::Tuple entry(2);
        entry.setItem(0, Py::String(path));
        entry.setItem(1, Py::String(why));
        kept.append(entry);
    }

    Py::Dict answer;
    answer.setItem("removed", removed);
    answer.setItem("bytes", Py::Long(static_cast<unsigned long>(done.bytes)));
    answer.setItem("kept", kept);
    return answer;
}
}  // namespace

PyObject* ApplicationPy::sDiscardUnreferencedRebuildResults(PyObject* /*self*/, PyObject* args)
{
    std::string asked;
    if (!folderAsked(args, asked)) {
        return nullptr;
    }
    PY_TRY
    {
        return Py::new_reference_to(asSaid(discardUnreferencedRebuildResults(asked)));
    }
    PY_CATCH
}

PyObject* ApplicationPy::sDiscardSourceMaterial(PyObject* /*self*/, PyObject* args)
{
    char* folder {};
    PyObject* paths {};
    if (!PyArg_ParseTuple(args, "etO", "utf-8", &folder, &paths)) {
        return nullptr;
    }
    const std::string asked(folder);
    PyMem_Free(folder);

    PY_TRY
    {
        std::vector<std::string> named;
        Py::Sequence asking(paths);
        named.reserve(asking.size());
        for (const auto& path : asking) {
            named.push_back(Py::String(path).as_std_string("utf-8"));
        }
        return Py::new_reference_to(asSaid(discardSourceMaterial(asked, named)));
    }
    PY_CATCH
}


PyObject* ApplicationPy::sProjectFileOf(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string file(path);
    PyMem_Free(path);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(projectFileOf(file)));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sProjectName(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string file(path);
    PyMem_Free(path);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(projectName(file)));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sCreateProject(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    char* name = nullptr;
    if (!PyArg_ParseTuple(args, "etet", "utf-8", &path, "utf-8", &name)) {
        return nullptr;
    }
    const std::string folder(path);
    const std::string projectName(name);
    PyMem_Free(path);
    PyMem_Free(name);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(createProject(folder, projectName)));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sMoveIntoProject(PyObject* /*self*/, PyObject* args)
{
    PyObject* document = nullptr;
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "O!et", &App::DocumentPy::Type, &document, "utf-8", &path)) {
        return nullptr;
    }
    const std::string projectFile(path);
    PyMem_Free(path);

    PY_TRY
    {
        auto* doc = static_cast<App::DocumentPy*>(document)->getDocumentPtr();
        return Py::new_reference_to(Py::String(moveIntoProject(*doc, projectFile)));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sProjectDocuments(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string projectFile(path);
    PyMem_Free(path);

    PY_TRY
    {
        Py::List list;
        for (const std::string& document : projectDocuments(projectFile)) {
            list.append(Py::String(document));
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sOutsideDocuments(PyObject* /*self*/, PyObject* args)
{
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "et", "utf-8", &path)) {
        return nullptr;
    }
    const std::string projectFile(path);
    PyMem_Free(path);

    PY_TRY
    {
        Py::List list;
        for (const OutsideDocument& outside : outsideDocuments(projectFile)) {
            Py::Dict entry;
            entry.setItem("path", Py::String(outside.path));
            entry.setItem("uuid", Py::String(outside.uuid));
            entry.setItem("version", Py::String(outside.version));
            entry.setItem("newer", Py::String(newerVersionOf(outside)));
            list.append(entry);
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sAddOutsideDocument(PyObject* /*self*/, PyObject* args)
{
    char* project = nullptr;
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "etet", "utf-8", &project, "utf-8", &path)) {
        return nullptr;
    }
    const std::string projectFile(project);
    const std::string document(path);
    PyMem_Free(project);
    PyMem_Free(path);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(addOutsideDocument(projectFile, document).version));
    }
    PY_CATCH;
}

PyObject* ApplicationPy::sUpdateOutsideDocument(PyObject* /*self*/, PyObject* args)
{
    char* project = nullptr;
    char* path = nullptr;
    if (!PyArg_ParseTuple(args, "etet", "utf-8", &project, "utf-8", &path)) {
        return nullptr;
    }
    const std::string projectFile(project);
    const std::string document(path);
    PyMem_Free(project);
    PyMem_Free(path);

    PY_TRY
    {
        return Py::new_reference_to(Py::String(updateOutsideDocument(projectFile, document)));
    }
    PY_CATCH;
}
