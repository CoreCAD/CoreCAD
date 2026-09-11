// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Joao Matos
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#pragma once

#include "ExportInfo.h"

#include <string>

namespace App
{
class Document;

/** Write a snapshot of `doc` into its transient directory, as the record's own writer writes it.
 *
 * A snapshot can become the record: recovery binds what it reads to the original document's path,
 * and an ordinary save then writes it there. So it is written AS the document, in the record's own
 * form -- anything else is a second record, and every duty the law places on the record would
 * reach only the first (Amendment 19 Clause 19.5).
 */
AppExport bool writeRecoverySnapshotToTransientDir(const Document& doc);

/// Where a snapshot of `doc` is written, and where recovery reads it from.
AppExport std::string recoverySnapshotPath(const Document& doc);
}  // namespace App
