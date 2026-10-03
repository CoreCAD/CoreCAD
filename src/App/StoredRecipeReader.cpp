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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <array>
# include <charconv>
# include <cstring>
# include <filesystem>
# include <fstream>
# include <functional>
# include <iterator>
# include <limits>
# include <map>
# include <memory>
# include <optional>
# include <sstream>
# include <string>
# include <vector>
#endif

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

#include <Base/FileInfo.h>
#include <Base/Persistence.h>
#include <Base/Uuid.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "StoredRecipe.h"
#include "private/StoredRecipeP.h"

#include <Base/Console.h>

#include "Document.h"
#include "DynamicProperty.h"
#include "Extension.h"
#include "ExpressionParser.h"
#include "DocumentObject.h"
#include "GeoFeature.h"
#include "Property.h"
#include "PropertyContainer.h"
#include "PropertyExpressionEngine.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "Services.h"

#include <Base/ServiceProvider.h>

using namespace App;

namespace fs = std::filesystem;

namespace
{

/// Point a reference at the objects the file named. `arrived` is asked first, so a pasted copy
/// references the copy that arrived with it, not the original wearing the same id.
bool restoreReference(Property& prop,
                      const std::vector<Binding>& bindings,
                      const Document& doc,
                      const std::map<std::string, DocumentObject*>& arrived)
{
    auto* link = dynamic_cast<PropertyLinkBase*>(&prop);
    if (link == nullptr) {
        return false;
    }

    std::vector<PropertyLinkBase::Pointing> pointing;
    pointing.reserve(bindings.size());
    for (const Binding& binding : bindings) {
        DocumentObject* target = nullptr;
        if (const auto found = arrived.find(binding.uuid); found != arrived.end()) {
            target = found->second;
        }
        else {
            for (DocumentObject* candidate : doc.getObjects()) {
                if (candidate != nullptr && candidate->Uid.getValueStr() == binding.uuid) {
                    target = candidate;
                    break;
                }
            }
        }
        if (target == nullptr) {
            return false;
        }
        pointing.push_back({target, binding.sub, binding.noPart});
    }

    return link->pointAt(pointing);
}

/// Read a handed-in value back from the project's source folder, by the same path that wrote it.
bool loadAsset(Property& prop, const std::string& directory, const std::string& id)
{
    const fs::path entry = fs::path(directory) / id;
    Base::FileInfo content((entry / assetContentFile).string());
    if (!content.exists()) {
        return false;
    }
    Base::ifstream stream(content, std::ios::in | std::ios::binary);
    Base::XMLReader reader(assetContentFile, stream);
    if (!reader.isValid()) {
        return false;
    }
    try {
        reader.readElement("Value");
        prop.Restore(reader);
        reader.readEndElement("Value");
        reader.readFiles(entry.string());
    }
    catch (const Base::Exception&) {
        // Unreadable material is treated as missing material (Amendment 19 Clause 19.1).
        return false;
    }
    catch (const std::exception&) {
        return false;
    }
    return true;
}

/// Step to the next child of the element just entered, or say it has closed. The file states no
/// counts, since a merged count silently drops content; the level decides, not the name.
bool nextChildOf(Base::XMLReader& reader, int containerLevel)
{
    return App::nextChildElement(reader, containerLevel);
}

/// An element this form does not expect here. Refused, never skipped.
void refuse(const char* expected, const Base::XMLReader& reader)
{
    throw Base::XMLParseException(std::string("Stored recipe: expected <") + expected
                                  + "> and found <" + reader.localName() + ">");
}

/// Refuse the whole file unless it states exactly the format this build reads (Amendment 19
/// Clause 19.7). A missing version is refused too; every file this program writes states one.
void refuseAFormatThisBuildDoesNotRead(const Base::XMLReader& reader)
{
    const std::string said = "This build reads recipe format version "
        + std::to_string(App::storedRecipeFormat) + ".";
    if (!reader.hasAttribute("Version")) {
        throw App::DocumentFormatUnknownError(
            "Stored recipe: the file states no format version, so what it holds cannot be "
            "interpreted. " + said);
    }
    const std::string stated = reader.getAttribute<const char*>("Version");
    // Parsed whole: "1.0" and "1x" are not version 1.
    int version = 0;
    const auto* const from = stated.data();
    const auto* const to = from + stated.size();
    const auto parsed = std::from_chars(from, to, version);
    const bool whole = parsed.ec == std::errc {} && parsed.ptr == to && !stated.empty();
    if (!whole || version != App::storedRecipeFormat) {
        throw App::DocumentFormatUnknownError("Stored recipe: the file is written to format "
                                              "version '"
                                              + stated + "', which this build does not read. "
                                              + said);
    }
}

/// A reader that maps each name the file stated to the name this document gave the object, so
/// formulas follow a renamed object. Only an arrival into existing content renames anything.
class ArrivingReader: public Base::XMLReader
{
public:
    ArrivingReader(const char* name, std::istream& stream, bool mapping)
        : Base::XMLReader(name, stream)
        , _mapping(mapping)
    {}

    void addName(const char* stated, const char* given) override
    {
        if (stated != nullptr && given != nullptr && std::strcmp(stated, given) != 0) {
            _names[stated] = given;
        }
    }

    const char* getName(const char* stated) const override
    {
        const auto found = _names.find(stated);
        return found != _names.end() ? found->second.c_str() : stated;
    }

    bool doNameMapping() const override
    {
        return _mapping;
    }

private:
    bool _mapping;
    std::map<std::string, std::string> _names;
};

/// Where the line holding position `at` begins.
std::size_t lineStartOf(const std::string& text, std::size_t at)
{
    const std::size_t newline = text.rfind('\n', at);
    return newline == std::string::npos ? 0 : newline + 1;
}

/// The whole lines from the one holding `at` to the one holding `end`, or nothing when more than
/// indentation precedes `at` -- not a shape this writer produces.
std::string wholeLines(const std::string& text, std::size_t at, std::size_t end)
{
    const std::size_t lineStart = lineStartOf(text, at);
    if (text.find_first_not_of(" \t", lineStart) != at) {
        return {};
    }
    const std::size_t lineEnd = text.find('\n', end);
    const std::size_t stop = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
    return text.substr(lineStart, stop - lineStart);
}

/// One object's appearance block, exactly as the file states it, indentation and all.
std::string liftDisplayBlock(const std::string& objectWords)
{
    const std::size_t at = objectWords.find("<Display>");
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t end = objectWords.find("</Display>", at);
    if (end == std::string::npos) {
        return {};
    }
    return wholeLines(objectWords, at, end);
}

/// One property's block, exactly as the file states it, indentation and all.
std::string liftPropertyBlock(const std::string& objectWords, const std::string& name)
{
    const std::string opening = "<Property" + attribute("name", name);
    const std::size_t at = objectWords.find(opening);
    if (at == std::string::npos) {
        return {};
    }
    // Depth-counted, so properties nested inside the value cannot end the block early.
    std::size_t depth = 0;
    std::size_t scan = at;
    while (scan < objectWords.size()) {
        const std::size_t open = objectWords.find("<Property", scan);
        const std::size_t close = objectWords.find("</Property>", scan);
        if (close == std::string::npos) {
            return {};
        }
        if (open != std::string::npos && open < close) {
            const std::size_t ends = objectWords.find('>', open);
            if (ends == std::string::npos) {
                return {};
            }
            // A self-closing declaration opens nothing.
            if (objectWords[ends - 1] != '/') {
                ++depth;
            }
            scan = ends + 1;
            continue;
        }
        --depth;
        scan = close + std::strlen("</Property>");
        if (depth == 0) {
            return wholeLines(objectWords, at, scan);
        }
    }
    return {};
}

/// A reference read from the file, waiting for the objects it points at to exist.
using PendingReference = std::pair<Property*, std::vector<Binding>>;

/// A well-formed value this build cannot read. Kept as the file states it, never half applied
/// (Amendment 19 Clause 19.1).
void keepUnreadValue(Document& doc,
                     PropertyContainer& owner,
                     Property& prop,
                     const std::string& name,
                     const std::string& type,
                     const std::string& why,
                     std::string words)
{
    // Reset to a fresh property's value: a half-read value is one nobody authored.
    try {
        std::unique_ptr<Property> fresh(static_cast<Property*>(prop.getTypeId().createInstance()));
        if (fresh) {
            if (std::unique_ptr<Property> untouched {fresh->Copy()}) {
                prop.Paste(*untouched);
            }
        }
    }
    catch (const Base::Exception&) {
        // Keeps what it has; the save writes the kept words either way.
    }

    if (words.empty()) {
        Base::Console().warning(
            "Stored recipe: '%s' (%s) states a value this build could not read (%s), and its "
            "words could not be kept. Saving this document would lose it.\n",
            name.c_str(),
            type.c_str(),
            why.c_str());
        doc.recordUnkeptStatement("'" + name + "' (" + type
                                  + "), a value this build could not read and whose words could "
                                    "not be kept");
        return;
    }

    // The reason is kept with the words, so a person can tell this from a missing add-on
    // (Amendment 19 Clause 19.4).
    owner.rememberStatedProperty(name.c_str(),
                                 std::move(words),
                                 "this build could not read the value it states (" + why + ")");
    Base::Console().warning(
        "Stored recipe: '%s' (%s) states a value this build could not read (%s). It is kept as "
        "written and the document is not whole.\n",
        name.c_str(),
        type.c_str(),
        why.c_str());
}

/// The property the statement belongs in, declaring it first when a person added it. Null when
/// this build has nowhere to put it.
Property* placeFor(const Base::XMLReader& reader,
                   PropertyContainer& owner,
                   const std::string& name,
                   const std::string& type)
{
    Property* prop = owner.getPropertyByName(name.c_str());
    if (prop == nullptr && reader.getAttribute<long>("dynamic", 0) == 1) {
        try {
            prop = owner.addDynamicProperty(
                type.c_str(),
                name.c_str(),
                reader.getAttribute<const char*>("group", ""),
                reader.getAttribute<const char*>("doc", ""),
                static_cast<short>(reader.getAttribute<long>("attributes", 0)),
                reader.getAttribute<long>("readonly", 0) == 1,
                reader.getAttribute<long>("hidden", 0) == 1);
        }
        catch (const Base::Exception&) {
            // A property type this build does not have; the caller keeps the block.
            prop = nullptr;
        }
    }
    if (prop == nullptr || prop->getTypeId().getName() != type) {
        return nullptr;
    }
    return prop;
}

/// The durable ids a `<Reference>` block names, in the order it names them.
std::vector<Binding> readBindings(Base::XMLReader& reader)
{
    std::vector<Binding> bindings;
    reader.readElement("Reference");
    const int reference = reader.level();
    while (nextChildOf(reader, reference)) {
        if (std::strcmp(reader.localName(), "Target") != 0) {
            refuse("Target", reader);
        }
        const bool noPart = !reader.hasAttribute("sub");
        bindings.push_back({reader.getAttribute<const char*>("uuid"),
                            noPart ? "" : reader.getAttribute<const char*>("sub"),
                            /*external=*/false,
                            noPart});
    }
    reader.readEndElement("Reference");
    return bindings;
}

/// Source material the file names by its content, read back from the project's source store.
void readAsset(PropertyContainer& owner,
               Property& prop,
               const std::string& name,
               const std::string& asset,
               const std::string& assetDirectory)
{
    if (!assetDirectory.empty() && loadAsset(prop, assetDirectory, asset)) {
        return;
    }
    // Remembered, so the next save keeps the name and the material can come back.
    Base::Console().warning("Stored recipe: missing source material '%s'\n", asset.c_str());
    owner.rememberMissingSource(name.c_str(), asset);
}

/// A value stated inline, handed to the property's own reader.
void readInlineValue(Base::XMLReader& reader,
                     Document& doc,
                     PropertyContainer& owner,
                     Property& prop,
                     const std::string& name,
                     const std::string& type,
                     const std::function<std::string(const std::string&)>& wordsFor)
{
    try {
        prop.Restore(reader);
    }
    catch (const Base::XMLParseException&) {
        // Broken structure refuses the whole file (Amendment 19 Clause 19.2).
        throw;
    }
    catch (const Base::Exception& e) {
        keepUnreadValue(doc, owner, prop, name, type, e.what(), wordsFor(name));
    }
    catch (const std::exception& e) {
        // e.g. `stod` on a value that is not a number.
        keepUnreadValue(doc, owner, prop, name, type, e.what(), wordsFor(name));
    }
}

/// A statement this build has no property for (or one of another type). Its words are kept.
void keepWithNoPlace(Document& doc,
                     PropertyContainer& owner,
                     const std::string& name,
                     const std::string& type,
                     std::string words)
{
    if (words.empty()) {
        Base::Console().warning(
            "Stored recipe: '%s' (%s) is not a property this build has, and its words could not "
            "be kept. Saving this document would lose it.\n",
            name.c_str(),
            type.c_str());
        // Recorded as well as warned: the save has to be able to ask what it would lose.
        doc.recordUnkeptStatement("'" + name + "' (" + type
                                  + "), which this build has no property for and whose words "
                                    "could not be kept");
        return;
    }
    owner.rememberStatedProperty(name.c_str(),
                                 std::move(words),
                                 "this build has no property of that name and type");
    Base::Console().warning("Stored recipe: '%s' (%s) is not a property this build has. It is "
                            "kept as written and the document is not whole.\n",
                            name.c_str(),
                            type.c_str());
}

/// Choices still waiting for a list that offers them once the whole container is read. Some lists
/// are built from other properties (a hole's thread class from its type); a name still waiting
/// here is kept as written, not replaced by the default.
void keepChoicesNothingOffers(Document& doc,
                              PropertyContainer& owner,
                              const std::function<std::string(const std::string&)>& wordsFor)
{
    std::vector<Property*> declared;
    owner.getPropertyList(declared);
    for (Property* prop : declared) {
        auto* choice = freecad_cast<PropertyEnumeration*>(prop);
        if (choice == nullptr || choice->nameAwaitingItsList().empty()) {
            continue;
        }
        const std::string why =
            "'" + choice->nameAwaitingItsList() + "' is not a value this build offers for it";
        const char* named = choice->getName();
        const std::string name = named != nullptr ? named : std::string {};
        choice->stopAwaitingItsList();
        keepUnreadValue(doc, owner, *choice, name, choice->getTypeId().getName(), why,
                        wordsFor(name));
    }
}

/// Step over the `<Unrecorded>` block: nothing in it can be restored.
void stepOverUnrecorded(Base::XMLReader& reader)
{
    reader.readElement("Unrecorded");
    const int unrecorded = reader.level();
    while (nextChildOf(reader, unrecorded)) {
        if (std::strcmp(reader.localName(), "Property") != 0) {
            refuse("Property", reader);
        }
    }
    reader.readEndElement("Unrecorded");
}

/// Restore the values of one `<Properties>` block onto a container, and the block that follows it.
void readProperties(Base::XMLReader& reader,
                    Document& doc,
                    PropertyContainer& owner,
                    std::vector<PendingReference>& pending,
                    const std::string& assetDirectory,
                    const std::function<std::string()>& ownWords)
{
    // Lifted lazily, only when something cannot be honoured. Each container gets its OWN block:
    // an appearance may reuse a property name of its object.
    std::string containerWords;
    bool lifted = false;
    const std::function<std::string(const std::string&)> wordsFor = [&](const std::string& name) {
        if (!lifted) {
            containerWords = ownWords ? ownWords() : std::string {};
            lifted = true;
        }
        return containerWords.empty() ? std::string {} : liftPropertyBlock(containerWords, name);
    };

    reader.readElement("Properties");
    const int properties = reader.level();
    while (nextChildOf(reader, properties)) {
        if (std::strcmp(reader.localName(), "Property") != 0) {
            refuse("Property", reader);
        }
        const std::string name = reader.getAttribute<const char*>("name");
        const std::string type = reader.getAttribute<const char*>("type");
        // A self-closing element has already ended, so its level is the list's: declared, no value.
        const bool valueStated = reader.level() > properties;

        Property* prop = placeFor(reader, owner, name, type);
        if (prop == nullptr) {
            keepWithNoPlace(doc, owner, name, type, wordsFor(name));
        }
        else if (const std::string asset = reader.getAttribute<const char*>("asset", "");
                 !asset.empty()) {
            readAsset(owner, *prop, name, asset, assetDirectory);
        }
        else if (reader.getAttribute<long>("reference", 0) == 1) {
            // Bound once every object exists, since a reference may point forwards.
            pending.emplace_back(prop, readBindings(reader));
        }
        else if (valueStated) {
            readInlineValue(reader, doc, owner, *prop, name, type, wordsFor);
        }
        reader.readEndElement("Property");
    }
    reader.readEndElement("Properties");

    keepChoicesNothingOffers(doc, owner, wordsFor);
    stepOverUnrecorded(reader);
}

/// Put back a capability the object asked for, or say so. Not fatal: its properties are then
/// kept as unplaced statements.
void grantCapability(Document& doc,
                     DocumentObject& obj,
                     const std::string& asks,
                     const std::string& name)
{
    const Base::Type capability = Base::Type::fromName(asks.c_str());
    const bool known = !capability.isBad()
        && capability.isDerivedFrom(App::Extension::getExtensionClassTypeId());
    if (known && obj.hasExtension(capability, false)) {
        return;  // the object's own class already composes it
    }

    App::Extension* granted = nullptr;
    if (known) {
        granted = static_cast<App::Extension*>(capability.createInstance());
        if (granted != nullptr && !granted->isPythonExtension()) {
            // Only script-grantable capabilities; a C++ one would change what the class is.
            delete granted;
            granted = nullptr;
        }
    }
    if (granted != nullptr) {
        granted->initExtension(&obj);
        return;
    }

    Base::Console().warning(
        "Stored recipe: '%s' asks for '%s', which this build cannot grant. What that capability "
        "carried is kept as written and the document is not whole.\n",
        name.c_str(),
        asks.c_str());
    doc.recordUnkeptStatement("the capability '" + asks + "' that '" + name
                              + "' asks for, which this build cannot grant");
}

/// The file's text, kept so what this build cannot construct is given back in the file's own
/// words (Amendment 19). Searches resume from the last object found, since objects are read in
/// file order.
class RecipeSource
{
public:
    explicit RecipeSource(std::string text)
        : _text(std::move(text))
    {}

    const std::string& text() const
    {
        return _text;
    }

    /// The document's own block, exactly as the file states it, indentation and all.
    std::string documentWords() const
    {
        return linesThrough(_text.find("<Document "), "</Document>");
    }

    /// One object's block, exactly as the file states it, indentation and all.
    std::string objectWords(const std::string& uuid)
    {
        return linesThrough(findObject(uuid), "</Object>");
    }

    /// One object's block, dedented so it can be given back at whatever depth the writer is at.
    std::string objectBlock(const std::string& uuid)
    {
        const std::size_t found = findObject(uuid);
        const std::string words = linesThrough(found, "</Object>");
        if (words.empty()) {
            return {};
        }
        const std::size_t start = found - lineStartOf(_text, found);
        const std::string indent = words.substr(0, start);
        if (indent.find_first_not_of(" \t") != std::string::npos) {
            return words.substr(start);
        }
        std::string dedented;
        dedented.reserve(words.size());
        std::size_t at = 0;
        while (at <= words.size()) {
            const std::size_t nl = words.find('\n', at);
            const std::size_t stop = (nl == std::string::npos) ? words.size() : nl;
            std::string line = words.substr(at, stop - at);
            if (!indent.empty() && line.rfind(indent, 0) == 0) {
                line.erase(0, indent.size());
            }
            dedented += line;
            if (nl == std::string::npos) {
                break;
            }
            dedented += '\n';
            at = nl + 1;
        }
        return dedented;
    }

private:
    std::size_t findObject(const std::string& uuid)
    {
        const std::string opening = "<Object" + attribute("uuid", uuid);
        std::size_t found = _text.find(opening, _cursor);
        if (found == std::string::npos) {
            found = _text.find(opening);
        }
        if (found != std::string::npos) {
            _cursor = found;
        }
        return found;
    }

    /// From the start of `start`'s line to the end of the first `closing` after it. Blocks of
    /// one kind never nest.
    std::string linesThrough(std::size_t start, const std::string& closing) const
    {
        if (start == std::string::npos) {
            return {};
        }
        const std::size_t end = _text.find(closing, start);
        if (end == std::string::npos) {
            return {};
        }
        const std::size_t lineStart = lineStartOf(_text, start);
        return _text.substr(lineStart, end + closing.size() - lineStart);
    }

    std::string _text;
    std::size_t _cursor {0};
};

}  // namespace

void App::restoreStoredRecipe(Document& doc,
                              std::istream& source,
                              bool finish,
                              const std::string& assetDirectory)
{
    RecipeArrival how;
    how.finish = finish;
    how.assetDirectory = assetDirectory;
    restoreStoredRecipe(doc, source, how);
}

void App::restoreStoredRecipe(Document& doc, std::istream& source, const RecipeArrival& how)
{
    const std::string& assetDirectory = how.assetDirectory;
    RecipeSource sourceText(std::string((std::istreambuf_iterator<char>(source)),
                                        std::istreambuf_iterator<char>()));
    std::istringstream parsed(sourceText.text());

    ArrivingReader reader("StoredRecipe", parsed, how.intoExistingContent);
    if (!reader.isValid()) {
        // A refusal is total; a document that never opened is not an empty one (Clause 19.2).
        throw Base::XMLParseException(
            reader.whyInvalid().empty()
                ? std::string("Stored recipe: the file could not be read as a recipe")
                : "Stored recipe: " + reader.whyInvalid());
    }

    // Lets formulas ask this reader for renamed objects. Only an arrival renames anything.
    std::optional<ExpressionParser::ExpressionImporter> naming;
    if (how.intoExistingContent) {
        naming.emplace(reader);
    }

    std::vector<PendingReference> pending;
    std::vector<DocumentObject*> restored;
    // By the Uid the file stated; see restoreReference.
    std::map<std::string, DocumentObject*> arrived;

    reader.readElement("Recipe");
    refuseAFormatThisBuildDoesNotRead(reader);
    const int recipe = reader.level();

    // A copy states no `<Document>` block, so read whichever element is actually there.
    if (!nextChildOf(reader, recipe)) {
        refuse("Objects", reader);
    }
    if (std::strcmp(reader.localName(), "Document") == 0) {
        const std::string documentUid = reader.getAttribute<const char*>("uuid");
        readProperties(reader, doc, doc, pending, assetDirectory, [&] {
            return sourceText.documentWords();
        });
        reader.readEndElement("Document");
        // The document model mints a fresh Uid if another open document already has this one.
        doc.Uid.setValue(documentUid);

        if (!nextChildOf(reader, recipe)) {
            refuse("Objects", reader);
        }
    }
    if (std::strcmp(reader.localName(), "Objects") != 0) {
        refuse("Objects", reader);
    }
    const int objects = reader.level();
    while (nextChildOf(reader, objects)) {
        if (std::strcmp(reader.localName(), "Object") != 0) {
            refuse("Object", reader);
        }
        const std::string uuid = reader.getAttribute<const char*>("uuid");
        const std::string type = reader.getAttribute<const char*>("type");
        const std::string name = reader.getAttribute<const char*>("name");
        const bool display = reader.getAttribute<long>("display", 0) == 1;
        const bool asked = reader.getAttribute<long>("extensions", 0) == 1;

        // The stated name, not a fresh one: formulas speak it.
        DocumentObject* obj = nullptr;
        try {
            obj = doc.addObject(type.c_str(), name.c_str(), /*isNew=*/false);
        }
        catch (const Base::Exception&) {
            // No such type in this build: keep the block as stated and read on (Amendment 19).
            std::string block = sourceText.objectBlock(uuid);
            if (block.empty()) {
                throw;
            }
            doc.keepUnreadObject(uuid, type, std::move(block));
            Base::Console().warning(
                "Stored recipe: '%s' is of type '%s', which this build cannot construct. "
                "Its content is kept as written and the document is not whole.\n",
                name.c_str(),
                type.c_str());
            reader.readEndElement("Object");
            continue;
        }
        if (obj != nullptr) {
            obj->Uid.setValue(uuid);
            restored.push_back(obj);
            arrived.emplace(uuid, obj);
            reader.addName(name.c_str(), obj->getNameInDocument());
            if (how.arrived != nullptr) {
                how.arrived->emplace_back(uuid, obj);
            }
            // As the archive does: stops features rebuilding on each property before references
            // are bound.
            obj->setStatus(ObjectStatus::Restore, true);
            if (asked) {
                // Before the properties: a capability gives its properties somewhere to live.
                reader.readElement("Extensions");
                const int extensions = reader.level();
                while (nextChildOf(reader, extensions)) {
                    if (std::strcmp(reader.localName(), "Extension") != 0) {
                        refuse("Extension", reader);
                    }
                    const std::string asks = reader.getAttribute<const char*>("type");
                    grantCapability(doc, *obj, asks, name);
                }
                reader.readEndElement("Extensions");
            }
            readProperties(reader, doc, *obj, pending, assetDirectory, [&] {
                return sourceText.objectWords(uuid);
            });
            obj->setStatus(ObjectStatus::Restore, false);

            if (display) {
                // A headless session keeps the file's words, or open-and-save would strip every
                // chosen colour (Amendment 19 Clause 19.1).
                reader.readElement("Display");
                PropertyContainer* appearance = appearanceOf(*obj);
                if (appearance == nullptr) {
                    obj->keepStatedAppearance(liftDisplayBlock(sourceText.objectWords(uuid)));
                    if (obj->statedAppearance().empty()) {
                        Base::Console().warning(
                            "Stored recipe: '%s' states an appearance this session has nowhere "
                            "to put, and its words could not be kept. Saving this document would "
                            "lose it.\n",
                            name.c_str());
                        doc.recordUnkeptStatement("the appearance '" + name
                                                  + "' states, which this session has nowhere to "
                                                    "put and whose words could not be kept");
                    }
                }
                if (appearance != nullptr) {
                    readProperties(reader, doc, *appearance, pending, assetDirectory, [&] {
                        return liftDisplayBlock(sourceText.objectWords(uuid));
                    });
                }
                reader.readEndElement("Display");
            }
        }
        reader.readEndElement("Object");
    }
    reader.readEndElement("Objects");

    reader.readEndElement("Recipe");

    // Now that every object exists, point the references at them.
    for (const auto& [prop, bindings] : pending) {
        if (!restoreReference(*prop, bindings, doc, arrived)) {
            // Kept, not dropped: a merge needs it to tell a deleted target from no reference.
            PropertyContainer* owner = prop->getContainer();
            const char* name = prop->getName();
            if (owner != nullptr && name != nullptr) {
                std::vector<PropertyContainer::StatedTarget> stated;
                stated.reserve(bindings.size());
                for (const Binding& binding : bindings) {
                    stated.push_back({binding.uuid, binding.sub, binding.noPart});
                }
                owner->rememberUnresolvedReference(name, std::move(stated));
            }
            Base::Console().warning(
                "Stored recipe: '%s' names an object this document does not hold. Where it "
                "pointed is kept as written and the document is not whole.\n",
                name != nullptr ? name : "");
        }
    }

    // The second pass that binds formulas, unless an opening document runs it itself.
    if (how.finish) {
        doc.afterRestore(restored, false);
    }

    // The file carries steps, never geometry, so everything still has to be built.
    for (DocumentObject* obj : restored) {
        obj->enforceRecompute();
    }

    // Blocked by what it holds, not by a rebuild request: cached geometry skips the rebuild
    // (Amendment 19).
    doc.blockWhatCouldNotBeHonoured();
}
