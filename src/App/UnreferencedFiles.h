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

#pragma once

#include <FCGlobal.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace App
{

/** What a project folder holds that nothing in it names any more.
 *
 *  A project accumulates files that nothing removes. Source material is stored under a digest of
 *  its own bytes, so replacing an import leaves the old body in `assets/` with nothing naming it,
 *  and `assets/` is visible and versioned -- the dead weight travels with the project.
 *
 *  **A save cannot decide this.** A save sees one document, and a file that document does not
 *  name may still be named by a sibling part it never opened: an imported body shared by five
 *  parts is stored once on purpose. Whether anything still names a file is a question about a
 *  **project**, and it needs every recipe in the folder read before anything is removed.
 *
 *  So this reports, and removes nothing. What it finds is **authored content** -- a part that
 *  quietly loses the body it was built from looks exactly like a part that never had one -- and
 *  under Amendment 19 Clause 19.4 only a person discards what a document holds.
 */

/** Every piece of source material one recipe names, by the id the project stores it under.
 *
 *  Reads the recipe as the text it is, rather than by opening the document: an id a build cannot
 *  interpret is still an id the file states, and a statement kept verbatim because this build
 *  could not honour it still names its source material (Amendment 19 Clause 19.1). Opening the
 *  document would collect what this session could make sense of, which is not the same question.
 *
 *  Throws where the recipe cannot be read, naming where it stopped. A recipe whose references
 *  could not be collected would have every one of them counted as absent.
 */
AppExport std::set<std::string> sourceMaterialNamedBy(const std::string& recipePath);

/// One file nothing names, and what it holds.
struct UnreferencedFile
{
    std::string path;
    std::uintmax_t bytes {};
};

/// What a survey of one project folder found.
struct SourceMaterialSurvey
{
    /// The recipes that were read, so a person can see what the answer is based on.
    std::vector<std::string> recipesRead;
    /// The entries in `assets/` that no recipe in the folder names, largest first.
    std::vector<UnreferencedFile> unreferenced;
    /// What those entries hold together.
    std::uintmax_t bytes {};
};

/** Survey one project folder: which source material nothing in it names any more.
 *
 *  Reads every native document in the folder. Throws where any one of them cannot be read,
 *  rather than reporting its source material as unreferenced.
 */
AppExport SourceMaterialSurvey surveyProjectSourceMaterial(const std::string& projectFolder);

}  // namespace App
