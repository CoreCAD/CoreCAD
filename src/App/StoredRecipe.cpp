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
# include <limits>
# include <map>
# include <sstream>
# include <string>
# include <vector>
#endif

#include <Base/Reader.h>
#include <Base/Writer.h>

#include "StoredRecipe.h"

#include "Document.h"
#include "DynamicProperty.h"
#include "DocumentObject.h"
#include "Property.h"
#include "PropertyContainer.h"
#include "PropertyLinks.h"

using namespace App;

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
};

/// A reference between objects binds by durable id (§10.1), and this form does not carry that
/// binding yet: writing a link the way the archive does — by the target's in-document name —
/// would build the positional addressing the whole direction exists to remove. So a link is
/// named as unrecorded and its binding is the next slice, not a silent omission.
bool isReference(const Property& prop)
{
    return prop.isDerivedFrom(PropertyLinkBase::getClassTypeId());
}

/// Everything the stored form has to say about one container's properties, in name order.
std::vector<StoredProperty> storedProperties(const PropertyContainer& owner)
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
            entry.reason = "reference";
            stored.push_back(entry);
            continue;
        }

        ScratchWriter scratch;
        scratch.Stream().precision(std::numeric_limits<double>::max_digits10);
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
void writeProperties(Base::Writer& writer, const PropertyContainer& owner)
{
    const std::vector<StoredProperty> stored = storedProperties(owner);

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
        if (entry->dynamic) {
            writer.Stream() << " dynamic=\"1\" group=\"" << entry->group << "\" doc=\""
                            << entry->documentation << "\" attributes=\"" << entry->attributes
                            << "\" readonly=\"" << (entry->readOnly ? 1 : 0) << "\" hidden=\""
                            << (entry->hidden ? 1 : 0) << "\"";
        }
        writer.Stream() << ">\n" << entry->body << writer.ind() << "</Property>\n";
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
void readProperties(Base::XMLReader& reader, PropertyContainer& owner)
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
            prop->Restore(reader);
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

std::string App::formatStoredRecipe(const Document& doc)
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
    writeProperties(writer, doc);
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
        writeProperties(writer, *obj);
        writer.decInd();
        writer.Stream() << writer.ind() << "</Object>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Objects>\n";

    writer.decInd();
    writer.Stream() << "</Recipe>\n";

    return writer.getString();
}

void App::restoreStoredRecipe(Document& doc, std::istream& source)
{
    Base::XMLReader reader("StoredRecipe", source);
    if (!reader.isValid()) {
        return;
    }

    reader.readElement("Recipe");

    reader.readElement("Document");
    const std::string documentUid = reader.getAttribute<const char*>("uuid");
    readProperties(reader, doc);
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
            readProperties(reader, *obj);
        }
        reader.readEndElement("Object");
    }
    reader.readEndElement("Objects");

    reader.readEndElement("Recipe");
}
