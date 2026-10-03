// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <bitset>
#include <stack>
#include <deque>
#include <iostream>
#include <utility>
#include <set>
#include <memory>
#include <new>
#include <string>
#include <map>
#include <vector>
#include <list>
#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>

#include <boost/algorithm/string.hpp>
#include <boost/bimap.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>

#include <boost/regex.hpp>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <QCryptographicHash>
#include <QCoreApplication>

#include <FCConfig.h>

#include <App/DocumentPy.h>
#include <Base/Interpreter.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/TimeInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Profiler.h>
#include <Base/Tools.h>
#include <Base/XMLTools.h>
#include <Base/Uuid.h>
#include <Base/Sequencer.h>
#include <Base/Stream.h>

#include "Document.h"
#include "RecipeText.h"
#include "private/DocumentP.h"
#include "Application.h"
#include "AutoTransaction.h"
#include "BackupPolicy.h"
#include "ExpressionParser.h"
#include "GeoFeature.h"
#include "License.h"
#include "Link.h"
#include "Origin.h"
#include "MergeDocuments.h"
#include "StringHasher.h"
#include "GeometryCache.h"
#include "SealedArchive.h"
#include "StoredRecipe.h"
#include "Transactions.h"



FC_LOG_LEVEL_INIT("App", true, true, true)

using Base::Console;
using Base::streq;
using Base::Writer;
using namespace App;
using namespace boost;

#if FC_DEBUG
#define FC_LOGFEATUREUPDATE
#endif

namespace fs = std::filesystem;

void Document::applyDocumentType(const char* type)
{
    if (Base::Tools::isNullOrEmpty(type)) {
        return;
    }
    DocumentType.setValue(type);

    // A frame-owning document owns its world coordinate frame and mints it eagerly at
    // creation (Amendment 9). The objects inside — bodies in a Part, component instances
    // and grounded joints in an Assembly — only ever look this up, never create it. The
    // shared App::Origin carries the world-frame datum planes/axes. Two document types
    // carry the world-frame flag: Part and Assembly.
    const std::string& docType = DocumentType.getStrValue();
    if (docType == DocTypePart || docType == DocTypeAssembly) {
        auto* origin = addObject<App::Origin>("Origin");
        if (origin) {
            origin->Label.setValue("Origin");
        }
    }
}

bool Document::admitsContentScope(DocumentObject::ContentScope scope) const
{
    using CS = DocumentObject::ContentScope;
    // A Generic object carries no document-scoped nature and is admitted everywhere.
    if (scope == CS::Generic) {
        return true;
    }
    const std::string& type = DocumentType.getStrValue();
    // Untyped/legacy document: deliberately fluid, admits every kind (Clause 8.2).
    if (type.empty()) {
        return true;
    }
    // Per-type content scope, authored once here from the §7.1/§7.5 scopes.
    if (type == DocTypePart) {
        // Sketches, features, bodies (§7.1); a local Spreadsheet driver (§7.5). The
        // world frame and datums are Generic and pass above without a listing.
        return scope == CS::Sketch || scope == CS::Feature || scope == CS::Body
            || scope == CS::Spreadsheet;
    }
    if (type == DocTypeAssembly) {
        // Component instances and mates (§7.1); a local Spreadsheet driver (§7.5).
        return scope == CS::AssemblyItem || scope == CS::Spreadsheet;
    }
    if (type == DocTypeDrawing) {
        // Views, dimensions, annotations, sheets (§7.1); a local Spreadsheet (§7.5).
        return scope == CS::DrawingView || scope == CS::Spreadsheet;
    }
    if (type == DocTypeSpreadsheet) {
        // Sole content type (§7.1/§7.5).
        return scope == CS::Spreadsheet;
    }
    // A future type introduced under §7.1 that carries no policy row yet: stay fluid
    // rather than refuse everything (a document that can hold nothing is worse than
    // one that holds too much until its policy lands).
    return true;
}

std::string Document::fileExtensionForType(const char* type)
{
    if (!Base::Tools::isNullOrEmpty(type)) {
        if (boost::iequals(type, DocTypePart)) {
            return "cpart";
        }
        if (boost::iequals(type, DocTypeAssembly)) {
            return "cassembly";
        }
    }
    return "FCStd";
}

std::string Document::documentFileExtension() const
{
    return fileExtensionForType(DocumentType.getStrValue().c_str());
}

std::vector<std::string> Document::nativeFormatExtensions()
{
    // The universal container extension plus every type-specific extension derived from the
    // DocumentType markers (fileExtensionForType is the authority). A new document type gains
    // its extension there and is picked up here automatically -- the open/merge/import dialogs
    // and the cross-link resolver all read this one list rather than duplicating literals.
    std::vector<std::string> exts {"FCStd"};
    for (const char* type : {DocTypePart, DocTypeAssembly, DocTypeDrawing, DocTypeSpreadsheet}) {
        std::string ext = fileExtensionForType(type);
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) {
            exts.push_back(ext);
        }
    }
    return exts;
}

bool Document::isNativeFormatExtension(const char* ext)
{
    if (Base::Tools::isNullOrEmpty(ext)) {
        return false;
    }
    for (const std::string& known : nativeFormatExtensions()) {
        if (boost::iequals(ext, known)) {
            return true;
        }
    }
    return false;
}
