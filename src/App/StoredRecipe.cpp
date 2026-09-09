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
# include <filesystem>
# include <fstream>
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
    const auto* feature = dynamic_cast<const GeoFeature*>(&owner);
    return feature == nullptr || !feature->holdsAuthoredGeometry();
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

/// Put a handed-in value into the project's source folder, and answer by what it holds.
///
/// The value is named by a digest of its own bytes, so an imported body used by five parts is
/// stored once and an unchanged import writes nothing new. It also means the name cannot go
/// stale: a different body is a different name, never the same name with different contents.
///
/// Nothing here removes a file that has stopped being referenced. Collecting those is a separate
/// operation on a project, not something a single save can decide.
std::optional<std::string> storeAsset(const Property& prop, const std::string& directory)
{
    Base::StringWriter blob;
    prop.SaveDocFile(blob);
    const std::string bytes = blob.getString();
    if (bytes.empty()) {
        return std::nullopt;
    }

    const QByteArray digest = QCryptographicHash::hash(
        QByteArray(bytes.data(), static_cast<int>(bytes.size())),
        QCryptographicHash::Sha1);
    const std::string id = QString::fromLatin1(digest.toHex()).toStdString();

    const fs::path file = fs::path(directory) / (id + ".brp");
    if (!fs::exists(file)) {
        fs::create_directories(directory);
        std::ofstream out(file, std::ios::out | std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return std::nullopt;
        }
    }
    return id;
}

/// Read a handed-in value back from the project's source folder.
bool loadAsset(Property& prop, const std::string& directory, const std::string& id)
{
    const std::string name = id + ".brp";
    Base::FileInfo file(directory + "/" + name);
    if (!file.exists()) {
        return false;
    }
    Base::ifstream stream(file, std::ios::in | std::ios::binary);
    Base::Reader reader(stream, name, 0);
    prop.RestoreDocFile(reader);
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

        // Geometry the document was handed is the project's source material, not a description
        // of anything, so it goes to the source folder and the recipe names it. Written into the
        // recipe it would be thousands of lines of coordinates in the middle of a file whose
        // whole purpose is to be read.
        if (prop->isDerivedFrom(PropertyGeometry::getClassTypeId()) && !assetDirectory.empty()) {
            const std::optional<std::string> id = storeAsset(*prop, assetDirectory);
            if (!id) {
                // No geometry to keep. Not a gap and not a value -- the property holds nothing,
                // and a document rebuilt without it holds nothing too.
                continue;
            }
            entry.asset = *id;
            stored.push_back(entry);
            continue;
        }

        ScratchWriter scratch;
        scratch.Stream().precision(std::numeric_limits<double>::max_digits10);
        // Values large enough to be kept beside the archive are written inline here instead: a
        // recipe that pointed at a second file would not be one file you can read.
        scratch.setForceXML(true);
        scratch.incInd();
        scratch.incInd();
        prop->Save(scratch);
        if (scratch.wantedSideFile()) {
            entry.reason = "value stored beside the document";
        }
        else {
            entry.body = scratch.getString();
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

    std::vector<const StoredProperty*> recorded;
    std::vector<const StoredProperty*> unrecorded;
    for (const StoredProperty& entry : stored) {
        (entry.reason.empty() ? recorded : unrecorded).push_back(&entry);
    }

    writer.Stream() << writer.ind() << "<Properties Count=\"" << recorded.size() << "\">\n";
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
            writer.Stream() << writer.ind() << "<Reference Count=\"" << entry->bindings.size()
                            << "\">\n";
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

    writer.Stream() << writer.ind() << "<Unrecorded Count=\"" << unrecorded.size() << "\">\n";
    writer.incInd();
    for (const StoredProperty* entry : unrecorded) {
        writer.Stream() << writer.ind() << "<Property name=\"" << entry->name << "\" type=\""
                        << entry->type << "\" reason=\"" << entry->reason << "\"/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Unrecorded>\n";
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
    const int count = reader.getAttribute<long>("Count");
    for (int i = 0; i < count; ++i) {
        reader.readElement("Property");
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
                    // like a part that never had one.
                    Base::Console().warning("Stored recipe: missing source geometry '%s'\n",
                                            asset.c_str());
                }
            }
            else if (reader.getAttribute<long>("reference", 0) == 1) {
                std::vector<Binding> bindings;
                reader.readElement("Reference");
                const int targets = reader.getAttribute<long>("Count");
                for (int t = 0; t < targets; ++t) {
                    reader.readElement("Target");
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
    const int unrecorded = reader.getAttribute<long>("Count");
    for (int i = 0; i < unrecorded; ++i) {
        reader.readElement("Property");
    }
    reader.readEndElement("Unrecorded");
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

    writer.Stream() << writer.ind() << "<Objects Count=\"" << objects.size() << "\">\n";
    writer.incInd();
    for (const DocumentObject* obj : objects) {
        writer.Stream() << writer.ind() << "<Object uuid=\"" << obj->Uid.getValueStr()
                        << "\" type=\"" << obj->getTypeId().getName() << "\" name=\""
                        << obj->getNameInDocument() << "\">\n";
        writer.incInd();
        writeProperties(writer, *obj, assetDirectory);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Object>\n";
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
    const int count = reader.getAttribute<long>("Count");
    for (int i = 0; i < count; ++i) {
        reader.readElement("Object");
        const std::string uuid = reader.getAttribute<const char*>("uuid");
        const std::string type = reader.getAttribute<const char*>("type");
        const std::string name = reader.getAttribute<const char*>("name");

        // The in-document name is restored, not regenerated: expressions and the document's own
        // reporting speak it, so a rebuilt document that renamed everything would be a different
        // document wearing the same values.
        DocumentObject* obj = doc.addObject(type.c_str(), name.c_str(), /*isNew=*/false);
        if (obj != nullptr) {
            obj->Uid.setValue(uuid);
            restored.push_back(obj);
            readProperties(reader, *obj, pending, assetDirectory);
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
