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

#include <set>
#include <string>

namespace App
{

class Document;
class DocumentObject;

/** What a rebuild would have produced, kept so that opening a document does not have to produce
 *  it again.
 *
 *  The document file is the recipe: it carries the steps and never the solid they make, so
 *  opening one means building it, and a part with a hundred features pays for all hundred every
 *  time it is opened. This is the answer to that, and its whole difficulty is not storing a
 *  solid — it is knowing when a stored solid has stopped being true.
 *
 *  **Nothing here counts.** There is no version number on a cached result and none in the recipe:
 *  a number that counts up has to be agreed between everyone who holds a copy, and two people
 *  editing the same part on two machines would disagree about it immediately. Instead an entry is
 *  named by a digest of what produced it — the object's own block of the recipe, plus the names
 *  of the entries its inputs were built from, all the way down. Two people who have the same text
 *  arrive at the same name without having spoken, one changed number renames only the features
 *  that stand on it, and a name can never be stale, because different content is a different
 *  name rather than the same name holding something else.
 *
 *  The one thing the recipe text cannot account for is this program: a solid built by an older
 *  build of Cruth should not be handed back by a newer one that would now build it differently.
 *  That is where a version legitimately belongs, and it is confined to the cache — which is
 *  private, local and disposable — so it never reaches the file people share and has nothing to
 *  conflict with. See `buildStamp` in the implementation for what it covers and what it does not.
 *
 *  Everything here is disposable by design. Deleting the cache costs one rebuild and loses
 *  nothing that was designed, which is precisely what makes it safe for version control to
 *  ignore.
 */

/** The name of the cache entry that would hold this object's built state.
 *
 *  Empty when the object cannot be identified this way — it stands, directly or through what it
 *  is built on, in a cycle of references, and a cycle has no bottom to start the digest from.
 *  Such an object is simply always rebuilt.
 */
AppExport std::string builtGeometryKey(const DocumentObject& obj,
                                       const std::string& assetDirectory = {});

/** Keep every object's built state in `cacheDirectory`, each under its own key.
 *
 *  Writing is content-addressed: an entry that is already there is already right, so a save that
 *  changed one feature writes one feature. Nothing is removed — an entry no longer named by any
 *  object is dead weight, and collecting dead weight is an operation on a project rather than
 *  something a single save should decide.
 */
AppExport void storeBuiltGeometry(const Document& doc,
                                  const std::string& cacheDirectory,
                                  const std::string& assetDirectory = {});

/** Give back every object whose built state the cache still holds, and answer with those it
 *  could not — the ones that have to be built.
 *
 *  An object restored this way is left untouched, so the recompute that follows passes over it.
 *  A miss is ordinary and costs only the rebuild it was going to cost anyway; there is no failure
 *  mode here short of the cache handing back something wrong, which is what the key exists to
 *  prevent.
 */
AppExport std::set<DocumentObject*> restoreBuiltGeometry(Document& doc,
                                                         const std::string& cacheDirectory,
                                                         const std::string& assetDirectory = {});

}  // namespace App
