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

fs::path resolvedPath(const std::string& path)
{
    return fs::weakly_canonical(fs::absolute(fs::path(path)));
}

bool isDocumentFile(const fs::path& path)
{
    const std::string ext = path.extension().string();
    return !ext.empty() && Document::isNativeFormatExtension(ext.c_str() + 1);
}

/// The identity a recipe states for its document, or empty for a file that states none.
std::string documentUuidOf(const fs::path& path)
{
    QFile file(qtPath(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QXmlStreamReader reader(&file);
    if (!reader.readNextStartElement() || reader.name() != QLatin1String("Recipe")) {
        return {};
    }
    while (reader.readNextStartElement()) {
        if (reader.name() == QLatin1String("Document")) {
            return reader.attributes().value(QLatin1String("uuid")).toString().toStdString();
        }
        reader.skipCurrentElement();
    }
    return {};
}

std::string newestVersionOf(const std::string& path)
{
    const std::vector<FileVersion> versions = fileVersions(path);
    return versions.empty() ? std::string() : versions.front().commit;
}

/// What a project file states.
struct ProjectContent
{
    std::string name;
    std::vector<OutsideDocument> outside;
};

ProjectContent readProject(const std::string& projectFile)
{
    QFile file(qtPath(projectFile));
    if (!file.open(QIODevice::ReadOnly)) {
        throw Base::FileException("Could not read the project file", projectFile.c_str());
    }
    const fs::path folder = resolvedPath(projectFile).parent_path();
    QXmlStreamReader reader(&file);
    if (!reader.readNextStartElement() || reader.name() != QLatin1String("Project")) {
        throw Base::FileException("Not a project file", projectFile.c_str());
    }
    ProjectContent content;
    content.name = reader.attributes().value(QLatin1String("name")).toString().toStdString();
    while (reader.readNextStartElement()) {
        if (reader.name() == QLatin1String("Outside")) {
            const auto attributes = reader.attributes();
            OutsideDocument outside;
            const fs::path stated(
                attributes.value(QLatin1String("path")).toString().toStdString());
            outside.path = fs::weakly_canonical(folder / stated).string();
            outside.uuid = attributes.value(QLatin1String("uuid")).toString().toStdString();
            outside.version = attributes.value(QLatin1String("version")).toString().toStdString();
            content.outside.push_back(outside);
        }
        reader.skipCurrentElement();
    }
    if (reader.hasError()) {
        throw Base::FileException("The project file cannot be read", projectFile.c_str());
    }
    return content;
}

/// Outside documents are written by path, relative to the project, so the file reads the same
/// on every machine the project is checked out on and one added document is one added line.
void writeProject(const fs::path& projectFile, ProjectContent content)
{
    const fs::path folder = projectFile.parent_path();
    std::sort(content.outside.begin(), content.outside.end(), [](const auto& a, const auto& b) {
        return a.path < b.path;
    });
    QFile file(qtPath(projectFile));
    if (!file.open(QIODevice::WriteOnly)) {
        throw Base::FileException("Could not write the project file", projectFile.string().c_str());
    }
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QLatin1String("Project"));
    writer.writeAttribute(QLatin1String("name"), QString::fromStdString(content.name));
    for (const OutsideDocument& outside : content.outside) {
        writer.writeStartElement(QLatin1String("Outside"));
        writer.writeAttribute(
            QLatin1String("path"),
            QString::fromStdString(fs::path(outside.path).lexically_relative(folder).generic_string())
        );
        if (!outside.uuid.empty()) {
            writer.writeAttribute(QLatin1String("uuid"), QString::fromStdString(outside.uuid));
        }
        if (!outside.version.empty()) {
            writer.writeAttribute(QLatin1String("version"), QString::fromStdString(outside.version));
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndDocument();
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
    return readProject(projectFile).name;
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
    writeProject(projectFile, {name, {}});
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

std::vector<std::string> App::projectDocuments(const std::string& projectFile)
{
    const fs::path project = resolvedPath(projectFile);
    const fs::path folder = project.parent_path();
    std::vector<std::string> found;
    std::error_code failed;
    fs::recursive_directory_iterator it(folder, failed);
    for (; !failed && it != fs::recursive_directory_iterator(); it.increment(failed)) {
        const fs::path& path = it->path();
        if (it->is_directory(failed)) {
            const std::string name = path.filename().string();
            if (name.front() == '.' || name == "assets" || fs::exists(path / ".git")
                || !projectFileIn(path).empty()) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (isDocumentFile(path)) {
            found.push_back(path.string());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::vector<OutsideDocument> App::outsideDocuments(const std::string& projectFile)
{
    return readProject(projectFile).outside;
}

OutsideDocument App::addOutsideDocument(const std::string& projectFile, const std::string& path)
{
    const fs::path project = resolvedPath(projectFile);
    const fs::path document = resolvedPath(path);
    if (!fs::is_regular_file(document) || !isDocumentFile(document)) {
        throw Base::FileException("Not a document", document.string().c_str());
    }
    if (fs::path(projectFileOf(document.string())) == project) {
        throw Base::FileException("The document is in this project already",
                                  document.string().c_str());
    }
    ProjectContent content = readProject(project.string());
    for (const OutsideDocument& listed : content.outside) {
        if (fs::path(listed.path) == document) {
            throw Base::FileException("The project lists this document already",
                                      document.string().c_str());
        }
    }
    const OutsideDocument added {document.string(),
                                 documentUuidOf(document),
                                 newestVersionOf(document.string())};
    content.outside.push_back(added);
    writeProject(project, content);
    return added;
}

std::string App::newerVersionOf(const OutsideDocument& outside)
{
    if (outside.version.empty()) {
        return {};
    }
    const std::string newest = newestVersionOf(outside.path);
    return newest == outside.version ? std::string() : newest;
}

std::string App::updateOutsideDocument(const std::string& projectFile, const std::string& path)
{
    const fs::path project = resolvedPath(projectFile);
    const fs::path document = resolvedPath(path);
    ProjectContent content = readProject(project.string());
    for (OutsideDocument& listed : content.outside) {
        if (fs::path(listed.path) == document) {
            listed.version = newestVersionOf(listed.path);
            writeProject(project, content);
            return listed.version;
        }
    }
    throw Base::FileException("The project does not list this document", document.string().c_str());
}
