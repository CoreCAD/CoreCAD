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

/// A property a person added whose value is not carried. It exists only because the file
/// declares it, so the declaration is still written. `Prop_NoPersist` means not even that.
bool onlyItsDeclarationIsCarried(const Property& prop, const PropertyContainer& owner)
{
    if (!prop.testStatus(Property::PropDynamic) || prop.testStatus(Property::PropNoPersist)) {
        return false;
    }
    return (owner.getPropertyType(&prop) & Prop_NoPersist) == 0;
}

/// A writer that reports whether the property asked for a side file instead of writing inline.
class ScratchWriter: public Base::StringWriter
{
public:
    bool wantedSideFile() const
    {
        return !FileList.empty();
    }
};

/// A property's own serialization of itself, and whether it asked for a file of its own.
struct OwnWords
{
    std::string text;
    bool wantedSideFile {false};
};

/// The property's own serialization of itself, inline and at full precision.
OwnWords writtenByItsOwnSerializer(const Property& prop)
{
    ScratchWriter scratch;
    scratch.Stream().precision(std::numeric_limits<double>::max_digits10);
    scratch.setForceXML(true);
    scratch.incInd();
    scratch.incInd();
    prop.Save(scratch);
    return {scratch.getString(), scratch.wantedSideFile()};
}

/// What the stored form says about one property: its value, or the reason there is none. A
/// property added at runtime carries its declaration too, or a reader would have nowhere to put it.
struct StoredProperty
{
    std::string name;
    std::string type;
    std::string body;   ///< the property's own serialization, empty when unrecorded
    std::string reason; ///< why it is unrecorded, empty when it is recorded
    bool dynamic {false};
    std::string group;
    std::string documentation;
    short attributes {0};
    bool readOnly {false};
    bool hidden {false};
    bool isReference {false};
    bool valueStated {true};  ///< false when the file carries the declaration and no value
    std::string verbatim;  ///< the file's own words for a property this build has no place for
    std::vector<Binding> bindings;
    std::string asset;  ///< the id of the file holding this value, empty when written inline
};

/// What a reference points at, as durable ids, or nothing when that kind of reference cannot say.
/// Never names: a name changes when documents merge (§10.1).
std::optional<std::vector<Binding>> referenceBindings(const Property& prop, const Document* home)
{
    const auto* link = dynamic_cast<const PropertyLinkBase*>(&prop);
    if (link == nullptr) {
        return std::nullopt;
    }
    std::vector<PropertyLinkBase::Pointing> pointing;
    if (!link->statesWhereItPoints(pointing)) {
        return std::nullopt;
    }

    std::vector<Binding> bindings;
    bindings.reserve(pointing.size());
    for (const PropertyLinkBase::Pointing& one : pointing) {
        // An empty target would read as "points somewhere unnamed", which the reader refuses.
        if (one.target == nullptr) {
            continue;
        }
        Binding binding {one.target->Uid.getValueStr(), one.sub};
        binding.noPart = one.noPart;
        binding.external = home != nullptr && one.target->getDocument() != home;
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

bool isReference(const Property& prop)
{
    return prop.isDerivedFrom(PropertyLinkBase::getClassTypeId());
}

/// A writer that names an entry's side files by order alone ("1.brp"), not after the owning
/// object, so identical values produce identical entries.
class AssetWriter: public Base::FileWriter
{
public:
    using Base::FileWriter::FileWriter;

    std::string addFile(const char* Name, const Base::Persistence* Object) override
    {
        const std::string requested = Name != nullptr ? Name : "";
        const std::string::size_type dot = requested.rfind('.');
        const std::string extension = dot == std::string::npos ? "" : requested.substr(dot);
        return Base::FileWriter::addFile((std::to_string(++_given) + extension).c_str(), Object);
    }

private:
    int _given {0};
};

/// What became of a handed-in value on its way to the source store. "Nothing to keep" and
/// "failed" must stay distinct, or a failed store reads as an empty property.
struct AssetOutcome
{
    enum class Result
    {
        Stored,
        NothingToKeep,
        Failed
    };

    Result result {Result::NothingToKeep};
    std::string id;
};

/// Write a value into the source store the way the archive writes it (its own `Save`, so mapped
/// element names survive), in an entry named by a digest of everything it holds.
AssetOutcome storeAsset(const Property& prop, const std::string& directory)
{
    // Staged first: the entry cannot be named until everything in it exists.
    const fs::path staging = fs::path(directory) / (".staging-" + Base::Uuid::createUuid());
    std::error_code failed;
    fs::create_directories(staging, failed);

    const auto abandon = [&staging](AssetOutcome::Result result) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return AssetOutcome {result, {}};
    };

    try {
        AssetWriter writer(staging.string().c_str());
        writer.Stream().precision(std::numeric_limits<double>::max_digits10);
        writer.putNextEntry(assetContentFile);
        writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n<Value>\n";
        prop.Save(writer);
        writer.Stream() << "</Value>\n";
        writer.writeFiles();
    }
    catch (const std::exception&) {
        return abandon(AssetOutcome::Result::Failed);
    }

    // Every file and its name, in a fixed order, so the same value gets the same id anywhere.
    QCryptographicHash digest(QCryptographicHash::Sha1);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(staging, failed)) {
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    bool holdsBulk = false;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        digest.addData(QByteArray(name.data(), static_cast<int>(name.size())));
        Base::ifstream stream(Base::FileInfo(file.string()), std::ios::in | std::ios::binary);
        const std::string bytes {std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
        if (name != assetContentFile && !bytes.empty()) {
            holdsBulk = true;
        }
        digest.addData(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
    }
    if (!holdsBulk) {
        return abandon(AssetOutcome::Result::NothingToKeep);
    }

    const std::string id = QString::fromLatin1(digest.result().toHex()).toStdString();
    const fs::path entry = fs::path(directory) / id;
    if (fs::exists(entry, failed)) {
        // Already stored: the name is the content.
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        return AssetOutcome {AssetOutcome::Result::Stored, id};
    }
    fs::rename(staging, entry, failed);
    if (failed) {
        return abandon(AssetOutcome::Result::Failed);
    }
    return AssetOutcome {AssetOutcome::Result::Stored, id};
}

/// A property's name, type and, for one a person added, its declaration.
StoredProperty declarationOf(const std::string& name,
                             const Property& prop,
                             const PropertyContainer& owner)
{
    StoredProperty entry;
    entry.name = name;
    entry.type = prop.getTypeId().getName();
    if (prop.testStatus(Property::PropDynamic)) {
        const DynamicProperty::PropData data = owner.getDynamicPropertyData(&prop);
        entry.dynamic = true;
        entry.group = data.group;
        entry.documentation = data.doc;
        entry.attributes = data.attr;
        entry.readOnly = data.readonly;
        entry.hidden = data.hidden;
    }
    return entry;
}

/// A reference, stated as the durable ids it points at wherever this file can say so.
void stateReference(StoredProperty& entry,
                    const std::string& name,
                    const Property& prop,
                    const PropertyContainer& owner)
{
    // Formulas point at objects from inside their text; the engine's own serializer writes the
    // durable id beside each one.
    if (prop.isDerivedFrom(PropertyExpressionContainer::getClassTypeId())) {
        entry.body = writtenByItsOwnSerializer(prop).text;
        return;
    }

    // A target this session could not resolve is written as the file stated it, not as the empty
    // live value (Amendment 19).
    if (const auto* kept = owner.unresolvedReference(name.c_str())) {
        for (const PropertyContainer::StatedTarget& target : *kept) {
            entry.bindings.push_back({target.uuid, target.sub, /*external=*/false, target.noPart});
        }
        entry.isReference = true;
        return;
    }

    const auto* asObject = dynamic_cast<const DocumentObject*>(&owner);
    const std::optional<std::vector<Binding>> bindings =
        referenceBindings(prop, asObject != nullptr ? asObject->getDocument() : nullptr);
    if (!bindings) {
        entry.reason = "reference kind not carried yet";
        return;
    }

    const bool leavesTheDocument =
        std::any_of(bindings->begin(), bindings->end(), [](const Binding& binding) {
            return binding.external;
        });
    if (leavesTheDocument) {
        // The link's own serializer already names the other document durably (Clause 3.7).
        entry.body = writtenByItsOwnSerializer(prop).text;
        return;
    }

    entry.bindings = *bindings;
    entry.isReference = true;
}

/// A value, inline when it can be read there, otherwise in the source store. False when the
/// property holds nothing to state.
bool stateValue(StoredProperty& entry, const Property& prop, const std::string& assetDirectory)
{
    if (!prop.holdsOpaqueBulk()) {
        const OwnWords words = writtenByItsOwnSerializer(prop);
        if (!words.wantedSideFile) {
            if (words.text.empty()) {
                // An empty block would make its reader report the whole document corrupt.
                entry.reason = "the property wrote no value";
            }
            else {
                entry.body = words.text;
            }
            return true;
        }
        // A fault in the property, not a reason to drop the value: store it like bulk.
        Base::Console().warning("Stored recipe: property '%s' (%s) states no value inline "
                                "yet asks for a file of its own.\n",
                                entry.name.c_str(),
                                entry.type.c_str());
    }

    if (!assetDirectory.empty()) {
        const AssetOutcome outcome = storeAsset(prop, assetDirectory);
        if (outcome.result == AssetOutcome::Result::NothingToKeep) {
            return false;
        }
        if (outcome.result == AssetOutcome::Result::Failed) {
            entry.reason = "value could not be written to the project store";
            return true;
        }
        entry.asset = outcome.id;
        return true;
    }

    // No store yet (the document has no folder): inline is better than nothing.
    const OwnWords inlined = writtenByItsOwnSerializer(prop);
    if (!inlined.wantedSideFile && !inlined.text.empty()) {
        entry.body = inlined.text;
    }
    else {
        entry.reason = "value kept beside the document, which has no folder yet";
    }
    return true;
}

/// What the stored form says about one property, or nothing when it is not the file's to state.
std::optional<StoredProperty> storedProperty(const std::string& name,
                                             const Property& prop,
                                             const PropertyContainer& owner,
                                             const std::string& assetDirectory)
{
    const bool carried = App::theRecipeCarries(prop, owner);
    const bool declaredOnly = !carried && onlyItsDeclarationIsCarried(prop, owner);
    if (!carried && !declaredOnly) {
        return std::nullopt;
    }
    if (owner.statedProperties().count(name) != 0) {
        // The file's own kept words stand for this name; the caller writes them.
        return std::nullopt;
    }

    StoredProperty entry = declarationOf(name, prop, owner);
    if (declaredOnly) {
        entry.valueStated = false;
        return entry;
    }
    if (isReference(prop)) {
        stateReference(entry, name, prop, owner);
        return entry;
    }

    // Material the file named and this session could not find: keep the name so it can be
    // reunited, and state the gap.
    const std::string missing = owner.missingSource(name.c_str());
    if (!missing.empty()) {
        entry.asset = missing;
        entry.reason = "source material '" + missing + "' was not found";
        return entry;
    }

    if (!stateValue(entry, prop, assetDirectory)) {
        return std::nullopt;
    }
    return entry;
}

/// Everything the stored form has to say about one container's properties, in name order.
std::vector<StoredProperty> storedProperties(const PropertyContainer& owner,
                                            const std::string& assetDirectory)
{
    std::map<std::string, Property*> properties;
    owner.getPropertyMap(properties);

    std::vector<StoredProperty> stored;
    for (const auto& [name, prop] : properties) {
        if (prop == nullptr) {
            continue;
        }
        if (std::optional<StoredProperty> entry =
                storedProperty(name, *prop, owner, assetDirectory)) {
            stored.push_back(std::move(*entry));
        }
    }

    // Kept words go back in name order, so a save that changed nothing changes nothing.
    for (const auto& [name, stated] : owner.statedProperties()) {
        StoredProperty kept;
        kept.name = name;
        kept.verbatim = stated.words;
        stored.push_back(kept);
    }
    std::stable_sort(stored.begin(),
                     stored.end(),
                     [](const StoredProperty& left, const StoredProperty& right) {
                         return left.name < right.name;
                     });

    return stored;
}

/// One `<Properties>` block, then `<Unrecorded>`: what could not be carried, with the reason.
void writeProperties(Base::Writer& writer,
                     const PropertyContainer& owner,
                     const std::string& assetDirectory)
{
    const std::vector<StoredProperty> stored = storedProperties(owner, assetDirectory);

    // Missing source material is in both lists: the name is carried and the gap is reported.
    std::vector<const StoredProperty*> recorded;
    std::vector<const StoredProperty*> unrecorded;
    for (const StoredProperty& entry : stored) {
        if (entry.reason.empty() || !entry.asset.empty()) {
            recorded.push_back(&entry);
        }
        if (!entry.reason.empty()) {
            unrecorded.push_back(&entry);
        }
    }

    writer.Stream() << writer.ind() << "<Properties>\n";
    writer.incInd();
    for (const StoredProperty* entry : recorded) {
        if (!entry->verbatim.empty()) {
            // Already at the right depth: properties are only ever written at one depth.
            writer.Stream() << entry->verbatim;
            continue;
        }
        writer.Stream() << writer.ind() << "<Property" << attribute("name", entry->name)
                        << attribute("type", entry->type);
        if (entry->isReference) {
            writer.Stream() << attribute("reference", "1");
        }
        if (!entry->asset.empty()) {
            writer.Stream() << attribute("asset", entry->asset);
        }
        if (entry->dynamic) {
            writer.Stream() << attribute("dynamic", "1") << attribute("group", entry->group)
                            << attribute("doc", entry->documentation)
                            << attribute("attributes", std::to_string(entry->attributes))
                            << attribute("readonly", entry->readOnly ? "1" : "0")
                            << attribute("hidden", entry->hidden ? "1" : "0");
        }
        if (!entry->valueStated) {
            // Declared with no value, which differs from an empty value.
            writer.Stream() << "/>\n";
            continue;
        }
        writer.Stream() << ">\n";
        if (entry->isReference) {
            writer.incInd();
            writer.Stream() << writer.ind() << "<Reference>\n";
            writer.incInd();
            for (const Binding& binding : entry->bindings) {
                // No part is not the same as `sub=""`, an empty part.
                writer.Stream() << writer.ind() << "<Target" << attribute("uuid", binding.uuid);
                if (!binding.noPart) {
                    writer.Stream() << attribute("sub", binding.sub);
                }
                writer.Stream() << "/>\n";
            }
            writer.decInd();
            writer.Stream() << writer.ind() << "</Reference>\n";
            writer.decInd();
        }
        else if (entry->asset.empty()) {
            writer.Stream() << entry->body;
        }
        writer.Stream() << writer.ind() << "</Property>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Properties>\n";

    writer.Stream() << writer.ind() << "<Unrecorded>\n";
    writer.incInd();
    for (const StoredProperty* entry : unrecorded) {
        writer.Stream() << writer.ind() << "<Property" << attribute("name", entry->name)
                        << attribute("type", entry->type) << attribute("reason", entry->reason)
                        << "/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Unrecorded>\n";
}

/// One object's block: what it is, and what it was authored to be.
void writeObject(Base::Writer& writer,
                 const DocumentObject& obj,
                 const std::string& assetDirectory,
                 bool withAppearance)
{
    const PropertyContainer* appearance = withAppearance ? appearanceOf(obj) : nullptr;
    // Appearance this session had nowhere to put, given back only when nothing live replaces it.
    const std::string& kept = obj.statedAppearance();
    const bool keepsAppearance = withAppearance && !kept.empty();
    const bool statesAppearance = appearance != nullptr || keepsAppearance;

    // Script-granted capabilities: the class does not compose them, so the reader must be told.
    std::vector<std::string> asked;
    for (auto it = const_cast<DocumentObject&>(obj).extensionBegin();
         it != const_cast<DocumentObject&>(obj).extensionEnd();
         ++it) {
        if (it->second != nullptr && it->second->isPythonExtension()) {
            asked.emplace_back(it->second->getExtensionTypeId().getName());
        }
    }

    writer.Stream() << writer.ind() << "<Object" << attribute("uuid", obj.Uid.getValueStr())
                    << attribute("type", obj.getTypeId().getName())
                    << attribute("name", obj.getNameInDocument());
    if (!asked.empty()) {
        // Marked so a reader knows to expect the block without looking ahead.
        writer.Stream() << attribute("extensions", "1");
    }
    if (statesAppearance) {
        writer.Stream() << attribute("display", "1");
    }
    writer.Stream() << ">\n";
    writer.incInd();
    if (!asked.empty()) {
        writer.Stream() << writer.ind() << "<Extensions>\n";
        writer.incInd();
        for (const std::string& type : asked) {
            writer.Stream() << writer.ind() << "<Extension" << attribute("type", type) << "/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</Extensions>\n";
    }
    writeProperties(writer, obj, assetDirectory);
    if (appearance != nullptr) {
        writer.Stream() << writer.ind() << "<Display>\n";
        writer.incInd();
        writeProperties(writer, *appearance, assetDirectory);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Display>\n";
    }
    else if (keepsAppearance) {
        writer.Stream() << kept;
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Object>\n";
}

}  // namespace

std::string App::formatStoredRecipe(const Document& doc,
                                    const std::string& assetDirectory,
                                    const RecipeScope& scope)
{
    Base::StringWriter writer;
    // Precision belongs to the writer; without this it rounds to six digits.
    writer.Stream().precision(std::numeric_limits<double>::max_digits10);

    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<Recipe" << attribute("Version", std::to_string(storedRecipeFormat))
                    << ">\n";
    writer.incInd();

    // A copy states no block rather than an empty one: it says nothing about a document.
    if (scope.withDocumentProperties) {
        writer.Stream() << writer.ind() << "<Document" << attribute("uuid", doc.Uid.getValueStr())
                        << ">\n";
        writer.incInd();
        writeProperties(writer, doc, assetDirectory);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Document>\n";
    }

    // Uid order, not creation order, so an inserted feature reads as one added block.
    const bool wholeDocument = scope.objects.empty();
    std::vector<const DocumentObject*> objects;
    if (wholeDocument) {
        for (const DocumentObject* obj : doc.getObjects()) {
            if (obj != nullptr) {
                objects.push_back(obj);
            }
        }
    }
    else {
        for (const DocumentObject* obj : scope.objects) {
            if (obj != nullptr) {
                objects.push_back(obj);
            }
        }
    }
    std::sort(objects.begin(),
              objects.end(),
              [](const DocumentObject* left, const DocumentObject* right) {
                  return left->Uid.getValueStr() < right->Uid.getValueStr();
              });

    writer.Stream() << writer.ind() << "<Objects>\n";
    writer.incInd();
    // Blocks this build could not construct go back in Uid order among the rest, so an unchanged
    // save is unchanged. A partial rendering carries none: nobody picked them.
    const std::vector<std::array<std::string, 3>> noneKept;
    const auto& kept = wholeDocument ? doc.unreadObjects() : noneKept;
    std::size_t nextKept = 0;
    const auto writeKeptUpTo = [&](const std::string& limit, bool toEnd) {
        while (nextKept < kept.size() && (toEnd || kept[nextKept][0] < limit)) {
            std::istringstream block(kept[nextKept][2]);
            std::string line;
            while (std::getline(block, line)) {
                if (line.empty()) {
                    writer.Stream() << "\n";
                }
                else {
                    writer.Stream() << writer.ind() << line << "\n";
                }
            }
            ++nextKept;
        }
    };
    for (const DocumentObject* obj : objects) {
        writeKeptUpTo(obj->Uid.getValueStr(), false);
        writeObject(writer, *obj, assetDirectory, /*withAppearance=*/true);
    }
    writeKeptUpTo(std::string {}, true);
    writer.decInd();
    writer.Stream() << writer.ind() << "</Objects>\n";

    writer.decInd();
    writer.Stream() << "</Recipe>\n";

    return writer.getString();
}

std::string App::formatStoredRecipeObject(const DocumentObject& obj,
                                          const std::string& assetDirectory)
{
    Base::StringWriter writer;
    // Same precision as the whole document, or the same object would read as a different one.
    writer.Stream().precision(std::numeric_limits<double>::max_digits10);
    writeObject(writer, obj, assetDirectory, /*withAppearance=*/false);
    return writer.getString();
}
