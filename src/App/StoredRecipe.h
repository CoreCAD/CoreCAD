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
#include <vector>

namespace App
{

class Document;
class DocumentObject;
class Property;
class PropertyContainer;

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
 *  The chosen appearance of each object -- the colours, the draw style, the transparency a person
 *  picked -- rides in a `<Display>` block inside the object, because a person chose it and nothing
 *  in the document produces it. Where that state lives is a question only the view layer can
 *  answer, so the file asks (`DisplayStateProvider`): a headless session gets no answer, writes no
 *  block, and a reader of such a file finds none. View state proper -- the camera, tree expansion,
 *  the thumbnail -- is not here and does not belong here: nobody authored it as part of the part.
 *
 *  Derived output is deliberately absent: geometry a feature builds is rebuilt from the recipe,
 *  never diffed (§10.4); geometry the document was merely handed — an import, a mesh, a point
 *  cloud — is authored content and is carried. What it can NOT yet carry is stated in the file
 *  itself: a value bulky enough that its property writes a side file is named as unrecorded.
 *
 *  `assetDirectory` is the project's source store — where a value goes when the property that
 *  holds it cannot say it inline, which is the case for anything that keeps its value in a file
 *  of its own: a handed-in solid, a mesh, a per-face colour array. The recipe names such a value
 *  by the content it holds rather than describing it in full, which keeps the recipe a file a
 *  person reads, stores one imported body once however many parts use it, and collapses a
 *  hundred objects wearing the same material to one entry. Left empty — a document with no
 *  folder yet — such a value is named as a gap rather than dropped.
 */
AppExport std::string formatStoredRecipe(const Document& doc,
                                         const std::string& assetDirectory = {});

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
AppExport void restoreStoredRecipe(Document& doc,
                                   std::istream& source,
                                   bool finish = true,
                                   const std::string& assetDirectory = {});

/** One object's block of the stored recipe, byte for byte as `formatStoredRecipe` writes it.
 *
 *  This exists so that a rebuild can be identified by the text that describes it. An object's
 *  block is everything the file says the object was authored to be, so two blocks that read the
 *  same describe the same object; and because the block is generated by the one writer, it cannot
 *  drift out of step with the file on disk the way a hand-written summary of "what matters" would.
 *
 *  The one thing deliberately left out is the object's chosen appearance, which the whole-document
 *  form carries. A colour is authored content and belongs in the file, but it does not change what
 *  a rebuild produces -- and a key that included it would throw away every cached solid in the
 *  document the moment somebody picked a different colour.
 */
AppExport std::string formatStoredRecipeObject(const DocumentObject& obj,
                                               const std::string& assetDirectory = {});

/** The properties the document archive keeps and the recipe deliberately does not.
 *
 *  Exactly the state a recompute produces: geometry a feature builds, and the other results an
 *  object publishes for reading rather than for editing. The recipe leaves these out because it
 *  is the source and they are what the source makes, so this list is the other half of the same
 *  ruling -- what a rebuild would have to produce, and therefore the only thing worth keeping to
 *  avoid one.
 *
 *  What it deliberately does NOT include is a value the recipe could not carry (`<Unrecorded>`).
 *  That is a gap in the record, not a rebuildable result, and putting authored content anywhere
 *  disposable would make a design deletable.
 */
AppExport std::vector<Property*> rebuiltProperties(const PropertyContainer& owner);

}  // namespace App
