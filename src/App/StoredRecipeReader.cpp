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

/// Point a reference property at objects again, given what the file said it pointed at.
///
/// `arrived` is what this read brought in, by the durable id the file stated for it. It is asked
/// first: an object pasted beside the one it was copied from references the copy that arrived with
/// it, not the original, even while the two still wear the same id.
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
            // The file names a target this document does not hold. Refusing is the honest
            // answer: a reference that quietly points at nothing is the failure the durable-id
            // binding exists to prevent.
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
        // The bulk the element named -- the shape, its mapped element names, its hasher table --
        // is asked for by name, exactly as the archive asks the zip for it.
        reader.readFiles(entry.string());
    }
    catch (const Base::Exception&) {
        // Material that is here but will not read back is material this session does not have.
        // Said with the same words as material that is absent, and kept the same way: the name
        // the file gives it is what the next save writes, so it comes back when the material
        // does (Amendment 19 Clause 19.1).
        return false;
    }
    catch (const std::exception&) {
        return false;
    }
    return true;
}

/// Step to the next child of the element the reader has just entered, or say it has closed.
///
/// A shared file states content, never a count of its own content. A declared length is a number
/// restating what the same file already holds, and in a file whose whole purpose is that people
/// diff and merge it that is a landmine: two people each adding one feature to a common ancestor
/// both write the same larger number, a textual merge takes it without even a conflict, and a
/// reader that loops that many times and stops drops one of the two added features without a
/// word. So structure is derived from what is there.
///
/// The LEVEL decides when to stop, not the name: a self-closing child ends at an event a reader
/// would otherwise mistake for the end of the list.
bool nextChildOf(Base::XMLReader& reader, int containerLevel)
{
    return App::nextChildElement(reader, containerLevel);
}

/// The file said something this form does not know how to read there.
///
/// Refused rather than skipped. Passing over an element a reader does not recognise is how a
/// document quietly comes back smaller than it was written, which is the one failure the whole
/// stored form exists to prevent.
void refuse(const char* expected, const Base::XMLReader& reader)
{
    throw Base::XMLParseException(std::string("Stored recipe: expected <") + expected
                                  + "> and found <" + reader.localName() + ">");
}

/// The file is written to a format this build does not read, and so it does not open at all.
///
/// Cruth (Amendment 19 Clause 19.7). The refusal is total and says which version was asked for
/// and which this build reads, because "this file will not open" without either number leaves a
/// person with nothing to do about it. Measured before this: a file stating version 99 opened
/// exactly as though it had stated version 1, and every value in it was interpreted by rules it
/// was never written to.
///
/// A file that states no version at all is refused on the same terms rather than assumed to be
/// the current one. Every file this program has ever written states it, so the assumption would
/// only ever be made about a file this program did not write.
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
    // Read whole or not at all: "1.0" and "1x" are not version 1, and a parse that took the
    // leading digits and went on would be interpreting a file by a rule it made up.
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

/// A reader that answers what this document called the objects a file named.
///
/// A recipe read into an empty document keeps every name the file states, so nothing is remapped
/// and each name answers with itself. A recipe arriving in a document that already holds content
/// cannot: a name may be taken, and the document gives the object one of its own. A formula binds
/// by name and has no way to see that, so the pair is kept here and the formula follows the object
/// -- the same service the document archive's own merge reader provides.
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

/// The whole lines from the one holding `at` to the one holding `end`, or nothing when
/// something other than indentation shares the line before `at` -- not a shape this writer
/// produces, and not a block to guess the extent of.
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
///
/// Lifted rather than rebuilt for the same reason a property block is: a session with no display
/// layer never read what is in there, so the only honest source for it is the file's own words.
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
///
/// Taken from the file's own words rather than rebuilt from what the reader understood, because a
/// block this build has no place for is a statement it cannot re-derive. It is kept at the depth
/// the file wrote it at and given back at that depth: this form writes a property at one depth
/// and only one, so the words the file used are the words that belong there.
std::string liftPropertyBlock(const std::string& objectWords, const std::string& name)
{
    const std::string opening = "<Property" + attribute("name", name);
    const std::size_t at = objectWords.find(opening);
    if (at == std::string::npos) {
        return {};
    }
    // Counted rather than searched for, so a value that states properties of its own inside its
    // body cannot end the block early.
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

/// A value the file states, whose element is well formed and whose content this build cannot turn
/// into a value. Kept as the file states it, and never half applied (Amendment 19 Clause 19.1).
void keepUnreadValue(Document& doc,
                     PropertyContainer& owner,
                     Property& prop,
                     const std::string& name,
                     const std::string& type,
                     const std::string& why,
                     std::string words)
{
    // Not half applied. A value assembled from the legible part of a statement is one nobody
    // authored, so what the half-read left behind is cleared to the value a property of this kind
    // holds before anything is put in it -- this session's choice, deliberately, and cheap because
    // it is paid only when a value fails. What the file states is what the next save writes, which
    // is where the duty actually binds.
    try {
        std::unique_ptr<Property> fresh(static_cast<Property*>(prop.getTypeId().createInstance()));
        if (fresh) {
            if (std::unique_ptr<Property> untouched {fresh->Copy()}) {
                prop.Paste(*untouched);
            }
        }
    }
    catch (const Base::Exception&) {
        // A property that cannot say what it would have been keeps what it has. The kept
        // statement is what the save emits either way, which is the duty this clause places.
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

    // The reason travels with the words: the console message is gone by the time a person opens
    // the list of what the document holds, and "no property of that name" -- the other reason a
    // block is kept -- would send them looking for a missing add-on instead of a value they can
    // retype (Amendment 19 Clause 19.4).
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

/// The property the file's statement belongs in, declaring it first when a person added it.
///
/// Null when this build has nowhere to put it.
Property* placeFor(const Base::XMLReader& reader,
                   PropertyContainer& owner,
                   const std::string& name,
                   const std::string& type)
{
    Property* prop = owner.getPropertyByName(name.c_str());
    if (prop == nullptr && reader.getAttribute<long>("dynamic", 0) == 1) {
        // A property the object was given at runtime has to be declared before it can hold
        // anything, so the file carries the declaration and the reader replays it.
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
            // A property type this build does not have -- an add-on's own kind. Declaring it
            // fails, and the read used to stop there, taking the rest of the document with it.
            // The block is kept as stated instead (Amendment 19).
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
    // The file names source material this project does not hold. Said out loud, because a part
    // quietly missing the body it was built from looks exactly like a part that never had one --
    // and remembered, because the next save would otherwise write the absence over the name and
    // make the loss permanent even after the material came back.
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
        // The file's STRUCTURE is broken here, which is not a value this build cannot read: a
        // refusal for that is total and belongs to the reader, not here (Amendment 19 Clause
        // 19.2).
        throw;
    }
    catch (const Base::Exception& e) {
        keepUnreadValue(doc, owner, prop, name, type, e.what(), wordsFor(name));
    }
    catch (const std::exception& e) {
        // A value that fails in the standard library rather than in ours -- a number that is not
        // one reaches `stod`, which throws something no catch of ours used to name. Measured: one
        // such value and the document did not open at all, not even as its own beginning.
        keepUnreadValue(doc, owner, prop, name, type, e.what(), wordsFor(name));
    }
}

/// A statement this build has no place for: no property of that name, or one of a different type.
///
/// Stepping over it is how a document quietly comes back smaller than it was written, so the
/// file's own words are kept and given back.
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
        // Warned about AND recorded: what a save would lose has to be answerable at the moment of
        // the save, and a message printed at load time is gone by then.
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

/// Choices still waiting, once the whole container is read, for a list that offers them.
///
/// An enumeration states the value that was chosen by name, and some lists are not fixed: a
/// hole's thread class is built from its thread type, so a name may not be lookupable at the
/// moment it is read. Such a name waits for a list that offers it, and every property read after
/// it is a chance for one to arrive. A name still waiting here is one nothing here offers -- a
/// value this build cannot honour, kept as the file worded it rather than quietly replaced by the
/// default (Amendment 19).
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

/// Step over the `<Unrecorded>` block: what the writer could not say, the reader cannot invent.
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
    // This container's own words, lifted only if something here cannot be honoured -- which is
    // almost never, and scanning the whole file for every container read would be a cost paid on
    // every load. Each container is given a lift of its OWN block and no more: a name inside an
    // appearance may also name one of the object's own properties, and keeping the wrong one of
    // the two would be worse than losing it.
    std::string containerWords;
    bool lifted = false;
    const std::function<std::string(const std::string&)> wordsFor = [&](const std::string& name) {
        if (!lifted) {
            // The file's words as written, indentation and all -- not the dedented form a kept
            // object is stored in, because a property block is given back at the depth it was
            // read at.
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
        // A property closed where it stands states that it exists and states no value for it. A
        // self-closing element has already ended by the time it is read, so its level is the
        // level of the list around it -- that, and not a flag, is what says a value is there.
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
            // Held until every object in the file exists: a reference may point forwards, and a
            // file whose meaning depended on the order it was read would have brought back the
            // ordering problem this form was written to remove.
            pending.emplace_back(prop, readBindings(reader));
        }
        else if (valueStated) {
            readInlineValue(reader, doc, owner, *prop, name, type, wordsFor);
        }
        // Otherwise declared and left as the object makes it. Asking the property to read a value
        // the file does not state is how a reader invents one.
        reader.readEndElement("Property");
    }
    reader.readEndElement("Properties");

    keepChoicesNothingOffers(doc, owner, wordsFor);
    stepOverUnrecorded(reader);
}

/// Put back a capability the file says the object asked for, and say so when this build cannot.
///
/// A capability that cannot be granted is not fatal: the properties it carried are read by the
/// ordinary path, which keeps a statement it has nowhere to put and refuses the save that would
/// write less than the file states (Amendment 19). What must not happen is silence.
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
            // Only a capability a script can be granted can be granted back to it. Composing a
            // C++ one here would give the object a capability its class never took.
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

/// The file a recipe was read from, kept as text so that what this build cannot construct is
/// given back in the file's own words rather than in this session's reading of them (Amendment
/// 19).
///
/// Objects are read in the order the file states them, so each object's block is looked for from
/// where the last one was found. Searching from the top for every object made a headless open of
/// a document with appearances scan the whole file once per object.
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
    ///
    /// The document is not an object and has no durable id to be found by, but it states
    /// properties like any other container and they are kept on the same terms.
    std::string documentWords() const
    {
        return linesThrough(_text.find("<Document "), "</Document>");
    }

    /// One object's block, exactly as the file states it, indentation and all.
    std::string objectWords(const std::string& uuid)
    {
        return linesThrough(findObject(uuid), "</Object>");
    }

    /// One object's block with the depth it was written at removed, so it can be given back at
    /// whatever depth the writer is at and restored on the way out.
    ///
    /// Cruth (Amendment 19): a document may name an object this build cannot construct -- a
    /// module that was not compiled in, an add-on that is absent, a scripted class that is gone.
    /// What is owed then is the statement itself and not this session's reading of it.
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

    /// From the start of the line `start` is on to the end of the first `closing` after it.
    ///
    /// Blocks of one kind are siblings and never nest, so the first closing tag is this block's
    /// own end.
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
    // Read once and kept, because a block this build cannot construct is given back from the
    // file's own words rather than from this session's reading of them (Amendment 19).
    RecipeSource sourceText(std::string((std::istreambuf_iterator<char>(source)),
                                        std::istreambuf_iterator<char>()));
    std::istringstream parsed(sourceText.text());

    ArrivingReader reader("StoredRecipe", parsed, how.intoExistingContent);
    if (!reader.isValid()) {
        // Nothing in this file could be read at all -- it is not a recipe, or its very first
        // element is broken. Returning here left the caller holding an empty document that looked
        // like the file, which is the failure Clause 19.2 forbids most plainly: a refusal is
        // total, and a document that never opened is not an empty one.
        throw Base::XMLParseException(
            reader.whyInvalid().empty()
                ? std::string("Stored recipe: the file could not be read as a recipe")
                : "Stored recipe: " + reader.whyInvalid());
    }

    // What the name map is for. A formula names the object it reads a value from, so a formula
    // arriving beside an object this document had to rename has to be told the new name; the
    // expression layer asks the reader that is currently reading, which is this one. Installed
    // only for an arrival, because a document being opened renames nothing.
    std::optional<ExpressionParser::ExpressionImporter> naming;
    if (how.intoExistingContent) {
        naming.emplace(reader);
    }

    std::vector<PendingReference> pending;
    std::vector<DocumentObject*> restored;
    // What this read brought in, by the durable id the file stated for it. A reference is pointed
    // at these before anything already in the document: an object arriving beside the one it was
    // copied from must reference the copy, even while the two still wear the same id.
    std::map<std::string, DocumentObject*> arrived;

    reader.readElement("Recipe");
    // Consulted before a single statement below it is interpreted (Clause 19.7). Read as the
    // format it was written to, or not read at all: a file interpreted as a format it was not
    // written to gives back a document that looks like the part and is not it, and every rule
    // after this one would be left catching that damage one case at a time.
    refuseAFormatThisBuildDoesNotRead(reader);
    const int recipe = reader.level();

    // The `<Document>` block is what the file says about the document itself, and a rendering
    // that carries objects alone states none. Read from whichever element is actually there
    // rather than from the one a whole-document file would have: demanding it would make a copy
    // unreadable, and assuming it would read the first object as if it were the document.
    if (!nextChildOf(reader, recipe)) {
        refuse("Objects", reader);
    }
    if (std::strcmp(reader.localName(), "Document") == 0) {
        const std::string documentUid = reader.getAttribute<const char*>("uuid");
        // The document states properties of its own -- what it is called, who made it, what it is
        // for, and anything a tool of someone else's added to it. They are kept on the same terms
        // as an object's: from the document's own block, so a name here cannot be confused with
        // the same name on an object.
        readProperties(reader, doc, doc, pending, assetDirectory, [&] {
            return sourceText.documentWords();
        });
        reader.readEndElement("Document");
        // A document's identity is its own, and the document model refuses to let two open
        // documents share one -- restoring the uuid into a copy of a document that is still open
        // mints a fresh one instead. That is the model's rule, not this reader's, and it is right:
        // the file names the document it came from, and a second live copy is not that document.
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

        // The in-document name is restored, not regenerated: expressions and the document's own
        // reporting speak it, so a rebuilt document that renamed everything would be a different
        // document wearing the same values.
        DocumentObject* obj = nullptr;
        try {
            obj = doc.addObject(type.c_str(), name.c_str(), /*isNew=*/false);
        }
        catch (const Base::Exception&) {
            // This build has no such type. The object is not dropped and the read does not stop:
            // dropping it would take every object stated after it as well, and the next save would
            // write all of that away. The block is kept exactly as stated, and the document reports
            // itself not whole (Amendment 19).
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
            // The name the file stated and the name this document gave it. The same for a document
            // being opened; different wherever the stated one was already taken.
            reader.addName(name.c_str(), obj->getNameInDocument());
            if (how.arrived != nullptr) {
                how.arrived->emplace_back(uuid, obj);
            }
            // Marked as being restored for the duration, exactly as the archive's own reader does
            // it. Some features rebuild themselves the moment one of their sizes changes, which is
            // right when a person types a number and wrong while a file is being read: it builds
            // the part three times over on the way in, and it builds it before the references it
            // is built on have been bound.
            obj->setStatus(ObjectStatus::Restore, true);
            if (asked) {
                // Read before the properties, because a capability the object asked for is what
                // gives the properties that follow it somewhere to live.
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
                // The appearance the file carries. A session with nowhere to put it -- a headless
                // one -- keeps the file's own words rather than guessing at a place for it: an
                // ordinary open-and-save would otherwise strip every colour a person chose, and
                // say nothing (Amendment 19 Clause 19.1).
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
                    // From the appearance block's own words: a name in here may also name one of
                    // the object's own properties, and the two are different statements.
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

    // Now that every object exists, point the references at them. A binding whose target is not
    // in the document is reported rather than passed over: a reference that quietly points at
    // nothing is the exact failure durable ids exist to prevent.
    for (const auto& [prop, bindings] : pending) {
        if (!restoreReference(*prop, bindings, doc, arrived)) {
            // Kept rather than dropped: the target may be absent because a branch deleted it, and
            // a save that wrote the emptiness back would erase where the reference pointed -- the
            // one thing a merge needs to tell a deletion from a reference nobody ever made.
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

    // A formula cannot be bound while the objects it names are still arriving, so the document
    // has a second pass for exactly this and the archive's own load path uses it. Reading a
    // recipe is reading a document, and it finishes the same way -- unless the caller is a
    // document being opened, which runs that pass itself once every document in the set is read.
    if (how.finish) {
        doc.afterRestore(restored, false);
    }

    // Everything read from a recipe still has to be built: the file carries the steps, never the
    // geometry they produce. So each object is marked as needing a rebuild -- a document read
    // from a recipe that reported itself up to date would be claiming geometry it does not have.
    for (DocumentObject* obj : restored) {
        obj->enforceRecompute();
    }

    // A node is blocked by what it holds, not by having been asked to rebuild: an object whose
    // geometry comes back from the rebuild store is never asked, and would otherwise report itself
    // up to date while its file states something this session could not produce (Amendment 19).
    doc.blockWhatCouldNotBeHonoured();
}
