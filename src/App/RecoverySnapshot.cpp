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

#include <sstream>
#include <utility>

#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Writer.h>
#include <Base/XMLTools.h>

#include "Application.h"
#include "Document.h"
#include "RecoverySnapshot.h"
#include "StoredRecipe.h"

namespace
{

/// Where a snapshot's handed-in geometry goes: beside the snapshot, under the name the recipe
/// reader looks for when it opens a file from that directory.
std::string recoveryAssetsFor(const App::Document& doc)
{
    return std::string(doc.TransientDir.getValue()) + "/assets";
}

void writeRecoveryMetadataFile(const App::Document& doc)
{
    std::string fileName = doc.TransientDir.getValue();
    fileName += "/fc_recovery_file.xml";
    const auto escapedLabel = XMLTools::escapeXml(doc.Label.getValue());
    const auto escapedFileName = XMLTools::escapeXml(doc.FileName.getValue());

    Base::FileInfo fileInfo(fileName);
    Base::ofstream file(fileInfo, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw Base::FileException("Failed to open auto-recovery metadata file", fileInfo);
    }

    file << "<?xml version='1.0' encoding='utf-8'?>\n"
         << "<AutoRecovery SchemaVersion=\"1\">\n"
         << "  <Status>Created</Status>\n"
         << "  <Label>" << escapedLabel << "</Label>\n"
         << "  <FileName>" << escapedFileName << "</FileName>\n"
         << "</AutoRecovery>\n";
}

}  // namespace

namespace App
{

std::string recoverySnapshotPath(const Document& doc)
{
    return std::string(doc.TransientDir.getValue()) + "/fc_recovery_file.cpart";
}

bool writeRecoverySnapshotToTransientDir(const Document& doc)
{
    if (!doc.canWriteRecoverySnapshot()) {
        std::stringstream message;
        message << "Document '" << doc.Label.getValue()
                << "' is not in a stable App state for recovery write";
        throw Base::RuntimeError(message.str().c_str());
    }

    writeRecoveryMetadataFile(doc);

    // Cruth (Amendment 19 Clause 19.5): a snapshot can become the record -- recovery binds what it
    // reads to the original document's path, and an ordinary save then writes it there. So it is
    // written AS the record, through the record's own writer. It used to be written as the archive
    // Amendment 18 replaced, which knows nothing of a kept statement or a remembered name, and a
    // crash cycle therefore erased what the record's writer had carefully kept.
    const std::string path = recoverySnapshotPath(doc);
    const std::string assets = recoveryAssetsFor(doc);
    Base::FileInfo dir(assets);
    if (!dir.exists() && !dir.createDirectory()) {
        throw Base::FileException("Failed to create auto-recovery directory", dir);
    }

    Base::FileInfo fileInfo(path);
    Base::ofstream file(fileInfo, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw Base::FileException("Failed to open auto-recovery file", fileInfo);
    }
    // A snapshot may hold more than the record -- material beside it, so that recovering is cheap
    // -- but never the record in a second form.
    file << formatStoredRecipe(doc, assets);
    file.close();

    if (!Base::FileInfo(path).exists()) {
        throw Base::FileException("Failed to write auto-recovery file", fileInfo);
    }
    return true;
}

}  // namespace App
