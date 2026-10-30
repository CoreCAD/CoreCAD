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
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <fstream>
#include <sstream>
#include <vector>
#endif

#include <zipios++/zipfile.h>

#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Uuid.h>
#include <Base/Writer.h>

#include "Document.h"
#include "DocumentObject.h"
#include "SealedArchive.h"
#include "StoredRecipe.h"
#include "UnreferencedFiles.h"

#include <filesystem>

namespace fs = std::filesystem;

using namespace App;

namespace
{

/// A folder that removes itself, so a failed export leaves nothing behind to be mistaken for one.
class ScratchFolder
{
public:
    ScratchFolder()
        : _path(fs::temp_directory_path() / ("cruth-export-" + Base::Uuid::createUuid()))
    {
        std::error_code failed;
        fs::create_directories(_path, failed);
    }
    ~ScratchFolder()
    {
        std::error_code ignored;
        fs::remove_all(_path, ignored);
    }
    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    const fs::path& path() const
    {
        return _path;
    }

private:
    fs::path _path;
};

std::string readWholeFile(const fs::path& file)
{
    Base::ifstream stream(Base::FileInfo(file.string()), std::ios::in | std::ios::binary);
    if (!stream) {
        throw Base::FileException("Could not read", file.string());
    }
    return std::string {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Where a piece of source material is to be found: written for this export, or already in the
/// project folder beside the document. Both are the project's own store; only the path differs.
fs::path locateAsset(const std::string& id, const fs::path& written, const fs::path& project)
{
    std::error_code failed;
    const fs::path fresh = written / id;
    if (fs::is_directory(fresh, failed)) {
        return fresh;
    }
    if (!project.empty()) {
        const fs::path stored = project / id;
        if (fs::is_directory(stored, failed)) {
            return stored;
        }
    }
    return {};
}

}  // namespace

std::set<std::string> App::writeSealedArchive(const Document& doc, const std::string& archivePath)
{
    ScratchFolder scratch;
    if (scratch.path().empty()) {
        throw Base::FileException("Could not make room to write the archive", archivePath);
    }

    // The recipe inside the archive is written by the one writer a document is written by
    // (Amendment 19 Clause 19.5), so what the archive says and what the record says cannot drift.
    // Its source material is written to one side rather than into the project folder: an export
    // reads a document, and a read must not edit the folder it read.
    const fs::path staged = scratch.path() / sealedArchiveAssetFolder;
    std::error_code failed;
    fs::create_directories(staged, failed);
    const std::string recipe = formatStoredRecipe(doc, staged.string());

    // Which source material this archive owes is asked of the recipe's own text, not of the
    // session: an id kept verbatim because this build could not honour the statement around it
    // still names material the receiver needs (Amendment 19 Clause 19.1).
    const fs::path recipeFile = scratch.path() / sealedArchiveRecipeEntry;
    {
        Base::ofstream out(Base::FileInfo(recipeFile.string()), std::ios::out | std::ios::binary);
        out << recipe;
        if (out.fail()) {
            throw Base::FileException("Could not write the recipe for", archivePath);
        }
    }
    const std::set<std::string> named = sourceMaterialNamedBy(recipeFile.string());

    const fs::path project = doc.assetDirectory().empty() ? fs::path {}
                                                          : fs::path(doc.assetDirectory());
    std::vector<std::pair<std::string, fs::path>> carry;
    for (const std::string& id : named) {
        const fs::path found = locateAsset(id, staged, project);
        if (found.empty()) {
            // Refused whole. A short archive that looks complete is worse than no archive: the
            // receiver cannot tell that what they are missing was ever there.
            throw Base::RuntimeError("'" + archivePath + "' was not written: the document names "
                                     "source material this project does not hold (" + id
                                     + "), and a sealed archive that names what it does not carry "
                                       "is not self-contained.");
        }
        carry.emplace_back(id, found);
    }

    // Written under a name of its own and moved into place once whole, so a failure leaves no
    // file for someone to hand over.
    const fs::path pending = fs::path(archivePath + ".writing");
    {
        Base::ZipWriter zip(pending.string().c_str());
        zip.putNextEntry(sealedArchiveRecipeEntry);
        zip.Stream().write(recipe.data(), static_cast<std::streamsize>(recipe.size()));

        for (const auto& [id, folder] : carry) {
            for (const auto& entry : fs::directory_iterator(folder, failed)) {
                if (!entry.is_regular_file(failed)) {
                    continue;
                }
                const std::string within = std::string(sealedArchiveAssetFolder) + "/" + id + "/"
                    + entry.path().filename().string();
                const std::string bytes = readWholeFile(entry.path());
                zip.putNextEntry(within.c_str());
                zip.Stream().write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
        }
        // The rebuild store is not reached from here at all. What the recipe produces is no part
        // of the record (Clause 18.1) and is disposable by definition (Clause 18.5), so an archive
        // carrying it would hand the receiver results they must not trust.
    }

    fs::rename(pending, fs::path(archivePath), failed);
    if (failed) {
        std::error_code ignored;
        fs::remove(pending, ignored);
        throw Base::FileException("Could not put the archive in place", archivePath);
    }

    // Carried, and said. A statement this build could not honour travels verbatim inside the
    // recipe; who is being handed one is the caller's to report, and a receiver rebuilding from
    // an archive is exactly who needs to know.
    std::set<std::string> unhonoured;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj->holdsUnhonouredStatement() && obj->getNameInDocument() != nullptr) {
            unhonoured.insert(obj->getNameInDocument());
        }
    }
    return unhonoured;
}

bool App::holdsSealedRecipe(const std::string& archivePath)
{
    try {
        zipios::ZipFile archive(archivePath);
        return archive.getEntry(sealedArchiveRecipeEntry, zipios::FileCollection::MATCH) != nullptr;
    }
    catch (const std::exception&) {
        return false;
    }
}

std::string App::unpackSealedArchive(const std::string& archivePath, const std::string& directory)
{
    zipios::ZipFile archive(archivePath);
    std::error_code failed;

    std::string recipe;
    for (const zipios::ConstEntryPointer& entry : archive.entries()) {
        if (!entry || !entry->isValid() || entry->isDirectory()) {
            continue;
        }
        const std::string name = entry->getName();
        std::istream* member = archive.getInputStream(entry);
        if (member == nullptr) {
            throw Base::FileException("Could not read a member of the archive", archivePath);
        }
        const std::string bytes {std::istreambuf_iterator<char>(*member),
                                 std::istreambuf_iterator<char>()};
        delete member;

        // Laid out exactly as a project folder lays it out, so the recipe reader resolves source
        // material by the one path that reads a project rather than by a second one for archives.
        const fs::path target = fs::path(directory) / name;
        fs::create_directories(target.parent_path(), failed);
        Base::ofstream out(Base::FileInfo(target.string()), std::ios::out | std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (out.fail()) {
            throw Base::FileException("Could not unpack the archive into", directory);
        }
        if (name == sealedArchiveRecipeEntry) {
            recipe = target.string();
        }
    }

    if (recipe.empty()) {
        throw Base::RuntimeError("'" + archivePath + "' holds no recipe.");
    }
    return recipe;
}
