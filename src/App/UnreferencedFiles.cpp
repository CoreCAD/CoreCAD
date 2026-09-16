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
#endif

#include <QFile>
#include <QXmlStreamReader>

#include <Base/Exception.h>

#include "Document.h"
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

SourceMaterialSurvey App::surveyProjectSourceMaterial(const std::string& projectFolder)
{
    SourceMaterialSurvey found;

    std::error_code failed;
    const fs::path folder(projectFolder);
    if (!fs::is_directory(folder, failed)) {
        throw Base::FileException("Not a project folder", projectFolder);
    }

    // Read every recipe first, and only then look at what is on disk: a folder whose recipes
    // cannot all be read has no answer to give, and the order makes that impossible to get wrong.
    std::vector<fs::path> recipes;
    for (const auto& entry : fs::directory_iterator(folder, failed)) {
        if (fs::is_regular_file(entry.path(), failed) && isARecipe(entry.path())) {
            recipes.push_back(entry.path());
        }
    }
    std::sort(recipes.begin(), recipes.end());

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

    // Largest first: what is worth acting on is what holds the most, and a list ordered by a
    // digest is ordered by nothing a person can use.
    std::sort(
        found.unreferenced.begin(),
        found.unreferenced.end(),
        [](const UnreferencedFile& left, const UnreferencedFile& right) {
            return left.bytes != right.bytes ? left.bytes > right.bytes : left.path < right.path;
        }
    );
    return found;
}
