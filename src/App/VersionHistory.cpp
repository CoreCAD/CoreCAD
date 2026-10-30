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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#endif

#include <git2.h>

#include <Base/Exception.h>
#include <Base/Uuid.h>

#include "VersionHistory.h"

#include "Application.h"
#include "Document.h"
#include "StoredRecipe.h"
#include "UnreferencedFiles.h"

using namespace App;
namespace fs = std::filesystem;

namespace
{

/// libgit2 counts its users; each call into this file is one for its duration.
struct GitSession
{
    GitSession()
    {
        git_libgit2_init();
    }
    ~GitSession()
    {
        git_libgit2_shutdown();
    }
    GitSession(const GitSession&) = delete;
    GitSession& operator=(const GitSession&) = delete;
};

template<class T, void (*Free)(T*)>
struct Freer
{
    void operator()(T* object) const
    {
        Free(object);
    }
};

using Repository = std::unique_ptr<git_repository, Freer<git_repository, git_repository_free>>;
using Commit = std::unique_ptr<git_commit, Freer<git_commit, git_commit_free>>;
using Tree = std::unique_ptr<git_tree, Freer<git_tree, git_tree_free>>;
using TreeEntry = std::unique_ptr<git_tree_entry, Freer<git_tree_entry, git_tree_entry_free>>;
using RevWalk = std::unique_ptr<git_revwalk, Freer<git_revwalk, git_revwalk_free>>;
using Object = std::unique_ptr<git_object, Freer<git_object, git_object_free>>;
using Blob = std::unique_ptr<git_blob, Freer<git_blob, git_blob_free>>;
using Index = std::unique_ptr<git_index, Freer<git_index, git_index_free>>;
using Signature = std::unique_ptr<git_signature, Freer<git_signature, git_signature_free>>;

[[noreturn]] void fail(const std::string& what)
{
    const git_error* error = git_error_last();
    std::string message = what;
    if (error != nullptr && error->message != nullptr) {
        message += ": " + std::string(error->message);
    }
    throw Base::RuntimeError(message);
}

/// The repository holding the file, and the file's path inside it as git names it.
struct Located
{
    Repository repository;
    std::string pathInRepository;
};

std::optional<Located> locate(const std::string& path)
{
    const fs::path file = fs::weakly_canonical(fs::absolute(fs::path(path)));
    git_repository* raw = nullptr;
    if (git_repository_open_ext(&raw, file.parent_path().string().c_str(), 0, nullptr) != 0) {
        return std::nullopt;
    }
    Repository repository(raw);
    const char* workdir = git_repository_workdir(raw);
    if (workdir == nullptr) {
        return std::nullopt;
    }
    const fs::path inside = file.lexically_relative(fs::weakly_canonical(fs::path(workdir)));
    if (inside.empty() || *inside.begin() == "..") {
        return std::nullopt;
    }
    return Located {std::move(repository), inside.generic_string()};
}

std::optional<git_oid> fileIdAt(git_commit* commit, const std::string& pathInRepository)
{
    git_tree* rawTree = nullptr;
    if (git_commit_tree(&rawTree, commit) != 0) {
        fail("Could not read a commit's files");
    }
    const Tree tree(rawTree);
    git_tree_entry* rawEntry = nullptr;
    if (git_tree_entry_bypath(&rawEntry, rawTree, pathInRepository.c_str()) != 0) {
        return std::nullopt;
    }
    const TreeEntry entry(rawEntry);
    if (git_tree_entry_type(rawEntry) != GIT_OBJECT_BLOB) {
        return std::nullopt;
    }
    return *git_tree_entry_id(rawEntry);
}

std::string commitId(const git_oid& id)
{
    char text[GIT_OID_MAX_HEXSIZE + 1] {};
    git_oid_tostr(text, sizeof(text), &id);
    return text;
}

/// The document file and every file of the source material it names, as paths in the repository.
std::vector<std::string> filesOfDocument(const std::string& path, const Located& located)
{
    std::vector<std::string> files {located.pathInRepository};
    const fs::path workdir =
        fs::weakly_canonical(fs::path(git_repository_workdir(located.repository.get())));
    const fs::path assets(Document::assetDirectoryFor(path));
    std::error_code failed;
    for (const std::string& id : sourceMaterialNamedBy(path)) {
        // Material the folder does not hold is already missing; the version records the folder
        // as it is, and the document says what it lacks when it is opened.
        const fs::path folder = fs::weakly_canonical(assets / id);
        for (const auto& entry : fs::recursive_directory_iterator(folder, failed)) {
            if (!entry.is_regular_file(failed)) {
                continue;
            }
            const fs::path inside = entry.path().lexically_relative(workdir);
            if (!inside.empty() && *inside.begin() != "..") {
                files.push_back(inside.generic_string());
            }
        }
    }
    return files;
}

Signature author(git_repository* repository)
{
    git_signature* raw = nullptr;
    if (git_signature_default(&raw, repository) == 0) {
        return Signature(raw);
    }
    const char* login = std::getenv("USER");
    if (login == nullptr || *login == '\0') {
        login = std::getenv("USERNAME");
    }
    if (git_signature_now(&raw, login != nullptr && *login != '\0' ? login : "Unknown", "") != 0) {
        fail("Could not name the author");
    }
    return Signature(raw);
}

}  // namespace

std::vector<FileVersion> App::fileVersions(const std::string& path)
{
    const GitSession session;
    std::optional<Located> located = locate(path);
    if (!located) {
        return {};
    }
    git_repository* repository = located->repository.get();

    git_revwalk* rawWalk = nullptr;
    if (git_revwalk_new(&rawWalk, repository) != 0) {
        fail("Could not walk the history");
    }
    const RevWalk walk(rawWalk);
    git_revwalk_sorting(rawWalk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    if (git_revwalk_push_head(rawWalk) != 0) {
        return {};  // a repository with nothing committed yet
    }
    git_revwalk_simplify_first_parent(rawWalk);

    std::vector<FileVersion> versions;
    git_oid id;
    while (git_revwalk_next(&id, rawWalk) == 0) {
        git_commit* rawCommit = nullptr;
        if (git_commit_lookup(&rawCommit, repository, &id) != 0) {
            fail("Could not read a commit");
        }
        const Commit commit(rawCommit);

        const std::optional<git_oid> here = fileIdAt(rawCommit, located->pathInRepository);
        if (!here) {
            continue;
        }
        bool changed = true;
        git_commit* rawParent = nullptr;
        if (git_commit_parentcount(rawCommit) > 0
            && git_commit_parent(&rawParent, rawCommit, 0) == 0) {
            const Commit parent(rawParent);
            const std::optional<git_oid> before = fileIdAt(rawParent, located->pathInRepository);
            changed = !before || git_oid_equal(&*here, &*before) == 0;
        }
        if (!changed) {
            continue;
        }

        const git_signature* author = git_commit_author(rawCommit);
        const char* summary = git_commit_summary(rawCommit);
        versions.push_back({commitId(id),
                            author != nullptr && author->name != nullptr ? author->name : "",
                            static_cast<std::int64_t>(git_commit_time(rawCommit)),
                            summary != nullptr ? summary : ""});
    }
    return versions;
}

std::string App::fileTextAt(const std::string& path, const std::string& revision)
{
    const GitSession session;
    std::optional<Located> located = locate(path);
    if (!located) {
        throw Base::FileException("The file is not kept in a git repository", path.c_str());
    }
    git_repository* repository = located->repository.get();

    git_object* rawObject = nullptr;
    if (git_revparse_single(&rawObject, repository, revision.c_str()) != 0) {
        fail("No version '" + revision + "'");
    }
    const Object object(rawObject);
    git_object* rawPeeled = nullptr;
    if (git_object_peel(&rawPeeled, rawObject, GIT_OBJECT_COMMIT) != 0) {
        fail("'" + revision + "' is not a version");
    }
    const Commit commit(reinterpret_cast<git_commit*>(rawPeeled));

    const std::optional<git_oid> fileId =
        fileIdAt(reinterpret_cast<git_commit*>(rawPeeled), located->pathInRepository);
    if (!fileId) {
        throw Base::FileException(("The file is not in version '" + revision + "'").c_str(),
                                  path.c_str());
    }
    git_blob* rawBlob = nullptr;
    if (git_blob_lookup(&rawBlob, repository, &*fileId) != 0) {
        fail("Could not read the file at '" + revision + "'");
    }
    const Blob blob(rawBlob);
    return {static_cast<const char*>(git_blob_rawcontent(rawBlob)),
            static_cast<std::size_t>(git_blob_rawsize(rawBlob))};
}

Document* App::openFileVersion(const std::string& path, const std::string& revision)
{
    const std::string text = fileTextAt(path, revision);

    DocumentInitFlags flags;
    flags.createView = false;
    flags.temporary = true;
    const std::string name = GetApplication().getUniqueDocumentName("Version");
    const std::string label = fs::path(path).stem().string() + " at " + revision.substr(0, 8);
    Document* version = GetApplication().newDocument(name.c_str(), label.c_str(), flags);

    try {
        std::istringstream source(text);
        restoreStoredRecipe(*version, source, true, Document::assetDirectoryFor(path));
        version->Uid.setValue(Base::Uuid());
        version->Label.setValue(label);  // the file states its own, which would hide which this is
    }
    catch (...) {
        GetApplication().closeDocument(version->getName());
        throw;
    }
    return version;
}

bool App::isKeptInHistory(const std::string& path)
{
    const GitSession session;
    return locate(path).has_value();
}

std::string App::historyFolderOf(const std::string& path)
{
    const GitSession session;
    const std::optional<Located> located = locate(path);
    if (!located) {
        return {};
    }
    fs::path folder(git_repository_workdir(located->repository.get()));
    if (!folder.has_filename()) {
        folder = folder.parent_path();
    }
    return fs::weakly_canonical(folder).string();
}

void App::startHistory(const std::string& folder)
{
    const GitSession session;
    git_repository* raw = nullptr;
    if (git_repository_init(&raw, folder.c_str(), 0) != 0) {
        fail("Could not start history in '" + folder + "'");
    }
    const Repository repository(raw);

    // The rebuild store is disposable by definition; a version never holds it.
    const fs::path ignore = fs::path(folder) / ".gitignore";
    const std::string line = std::string(Document::cacheFolderName) + "/";
    std::string existing;
    {
        std::ifstream in(ignore);
        existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (("\n" + existing).find("\n" + line + "\n") == std::string::npos) {
        std::ofstream out(ignore, std::ios::app);
        if (!existing.empty() && existing.back() != '\n') {
            out << '\n';
        }
        out << line << '\n';
    }
}

std::string App::commitFile(const std::string& path, const std::string& message)
{
    const GitSession session;
    std::optional<Located> located = locate(path);
    if (!located) {
        throw Base::FileException("The file is not kept in a git repository", path.c_str());
    }
    git_repository* repository = located->repository.get();

    const std::size_t start = message.find_first_not_of(" \t\r\n");
    const std::string trimmed = start == std::string::npos ? std::string {} : message.substr(start);
    git_buf cleaned = GIT_BUF_INIT;
    git_message_prettify(&cleaned, trimmed.c_str(), 0, 0);
    const std::string text = cleaned.ptr != nullptr ? cleaned.ptr : "";
    git_buf_dispose(&cleaned);
    if (text.empty()) {
        throw Base::ValueError("A version needs a message saying what changed");
    }

    Commit parent;
    git_oid headId;
    if (git_reference_name_to_id(&headId, repository, "HEAD") == 0) {
        git_commit* raw = nullptr;
        if (git_commit_lookup(&raw, repository, &headId) != 0) {
            fail("Could not read the current version");
        }
        parent.reset(raw);
    }

    // Built from the current version rather than the repository's staging area, so only this
    // document's files differ from it.
    git_index* rawIndex = nullptr;
    if (git_index_new(&rawIndex) != 0) {
        fail("Could not prepare the version");
    }
    const Index index(rawIndex);
    std::optional<git_oid> parentTreeId;
    if (parent) {
        git_tree* rawTree = nullptr;
        if (git_commit_tree(&rawTree, parent.get()) != 0) {
            fail("Could not read the current version's files");
        }
        const Tree tree(rawTree);
        parentTreeId = *git_tree_id(rawTree);
        if (git_index_read_tree(rawIndex, rawTree) != 0) {
            fail("Could not read the current version's files");
        }
    }

    const std::vector<std::string> files = filesOfDocument(path, *located);
    for (const std::string& file : files) {
        git_index_entry entry {};
        if (git_blob_create_from_workdir(&entry.id, repository, file.c_str()) != 0) {
            fail("Could not read '" + file + "'");
        }
        entry.mode = GIT_FILEMODE_BLOB;
        entry.path = file.c_str();
        if (git_index_add(rawIndex, &entry) != 0) {
            fail("Could not add '" + file + "'");
        }
    }

    git_oid treeId;
    if (git_index_write_tree_to(&treeId, rawIndex, repository) != 0) {
        fail("Could not write the version's files");
    }
    if (parentTreeId && git_oid_equal(&treeId, &*parentTreeId) != 0) {
        throw Base::RuntimeError("Nothing has changed since the last version");
    }
    git_tree* rawTree = nullptr;
    if (git_tree_lookup(&rawTree, repository, &treeId) != 0) {
        fail("Could not write the version's files");
    }
    const Tree tree(rawTree);

    const Signature signature = author(repository);
    const git_commit* parents[] = {parent.get()};
    git_oid id;
    if (git_commit_create(&id,
                          repository,
                          "HEAD",
                          signature.get(),
                          signature.get(),
                          nullptr,
                          text.c_str(),
                          rawTree,
                          parent ? 1 : 0,
                          parents)
        != 0) {
        fail("Could not record the version");
    }

    // The staging area now matches the new version for these files, as after `git commit <file>`.
    git_index* rawStaging = nullptr;
    if (git_repository_index(&rawStaging, repository) == 0) {
        const Index staging(rawStaging);
        for (const std::string& file : files) {
            git_index_add_bypath(rawStaging, file.c_str());
        }
        git_index_write(rawStaging);
    }
    return commitId(id);
}
