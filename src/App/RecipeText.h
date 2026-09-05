// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <FCGlobal.h>

#include <string>

namespace App
{

class Document;

/** A readable rendering of a document's authored recipe — the steps a person took, laid out
 *  one fact per line so a line-based difference tool reports "this hole moved" rather than
 *  "the file changed".
 *
 *  This is a **view**, not a stored form: it is written on save, never read back, and the
 *  document archive remains the file of record (§10.4 welcomes a human-readable diff view and
 *  holds the canonical recipe serialization deliberately open). Nothing here parses, so there
 *  is no format version to honour and no escaping rule to get right — the layout can change
 *  whenever a better one is found.
 *
 *  Two properties are deliberate. It carries **no clock and no build stamp**, so re-saving an
 *  unchanged document reproduces it byte for byte and version control reports nothing. And it
 *  prints each object's **unrecorded properties** (App::unrecordedProperties) rather than
 *  hiding them, because what the recipe layer cannot yet say is exactly what decides whether it
 *  could ever become a stored form.
 *
 *  Values are the recipe layer's own canonical strings, not a prettier re-rendering: one value
 *  language, so what the view shows and what a merge compares can never disagree.
 */
AppExport std::string formatDocumentRecipeText(const Document& doc);

/// Write formatDocumentRecipeText beside a saved document as `<documentPath>.recipe`. Returns
/// false if the file could not be opened; a recipe view is a convenience and must never be able
/// to fail a save.
AppExport bool writeDocumentRecipeText(const Document& doc, const char* documentPath);

}  // namespace App
