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


#pragma once

#include <FCGlobal.h>

#include <cstdint>
#include <string>
#include <vector>

namespace App
{

class Document;

/** Reading a document's past versions out of the git repository its file is kept in.
 *
 *  Versions are a file-system concern, not part of the document model: nothing here is known to
 *  a document, and a past version arrives the way any file does — as a stored recipe, read into
 *  a document of its own. History is read along first parents, so it is the line of versions a
 *  person saw on their branch.
 */

/// One commit in which the file's content changed.
struct FileVersion
{
    std::string commit;   ///< the full commit id
    std::string author;
    std::int64_t time {};  ///< seconds since the epoch
    std::string summary;  ///< the first line of the commit message
};

/// The versions of the file at `path`, newest first. Empty when the file is not in a git
/// repository or was never committed. A commit that left the file as it was is not a version.
AppExport std::vector<FileVersion> fileVersions(const std::string& path);

/// The file's text as committed at `revision` — a commit id, or any name git accepts such as
/// "HEAD". Throws when there is no such revision or the file is not in it.
AppExport std::string fileTextAt(const std::string& path, const std::string& revision);

/** The file as committed at `revision`, read into a new hidden, temporary document for reading
 *  and comparing. The caller closes it.
 *
 *  It gets a document identity of its own: links between documents find their target by that
 *  identity, and must keep finding the open document rather than its past version. Its objects
 *  keep theirs, which is what lets a comparison line them up with the open one.
 */
AppExport Document* openFileVersion(const std::string& path, const std::string& revision);

}  // namespace App
