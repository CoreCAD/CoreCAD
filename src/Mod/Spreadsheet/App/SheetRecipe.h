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

#ifndef SPREADSHEET_SHEETRECIPE_H
#define SPREADSHEET_SHEETRECIPE_H

#include <App/RecipeDetail.h>
#include <Mod/Spreadsheet/SpreadsheetGlobal.h>

namespace App
{
class DocumentObject;
}

namespace Spreadsheet
{

/** A spreadsheet's contribution to a readable document recipe.
 *
 *  Every cell a sheet holds lives inside one property, and that property is link-shaped -- it
 *  carries the sheet's references to the rest of the document as well as its contents. The
 *  generic emitter therefore walked it as a link, took the references, and dropped the contents
 *  without even reporting a gap: a sheet of a hundred formulas wrote out as its name and nothing
 *  else. This says what is in there, the same way the sketch provider says what is in a sketch.
 *
 *  What is emitted is the authored text of each used cell -- the literal or the formula exactly
 *  as a person typed it, never the computed result, which is an outcome the sheet recomputes.
 *  Formatting is emitted where a person set it, and a sheet is the deliberate exception to the
 *  rule that presentation stays out of a recipe. On a solid a colour is how the thing is drawn;
 *  on a sheet it is a convention that carries information -- highlighting marks the inputs
 *  somebody is meant to change, or flags a value out of range -- authored once into the shared
 *  document and read by everyone who opens it. The display unit is NOT in that group: the
 *  architecture names "metric vs imperial annotations" as display-only, so it is left out on
 *  those grounds, and is not reported as a gap any more than a label is.
 *
 *  A note on identity, because it differs from every other provider: a cell has no durable id.
 *  It is addressed by position ("A1"), and inserting a row moves it. An alias is the closest
 *  thing a cell has to a durable name, which is why it is recorded beside the content. This is
 *  honest for a view a person reads; a merge over cells would need that question answered first.
 */
SpreadsheetExport App::RecipeDetail sheetRecipeDetail(const App::DocumentObject& obj);

/// Register sheetRecipeDetail against Spreadsheet::Sheet. Called once, at module init.
SpreadsheetExport void registerSheetRecipeDetail();

}  // namespace Spreadsheet

#endif  // SPREADSHEET_SHEETRECIPE_H
