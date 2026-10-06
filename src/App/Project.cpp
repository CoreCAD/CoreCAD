// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
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

#ifndef _PreComp_
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#endif

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <Base/Exception.h>

#include "Project.h"

#include "Document.h"
#include "VersionHistory.h"

using namespace App;
namespace fs = std::filesystem;

namespace
{

/// The project file in `folder`, or empty. Where a folder holds more than one, the first by name
/// is the project, so the answer does not depend on the order the folder is read in.
fs::path projectFileIn(const fs::path& folder)
{
    std::vector<fs::path> found;
    std::error_code failed;
    for (const auto& entry : fs::directory_iterator(folder, failed)) {
        if (entry.path().extension() == projectFileExtension && entry.is_regular_file(failed)) {
            found.push_back(entry.path());
        }
    }
    if (found.empty()) {
        return {};
    }
    return *std::min_element(found.begin(), found.end());
}

QString qtPath(const fs::path& path)
{
    return QString::fromStdString(path.string());
}

}  // namespace

std::string App::projectFileOf(const std::string& path)
{
    const std::string repository = historyFolderOf(path);
    if (repository.empty()) {
        return {};
    }
    const fs::path top(repository);
    fs::path folder = fs::weakly_canonical(fs::absolute(fs::path(path))).parent_path();
    while (true) {
        const fs::path found = projectFileIn(folder);
        if (!found.empty()) {
            return found.string();
        }
        if (folder == top || !folder.has_relative_path()) {
            return {};
        }
        folder = folder.parent_path();
    }
}

std::string App::projectName(const std::string& projectFile)
{
    QFile file(qtPath(projectFile));
    if (!file.open(QIODevice::ReadOnly)) {
        throw Base::FileException("Could not read the project file", projectFile.c_str());
    }
    QXmlStreamReader reader(&file);
    while (reader.readNextStartElement()) {
        if (reader.name() == QLatin1String("Project")) {
            return reader.attributes().value(QLatin1String("name")).toString().toStdString();
        }
        reader.skipCurrentElement();
    }
    throw Base::FileException("Not a project file", projectFile.c_str());
}

std::string App::createProject(const std::string& folder, const std::string& name)
{
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) {
        throw Base::ValueError("A project needs a name that can be a file name");
    }
    const fs::path where = fs::weakly_canonical(fs::absolute(fs::path(folder)));
    fs::create_directories(where);
    if (!projectFileIn(where).empty()) {
        throw Base::FileException("The folder already holds a project", where.string().c_str());
    }
    if (historyFolderOf((where / projectFileExtension).string()).empty()) {
        startHistory(where.string());
    }

    const fs::path projectFile = where / (name + projectFileExtension);
    QFile file(qtPath(projectFile));
    if (!file.open(QIODevice::WriteOnly)) {
        throw Base::FileException("Could not write the project file", projectFile.string().c_str());
    }
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QLatin1String("Project"));
    writer.writeAttribute(QLatin1String("name"), QString::fromStdString(name));
    writer.writeEndElement();
    writer.writeEndDocument();
    return projectFile.string();
}

std::string App::moveIntoProject(Document& doc, const std::string& projectFile)
{
    const std::string current = doc.FileName.getStrValue();
    if (current.empty()) {
        throw Base::FileException("A document never saved has no file to move");
    }
    const fs::path from = fs::weakly_canonical(fs::absolute(fs::path(current)));
    const fs::path to =
        fs::weakly_canonical(fs::absolute(fs::path(projectFile))).parent_path() / from.filename();
    if (to == from) {
        return to.string();
    }
    if (fs::exists(to)) {
        throw Base::FileException("The project already holds a file of that name",
                                  to.string().c_str());
    }
    if (!doc.saveAs(to.string().c_str())) {
        throw Base::FileException("Could not save the document into the project",
                                  to.string().c_str());
    }
    // Only one copy may remain: two files with one document identity let a reference between
    // documents find the wrong one.
    fs::remove(from);
    return to.string();
}
