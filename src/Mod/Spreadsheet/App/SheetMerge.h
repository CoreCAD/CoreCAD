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


#ifndef SPREADSHEET_SHEETMERGE_H
#define SPREADSHEET_SHEETMERGE_H

#include <Mod/Spreadsheet/SpreadsheetGlobal.h>

namespace Spreadsheet
{

/** Merges two versions of a sheet cell by cell (prototype, not merged).
 *
 *  A cell is matched across versions by its row's id and its column's id, never by address.
 *  Where one side inserted or removed rows or columns, the merged sheet keeps that side's
 *  layout and places the other side's cells into it.
 */
SpreadsheetExport void registerSheetMerge();

}  // namespace Spreadsheet

#endif  // SPREADSHEET_SHEETMERGE_H
