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
# include <cstring>
# include <filesystem>
# include <fstream>
# include <iterator>
# include <limits>
# include <map>
# include <optional>
# include <sstream>
# include <string>
# include <vector>
#endif

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

#include <Base/FileInfo.h>
#include <Base/Uuid.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "StoredRecipe.h"

#include <Base/Console.h>

#include "Document.h"
#include "DynamicProperty.h"
#include "DocumentObject.h"
#include "GeoFeature.h"
#include "Property.h"
#include "PropertyContainer.h"
#include "PropertyExpressionEngine.h"
#include "PropertyGeo.h"
#include "PropertyLinks.h"
#include "Services.h"

#include <Base/ServiceProvider.h>

using namespace App;

namespace fs = std::filesystem;

namespace
{

/// Derived and non-persisted state is never authored source. The same declaration the recipe
/// emitter reads (ObjectRecipe.cpp), so the stored form and the readable view agree about what
/// counts as authored — two answers to that question would be two definitions of the document.
constexpr short excludedPropertyFlags = Prop_Output | Prop_Transient | Prop_NoPersist;

/// Properties that ARE authored even though their declared flags say otherwise.
///
/// An object's name for the user is declared `Prop_Output`, which in this vocabulary means
/// "changing it need not recompute anything" and not "the program computed it" — but the stored
/// form reads that same flag as the line between source and rebuilt output, so a name a person
/// typed was being dropped. The flags cannot answer a question they were not asked; until the
/// document model says outright which properties are authored, the exceptions are named here
/// rather than inferred, so the list is visible and short instead of silent.
bool isAuthoredDespiteItsFlags(const std::string& name)
{
    return name == "Label";
}

/// The one file inside a source-store entry that is not bulk: the property's own element, naming
/// what sits beside it.
constexpr const char* assetContentFile = "value.xml";

/// Geometry the recipe BUILDS, as opposed to geometry it was handed.
///
/// A part's shape is declared `Prop_None` -- it claims to be neither output nor transient -- so
/// the flags do not keep it out, and asking the writer to inline bulky values put the whole solid
/// into the recipe as text. That is not a big file, it is a wrong one: for a feature the recipe
/// is the source and the shape is what the source builds, and a file carrying both can disagree
/// with itself. Measured: with the shape inline, a test that rebuilt a part from the recipe with
/// none of its references restored still produced the right solid -- the file was answering with
/// the old geometry instead of rebuilding.
///
/// An imported solid, a scanned mesh, a measured point cloud are the opposite case: no property
/// of the document produces them, so the geometry IS the authored content. Excluding those by
/// kind dropped them without a word -- an imported part came back empty. The object answers for
/// itself (`holdsAuthoredGeometry`), because whether geometry is source or output is a fact
/// about the type that holds it and not about the property's class.
bool isBuiltGeometry(const Property& prop, const PropertyContainer& owner)
{
    if (!prop.isDerivedFrom(PropertyGeometry::getClassTypeId())) {
        return false;
    }
    const auto* object = dynamic_cast<const DocumentObject*>(&owner);
    return object == nullptr || !object->holdsAuthoredGeometry();
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

/// The property's own serialization of itself, inline.
///
/// The one value dialect in the program is the property's own writer, so anything the recipe
/// cannot say better in its own words is said in that one -- at full precision, and inline,
/// because a recipe that pointed at a second file would not be one file you can read.
std::string writtenByItsOwnSerializer(const Property& prop)
{
    ScratchWriter scratch;
    scratch.Stream().precision(std::numeric_limits<double>::max_digits10);
    scratch.setForceXML(true);
    scratch.incInd();
    scratch.incInd();
    prop.Save(scratch);
    return scratch.getString();
}

/// One end of a reference: the target's durable id, and the part of it that was picked.
///
/// A sub-element string ("Face6") names a position in a computed shape, not an identity, so it
/// can never be what the reference BINDS by — but leaving it out would lose which face a sketch
/// was drawn on. It rides alongside the durable id, exactly as the document archive already
/// carries a name and a position side by side.
struct Binding
{
    std::string uuid;
    std::string sub;
    bool external {false};  ///< the target lives in another document
};

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
    std::vector<Binding> bindings;
    std::string asset;  ///< the id of the file holding this value, empty when written inline
};

/// What a reference property points at, or nothing when this form cannot read that kind yet.
///
/// The archive writes a link as the target's in-document name. A stored recipe may not: a name
/// is the document's own bookkeeping and changes when a document is merged into another, which
/// is precisely the positional addressing this direction exists to remove (§10.1). So a link is
/// read here as durable ids, and written as durable ids.
std::optional<std::vector<Binding>> referenceBindings(const Property& prop, const Document* home)
{
    const auto bind = [home](const DocumentObject* target, const std::string& sub) {
        Binding binding {target->Uid.getValueStr(), sub};
        // A target in another document is a second question -- which document -- and the link
        // property answers it in its own writing. Marked here so the caller can hand the whole
        // property over rather than saying half of it in this file's words.
        binding.external = home != nullptr && target->getDocument() != home;
        return binding;
    };

    std::vector<Binding> bindings;

    // A link that points at nothing is written as nothing. Recording an empty target would say
    // "this reference points somewhere I could not name", which is a different fact and one the
    // reader would rightly refuse to restore.
    const auto add = [&bindings, &bind](const DocumentObject* target, const std::string& sub) {
        if (target != nullptr) {
            bindings.push_back(bind(target, sub));
        }
    };

    // Most-derived first: an XLink IS a PropertyLink, and a sub-list link is neither.
    if (const auto* link = dynamic_cast<const PropertyXLinkSubList*>(&prop)) {
        for (const DocumentObject* target : link->getValues()) {
            const std::vector<std::string> subs =
                link->getSubValues(const_cast<DocumentObject*>(target));
            if (subs.empty()) {
                add(target, {});
            }
            for (const std::string& sub : subs) {
                add(target, sub);
            }
        }
        return bindings;
    }
    if (const auto* link = dynamic_cast<const PropertyXLink*>(&prop)) {
        const std::vector<std::string>& subs = link->getSubValues();
        if (subs.empty()) {
            add(link->getValue(), {});
        }
        for (const std::string& sub : subs) {
            add(link->getValue(), sub);
        }
        return bindings;
    }
    if (const auto* link = dynamic_cast<const PropertyLinkSubList*>(&prop)) {
        const std::vector<DocumentObject*>& targets = link->getValues();
        const std::vector<std::string>& subs = link->getSubValues();
        for (std::size_t i = 0; i < targets.size(); ++i) {
            add(targets[i], i < subs.size() ? subs[i] : std::string());
        }
        return bindings;
    }
    if (const auto* link = dynamic_cast<const PropertyLinkSub*>(&prop)) {
        const std::vector<std::string>& subs = link->getSubValues();
        if (subs.empty()) {
            add(link->getValue(), {});
        }
        for (const std::string& sub : subs) {
            add(link->getValue(), sub);
        }
        return bindings;
    }
    if (const auto* link = dynamic_cast<const PropertyLinkList*>(&prop)) {
        for (const DocumentObject* target : link->getValues()) {
            add(target, {});
        }
        return bindings;
    }
    if (const auto* link = dynamic_cast<const PropertyLink*>(&prop)) {
        add(link->getValue(), {});
        return bindings;
    }

    return std::nullopt;
}

/// Point a reference property at objects again, given what the file said it pointed at.
bool restoreReference(Property& prop, const std::vector<Binding>& bindings, const Document& doc)
{
    std::vector<DocumentObject*> targets;
    std::vector<std::string> subs;
    for (const Binding& binding : bindings) {
        DocumentObject* target = nullptr;
        for (DocumentObject* candidate : doc.getObjects()) {
            if (candidate != nullptr && candidate->Uid.getValueStr() == binding.uuid) {
                target = candidate;
                break;
            }
        }
        if (target == nullptr) {
            // The file names a target this document does not hold. Refusing is the honest
            // answer: a reference that quietly points at nothing is the failure the durable-id
            // binding exists to prevent.
            return false;
        }
        targets.push_back(target);
        subs.push_back(binding.sub);
    }

    const bool anySub =
        std::any_of(subs.begin(), subs.end(), [](const std::string& sub) { return !sub.empty(); });

    if (auto* link = dynamic_cast<PropertyXLinkSubList*>(&prop)) {
        std::map<DocumentObject*, std::vector<std::string>> picked;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (!subs[i].empty()) {
                picked[targets[i]].push_back(subs[i]);
            }
            else {
                picked.emplace(targets[i], std::vector<std::string> {});
            }
        }
        link->setValues(picked);
        return true;
    }
    if (auto* link = dynamic_cast<PropertyXLink*>(&prop)) {
        std::vector<std::string> picked;
        std::copy_if(subs.begin(), subs.end(), std::back_inserter(picked), [](const auto& sub) {
            return !sub.empty();
        });
        link->setValue(targets.empty() ? nullptr : targets.front(), picked);
        return true;
    }
    if (auto* link = dynamic_cast<PropertyLinkSubList*>(&prop)) {
        link->setValues(targets, subs);
        return true;
    }
    if (auto* link = dynamic_cast<PropertyLinkSub*>(&prop)) {
        std::vector<std::string> picked;
        std::copy_if(subs.begin(), subs.end(), std::back_inserter(picked), [](const auto& sub) {
            return !sub.empty();
        });
        link->setValue(targets.empty() ? nullptr : targets.front(), picked);
        return true;
    }
    if (auto* link = dynamic_cast<PropertyLinkList*>(&prop)) {
        link->setValues(targets);
        return true;
    }
    if (auto* link = dynamic_cast<PropertyLink*>(&prop)) {
        if (anySub) {
            return false;
        }
        link->setValue(targets.empty() ? nullptr : targets.front());
        return true;
    }

    return false;
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
    reader.readElement("Value");
    prop.Restore(reader);
    reader.readEndElement("Value");
    // The bulk the element named -- the shape, its mapped element names, its hasher table -- is
    // asked for by name, exactly as the archive asks the zip for it.
    reader.readFiles(entry.string());
    return true;
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
        if ((owner.getPropertyType(prop) & excludedPropertyFlags) != 0
            && !isAuthoredDespiteItsFlags(name)) {
            continue;
        }
        if (prop->testStatus(Property::PropNoPersist)) {
            continue;
        }
        if (isBuiltGeometry(*prop, owner)) {
            continue;
        }

        StoredProperty entry;
        entry.name = name;
        entry.type = prop->getTypeId().getName();
        if (prop->testStatus(Property::PropDynamic)) {
            const DynamicProperty::PropData data = owner.getDynamicPropertyData(prop);
            entry.dynamic = true;
            entry.group = data.group;
            entry.documentation = data.doc;
            entry.attributes = data.attr;
            entry.readOnly = data.readonly;
            entry.hidden = data.hidden;
        }

        if (isReference(*prop)) {
            // An expression engine is a reference property by inheritance -- it holds the
            // formulas an object was given -- but what it points at lives inside the text of
            // each formula, which still speaks object names. It is written by its own
            // serializer here, and the names inside a formula remain the one binding in this
            // file that is not durable.
            if (prop->isDerivedFrom(PropertyExpressionContainer::getClassTypeId())) {
                entry.body = writtenByItsOwnSerializer(*prop);
                stored.push_back(entry);
                continue;
            }

            const auto* asObject = dynamic_cast<const DocumentObject*>(&owner);
            const std::optional<std::vector<Binding>> bindings =
                referenceBindings(*prop, asObject != nullptr ? asObject->getDocument() : nullptr);
            if (!bindings) {
                entry.reason = "reference kind not carried yet";
                stored.push_back(entry);
                continue;
            }

            const bool leavesTheDocument =
                std::any_of(bindings->begin(), bindings->end(), [](const Binding& binding) {
                    return binding.external;
                });
            if (leavesTheDocument) {
                // A reference that leaves the document asks a second question -- which document
                // -- and the link property already answers it durably: it writes the target
                // document's uuid beside the object's, keeping the file path as a locator hint
                // only (Clause 3.7). So the file lets that property speak for itself rather than
                // inventing a second, weaker way of saying the same thing.
                entry.body = writtenByItsOwnSerializer(*prop);
                stored.push_back(entry);
                continue;
            }

            entry.bindings = *bindings;
            entry.isReference = true;
            stored.push_back(entry);
            continue;
        }

        // Source material this session was told about and could not find. The property holds
        // nothing as a result, and re-deriving the record from what is in memory would write that
        // emptiness over the name -- destroying the one thing that could reunite the document
        // with its material. The record keeps the name and states the gap.
        const std::string missing = owner.missingSource(name.c_str());
        if (!missing.empty()) {
            entry.asset = missing;
            entry.reason = "source material '" + missing + "' was not found";
            stored.push_back(entry);
            continue;
        }

        // The property states its own value in the file. That is the rule and not the exception:
        // a colour, a placement, a list of numbers is authored content, and a record that pointed
        // at a second file for it could lose the value while still looking complete.
        if (!prop->holdsOpaqueBulk()) {
            ScratchWriter scratch;
            scratch.Stream().precision(std::numeric_limits<double>::max_digits10);
            scratch.setForceXML(true);
            scratch.incInd();
            scratch.incInd();
            prop->Save(scratch);
            if (!scratch.wantedSideFile()) {
                if (scratch.getString().empty()) {
                    // The property wrote nothing and asked for nowhere to put it. An empty block
                    // is not a value: its own reader looks for an element that is not there and
                    // reports the whole document as corrupt. Named as a gap, which is what it is.
                    entry.reason = "the property wrote no value";
                }
                else {
                    entry.body = scratch.getString();
                }
                stored.push_back(entry);
                continue;
            }
            // It says it is not bulk and then asks for a file anyway. That is a fault in the
            // property, not a reason to drop the value, so it goes to the store like bulk does
            // and the fault stays visible in the file.
            Base::Console().warning("Stored recipe: property '%s' (%s) states no value inline "
                                    "yet asks for a file of its own.\n",
                                    name.c_str(),
                                    entry.type.c_str());
        }

        // Compiled bulk -- a solid, a mesh, a point cloud, an embedded file. It goes to the
        // project's source store and the recipe names it by the content it holds, which stores
        // one imported body once however many parts use it.
        if (!assetDirectory.empty()) {
            const AssetOutcome outcome = storeAsset(*prop, assetDirectory);
            if (outcome.result == AssetOutcome::Result::NothingToKeep) {
                // Not a gap and not a value -- the property holds nothing, and a document rebuilt
                // without it holds nothing too.
                continue;
            }
            if (outcome.result == AssetOutcome::Result::Failed) {
                // The value is real and the store would not take it. Named, because a write that
                // failed and a property that was empty must not read the same on disk.
                entry.reason = "value could not be written to the project store";
                stored.push_back(entry);
                continue;
            }
            entry.asset = outcome.id;
            stored.push_back(entry);
            continue;
        }

        // No store to put it in -- a document with no folder yet. Whatever the property can say
        // inline is better than nothing, and for a solid that is the whole solid as text.
        ScratchWriter inlined;
        inlined.Stream().precision(std::numeric_limits<double>::max_digits10);
        inlined.setForceXML(true);
        inlined.incInd();
        inlined.incInd();
        prop->Save(inlined);
        if (!inlined.wantedSideFile() && !inlined.getString().empty()) {
            entry.body = inlined.getString();
        }
        else {
            entry.reason = "value kept beside the document, which has no folder yet";
        }
        stored.push_back(entry);
    }

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
        writer.Stream() << writer.ind() << "<Property name=\"" << entry->name << "\" type=\""
                        << entry->type << "\"";
        if (entry->isReference) {
            writer.Stream() << " reference=\"1\"";
        }
        if (!entry->asset.empty()) {
            writer.Stream() << " asset=\"" << entry->asset << "\"";
        }
        if (entry->dynamic) {
            writer.Stream() << " dynamic=\"1\" group=\"" << entry->group << "\" doc=\""
                            << entry->documentation << "\" attributes=\"" << entry->attributes
                            << "\" readonly=\"" << (entry->readOnly ? 1 : 0) << "\" hidden=\""
                            << (entry->hidden ? 1 : 0) << "\"";
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
                writer.Stream() << writer.ind() << "<Target uuid=\"" << binding.uuid
                                << "\" sub=\"" << binding.sub << "\"/>\n";
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
        writer.Stream() << writer.ind() << "<Property name=\"" << entry->name << "\" type=\""
                        << entry->type << "\" reason=\"" << entry->reason << "\"/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Unrecorded>\n";
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

/// Restore the values of one `<Properties>` block onto a container, then step over the
/// `<Unrecorded>` block that follows it: what the writer could not say, the reader cannot
/// invent.
/// A reference read from the file, waiting for the objects it points at to exist.
using PendingReference = std::pair<Property*, std::vector<Binding>>;

void readProperties(Base::XMLReader& reader,
                    PropertyContainer& owner,
                    std::vector<PendingReference>& pending,
                    const std::string& assetDirectory)
{
    reader.readElement("Properties");
    const int properties = reader.level();
    while (nextChildOf(reader, properties)) {
        if (std::strcmp(reader.localName(), "Property") != 0) {
            refuse("Property", reader);
        }
        const std::string name = reader.getAttribute<const char*>("name");
        const std::string type = reader.getAttribute<const char*>("type");

        Property* prop = owner.getPropertyByName(name.c_str());
        if (prop == nullptr && reader.getAttribute<long>("dynamic", 0) == 1) {
            // A property the object was given at runtime has to be declared before it can hold
            // anything, so the file carries the declaration and the reader replays it.
            prop = owner.addDynamicProperty(
                type.c_str(),
                name.c_str(),
                reader.getAttribute<const char*>("group", ""),
                reader.getAttribute<const char*>("doc", ""),
                static_cast<short>(reader.getAttribute<long>("attributes", 0)),
                reader.getAttribute<long>("readonly", 0) == 1,
                reader.getAttribute<long>("hidden", 0) == 1);
        }
        if (prop != nullptr && prop->getTypeId().getName() == type) {
            const std::string asset = reader.getAttribute<const char*>("asset", "");
            if (!asset.empty()) {
                if (assetDirectory.empty() || !loadAsset(*prop, assetDirectory, asset)) {
                    // The file names source material this project does not hold. Said out loud,
                    // because a part quietly missing the body it was built from looks exactly
                    // like a part that never had one -- and remembered, because the next save
                    // would otherwise write the absence over the name and make the loss
                    // permanent even after the material came back.
                    Base::Console().warning("Stored recipe: missing source material '%s'\n",
                                            asset.c_str());
                    owner.rememberMissingSource(name.c_str(), asset);
                }
            }
            else if (reader.getAttribute<long>("reference", 0) == 1) {
                std::vector<Binding> bindings;
                reader.readElement("Reference");
                const int reference = reader.level();
                while (nextChildOf(reader, reference)) {
                    if (std::strcmp(reader.localName(), "Target") != 0) {
                        refuse("Target", reader);
                    }
                    bindings.push_back({reader.getAttribute<const char*>("uuid"),
                                        reader.getAttribute<const char*>("sub")});
                }
                reader.readEndElement("Reference");
                // Held until every object in the file exists: a reference may point forwards,
                // and a file whose meaning depended on the order it was read would have brought
                // back the ordering problem this form was written to remove.
                pending.emplace_back(prop, std::move(bindings));
            }
            else {
                prop->Restore(reader);
            }
        }
        reader.readEndElement("Property");
    }
    reader.readEndElement("Properties");

    reader.readElement("Unrecorded");
    const int unrecorded = reader.level();
    while (nextChildOf(reader, unrecorded)) {
        if (std::strcmp(reader.localName(), "Property") != 0) {
            refuse("Property", reader);
        }
    }
    reader.readEndElement("Unrecorded");
}

/// The container holding an object's chosen appearance, or null when this session has none.
///
/// A colour a person chose is authored content and belongs in the file of record, not in the
/// deletable project cache -- but only the view layer knows where that state lives, so the file
/// asks for it rather than reaching for it. A headless session gets no answer and writes no
/// appearance, which is honest: it chose none.
PropertyContainer* appearanceOf(const DocumentObject& obj)
{
    auto* display = Base::provideService<DisplayStateProvider>();
    return display != nullptr ? display->appearanceOf(obj) : nullptr;
}

/// One object's block: what it is, and what it was authored to be.
void writeObject(Base::Writer& writer,
                 const DocumentObject& obj,
                 const std::string& assetDirectory,
                 bool withAppearance)
{
    const PropertyContainer* appearance = withAppearance ? appearanceOf(obj) : nullptr;

    writer.Stream() << writer.ind() << "<Object uuid=\"" << obj.Uid.getValueStr() << "\" type=\""
                    << obj.getTypeId().getName() << "\" name=\"" << obj.getNameInDocument()
                    << "\"";
    if (appearance != nullptr) {
        // Marked on the object, so a reader knows whether to expect the block without having to
        // look ahead for it.
        writer.Stream() << " display=\"1\"";
    }
    writer.Stream() << ">\n";
    writer.incInd();
    writeProperties(writer, obj, assetDirectory);
    if (appearance != nullptr) {
        writer.Stream() << writer.ind() << "<Display>\n";
        writer.incInd();
        writeProperties(writer, *appearance, assetDirectory);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Display>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Object>\n";
}

}  // namespace

std::string App::formatStoredRecipe(const Document& doc, const std::string& assetDirectory)
{
    Base::StringWriter writer;
    // Full precision, set here because it belongs to the WRITER and not to the value: the
    // document archive is exact only because the archive's own writer asks for these digits, so
    // any second writer starts out quietly rounding to six of them. Measured: a length of
    // 12.345678901234567 came back as 12.3457 before this line existed.
    writer.Stream().precision(std::numeric_limits<double>::max_digits10);

    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>\n"
                    << "<Recipe Version=\"1\">\n";
    writer.incInd();

    // The document's own authored facts -- who wrote it, when it was created, what it is called
    // -- belong to the recipe as much as any object does. The walk that produces the readable
    // view covers objects only, which is why a document's own content had nowhere to go.
    writer.Stream() << writer.ind() << "<Document uuid=\"" << doc.Uid.getValueStr() << "\">\n";
    writer.incInd();
    writeProperties(writer, doc, assetDirectory);
    writer.decInd();
    writer.Stream() << writer.ind() << "</Document>\n";

    // Objects in durable-id order. Creation order is a fact about the session that produced the
    // document, not about the design, and letting it set the order in the file is what makes an
    // inserted feature read as a rewritten file.
    std::vector<const DocumentObject*> objects;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr) {
            objects.push_back(obj);
        }
    }
    std::sort(objects.begin(),
              objects.end(),
              [](const DocumentObject* left, const DocumentObject* right) {
                  return left->Uid.getValueStr() < right->Uid.getValueStr();
              });

    writer.Stream() << writer.ind() << "<Objects>\n";
    writer.incInd();
    for (const DocumentObject* obj : objects) {
        writeObject(writer, *obj, assetDirectory, /*withAppearance=*/true);
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Objects>\n";

    writer.decInd();
    writer.Stream() << "</Recipe>\n";

    return writer.getString();
}

void App::restoreStoredRecipe(Document& doc,
                              std::istream& source,
                              bool finish,
                              const std::string& assetDirectory)
{
    Base::XMLReader reader("StoredRecipe", source);
    if (!reader.isValid()) {
        return;
    }

    std::vector<PendingReference> pending;
    std::vector<DocumentObject*> restored;

    reader.readElement("Recipe");

    reader.readElement("Document");
    const std::string documentUid = reader.getAttribute<const char*>("uuid");
    readProperties(reader, doc, pending, assetDirectory);
    reader.readEndElement("Document");
    // A document's identity is its own, and the document model refuses to let two open
    // documents share one -- restoring the uuid into a copy of a document that is still open
    // mints a fresh one instead. That is the model's rule, not this reader's, and it is right:
    // the file names the document it came from, and a second live copy is not that document.
    doc.Uid.setValue(documentUid);

    reader.readElement("Objects");
    const int objects = reader.level();
    while (nextChildOf(reader, objects)) {
        if (std::strcmp(reader.localName(), "Object") != 0) {
            refuse("Object", reader);
        }
        const std::string uuid = reader.getAttribute<const char*>("uuid");
        const std::string type = reader.getAttribute<const char*>("type");
        const std::string name = reader.getAttribute<const char*>("name");
        const bool display = reader.getAttribute<long>("display", 0) == 1;

        // The in-document name is restored, not regenerated: expressions and the document's own
        // reporting speak it, so a rebuilt document that renamed everything would be a different
        // document wearing the same values.
        DocumentObject* obj = doc.addObject(type.c_str(), name.c_str(), /*isNew=*/false);
        if (obj != nullptr) {
            obj->Uid.setValue(uuid);
            restored.push_back(obj);
            // Marked as being restored for the duration, exactly as the archive's own reader does
            // it. Some features rebuild themselves the moment one of their sizes changes, which is
            // right when a person types a number and wrong while a file is being read: it builds
            // the part three times over on the way in, and it builds it before the references it
            // is built on have been bound.
            obj->setStatus(ObjectStatus::Restore, true);
            readProperties(reader, *obj, pending, assetDirectory);
            obj->setStatus(ObjectStatus::Restore, false);

            if (display) {
                // The appearance the file carries. A session with nowhere to put it -- a headless
                // one -- steps over the block rather than guessing at a place for it.
                reader.readElement("Display");
                PropertyContainer* appearance = appearanceOf(*obj);
                if (appearance != nullptr) {
                    readProperties(reader, *appearance, pending, assetDirectory);
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
        if (!restoreReference(*prop, bindings, doc)) {
            Base::Console().warning("Stored recipe: could not restore the reference '%s'\n",
                                    prop->getName() != nullptr ? prop->getName() : "");
        }
    }

    // A formula cannot be bound while the objects it names are still arriving, so the document
    // has a second pass for exactly this and the archive's own load path uses it. Reading a
    // recipe is reading a document, and it finishes the same way -- unless the caller is a
    // document being opened, which runs that pass itself once every document in the set is read.
    if (finish) {
        doc.afterRestore(restored, false);
    }

    // Everything read from a recipe still has to be built: the file carries the steps, never the
    // geometry they produce. So each object is marked as needing a rebuild -- a document read
    // from a recipe that reported itself up to date would be claiming geometry it does not have.
    for (DocumentObject* obj : restored) {
        obj->enforceRecompute();
    }
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

std::vector<Property*> App::rebuiltProperties(const PropertyContainer& owner)
{
    std::map<std::string, Property*> properties;
    owner.getPropertyMap(properties);

    std::vector<Property*> rebuilt;
    for (const auto& [name, prop] : properties) {
        if (prop == nullptr) {
            continue;
        }
        // Nothing the archive itself refuses to keep. A value declared transient is regenerated
        // by whatever produces it, and a store that claimed to hold it would be lying about a
        // value that was never written.
        if (prop->testStatus(Property::PropNoPersist) || prop->testStatus(Property::Transient)
            || (owner.getPropertyType(prop) & Prop_Transient) != 0) {
            continue;
        }
        // Output, in the two ways an object says it: geometry it builds, and a value it declares
        // is a result rather than a setting. `Label` says the second and means neither -- it is
        // the one authored value wearing the output flag, and the recipe keeps it.
        if (isBuiltGeometry(*prop, owner)
            || ((owner.getPropertyType(prop) & Prop_Output) != 0
                && !isAuthoredDespiteItsFlags(name))) {
            rebuilt.push_back(prop);
        }
    }
    return rebuilt;
}
