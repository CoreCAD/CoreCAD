// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
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

#include <iosfwd>
#include <string>

namespace App
{

class Document;

/** The authored content of a document in a form meant to be READ BACK.
 *
 *  The recipe text view (RecipeText.h) is written for a person: it rounds an outcome to four
 *  decimals, names a value without naming its kind, and never returns to the document. Those are
 *  the right choices for reading a change and the wrong ones for a file of record, so the stored
 *  form is a second rendering of the same content rather than a rename of the first — the two
 *  answer different questions and are allowed to disagree about precision.
 *
 *  Three properties are the point of it:
 *   - **Exact.** Each value is written by the property's own serializer, the same one the document
 *     archive uses, so there is one value dialect in the program and not a second that drifts.
 *   - **Keyed by durable identity.** An object is addressed by its Uid and written in Uid order,
 *     so the file does not depend on the order the objects were created in — which is what makes
 *     one added feature read as one added block rather than as a rewritten file.
 *   - **Honest.** What it does not store, it names (`<Unrecorded>`), so the file states its own
 *     gaps instead of quietly dropping content.
 *
 *  Derived output is deliberately absent: geometry a feature builds is rebuilt from the recipe,
 *  never diffed (§10.4); geometry the document was merely handed — an import, a mesh, a point
 *  cloud — is authored content and is carried. What it can NOT yet carry is stated in the file
 *  itself: a value bulky enough that its property writes a side file is named as unrecorded.
 */
AppExport std::string formatStoredRecipe(const Document& doc);

/** Rebuild a document's authored content from a stored recipe, into `doc`.
 *
 *  Objects are created with the type, in-document name and durable Uid the file names, then each
 *  stored property is restored by its own serializer. The caller recomputes: reading the source
 *  and rebuilding the output are separate steps, and keeping them separate is what lets a failed
 *  rebuild be reported rather than silently producing an empty document.
 *
 *  `finish` runs the document's own second pass, the one that lets formulas bind once every
 *  object exists. A document being opened does that pass itself, later and in dependency order
 *  across all the documents being opened together, so it asks for it to be left undone here.
 */
AppExport void restoreStoredRecipe(Document& doc, std::istream& source, bool finish = true);

}  // namespace App
