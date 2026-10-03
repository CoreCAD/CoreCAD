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

/// A property a person added at runtime, whose VALUE this form does not carry.
///
/// Declaring a property and giving it a value are two acts, and the flags answer only the second.
/// A property built into a class is declared by the class, so dropping it from the file loses a
/// value and nothing else; a property a person added exists only because the file says it does,
/// and dropping it loses the property itself -- it does not come back at all, and neither does
/// anything that named it. Measured before this: a part declaring one property per flag came
/// back missing the two whose values are not authored source, with no complaint.
///
/// `Prop_NoPersist` is not among them, and means what it says: that property is not to be in the
/// file at all, declaration included.
bool onlyItsDeclarationIsCarried(const Property& prop, const PropertyContainer& owner)
{
    if (!prop.testStatus(Property::PropDynamic) || prop.testStatus(Property::PropNoPersist)) {
        return false;
    }
    return (owner.getPropertyType(&prop) & Prop_NoPersist) == 0;
}

/// A writer that can be asked whether the property just written wanted a file of its own.
///
/// A property big enough to be stored beside the XML (a shape, an image) asks the writer for a
/// side file rather than writing its value inline. Nothing here can carry that yet, and a value
/// that vanishes silently is exactly the failure this whole exercise is meant to remove — so the
/// property is written to a scratch writer first, and one that asked for a file is named as
/// unrecorded instead of half-written.
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

/// The property's own serialization of itself, inline.
///
/// The one value dialect in the program is the property's own writer, so anything the recipe
/// cannot say better in its own words is said in that one -- at full precision, and inline,
/// because a recipe that pointed at a second file would not be one file you can read.
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

/// What the stored form says about one property: its value, or the reason there is none.
///
/// A property an object was given at runtime carries its declaration too. The readable view
/// prints such a value like any other, which reads perfectly well and cannot be read back: a
/// document rebuilt from it would have nowhere to put the value, because the property it belongs
/// to does not exist until something declares it.
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

/// What a reference property points at, or nothing when that kind of reference cannot say.
///
/// The archive writes a link as the target's in-document name. A stored recipe may not: a name
/// is the document's own bookkeeping and changes when a document is merged into another, which
/// is precisely the positional addressing this direction exists to remove (§10.1). So a link is
/// read here as durable ids, and written as durable ids.
///
/// The property answers for itself. This used to be a ladder of class tests, written out twice --
/// once here and once to point a reference again -- which is the file knowing what a property IS
/// instead of asking what it can DO, and which says nothing at all about the one link class
/// nobody remembered to add to both lists.
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
        // A link that points at nothing is written as nothing. Recording an empty target would say
        // "this reference points somewhere I could not name", which is a different fact and one the
        // reader would rightly refuse to restore.
        if (one.target == nullptr) {
            continue;
        }
        Binding binding {one.target->Uid.getValueStr(), one.sub};
        binding.noPart = one.noPart;
        // A target in another document is a second question -- which document -- and the link
        // property answers it in its own writing. Marked here so the caller can hand the whole
        // property over rather than saying half of it in this file's words.
        binding.external = home != nullptr && one.target->getDocument() != home;
        bindings.push_back(std::move(binding));
    }
    return bindings;
}

bool isReference(const Property& prop)
{
    return prop.isDerivedFrom(PropertyLinkBase::getClassTypeId());
}

/// Put a value the recipe cannot say inline into the project's source folder, and answer by what
/// it holds.
///
/// The value is written **the way the document archive writes it** -- the property's own `Save`,
/// producing a small element that names its side files, and the side files themselves. The earlier
/// form asked only for the raw bytes (`SaveDocFile`), which is most of a shape and not all of it:
/// the mapped-element names and the hasher table are written by `Save` and were dropped. Measured:
/// a shape copied from a feature carried 26 mapped element names before a save and none after --
/// the face identities were destroyed silently, which is the one failure durable identity exists to
/// prevent. The asymmetry was also backwards, since a built shape's map can be regenerated by
/// rebuilding it and a handed-in shape's cannot.
///
/// The entry is named by a digest of everything it holds, so an unchanged import writes nothing new
/// and one body handed to five parts is stored once -- unless the copies carry different mapped
/// names, in which case they are different content and are stored separately, which is correct.
///
/// Nothing here removes an entry that has stopped being referenced. Collecting those is a separate
/// operation on a project, not something a single save can decide.
/// A writer that gives the files inside a source-store entry names of its own.
///
/// A property names its side files after the object that holds it ("First.Shape.brp"), which is
/// right inside an archive belonging to one document and wrong for a store whose entries are named
/// by what they hold: the same body handed to two objects would produce two entries differing only
/// in a name nobody reads. The names here depend on nothing but the order the property asked for
/// them, so identical values produce identical entries.
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

/// What became of a handed-in value on its way to the project's source folder.
///
/// "There was nothing to keep" and "it could not be written" were one answer before, and the
/// caller could only act on the first meaning -- so a store that failed dropped the value from
/// the record exactly as if the object had never held one. They are different facts and the file
/// has to be able to state each of them.
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

AssetOutcome storeAsset(const Property& prop, const std::string& directory)
{
    // Written to one side first, because the entry cannot be named until everything in it exists.
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

    // The digest covers every file and its name, in a fixed order, so the same value reaches the
    // same name on any machine and a different value can never reach the same one.
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
        // No value to keep. Not a gap and not a value -- the property holds nothing, and a document
        // rebuilt without it holds nothing too.
        return abandon(AssetOutcome::Result::NothingToKeep);
    }

    const std::string id = QString::fromLatin1(digest.result().toHex()).toStdString();
    const fs::path entry = fs::path(directory) / id;
    if (fs::exists(entry, failed)) {
        // The name is the content: an entry that is there already holds exactly this value.
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

/// Everything the stored form says about a property before anything is said about its value:
/// what it is called, what kind it is, and -- for one a person added -- its declaration.
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
    // An expression engine is a reference property by inheritance -- it holds the formulas an
    // object was given -- but what it points at lives inside the text of each formula, which
    // speaks object names. It is written by its own serializer here, which states the durable
    // identity of each intra-document reference beside the formula, keyed by the part of the text
    // it was written from. References that leave the document remain name-based, reserved for the
    // PropertyXLink step.
    if (prop.isDerivedFrom(PropertyExpressionContainer::getClassTypeId())) {
        entry.body = writtenByItsOwnSerializer(prop).text;
        return;
    }

    // Where this session could not resolve what the file stated, the file says it again. The
    // live property holds nothing, so deriving the reference from memory would write "points at
    // nothing" over "points at that object" -- a fact about this session published as a fact
    // about the design (Amendment 19).
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
        // A reference that leaves the document asks a second question -- which document -- and
        // the link property already answers it durably: it writes the target document's uuid
        // beside the object's, keeping the file path as a locator hint only (Clause 3.7). So the
        // file lets that property speak for itself rather than inventing a second, weaker way of
        // saying the same thing.
        entry.body = writtenByItsOwnSerializer(prop).text;
        return;
    }

    entry.bindings = *bindings;
    entry.isReference = true;
}

/// A value, stated inline when it can be read there and placed in the source store when not.
///
/// Answers false when there is nothing to state at all: a property holding no bulk is not a gap
/// and not a value, and a document rebuilt without it holds nothing too.
bool stateValue(StoredProperty& entry, const Property& prop, const std::string& assetDirectory)
{
    // The property states its own value in the file. That is the rule and not the exception: a
    // colour, a placement, a list of numbers is authored content, and a record that pointed at a
    // second file for it could lose the value while still looking complete.
    if (!prop.holdsOpaqueBulk()) {
        const OwnWords words = writtenByItsOwnSerializer(prop);
        if (!words.wantedSideFile) {
            if (words.text.empty()) {
                // The property wrote nothing and asked for nowhere to put it. An empty block is
                // not a value: its own reader looks for an element that is not there and reports
                // the whole document as corrupt. Named as a gap, which is what it is.
                entry.reason = "the property wrote no value";
            }
            else {
                entry.body = words.text;
            }
            return true;
        }
        // It says it is not bulk and then asks for a file anyway. That is a fault in the property,
        // not a reason to drop the value, so it goes to the store like bulk does and the fault
        // stays visible in the file.
        Base::Console().warning("Stored recipe: property '%s' (%s) states no value inline "
                                "yet asks for a file of its own.\n",
                                entry.name.c_str(),
                                entry.type.c_str());
    }

    // Compiled bulk -- a solid, a mesh, a point cloud, an embedded file. It goes to the project's
    // source store and the recipe names it by the content it holds, which stores one imported
    // body once however many parts use it.
    if (!assetDirectory.empty()) {
        const AssetOutcome outcome = storeAsset(prop, assetDirectory);
        if (outcome.result == AssetOutcome::Result::NothingToKeep) {
            return false;
        }
        if (outcome.result == AssetOutcome::Result::Failed) {
            // The value is real and the store would not take it. Named, because a write that
            // failed and a property that was empty must not read the same on disk.
            entry.reason = "value could not be written to the project store";
            return true;
        }
        entry.asset = outcome.id;
        return true;
    }

    // No store to put it in -- a document with no folder yet. Whatever the property can say
    // inline is better than nothing, and for a solid that is the whole solid as text.
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
        // The file's own words stand for this name until something supersedes them, and they are
        // written by the caller. Saying it twice would be a file that disagrees with itself.
        return std::nullopt;
    }

    StoredProperty entry = declarationOf(name, prop, owner);
    if (declaredOnly) {
        // Stated as a property that exists and no more. The value is whatever the object makes of
        // it on the next recompute, which is exactly what its flags say.
        entry.valueStated = false;
        return entry;
    }
    if (isReference(prop)) {
        stateReference(entry, name, prop, owner);
        return entry;
    }

    // Source material this session was told about and could not find. The property holds
    // nothing as a result, and re-deriving the record from what is in memory would write that
    // emptiness over the name -- destroying the one thing that could reunite the document with
    // its material. The record keeps the name and states the gap.
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

    // Given back in their own place among the properties this build does understand, so a save
    // that changed nothing changes nothing (Amendment 18 Clause 18.1).
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

/// One `<Properties>` block: the values, then the properties this form cannot yet carry, each
/// with the reason it could not. A reader of the file never has to guess what is missing.
void writeProperties(Base::Writer& writer,
                     const PropertyContainer& owner,
                     const std::string& assetDirectory)
{
    const std::vector<StoredProperty> stored = storedProperties(owner, assetDirectory);

    // A property can be in both lists at once, and one of them is exactly why: a record that
    // names source material it cannot find still CARRIES the name -- so the value block keeps it
    // and can be resolved by putting the material back -- while the gap is reported alongside,
    // because a file that carried the name and said nothing would read as a file with no gap.
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
            // Exactly as the file said it, at the depth the file said it: this form writes a
            // property at one depth, so its own words are already the words that belong here.
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
            // Closed where it stands: the file states that the property exists and states no
            // value for it, which is a different thing from stating an empty one.
            writer.Stream() << "/>\n";
            continue;
        }
        writer.Stream() << ">\n";
        if (entry->isReference) {
            // A reference is written as the durable ids it points at -- never as the target's
            // name, which is the document's own bookkeeping and does not survive being merged
            // into another document (§10.1).
            writer.incInd();
            writer.Stream() << writer.ind() << "<Reference>\n";
            writer.incInd();
            for (const Binding& binding : entry->bindings) {
                // A target named with no part says nothing about a part. `sub=""` is a part too
                // -- an empty one -- and writing the two alike made the reader drop it (#137).
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
    // What the file stated and this session had nowhere to put. Given back exactly as it was
    // read, and only where nothing live supersedes it: a session that applied the block writes
    // what it holds, and would say the same thing twice otherwise.
    const std::string& kept = obj.statedAppearance();
    const bool keepsAppearance = withAppearance && !kept.empty();
    const bool statesAppearance = appearance != nullptr || keepsAppearance;

    // A capability a script asked for by name: the object's class does not compose it, so nothing
    // would put it back on the way in, and the properties it carries would arrive with nowhere to
    // go. Stated the way a type is stated -- the file names what the object holds; it does not
    // decide what code runs, and a name here is looked up in what this build already has.
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
        // Marked on the object for the same reason the appearance is: so a reader knows whether
        // to expect the block without having to look ahead for it.
        writer.Stream() << attribute("extensions", "1");
    }
    if (statesAppearance) {
        // Marked on the object, so a reader knows whether to expect the block without having to
        // look ahead for it.
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
    // Full precision, set here because it belongs to the WRITER and not to the value: the
    // document archive is exact only because the archive's own writer asks for these digits, so
    // any second writer starts out quietly rounding to six of them. Measured: a length of
    // 12.345678901234567 came back as 12.3457 before this line existed.
    writer.Stream().precision(std::numeric_limits<double>::max_digits10);

    // The format the statements below are written to, stated so that a reader can ask whether it
    // reads this file at all before it starts interpreting it (Clause 19.7). The one constant the
    // reader checks against, so the stamp can never come to mean something the check does not.
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<Recipe" << attribute("Version", std::to_string(storedRecipeFormat))
                    << ">\n";
    writer.incInd();

    // The document's own authored facts -- who wrote it, when it was created, what it is called
    // -- belong to the recipe as much as any object does. The walk that produces the readable
    // view covers objects only, which is why a document's own content had nowhere to go.
    //
    // A rendering that carries objects alone states no block here at all rather than an empty
    // one: "this rendering says nothing about a document" and "the document states nothing" are
    // different facts, and only the first one is true of a copy.
    if (scope.withDocumentProperties) {
        writer.Stream() << writer.ind() << "<Document" << attribute("uuid", doc.Uid.getValueStr())
                        << ">\n";
        writer.incInd();
        writeProperties(writer, doc, assetDirectory);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Document>\n";
    }

    // Objects in durable-id order. Creation order is a fact about the session that produced the
    // document, not about the design, and letting it set the order in the file is what makes an
    // inserted feature read as a rewritten file.
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
    // Blocks this build could not construct are given back in the same durable-id order as the
    // objects it could, so each one lands exactly where it was and a save that changed nothing
    // changes nothing (Amendment 19, Amendment 18 Clause 18.1).
    // A rendering of part of the document carries none of them: a block this build could not
    // construct is not an object anybody can pick, and putting every one of them into a copy of
    // two features would state content nobody asked to copy.
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
    // The same digits the whole-document writer asks for. A block rendered at a different
    // precision would describe the same object and read as a different one.
    writer.Stream().precision(std::numeric_limits<double>::max_digits10);
    writeObject(writer, obj, assetDirectory, /*withAppearance=*/false);
    return writer.getString();
}
