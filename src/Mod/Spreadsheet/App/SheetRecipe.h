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
 *  The unit a person chose to work a cell in is recorded with the content, not with the
 *  formatting: everywhere else in a document that choice is destroyed at entry, so a cell is one
 *  of the few places it survives, and it says something about the design rather than about how
 *  the sheet looks. Presentation proper (alignment, style, colours, spans) is emitted only where
 *  it was explicitly set, so an ordinary sheet stays quiet and a deliberately formatted one still
 *  says what was deliberate. None of it follows the reader: switching the application between
 *  metric and imperial leaves the file byte for byte the same.
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
