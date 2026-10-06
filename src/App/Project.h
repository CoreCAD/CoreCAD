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

namespace App
{

class Document;

/** Projects: a folder kept in version control, holding its documents and a project file.
 *
 *  A document is in the project whose file sits in its own folder or the nearest folder above
 *  it, inside the same repository; a project is what gives a document its history. The project
 *  file names the project, and will list the documents it uses from outside its folder. Nothing
 *  here is known to a document: a project tracks files, the files do not know their project.
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

}  // namespace App
