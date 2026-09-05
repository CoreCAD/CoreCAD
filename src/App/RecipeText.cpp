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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <map>
# include <sstream>
# include <string>
# include <vector>
#endif

#include <Base/FileInfo.h>
#include <Base/Stream.h>

#include "RecipeText.h"

#include "Document.h"
#include "DocumentObject.h"
#include "ObjectRecipe.h"
#include "RecipeDetail.h"

using namespace App;

namespace
{

/// Every reference is printed by the target's in-document name, which is stable for the life of
/// the object (only its Label is user-editable), so a diff reads "-> Sketch" rather than a UUID.
/// The binding itself stays by durable id (§10.1) — this is a rendering, not a change of key.
std::map<std::string, std::string> nameByDurableId(const Document& doc)
{
    std::map<std::string, std::string> names;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr) {
            names[obj->Uid.getValueStr()] = obj->getNameInDocument();
        }
    }
    return names;
}

/// The block's first line: the object, its type, and its label when the user has renamed it
/// away from the in-document name. It is the only line starting in column 0, which is what
/// lets a difference tool be told to quote it above every change (see the .recipe rule in
/// .gitattributes).
std::string headingLine(const DocumentObject& obj, const std::string& type)
{
    std::string heading = std::string(obj.getNameInDocument()) + " (" + type + ")";

    const char* label = obj.Label.getValue();
    if (label != nullptr && *label != '\0' && std::string(label) != obj.getNameInDocument()) {
        heading += " \"" + std::string(label) + "\"";
    }

    return heading;
}


/// A sub-entity has no name a person could use, so it is identified by the head of its durable
/// tag -- short enough to read, long enough not to collide within one sketch.
std::string shortId(const std::string& id)
{
    constexpr std::string::size_type shown = 6;
    return id.size() > shown ? id.substr(0, shown) : id;
}

/// One line per sub-entity rather than one per field: a line's two ends move together, and
/// "this line moved" is what a person wants to read. The fields stay in name order, so the
/// line's shape is a property of the entity and not of the walk.
std::string detailLine(const RecipeNode& node)
{
    std::string line = "    " + node.type + " " + shortId(node.id);

    for (const auto& [name, value] : node.fields) {
        if (!value.empty()) {
            line += "  " + name + " = " + value;
        }
    }
    for (const RecipeRef& ref : node.refs) {
        line += "  -> " + shortId(ref.target);
        if (ref.pos != 0) {
            line += ":" + std::to_string(ref.pos);
        }
    }

    return line;
}

}  // namespace

std::string App::formatDocumentRecipeText(const Document& doc)
{
    std::ostringstream out;

    // A fixed preamble, and deliberately no clock, version or file name in it: a document
    // re-saved unchanged must reproduce this file byte for byte, or the view becomes the very
    // history noise the byte-stability work removed from the archive.
    out << "# Recipe view -- the authored steps in this document, one fact per line.\n"
        << "# Written on save from the document itself, never read back. Do not edit.\n";

    const std::map<std::string, std::string> names = nameByDurableId(doc);

    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }

        const RecipeNode node = emitObjectRecipe(*obj);
        out << "\n" << headingLine(*obj, node.type) << "\n";

        // Fields arrive id-keyed and name-ordered from the emitter, so the block order is a
        // property of the model rather than of the walk that produced it.
        for (const auto& [name, value] : node.fields) {
            out << "  " << name << " = " << value << "\n";
        }

        // References are printed after the values and sorted by the name they resolve to, so
        // adding a reference never shifts an unrelated line.
        std::vector<std::string> references;
        references.reserve(node.refs.size());
        for (const RecipeRef& ref : node.refs) {
            const auto found = names.find(ref.target);
            std::string rendered = found != names.end() ? found->second : ref.target;
            if (ref.pos != 0) {
                rendered += ":" + std::to_string(ref.pos);
            }
            references.push_back(std::move(rendered));
        }
        std::sort(references.begin(), references.end());
        for (const std::string& reference : references) {
            out << "  -> " << reference << "\n";
        }

        // Content the generic walk could only see as an opaque property, said by the module
        // that owns the type -- a sketch's lines and dimensions, chiefly.
        const RecipeDetail detail = recipeDetail(*obj);
        for (const RecipeDetailSection& section : detail.sections) {
            out << "  " << section.name << ":\n";
            for (const RecipeNode& node : section.nodes) {
                out << detailLine(node) << "\n";
            }
        }

        // What the emitter reached and had no words for. Printed, not hidden: a value listed
        // here is invisible to a diff, and the list is the measurement that decides whether a
        // recipe could ever stand in for the document. A property a provider has accounted for
        // above is no longer missing, so it drops out of the list.
        std::vector<std::string> unrecorded = unrecordedProperties(*obj);
        unrecorded.erase(std::remove_if(unrecorded.begin(),
                                        unrecorded.end(),
                                        [&detail](const std::string& name) {
                                            return std::find(detail.coveredProperties.begin(),
                                                             detail.coveredProperties.end(),
                                                             name)
                                                != detail.coveredProperties.end();
                                        }),
                         unrecorded.end());
        if (!unrecorded.empty()) {
            out << "  not recorded:";
            for (const std::string& name : unrecorded) {
                out << " " << name;
            }
            out << "\n";
        }
    }

    return out.str();
}

bool App::writeDocumentRecipeText(const Document& doc, const char* documentPath)
{
    if (documentPath == nullptr || *documentPath == '\0') {
        return false;
    }

    Base::FileInfo target(std::string(documentPath) + ".recipe");
    Base::ofstream file(target, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Binary mode, so the line endings written are the ones rendered above on every platform:
    // a recipe view committed on Windows and on Linux must be the same file.
    file << formatDocumentRecipeText(doc);

    return true;
}
