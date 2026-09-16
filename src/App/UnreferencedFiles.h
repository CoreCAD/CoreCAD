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
#include <utility>
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
struct ProjectSurvey
{
    /// The recipes that were read, so a person can see what the answer is based on.
    std::vector<std::string> recipesRead;
    /// The entries no recipe in the folder names, largest first.
    std::vector<UnreferencedFile> unreferenced;
    /// What those entries hold together.
    std::uintmax_t bytes {};
};

/** Survey one project folder: which source material nothing in it names any more.
 *
 *  Reads every native document in the folder. Throws where any one of them cannot be read,
 *  rather than reporting its source material as unreferenced.
 */
AppExport ProjectSurvey surveyProjectSourceMaterial(const std::string& projectFolder);

/** Survey one project folder: which kept rebuild results nothing in it names any more.
 *
 *  The other half of the same question, and deliberately not the same answer. What is kept under
 *  `.cruth/` is what a rebuild would have produced, named by a digest of the recipe text that
 *  produced it -- so editing one dimension does not overwrite the old entry, it writes a new one
 *  beside it, and the old one is dead weight from that moment on. Removing one costs a rebuild;
 *  removing source material can cost a design. They are two operations, not one.
 *
 *  **These names are derived, so they cannot be read off the file.** Slice one could read a
 *  recipe as the text it is, because an asset id is stated there in full. A rebuild entry's name
 *  is computed from the recipe and from every recipe it stands on, by the same code that wrote
 *  it, so the only honest way to ask what names an entry is to open the document and compute it.
 *
 *  That is why this refuses more than the other half does:
 *
 *  - A document already open is refused, naming it. What is on disk was named by the last save;
 *    what is in memory may be neither saved nor the same, and the answer would be about neither.
 *  - A document holding a statement it could not honour is refused, naming it and the reason.
 *    What could not be read may name anything, so nothing here may conclude from an absence of
 *    references (Amendment 19 Clause 19.3).
 *  - A document that cannot be opened at all stops the survey, as in the other half.
 *
 *  Removes nothing, for the same reason: what to do about what this finds is a person's call.
 */
AppExport ProjectSurvey surveyProjectRebuildStore(const std::string& projectFolder);

/// What a discard did, and what it declined to do.
struct Discarded
{
    /// What was removed, and what it held.
    std::vector<UnreferencedFile> removed;
    /// What those held together.
    std::uintmax_t bytes {};
    /// What was asked for and left alone, each with the reason, because a person who asked for
    /// something and did not get it is owed the reason rather than a smaller number.
    std::vector<std::pair<std::string, std::string>> kept;
};

/** Remove the kept rebuild results nothing in this project names any more.
 *
 *  **This is the safe half, and it is safe for one reason: an entry here costs a rebuild.**
 *  Nothing in it was designed. The survey is run again from scratch inside this call and only
 *  what it names now is removed, so a list a person was looking at while something else changed
 *  cannot be acted on. It inherits every refusal the survey makes -- an open document, a document
 *  holding a statement it could not honour, a document that will not open -- which is what stops
 *  this from emptying the store of a part somebody is editing.
 *
 *  Takes no list. That is the difference from the other half rather than an omission: there is
 *  nothing here to choose between, because every answer costs the same and the cost is a rebuild.
 */
AppExport Discarded discardUnreferencedRebuildResults(const std::string& projectFolder);

/** Remove named source material this project holds and nothing in it names any more.
 *
 *  **This is the other half, and it is deliberately not the same act.** What is in `assets/` is
 *  authored content -- an imported body, a scanned mesh, something a person was handed and cannot
 *  produce again -- and a part that quietly loses the body it was built from looks exactly like a
 *  part that never had one. So there is no sweep here and no "remove what you found": a person
 *  names each file, and nothing else is touched.
 *
 *  Each named file is removed only if the survey, run again inside this call, still says nothing
 *  names it. Anything else is left alone and reported with its reason -- named again since, not
 *  there, or never part of this project. A path this cannot confirm for itself is not removed on
 *  the strength of having been asked.
 */
AppExport Discarded discardSourceMaterial(const std::string& projectFolder,
                                          const std::vector<std::string>& named);

}  // namespace App
