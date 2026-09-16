// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <algorithm>
#include <filesystem>
#include <map>
#endif

#include <QFile>
#include <QXmlStreamReader>

#include <Base/Exception.h>
#include <Base/FileInfo.h>

#include "Application.h"
#include "Document.h"
#include "GeometryCache.h"
#include "UnreferencedFiles.h"

using namespace App;

namespace fs = std::filesystem;

namespace
{
/// The attribute a recipe names source material with. One word, in one place.
constexpr const char* assetAttribute = "asset";

/// Everything the folder keeps its source material under.
const char* sourceFolderName = "assets";

/// What one entry holds, itself or everything beneath it.
std::uintmax_t bytesHeldBy(const fs::path& entry)
{
    std::error_code failed;
    if (fs::is_regular_file(entry, failed)) {
        const std::uintmax_t size = fs::file_size(entry, failed);
        return failed ? 0 : size;
    }
    std::uintmax_t total = 0;
    for (fs::recursive_directory_iterator it(entry, failed), end; it != end; it.increment(failed)) {
        if (fs::is_regular_file(it->path(), failed)) {
            const std::uintmax_t size = fs::file_size(it->path(), failed);
            total += failed ? 0 : size;
        }
    }
    return total;
}

/// Whether this file is one of ours to read -- a whole native document, not an import beside it.
bool isARecipe(const fs::path& file)
{
    std::string extension = file.extension().string();
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    return Document::isNativeFormatExtension(extension.c_str());
}

/// Every recipe in the folder, in a settled order, or a refusal where the folder is not one.
std::vector<fs::path> recipesIn(const fs::path& folder, const std::string& asked)
{
    std::error_code failed;
    if (!fs::is_directory(folder, failed)) {
        throw Base::FileException("Not a project folder", asked);
    }
    std::vector<fs::path> recipes;
    for (const auto& entry : fs::directory_iterator(folder, failed)) {
        if (fs::is_regular_file(entry.path(), failed) && isARecipe(entry.path())) {
            recipes.push_back(entry.path());
        }
    }
    std::sort(recipes.begin(), recipes.end());
    return recipes;
}

/// Largest first: what is worth acting on is what holds the most, and a list ordered by a digest
/// is ordered by nothing a person can use.
void orderByWhatTheyHold(std::vector<UnreferencedFile>& files)
{
    std::sort(
        files.begin(),
        files.end(),
        [](const UnreferencedFile& left, const UnreferencedFile& right) {
            return left.bytes != right.bytes ? left.bytes > right.bytes : left.path < right.path;
        }
    );
}
}  // namespace

std::set<std::string> App::sourceMaterialNamedBy(const std::string& recipePath)
{
    QFile recipe(QString::fromUtf8(recipePath.c_str()));
    if (!recipe.open(QIODevice::ReadOnly)) {
        throw Base::FileException("Could not read the recipe", recipePath);
    }

    std::set<std::string> named;
    QXmlStreamReader reading(&recipe);
    while (!reading.atEnd()) {
        if (reading.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        // Every element is asked, not only the one that carries it today: a form that moved the
        // attribute would otherwise report every file it names as named by nothing.
        const QString id = reading.attributes().value(QLatin1String(assetAttribute)).toString();
        if (!id.isEmpty()) {
            named.insert(id.toStdString());
        }
    }

    if (reading.hasError()) {
        // Where it stopped, because a person given "this file will not read" has nothing to act
        // on -- and because the alternative is treating everything after that point as absent.
        throw Base::XMLParseException(
            recipePath + ": " + reading.errorString().toStdString() + " (line "
            + std::to_string(reading.lineNumber()) + ", column "
            + std::to_string(reading.columnNumber()) + ")"
        );
    }
    return named;
}

ProjectSurvey App::surveyProjectSourceMaterial(const std::string& projectFolder)
{
    ProjectSurvey found;

    std::error_code failed;
    const fs::path folder(projectFolder);

    // Read every recipe first, and only then look at what is on disk: a folder whose recipes
    // cannot all be read has no answer to give, and the order makes that impossible to get wrong.
    const std::vector<fs::path> recipes = recipesIn(folder, projectFolder);

    std::set<std::string> named;
    for (const fs::path& recipe : recipes) {
        const std::set<std::string> names = sourceMaterialNamedBy(recipe.string());
        named.insert(names.begin(), names.end());
        found.recipesRead.push_back(recipe.string());
    }

    const fs::path source = folder / sourceFolderName;
    if (!fs::is_directory(source, failed)) {
        // Nothing has been stored yet, so nothing is unreferenced. Not an error: a project need
        // not have source material.
        return found;
    }

    for (const auto& entry : fs::directory_iterator(source, failed)) {
        const std::string id = entry.path().filename().string();
        if (named.count(id) != 0) {
            continue;
        }
        const std::uintmax_t bytes = bytesHeldBy(entry.path());
        found.unreferenced.push_back({entry.path().string(), bytes});
        found.bytes += bytes;
    }

    orderByWhatTheyHold(found.unreferenced);
    return found;
}

namespace
{
/// Every document open right now, by name. What a survey finds already open it must leave open.
std::set<std::string> whatIsOpen()
{
    std::set<std::string> open;
    for (const Document* doc : GetApplication().getDocuments()) {
        if (doc != nullptr) {
            open.insert(doc->getName());
        }
    }
    return open;
}

/// Close everything the survey opened, and nothing it found already open. A document opened for
/// an answer must not outlive the answer, and one the person had open must not be taken from
/// them -- including where the survey is leaving by way of a refusal.
void closeWhateverWasOpened(const std::set<std::string>& wasOpen)
{
    for (const std::string& name : whatIsOpen()) {
        if (wasOpen.count(name) != 0) {
            continue;
        }
        try {
            if (GetApplication().getDocument(name.c_str()) != nullptr) {
                GetApplication().closeDocument(name.c_str());
            }
        }
        catch (const std::exception&) {
            // Nothing was written, so a document that will not close costs only its memory. The
            // answer the survey came for still stands.
        }
    }
}
}  // namespace

ProjectSurvey App::surveyProjectRebuildStore(const std::string& projectFolder)
{
    ProjectSurvey found;

    std::error_code failed;
    const fs::path folder(projectFolder);
    const std::vector<fs::path> recipes = recipesIn(folder, projectFolder);

    // An entry's name is derived, so the only way to ask what names it is to compute it, by the
    // same code that wrote it. That means opening the documents -- and putting back exactly the
    // session that was here before.
    const std::set<std::string> wasOpen = whatIsOpen();
    std::map<std::string, std::set<std::string>> named;
    try {
        for (const fs::path& recipe : recipes) {
            const std::string path = Base::FileInfo(recipe.string()).filePath();
            Document* doc = GetApplication().getDocumentByPath(path.c_str());
            if (doc != nullptr && wasOpen.count(doc->getName()) != 0) {
                // What is kept on disk was named by the last save. What is open may be neither
                // saved nor the same, and an answer about it would be an answer to another
                // question.
                throw Base::RuntimeError(
                    recipe.string()
                    + " is open. What is kept here was named when it was last saved, so close it "
                      "before asking what still names it."
                );
            }
            if (doc == nullptr) {
                DocumentInitFlags how;
                how.createView = false;
                doc = GetApplication().openDocument(recipe.string().c_str(), how);
            }
            if (doc == nullptr) {
                throw Base::FileException("Could not open the recipe", recipe.string());
            }
            if (!doc->seesEveryReference()) {
                // What could not be read may name anything, so an absence of references here is
                // no longer evidence of anything (Amendment 19 Clause 19.3).
                throw Base::RuntimeError(recipe.string() + ": " + doc->whyReferencesAreFrozen());
            }
            named[doc->Uid.getValueStr()] = builtGeometryKeys(*doc, doc->assetDirectory());
            found.recipesRead.push_back(recipe.string());
        }
    }
    catch (...) {
        closeWhateverWasOpened(wasOpen);
        throw;
    }
    closeWhateverWasOpened(wasOpen);

    const auto report = [&found](const fs::path& entry) {
        const std::uintmax_t bytes = bytesHeldBy(entry);
        found.unreferenced.push_back({entry.string(), bytes});
        found.bytes += bytes;
    };

    const fs::path store = folder / Document::cacheFolderName;
    if (!fs::is_directory(store, failed)) {
        // Nothing has been kept yet, so nothing is unreferenced.
        return found;
    }

    for (const auto& perDocument : fs::directory_iterator(store, failed)) {
        const auto names = named.find(perDocument.path().filename().string());
        if (names == named.end()) {
            // Nothing in the folder is that document any more -- it was deleted, or moved away
            // and left this behind. Everything kept for it is kept for nothing, the display state
            // beside the results included.
            report(perDocument.path());
            continue;
        }

        // For a document that is still here, only the results are asked about. The colours and
        // the camera beside them are rebuildable too, but a person chose them, and they are named
        // by the document rather than by a digest that could go out of date.
        const fs::path entries(builtGeometryFolder(perDocument.path().string()));
        if (!fs::is_directory(entries, failed)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(entries, failed)) {
            if (names->second.count(entry.path().filename().string()) == 0) {
                report(entry.path());
            }
        }
    }

    orderByWhatTheyHold(found.unreferenced);
    return found;
}

namespace
{
/// Where a path points once the spelling stops mattering, so that a file named one way and found
/// another is still the same file.
fs::path whereItPoints(const fs::path& path)
{
    std::error_code failed;
    const fs::path settled = fs::weakly_canonical(path, failed);
    return failed ? path.lexically_normal() : settled;
}

/// Remove one entry, itself or everything beneath it, and say why if it would not go.
bool letGoOf(const fs::path& entry, std::string& why)
{
    std::error_code failed;
    fs::remove_all(entry, failed);
    if (failed) {
        why = failed.message();
        return false;
    }
    return true;
}
}  // namespace

Discarded App::discardUnreferencedRebuildResults(const std::string& projectFolder)
{
    // Asked again here rather than taken from the caller: a list a person was reading while
    // something else saved is a list about a project that has moved on.
    const ProjectSurvey found = surveyProjectRebuildStore(projectFolder);

    Discarded done;
    for (const UnreferencedFile& entry : found.unreferenced) {
        std::string why;
        if (letGoOf(fs::path(entry.path), why)) {
            done.removed.push_back(entry);
            done.bytes += entry.bytes;
        }
        else {
            done.kept.emplace_back(entry.path, why);
        }
    }
    return done;
}

Discarded App::discardSourceMaterial(const std::string& projectFolder,
                                     const std::vector<std::string>& named)
{
    const ProjectSurvey found = surveyProjectSourceMaterial(projectFolder);

    // What the survey itself says is unreferenced, and nothing else. A path is removed because
    // this call could confirm it, never because it was asked for: authored content is not
    // deleted on the strength of an argument.
    std::map<fs::path, const UnreferencedFile*> confirmed;
    for (const UnreferencedFile& entry : found.unreferenced) {
        confirmed[whereItPoints(fs::path(entry.path))] = &entry;
    }

    // The one place anything here is allowed to remove from.
    const fs::path source = whereItPoints(fs::path(projectFolder) / sourceFolderName);

    Discarded done;
    for (const std::string& asked : named) {
        const fs::path where = whereItPoints(fs::path(asked));
        const auto entry = confirmed.find(where);
        if (entry == confirmed.end()) {
            std::error_code failed;
            // Three situations, and a person acts differently on each: the file is not in this
            // project at all, it is not there to remove, or the project still names it. The
            // first is the one that matters most -- an argument naming a file elsewhere on the
            // machine is answered rather than obeyed.
            const std::string why =
                where.parent_path() != source
                ? "it is not source material of this project"
                : (!fs::exists(where, failed) ? "there is nothing there"
                                              : "something in this project names it");
            done.kept.emplace_back(asked, why);
            continue;
        }

        std::string why;
        if (letGoOf(entry->first, why)) {
            done.removed.push_back(*entry->second);
            done.bytes += entry->second->bytes;
        }
        else {
            done.kept.emplace_back(asked, why);
        }
    }
    return done;
}
