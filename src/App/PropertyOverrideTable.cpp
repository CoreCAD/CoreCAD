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
#include <sstream>
#endif

#include <Base/Reader.h>
#include <Base/Writer.h>

#include "Document.h"
#include "DocumentObject.h"
#include "PropertyOverrideTable.h"

using namespace App;

namespace
{
/// The file's own words for a value, given back at the depth they are being written at.
///
/// A kept value is re-stated line for line, so a save that changed nothing changes nothing
/// (Amendment 18 Clause 18.1) except for where on the page it sits.
std::string atThisDepth(const std::string& words, const std::string& pad)
{
    std::istringstream lines(words);
    std::ostringstream out;
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            out << '\n';
        }
        else {
            out << pad << line << '\n';
        }
    }
    return out.str();
}

/// What a value read out of an override table is called when a parser has to name its source.
constexpr const char* statementSource = "an override's stated value";

/** A property of the same kind as `original`, holding the same value.
 *
 * NOT `Copy()`. A property's Copy answers with the kind that IMPLEMENTS it rather than the kind it
 * is: a length copies to a plain float, losing both the name of its kind and the unit that makes
 * it a length. Measured -- an override of a Pad's `Length`, which is the example the architecture
 * uses for configurations, was stored as a float, compared against a length on the way back in,
 * and never applied. A fresh instance of the stated kind takes the value through the same Paste
 * that undo uses, and is the kind it says it is.
 */
std::shared_ptr<App::Property> likeThisOne(const App::Property& original)
{
    std::shared_ptr<App::Property> mine(
        static_cast<App::Property*>(original.getTypeId().createInstance()));
    if (mine) {
        mine->Paste(original);
    }
    return mine;
}

/// The property kind the file names, or a bad type if this build has no such kind.
///
/// Asked of the table of kinds this build already has, and of nothing else. The usual way to ask
/// -- `getTypeIfDerivedFrom` -- first LOADS the module the name begins with, through the Python
/// interpreter, so a document naming `Addon::PropertyDial` makes the program import `Addon` on
/// open. That is the same defect in a quieter form: what runs is whatever that module does when
/// imported rather than what the file spells out, but the file still decides that it runs.
///
/// The cost is that an override of a kind belonging to a module nothing has loaded is not read.
/// It is kept, named, and blocks what it would have set, which is what this build owes a value it
/// cannot read -- and a document does not get to decide what this session imports.
Base::Type propertyKindNamed(const std::string& type)
{
    const Base::Type kind = Base::Type::fromName(type.c_str());
    return kind.isDerivedFrom(App::Property::getClassTypeId()) ? kind : Base::Type::BadType;
}
}  // namespace

TYPESYSTEM_SOURCE(App::PropertyOverrideTable, App::Property)

PropertyOverrideTable::PropertyOverrideTable() = default;

PropertyOverrideTable::~PropertyOverrideTable() = default;

const PropertyOverrideTable::Stated* PropertyOverrideTable::stated(const Address& address) const
{
    const auto found = _stated.find(address);
    return found == _stated.end() ? nullptr : &found->second;
}

void PropertyOverrideTable::stateValue(const Address& address, const Property& takenFrom)
{
    if (takenFrom.holdsOpaqueBulk()) {
        throw Base::ValueError("An override states a value the recipe can carry, and a "
                               "'" + std::string(takenFrom.getTypeId().getName())
                               + "' keeps its value in a file of its own.");
    }
    std::shared_ptr<Property> mine = likeThisOne(takenFrom);
    if (!mine) {
        throw Base::RuntimeError("A '" + std::string(takenFrom.getTypeId().getName())
                                 + "' would not give a copy of its value.");
    }
    Stated entry;
    entry.type = mine->getTypeId().getName();
    entry.value = std::move(mine);

    aboutToSetValue();
    _stated[address] = std::move(entry);
    hasSetValue();
}

bool PropertyOverrideTable::forgetValue(const Address& address)
{
    if (_stated.find(address) == _stated.end()) {
        return false;
    }
    aboutToSetValue();
    _stated.erase(address);
    hasSetValue();
    return true;
}

int PropertyOverrideTable::forgetOption(const std::string& option)
{
    int gone = 0;
    for (auto entry = _stated.begin(); entry != _stated.end();) {
        if (entry->first.option == option) {
            if (gone == 0) {
                aboutToSetValue();
            }
            entry = _stated.erase(entry);
            ++gone;
        }
        else {
            ++entry;
        }
    }
    if (gone > 0) {
        hasSetValue();
    }
    return gone;
}

void PropertyOverrideTable::clear()
{
    if (_stated.empty()) {
        return;
    }
    aboutToSetValue();
    _stated.clear();
    hasSetValue();
}

std::map<PropertyOverrideTable::Address, PropertyOverrideTable::Stated>
PropertyOverrideTable::copyOfWhatIsStated() const
{
    std::map<Address, Stated> mine;
    for (const auto& [address, stated] : _stated) {
        Stated entry;
        entry.type = stated.type;
        entry.words = stated.words;
        entry.reason = stated.reason;
        if (stated.value) {
            // Its own property and not a shared one: two tables holding the same value must not
            // be one table that two things can edit.
            entry.value = likeThisOne(*stated.value);
        }
        mine[address] = std::move(entry);
    }
    return mine;
}

// -----------------------------------------------------------------------------------------------
//  The stored form
// -----------------------------------------------------------------------------------------------

void PropertyOverrideTable::Save(Base::Writer& writer) const
{
    // No count. A file states its content, never a number restating how much of it there is: two
    // people each adding an override to a common ancestor would both write the same larger number
    // and a textual merge would take it without a conflict, dropping one of the two additions.
    writer.Stream() << writer.ind() << "<Overrides>\n";
    writer.incInd();
    for (const auto& [address, stated] : _stated) {
        writer.Stream() << writer.ind() << "<Override option=\""
                        << encodeAttribute(address.option) << "\" object=\""
                        << encodeAttribute(address.object) << "\" property=\""
                        << encodeAttribute(address.property) << "\" type=\""
                        << encodeAttribute(stated.type) << "\">\n";
        writer.incInd();
        if (stated.value) {
            // The value writes itself, by the one serializer every other authored value in the
            // document is written by. Nothing here knows what a length or a colour looks like.
            stated.value->Save(writer);
        }
        else {
            // A value this build could not read, given back exactly as it arrived.
            writer.Stream() << atThisDepth(stated.words, writer.ind());
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</Override>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Overrides>\n";
}

void PropertyOverrideTable::Restore(Base::XMLReader& reader)
{
    std::map<Address, Stated> read;

    reader.readElement("Overrides");
    const int table = reader.level();
    while (nextChildElement(reader, table)) {
        expectElement(reader, "Override");
        Address address;
        address.option = reader.getAttribute<const char*>("option");
        address.object = reader.getAttribute<const char*>("object");
        address.property = reader.getAttribute<const char*>("property");
        Stated stated;
        stated.type = reader.getAttribute<const char*>("type", "");

        // The value is taken as TEXT before anything tries to interpret it, so that a value this
        // build cannot read is still a value this build can give back (Amendment 19 Clause 19.1).
        // An empty override -- one that states an address and no value -- stops here.
        std::string words;
        const int inside = reader.level();
        if (inside > table && nextChildElement(reader, inside)) {
            words = reader.readElementAsText();
        }

        if (words.empty()) {
            stated.reason = "the file states no value for it";
            read[address] = std::move(stated);
            continue;
        }

        const Base::Type kind = propertyKindNamed(stated.type);
        if (kind.isBad()) {
            stated.words = words;
            stated.reason = "this build has no property of kind '" + stated.type + "'";
            read[address] = std::move(stated);
            continue;
        }

        std::shared_ptr<Property> value(static_cast<Property*>(kind.createInstance()));
        if (!value) {
            stated.words = words;
            stated.reason = "a '" + stated.type + "' could not be made to read it into";
            read[address] = std::move(stated);
            continue;
        }

        // Read from the words rather than from the live reader, so that a value that will not read
        // leaves the words intact -- a reader stopped half way through a value it could not
        // interpret has already consumed the text that says what it was.
        //
        // Held in an element of its own, which is what a property's own reader expects to be
        // stepped into: it asks for its element by name from wherever the reader is standing, and
        // a reader standing at the top of a document whose only element IS that value would read
        // straight past it and off the end.
        std::istringstream text("<Value>\n" + words + "</Value>\n");
        Base::XMLReader statement(statementSource, text);
        if (!statement.isValid()) {
            stated.words = words;
            stated.reason = "the value is not well-formed: " + statement.whyInvalid();
            read[address] = std::move(stated);
            continue;
        }
        try {
            statement.readElement("Value");
            value->Restore(statement);
            stated.value = std::move(value);
        }
        catch (const Base::Exception& why) {
            stated.words = words;
            stated.reason = "a '" + stated.type + "' will not read it: " + why.what();
        }
        catch (const std::exception& why) {
            // A value that fails in the standard library rather than in ours -- a number that is
            // not one reaches `stod`, which throws something no catch of ours would otherwise name.
            stated.words = words;
            stated.reason = "a '" + stated.type + "' will not read it: " + why.what();
        }
        read[address] = std::move(stated);
    }
    reader.readEndElement("Overrides");

    aboutToSetValue();
    _stated = std::move(read);
    hasSetValue();
}

// -----------------------------------------------------------------------------------------------
//  Python
// -----------------------------------------------------------------------------------------------

PyObject* PropertyOverrideTable::getPyObject()
{
    Py::Dict byOption;
    for (const auto& [address, stated] : _stated) {
        Py::Dict byObject;
        if (byOption.hasKey(address.option)) {
            byObject = Py::Dict(byOption.getItem(address.option));
        }
        Py::Dict byProperty;
        if (byObject.hasKey(address.object)) {
            byProperty = Py::Dict(byObject.getItem(address.object));
        }
        // A value this build could not read reads as None. It is not a value, and dressing it as
        // one -- as its own words, say -- would make it indistinguishable from an override whose
        // value really is that text. Why it could not be read is stated on the object whose value
        // it would have set, which is where anything that has to act on it will look.
        byProperty.setItem(address.property,
                           stated.value ? Py::asObject(stated.value->getPyObject())
                                        : Py::Object(Py_None));
        byObject.setItem(address.object, byProperty);
        byOption.setItem(address.option, byObject);
    }
    return Py::new_reference_to(byOption);
}

void PropertyOverrideTable::setPyObject(PyObject* value)
{
    auto* owner = dynamic_cast<DocumentObject*>(getContainer());
    Document* doc = owner != nullptr ? owner->getDocument() : nullptr;
    if (doc == nullptr) {
        throw Base::RuntimeError("An override table states values for objects in a document, and "
                                 "this one is not in a document yet.");
    }
    if (!PyDict_Check(value)) {
        throw Base::TypeError("Overrides are stated as {option: {object: {property: value}}}.");
    }

    // Built whole before anything is kept, so a table half-replaced by a statement that turned out
    // to be wrong is not a state this can end in.
    std::map<Address, Stated> stating;
    const Py::Dict byOption(value);
    for (const auto& optionKey : byOption.keys()) {
        const std::string option = Py::Object(optionKey).as_string();
        const Py::Object statedForOption = byOption.getItem(optionKey);
        if (!statedForOption.isDict()) {
            throw Base::TypeError("Option '" + option
                                  + "' must state {object: {property: value}}.");
        }
        const Py::Dict byObject(statedForOption);
        for (const auto& objectKey : byObject.keys()) {
            const std::string objectName = Py::Object(objectKey).as_string();
            const Py::Object statedForObject = byObject.getItem(objectKey);
            if (!statedForObject.isDict()) {
                throw Base::TypeError("Object '" + objectName + "' under option '" + option
                                      + "' must state {property: value}.");
            }
            DocumentObject* target = doc->getObject(objectName.c_str());
            if (target == nullptr) {
                throw Base::NameError("This document holds no object '" + objectName + "'.");
            }
            const Py::Dict byProperty(statedForObject);
            for (const auto& propertyKey : byProperty.keys()) {
                const std::string propertyName = Py::Object(propertyKey).as_string();
                Property* live = target->getPropertyByName(propertyName.c_str());
                if (live == nullptr) {
                    throw Base::NameError("'" + objectName + "' has no property '" + propertyName
                                          + "'.");
                }
                // Typed by the property it is for, and refused here if that property would refuse
                // it. An override that cannot be a value of the kind it overrides is a mistake
                // worth hearing about while the person is making it, rather than a statement the
                // document carries and then declines to honour on every open.
                if (live->holdsOpaqueBulk()) {
                    throw Base::ValueError("'" + objectName + "." + propertyName
                                           + "' keeps its value in a file of its own, which is "
                                             "not something an override can state.");
                }
                std::shared_ptr<Property> mine = likeThisOne(*live);
                if (!mine) {
                    throw Base::RuntimeError("'" + objectName + "." + propertyName
                                             + "' would not give a copy of its value.");
                }
                mine->setPyObject(byProperty.getItem(propertyKey).ptr());
                Stated entry;
                entry.type = mine->getTypeId().getName();
                entry.value = std::move(mine);
                stating[Address {option, objectName, propertyName}] = std::move(entry);
            }
        }
    }

    aboutToSetValue();
    _stated = std::move(stating);
    hasSetValue();
}

// -----------------------------------------------------------------------------------------------
//  The rest of what a property owes
// -----------------------------------------------------------------------------------------------

Property* PropertyOverrideTable::Copy() const
{
    auto* copy = new PropertyOverrideTable();
    copy->_stated = copyOfWhatIsStated();
    return copy;
}

void PropertyOverrideTable::Paste(const Property& from)
{
    const auto* table = dynamic_cast<const PropertyOverrideTable*>(&from);
    if (table == nullptr) {
        return;
    }
    aboutToSetValue();
    _stated = table->copyOfWhatIsStated();
    hasSetValue();
}

unsigned int PropertyOverrideTable::getMemSize() const
{
    unsigned int size = 0;
    for (const auto& [address, stated] : _stated) {
        size += static_cast<unsigned int>(address.option.size() + address.object.size()
                                          + address.property.size() + stated.type.size()
                                          + stated.words.size() + stated.reason.size());
        if (stated.value) {
            size += stated.value->getMemSize();
        }
    }
    return size;
}

bool PropertyOverrideTable::isSame(const Property& other) const
{
    if (&other == this) {
        return true;
    }
    const auto* table = dynamic_cast<const PropertyOverrideTable*>(&other);
    if (table == nullptr || table->_stated.size() != _stated.size()) {
        return false;
    }
    auto theirs = table->_stated.begin();
    for (auto mine = _stated.begin(); mine != _stated.end(); ++mine, ++theirs) {
        if (!(mine->first == theirs->first) || mine->second.type != theirs->second.type
            || mine->second.words != theirs->second.words) {
            return false;
        }
        const bool mineRead = static_cast<bool>(mine->second.value);
        if (mineRead != static_cast<bool>(theirs->second.value)) {
            return false;
        }
        if (mineRead && !mine->second.value->isSame(*theirs->second.value)) {
            return false;
        }
    }
    return true;
}
