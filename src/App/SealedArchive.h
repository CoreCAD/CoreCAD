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

#include <set>
#include <string>

namespace App
{

class Document;

/** A document's stored form is one readable file; a sealed archive is how it is handed over.
 *
 *  Amendment 18 Clause 18.1 rules that the record is the recipe together with the source material
 *  the recipe names, and that a sealed archive remains a supported export and import -- for a
 *  release, a transfer, or a records system, where being self-contained is the requirement.
 *  Handing over the record itself means handing over the recipe plus the right files out of a
 *  folder shared with sibling parts, and being right about which ones. The archive answers that
 *  question once, in one file.
 *
 *  An export is a **rendering** of a document and never the document's record, and three things
 *  follow from that, all of them decided here:
 *
 *  1. It carries **no rebuilt content**. What the recipe produces is no part of the record
 *     (Clause 18.1) and the rebuild store is disposable by definition (Clause 18.5), so shipping
 *     it would ship something the receiver must not trust.
 *  2. It carries **everything the recipe names**, with no exception for material shared between
 *     parts. Self-containedness is the whole reason the form exists; a receiver who cannot
 *     rebuild has been handed a record with a hole in it. Where a named piece cannot be written,
 *     the export fails as a whole and leaves no file behind -- a short archive that looks complete
 *     is worse than no archive.
 *  3. It carries a statement this build could not honour **exactly as the file words it**, rather
 *     than refusing over it. The recipe inside the archive is written by the one writer of Clause
 *     19.5, which already states such a statement verbatim (Clause 19.1); refusing would make the
 *     export say less than the record it renders. What it does instead is say so: the caller is
 *     told what is being handed on unhonoured, because a receiver rebuilding from an archive is
 *     exactly who needs to know.
 */

/// The name the recipe is stored under inside a sealed archive.
inline constexpr const char* sealedArchiveRecipeEntry = "recipe";

/// The folder inside a sealed archive holding the source material the recipe names.
inline constexpr const char* sealedArchiveAssetFolder = "assets";

/** Write `doc` and every piece of source material its recipe names into one sealed archive.
 *
 *  Throws Base::FileException where the archive cannot be written, and Base::RuntimeError where
 *  the recipe names source material this project does not hold. Either way no file is left at
 *  `archivePath`: the archive is written to one side and named only once it is whole.
 *
 *  Returns the ids of the statements the recipe carries that this build could not honour, so the
 *  caller can say what is being handed on. An empty set is the ordinary case.
 */
AppExport std::set<std::string> writeSealedArchive(const Document& doc,
                                                   const std::string& archivePath);

/// True where this file is a sealed archive holding a recipe, as opposed to a legacy container.
AppExport bool holdsSealedRecipe(const std::string& archivePath);

/** Unpack a sealed archive into `directory`, and hand back the path of the recipe inside it.
 *
 *  The source material lands in `directory/assets`, laid out exactly as a project folder lays it
 *  out, so the recipe reader resolves it by the one path that reads a project.
 */
AppExport std::string unpackSealedArchive(const std::string& archivePath,
                                          const std::string& directory);

}  // namespace App
