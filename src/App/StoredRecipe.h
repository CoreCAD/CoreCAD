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
#include <utility>
#include <vector>

namespace App
{

class Document;
class DocumentObject;
class Property;
class PropertyContainer;

/// How much of a document one rendering carries. A document of record carries all of it; a copy
/// carries only the picked objects and no `<Document>` block, since a document's own facts belong
/// to the document they came from. Same writer either way (Amendment 19 Clause 19.5).
struct RecipeScope
{
    /// The objects to state. Empty means every object the document holds.
    std::vector<const DocumentObject*> objects;
    /// Whether the document's own authored properties are part of this rendering.
    bool withDocumentProperties {true};
};

/// The version of the recipe format this build writes, and the only one it reads (Amendment 19
/// Clause 19.7). It names the encoding, never the program that wrote the file.
constexpr int storedRecipeFormat = 1;

/** The authored content of a document, in a form meant to be read back.
 *
 *  Unlike the readable view (RecipeText.h), it is exact: each value is written by the property's
 *  own serializer. Objects are keyed and ordered by Uid, so one added feature reads as one added
 *  block. What it cannot store it names in `<Unrecorded>`. A chosen appearance rides in a
 *  `<Display>` block; camera and tree state do not belong here.
 *
 *  Geometry a feature builds is left out and rebuilt (§10.4); geometry the document was handed is
 *  carried. A value the property cannot state inline goes to `assetDirectory`, the project's
 *  source store, named by its content; with no directory it is named as a gap.
 *
 *  Blocks this build could not construct are given back only by a whole-document rendering.
 */
AppExport std::string formatStoredRecipe(const Document& doc,
                                         const std::string& assetDirectory = {},
                                         const RecipeScope& scope = {});

/** Rebuild a document's authored content from a stored recipe, into `doc`.
 *
 *  Objects get the type, name and Uid the file states, then each property is restored by its own
 *  serializer. The caller recomputes, so a failed rebuild can be reported.
 *
 *  `finish` runs the document's second pass that binds formulas. A document being opened leaves
 *  it undone and runs it later, across every document opened together.
 */
AppExport void restoreStoredRecipe(Document& doc,
                                   std::istream& source,
                                   bool finish = true,
                                   const std::string& assetDirectory = {});

/// The terms on which a stored recipe arrives in a document: plain for an open into an empty
/// document, different for a copy landing beside existing content.
struct RecipeArrival
{
    /// Run the document's own second pass here -- see `restoreStoredRecipe`.
    bool finish {true};

    /// Where material the recipe names by content is read from.
    std::string assetDirectory;

    /// The document already holds content, so a stated name may be taken. The document renames
    /// the object and the reader keeps the pair, so formulas naming it follow.
    bool intoExistingContent {false};

    /// Filled with what arrived, in file order, each paired with the Uid the file gave it. A copy
    /// mints its own Uid (§10.7), so that is not necessarily the one it wears now.
    std::vector<std::pair<std::string, DocumentObject*>>* arrived {nullptr};
};

AppExport void restoreStoredRecipe(Document& doc, std::istream& source, const RecipeArrival& how);

/// One object's block, byte for byte as `formatStoredRecipe` writes it, so a rebuild can be keyed
/// by the text that describes it. Appearance is left out: a colour change must not throw away
/// cached solids.
AppExport std::string formatStoredRecipeObject(const DocumentObject& obj,
                                               const std::string& assetDirectory = {});

/// Whether the recipe carries this value, or a rebuild would produce it again. The one test for
/// "is this in the file".
AppExport bool theRecipeCarries(const Property& prop, const PropertyContainer& owner);

/// The properties the archive keeps and the recipe does not: what a recompute produces. Never a
/// value the recipe merely failed to carry -- that is a gap, not something disposable.
AppExport std::vector<Property*> rebuiltProperties(const PropertyContainer& owner);

}  // namespace App
