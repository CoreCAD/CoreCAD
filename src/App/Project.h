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

#include <string>
#include <vector>

namespace App
{

class Document;

/** Projects: a folder kept in version control, holding its documents and a project file.
 *
 *  A document is in the project whose file sits in its own folder or the nearest folder above
 *  it, inside the same repository; a project is what gives a document its history. The project
 *  file names the project and lists the documents it uses from outside its folder, each held at
 *  a version. Nothing here is known to a document: a project tracks files, the files do not know
 *  their project.
 */

/// The extension of a project file, with its dot.
constexpr const char* projectFileExtension = ".cproject";

/// The project file of the project the document at `path` is in, or empty when it is in none.
AppExport std::string projectFileOf(const std::string& path);

/// The project's name, as its file states it.
AppExport std::string projectName(const std::string& projectFile);

/** Make a project named `name` in `folder`: the folder is created when missing, its history is
 *  started unless it is already kept in one, and the project file is written there. Returns the
 *  project file. Throws when the name is empty or the folder already holds a project file.
 */
AppExport std::string createProject(const std::string& folder, const std::string& name);

/** Move a saved document into the project's folder, keeping its file name. The document is
 *  saved there with its source material, then its old file is removed; the source material it
 *  leaves behind may still be named by other documents, so it stays. Returns the new path.
 *
 *  Does nothing when the document is already in that folder. Throws when it was never saved or
 *  a file of that name is already there.
 */
AppExport std::string moveIntoProject(Document& doc, const std::string& projectFile);

/// The documents in the project's folder that are in this project, by path. A folder holding
/// a project or a repository of its own is another project's, and source material is not a
/// document.
AppExport std::vector<std::string> projectDocuments(const std::string& projectFile);

/// A document the project uses from outside its folder.
struct OutsideDocument
{
    std::string path;     ///< absolute
    std::string uuid;     ///< the document's identity, empty when its file does not state one
    std::string version;  ///< the commit it is held at, empty when it is not versioned
};

/// The documents the project file lists from outside the project, by path.
AppExport std::vector<OutsideDocument> outsideDocuments(const std::string& projectFile);

/** List the document at `path` in the project where it is; it is never moved. It is held at its
 *  newest committed version, or at none when it is not versioned. Throws when the file is
 *  missing, is not a document, is in this project already, or is listed already.
 */
AppExport OutsideDocument addOutsideDocument(const std::string& projectFile,
                                             const std::string& path);

/// The commit of a newer version of an outside document than the one it is held at, or empty
/// when there is none or it is not versioned.
AppExport std::string newerVersionOf(const OutsideDocument& outside);

/// Hold the outside document at `path` at its newest version; returns that commit. Throws when
/// the project does not list it.
AppExport std::string updateOutsideDocument(const std::string& projectFile,
                                            const std::string& path);

}  // namespace App
