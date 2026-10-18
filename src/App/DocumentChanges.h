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

#include "Recipe.h"

namespace App
{

class Document;

/** One change between two versions of a document, as a history list shows it.
 *
 *  `section` is empty for a change to the object itself, or names the part of its interior the
 *  change is in ("geometry", "constraints"), in which case `change.id` is that entity's id. A
 *  rename is reported as a FieldChanged on "Label": a name is not part of the recipe, but a
 *  person reading a history expects to see it.
 */
struct DocumentChange
{
    std::string objectId;    ///< the object's durable Uid
    std::string label;       ///< its label in the newer version, or the older if it was removed
    std::string objectType;  ///< e.g. "PartDesign::Pad"
    std::string section;
    RecipeChange change;
    std::string text;        ///< the change in plain words, e.g. "Pad: Length 10 mm → 40 mm"
};

/** Every authored change from `before` to `after`, in the newer document's object order, with
 *  removed objects last. Objects are matched by durable Uid, so the two documents must be
 *  versions of one model, not two models built alike. Reads authored state only: neither
 *  document is recomputed, and a change to built geometry alone is not a change. A stored value
 *  that moved only because another edit did, as a sketch's solver moves line ends when a
 *  dimension changes, is not a change either.
 */
AppExport std::vector<DocumentChange> diffDocuments(const Document& before, const Document& after);

/// The changes as plain text, one per line, or a line saying there are none.
AppExport std::string formatDocumentChanges(const std::vector<DocumentChange>& changes);

}  // namespace App
