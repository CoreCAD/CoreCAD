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
#include <Base/Uuid.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>

#include "StoredRecipe.h"

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

/// The one file inside a source-store entry that is not bulk: the property's own element, naming
/// what sits beside it.
constexpr const char* assetContentFile = "value.xml";

/// Content the recipe PRODUCES, as opposed to content the document was handed.
///
/// A part's shape is declared `Prop_None` -- it claims to be neither output nor transient -- so
/// the flags do not keep it out, and asking the writer to inline bulky values put the whole solid
/// into the recipe as text. That is not a big file, it is a wrong one: for a feature the recipe
/// is the source and the shape is what the source builds, and a file carrying both can disagree
/// with itself. Measured: with the shape inline, a test that rebuilt a part from the recipe with
/// none of its references restored still produced the right solid -- the file was answering with
/// the old geometry instead of rebuilding.
///
/// An imported solid, a scanned mesh, a measured point cloud, the bytes of a file a person handed
/// over are the opposite case: no step of the document produces them, so that content IS the
/// authored content. Excluding those by kind dropped them without a word -- an imported part came
/// back empty.
///
/// **The object answers, and the property's class is never tested** (Amendment 18 Clause 18.2).
/// It used to be tested: only a `PropertyGeometry` could be output, so every other kind of bulk
/// was authored content whatever produced it. Measured: a machine toolpath -- produced by the job
/// above it, and reproducible from it -- was written into the source store, visible and versioned
/// and kept for ever, beside the imported bodies a person chose.
///
/// Which leaves one question for the property, and it is a different question: whether the value
/// can be stated in a file meant to be read at all (`holdsOpaqueBulk`). A colour is not placed
/// anywhere; it is simply written down.
bool theObjectProducedIt(const Property& prop, const PropertyContainer& owner)
{
    if (!prop.holdsOpaqueBulk()) {
        return false;
    }
    const auto* object = dynamic_cast<const DocumentObject*>(&owner);
    return object != nullptr && object->producesContentOf(prop);
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
    bool noPart {false};    ///< named with no part at all; written with no `sub`, not `sub=""`
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

/// Everything the stored form has to say about one container's properties, in name order.
std::vector<StoredProperty> storedProperties(const PropertyContainer& owner,
                                            const std::string& assetDirectory)
{
    std::map<std::string, Property*> properties;
    owner.getPropertyMap(properties);

    std::vector<StoredProperty> stored;
    for (const auto& [name, prop] : properties) {
        const bool carried = prop != nullptr && App::theRecipeCarries(*prop, owner);
        const bool declaredOnly =
            prop != nullptr && !carried && onlyItsDeclarationIsCarried(*prop, owner);
        if (prop == nullptr || (!carried && !declaredOnly)) {
            continue;
        }

        if (owner.statedProperties().count(name) != 0) {
            // The file's own words stand for this name until something supersedes them, and they
            // are written below. Saying it twice would be a file that disagrees with itself.
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

        if (declaredOnly) {
            // Stated as a property that exists and no more. The value is whatever the object
            // makes of it on the next recompute, which is exactly what its flags say.
            entry.valueStated = false;
            stored.push_back(entry);
            continue;
        }

        if (isReference(*prop)) {
            // An expression engine is a reference property by inheritance -- it holds the
            // formulas an object was given -- but what it points at lives inside the text of
            // each formula, which speaks object names. It is written by its own serializer
            // here, which states the durable identity of each intra-document reference beside
            // the formula, keyed by the part of the text it was written from. References that
            // leave the document remain name-based, reserved for the PropertyXLink step.
            if (prop->isDerivedFrom(PropertyExpressionContainer::getClassTypeId())) {
                entry.body = writtenByItsOwnSerializer(*prop);
                stored.push_back(entry);
                continue;
            }

            // Where this session could not resolve what the file stated, the file says it again.
            // The live property holds nothing, so deriving the reference from memory would write
            // "points at nothing" over "points at that object" -- a fact about this session
            // published as a fact about the design (Amendment 19).
            if (const auto* kept = owner.unresolvedReference(name.c_str())) {
                for (const PropertyContainer::StatedTarget& target : *kept) {
                    entry.bindings.push_back(
                        {target.uuid, target.sub, /*external=*/false, target.noPart});
                }
                entry.isReference = true;
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
                writer.Stream() << writer.ind() << "<Target uuid=\"" << binding.uuid << "\"";
                if (!binding.noPart) {
                    writer.Stream() << " sub=\"" << binding.sub << "\"";
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

std::string liftObjectWords(const std::string& source, const std::string& uuid);
std::string liftDocumentWords(const std::string& source);

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
    std::size_t lineStart = objectWords.rfind('\n', at);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    if (objectWords.find_first_not_of(" \t", lineStart) != at) {
        // Something else shares the line. Not a shape this writer produces.
        return {};
    }
    const std::string closing = "</Display>";
    const std::size_t end = objectWords.find(closing, at);
    if (end == std::string::npos) {
        return {};
    }
    const std::size_t lineEnd = objectWords.find('\n', end);
    return objectWords.substr(lineStart,
                              (lineEnd == std::string::npos ? objectWords.size() : lineEnd + 1)
                                  - lineStart);
}

/// One property's block, exactly as the file states it, indentation and all.
///
/// Taken from the file's own words rather than rebuilt from what the reader understood, because a
/// block this build has no place for is a statement it cannot re-derive. It is kept at the depth
/// the file wrote it at and given back at that depth: this form writes a property at one depth
/// and only one, so the words the file used are the words that belong there.
std::string liftPropertyBlock(const std::string& objectWords, const std::string& name)
{
    const std::string opening = "<Property name=\"" + name + "\"";
    const std::size_t at = objectWords.find(opening);
    if (at == std::string::npos) {
        return {};
    }
    std::size_t lineStart = objectWords.rfind('\n', at);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    if (objectWords.find_first_not_of(" \t", lineStart) != at) {
        // Something else shares the line. Not a shape this writer produces, and not a block to
        // guess the extent of.
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
            const std::size_t lineEnd = objectWords.find('\n', scan);
            return objectWords.substr(lineStart,
                                      (lineEnd == std::string::npos ? objectWords.size()
                                                                    : lineEnd + 1)
                                          - lineStart);
        }
    }
    return {};
}

/// Restore the values of one `<Properties>` block onto a container, then step over the
/// `<Unrecorded>` block that follows it: what the writer could not say, the reader cannot
/// invent.
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
    const auto wordsFor = [&](const std::string& name) {
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
        // A property closed where it stands states that it exists and states no value for it.
        // A self-closing element has already ended by the time it is read, so its level is the
        // level of the list around it -- that, and not a flag, is what says a value is there.
        const bool valueStated = reader.level() > properties;

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
                // fails, and the read used to stop there, taking the rest of the document with
                // it. The block is kept as stated instead (Amendment 19).
                prop = nullptr;
            }
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
                    const bool noPart = !reader.hasAttribute("sub");
                    bindings.push_back({reader.getAttribute<const char*>("uuid"),
                                        noPart ? "" : reader.getAttribute<const char*>("sub"),
                                        /*external=*/false,
                                        noPart});
                }
                reader.readEndElement("Reference");
                // Held until every object in the file exists: a reference may point forwards,
                // and a file whose meaning depended on the order it was read would have brought
                // back the ordering problem this form was written to remove.
                pending.emplace_back(prop, std::move(bindings));
            }
            else if (!valueStated) {
                // Declared and left as the object makes it. Asking the property to read a value
                // the file does not state is how a reader invents one.
            }
            else {
                try {
                    prop->Restore(reader);
                }
                catch (const Base::XMLParseException&) {
                    // The file's STRUCTURE is broken here, which is not a value this build cannot
                    // read: a refusal for that is total and belongs to the reader, not here
                    // (Amendment 19 Clause 19.2).
                    throw;
                }
                catch (const Base::Exception& e) {
                    keepUnreadValue(doc, owner, *prop, name, type, e.what(), wordsFor(name));
                }
                catch (const std::exception& e) {
                    // A value that fails in the standard library rather than in ours -- a number
                    // that is not one reaches `stod`, which throws something no catch of ours
                    // used to name. Measured: one such value and the document did not open at
                    // all, not even as its own beginning.
                    keepUnreadValue(doc, owner, *prop, name, type, e.what(), wordsFor(name));
                }
            }
        }
        else {
            // This build has no place for what the file states here: no property of that name, or
            // one of a different type. Stepping over it is how a document quietly comes back
            // smaller than it was written, so the file's own words are kept and given back.
            std::string words = wordsFor(name);
            if (words.empty()) {
                Base::Console().warning(
                    "Stored recipe: '%s' (%s) is not a property this build has, and its words "
                    "could not be kept. Saving this document would lose it.\n",
                    name.c_str(),
                    type.c_str());
                // Warned about AND recorded: what a save would lose has to be answerable at the
                // moment of the save, and a message printed at load time is gone by then.
                doc.recordUnkeptStatement("'" + name + "' (" + type
                                          + "), which this build has no property for and whose "
                                            "words could not be kept");
            }
            else {
                owner.rememberStatedProperty(name.c_str(),
                                             std::move(words),
                                             "this build has no property of that name and type");
                Base::Console().warning(
                    "Stored recipe: '%s' (%s) is not a property this build has. It is kept as "
                    "written and the document is not whole.\n",
                    name.c_str(),
                    type.c_str());
            }
        }
        reader.readEndElement("Property");
    }
    reader.readEndElement("Properties");

    // An enumeration states the value that was chosen by name, and some lists are not fixed: a
    // hole's thread class is built from its thread type, so a name may not be lookupable at the
    // moment it is read. Such a name waits for a list that offers it, and every property read
    // after it is a chance for one to arrive. Here the container has been read in full, so a name
    // still waiting is one nothing here offers -- a value this build cannot honour, kept as the
    // file worded it rather than quietly replaced by the default (Amendment 19).
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

    writer.Stream() << writer.ind() << "<Object uuid=\"" << obj.Uid.getValueStr() << "\" type=\""
                    << obj.getTypeId().getName() << "\" name=\"" << obj.getNameInDocument()
                    << "\"";
    if (!asked.empty()) {
        // Marked on the object for the same reason the appearance is: so a reader knows whether
        // to expect the block without having to look ahead for it.
        writer.Stream() << " extensions=\"1\"";
    }
    if (statesAppearance) {
        // Marked on the object, so a reader knows whether to expect the block without having to
        // look ahead for it.
        writer.Stream() << " display=\"1\"";
    }
    writer.Stream() << ">\n";
    writer.incInd();
    if (!asked.empty()) {
        writer.Stream() << writer.ind() << "<Extensions>\n";
        writer.incInd();
        for (const std::string& type : asked) {
            writer.Stream() << writer.ind() << "<Extension type=\"" << type << "\"/>\n";
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
                    << "<Recipe Version=\"" << storedRecipeFormat << "\">\n";
    writer.incInd();

    // The document's own authored facts -- who wrote it, when it was created, what it is called
    // -- belong to the recipe as much as any object does. The walk that produces the readable
    // view covers objects only, which is why a document's own content had nowhere to go.
    //
    // A rendering that carries objects alone states no block here at all rather than an empty
    // one: "this rendering says nothing about a document" and "the document states nothing" are
    // different facts, and only the first one is true of a copy.
    if (scope.withDocumentProperties) {
        writer.Stream() << writer.ind() << "<Document uuid=\"" << doc.Uid.getValueStr() << "\">\n";
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

namespace
{

/// One object's block, lifted from the file exactly as the file states it.
///
/// Cruth (Amendment 19): a document may name an object this build cannot construct -- a module
/// that was not compiled in, an add-on that is absent, a scripted class that is gone. The block is
/// taken from the source text rather than rebuilt from what the reader understood, because what is
/// owed here is the statement itself and not this session's reading of it.
///
/// Object blocks are siblings and never nest, so the first `</Object>` after the opening tag is
/// this object's own end. The leading indentation is removed so the block can be given back at
/// whatever depth the writer is at, and restored on the way out.
/// One object's block, exactly as the file states it, indentation and all.
/// The document's own block, exactly as the file states it, indentation and all.
///
/// The document is not an object and has no durable id to be found by, but it states properties
/// like any other container and they are kept on the same terms.
std::string liftDocumentWords(const std::string& source)
{
    const std::size_t start = source.find("<Document ");
    if (start == std::string::npos) {
        return {};
    }
    const std::string closing = "</Document>";
    const std::size_t end = source.find(closing, start);
    if (end == std::string::npos) {
        return {};
    }
    std::size_t lineStart = source.rfind('\n', start);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    return source.substr(lineStart, end + closing.size() - lineStart);
}

std::string liftObjectWords(const std::string& source, const std::string& uuid)
{
    const std::string opening = "<Object uuid=\"" + uuid + "\"";
    const std::size_t start = source.find(opening);
    if (start == std::string::npos) {
        return {};
    }
    const std::string closing = "</Object>";
    const std::size_t end = source.find(closing, start);
    if (end == std::string::npos) {
        return {};
    }
    std::size_t lineStart = source.rfind('\n', start);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    return source.substr(lineStart, end + closing.size() - lineStart);
}

std::string liftObjectBlock(const std::string& source, const std::string& uuid)
{
    const std::string opening = "<Object uuid=\"" + uuid + "\"";
    const std::size_t start = source.find(opening);
    if (start == std::string::npos) {
        return {};
    }
    const std::string closing = "</Object>";
    const std::size_t end = source.find(closing, start);
    if (end == std::string::npos) {
        return {};
    }

    std::string block = source.substr(start, end + closing.size() - start);

    // The depth this block was written at, taken from the line it starts on.
    std::size_t lineStart = source.rfind('\n', start);
    lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
    const std::string indent = source.substr(lineStart, start - lineStart);
    if (indent.find_first_not_of(" \t") != std::string::npos) {
        return block;
    }

    std::string dedented;
    dedented.reserve(block.size());
    std::size_t at = 0;
    while (at <= block.size()) {
        std::size_t nl = block.find('\n', at);
        const std::size_t stop = (nl == std::string::npos) ? block.size() : nl;
        std::string line = block.substr(at, stop - at);
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
    const std::string sourceText((std::istreambuf_iterator<char>(source)),
                                 std::istreambuf_iterator<char>());
    std::istringstream parsed(sourceText);

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
            return liftDocumentWords(sourceText);
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
            std::string block = liftObjectBlock(sourceText, uuid);
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
                return liftObjectWords(sourceText, uuid);
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
                    obj->keepStatedAppearance(liftDisplayBlock(liftObjectWords(sourceText, uuid)));
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
                        return liftDisplayBlock(liftObjectWords(sourceText, uuid));
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

bool App::theRecipeCarries(const Property& prop, const PropertyContainer& owner)
{
    const std::string name = owner.getPropertyName(&prop) != nullptr
        ? std::string(owner.getPropertyName(&prop))
        : std::string();
    if ((owner.getPropertyType(&prop) & excludedPropertyFlags) != 0
        && !isAuthoredDespiteItsFlags(name)) {
        return false;
    }
    if (prop.testStatus(Property::PropNoPersist)) {
        return false;
    }
    return !theObjectProducedIt(prop, owner);
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
        // Output, in the two ways an object says it: content it says it produced, and a value
        // it declares is a result rather than a setting. `Label` says the second and means
        // neither -- it is the one authored value wearing the output flag, and the recipe keeps
        // it.
        if (theObjectProducedIt(*prop, owner)
            || ((owner.getPropertyType(prop) & Prop_Output) != 0
                && !isAuthoredDespiteItsFlags(name))) {
            rebuilt.push_back(prop);
        }
    }
    return rebuilt;
}
