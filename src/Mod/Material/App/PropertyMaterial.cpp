// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <QMetaType>
#include <QStringList>
#include <QUuid>

#include <optional>
#include <string>

#include <App/Application.h>
#include <App/Property.h>
#include <Base/Console.h>
#include <Base/Quantity.h>
#include <Base/Writer.h>
#include <Gui/MetaTypes.h>

#include "Exceptions.h"
#include "MaterialManager.h"
#include "MaterialPy.h"
#include "Model.h"
#include "PropertyMaterial.h"

using namespace Materials;

/* TRANSLATOR Material::PropertyMaterial */

namespace
{

/// The kinds of value this form states in full.
///
/// A table of measurements, an embedded image and a list are real material data and are not
/// written here yet. They are NAMED instead of passed over, because a copy that quietly held
/// less than the material it copied would be a document reporting itself complete while the
/// part it describes had lost half of what it was made of.
bool statesItselfInFull(Materials::MaterialValue::ValueType type)
{
    switch (type) {
        case Materials::MaterialValue::String:
        case Materials::MaterialValue::Boolean:
        case Materials::MaterialValue::Integer:
        case Materials::MaterialValue::Float:
        case Materials::MaterialValue::Quantity:
        case Materials::MaterialValue::Color:
        case Materials::MaterialValue::File:
        case Materials::MaterialValue::URL:
        case Materials::MaterialValue::MultiLineString:
        case Materials::MaterialValue::SVG:
            return true;
        default:
            return false;
    }
}

/// One value, written so that reading it back on any machine gives the value that was written.
///
/// Never the user string. That is rendered through whichever unit schema the person saving
/// happens to have set, so the same density leaves one machine as "7900 kg/m^3" and another as
/// "0.29 lb/in^3" -- and a record whose meaning depends on a setting of the machine that wrote
/// it is not a record. A quantity is stated in the unit the material's own model declares for
/// it, which is the unit the library card states it in and the one a person checks against a
/// datasheet; where there is no declared unit the internal one is named beside the number, so
/// the number is never left to be read as whatever the reader assumes.
///
/// The precision is the material format's own (`MaterialValue::PRECISION`), which is what the
/// library card holding this value was written at -- so the copy carries what its source had,
/// and a value written, read and written again comes back the same text rather than drifting a
/// digit on every save of a file that people diff.
QString exactly(const Materials::MaterialProperty& property)
{
    const int digits = Materials::MaterialValue::PRECISION;
    if (property.getType() == Materials::MaterialValue::Quantity) {
        const auto quantity = property.getValue().value<Base::Quantity>();
        const QString units = property.getUnits();
        if (!units.isEmpty()) {
            try {
                const Base::Quantity declared = Base::Quantity::parse(units.toStdString());
                if (declared.getUnit() == quantity.getUnit() && declared.getValue() != 0.0) {
                    return QString::number(quantity.getValueAs(declared), 'g', digits)
                        + QStringLiteral(" ") + units;
                }
            }
            catch (const Base::ParserError&) {
                // The model declares a unit this build cannot parse. The internal one below
                // still says exactly what the number means.
            }
        }
        const QString number = QString::number(quantity.getValue(), 'g', digits);
        const std::string unit = quantity.getUnit().getString();
        if (unit.empty()) {
            return number;
        }
        return number + QStringLiteral(" ") + QString::fromStdString(unit);
    }
    if (property.getType() == Materials::MaterialValue::Float) {
        return QString::number(property.getValue().toDouble(), 'g', digits);
    }
    return property.getValue().toString();
}

/// Write one of the material's properties: what it is, and either its value or why it is absent.
void writeProperty(Base::Writer& writer,
                   const char* section,
                   const QString& name,
                   const Materials::MaterialProperty& property)
{
    writer.Stream() << writer.ind() << '<' << section << " name=\""
                    << Base::Persistence::encodeAttribute(name.toStdString()) << "\" type=\""
                    << Base::Persistence::encodeAttribute(
                           property.getPropertyType().toStdString())
                    << "\"";
    if (!property.getUnits().isEmpty()) {
        writer.Stream() << " units=\""
                        << Base::Persistence::encodeAttribute(property.getUnits().toStdString())
                        << "\"";
    }
    if (!property.getModelUUID().isEmpty()) {
        writer.Stream() << " model=\"" << property.getModelUUID().toStdString() << "\"";
    }
    if (!statesItselfInFull(property.getType())) {
        writer.Stream() << " uncarried=\"a value of this kind is not written here yet\"";
    }
    else if (!property.isNull()) {
        writer.Stream() << " value=\""
                        << Base::Persistence::encodeAttribute(exactly(property).toStdString())
                        << "\"";
    }
    writer.Stream() << "/>" << std::endl;
}

/// The copy of the library value that travels with the document (Amendment 18 Clause 18.3).
///
/// Everything the material says about itself, so that a document opened where the library is
/// absent still knows what the part is made of -- what it weighs, how it behaves under load, how
/// it is drawn -- rather than naming a material and computing with the default's numbers. The
/// identity stays in the element above this one: the identity says where the value came from,
/// the copy is what the document means.
void writeCarried(Base::Writer& writer, const Materials::Material& material)
{
    writer.Stream() << writer.ind() << "<Carried>" << std::endl;
    writer.incInd();

    const bool provenance = !material.getAuthor().isEmpty() || !material.getLicense().isEmpty()
        || !material.getDescription().isEmpty() || !material.getURL().isEmpty()
        || !material.getReference().isEmpty() || !material.getParentUUID().isEmpty();
    if (provenance) {
        // Who wrote the value and on what terms it may be used. A copy that kept the numbers and
        // dropped the licence would leave a person holding material data they could no longer
        // tell the origin of once the library was gone.
        writer.Stream() << writer.ind() << "<Provenance";
        const auto attribute = [&writer](const char* key, const QString& value) {
            if (!value.isEmpty()) {
                writer.Stream() << ' ' << key << "=\""
                                << Base::Persistence::encodeAttribute(value.toStdString()) << "\"";
            }
        };
        attribute("author", material.getAuthor());
        attribute("license", material.getLicense());
        attribute("description", material.getDescription());
        attribute("url", material.getURL());
        attribute("reference", material.getReference());
        attribute("parent", material.getParentUUID());
        writer.Stream() << "/>" << std::endl;
    }

    // In name order, so that two documents carrying the same material carry the same text and a
    // save that changed nothing changes nothing.
    QStringList tags(material.getTags().values());
    tags.sort();
    for (const QString& tag : tags) {
        writer.Stream() << writer.ind() << "<Tag name=\""
                        << Base::Persistence::encodeAttribute(tag.toStdString()) << "\"/>"
                        << std::endl;
    }

    for (const auto& [name, property] : material.getPhysicalProperties()) {
        writeProperty(writer, "Physical", name, *property);
    }
    for (const auto& [name, property] : material.getAppearanceProperties()) {
        writeProperty(writer, "Appearance", name, *property);
    }

    writer.decInd();
    writer.Stream() << writer.ind() << "</Carried>" << std::endl;
}

/// The same copy, rendered on its own, for comparing one material against another.
std::string carriedText(const Materials::Material& material)
{
    Base::StringWriter writer;
    writeCarried(writer, material);
    return writer.getString();
}

/// Give the material somewhere to hold a value whose defining model this machine does not have.
///
/// The model states the property's kind and its unit, and without the model there is no slot for
/// the value to go in. What the file states about the property is enough to make one: the whole
/// point of carrying the copy is that the document does not depend on anything else being here.
void makeRoomFor(Materials::Material& material,
                 bool appearance,
                 const QString& name,
                 const QString& type,
                 const QString& units,
                 const QString& model)
{
    Materials::ModelProperty definition(name, name, type, units, {}, {});
    try {
        auto property = std::make_shared<Materials::MaterialProperty>(definition, model);
        if (appearance) {
            material.getAppearanceProperties()[name] = property;
        }
        else {
            material.getPhysicalProperties()[name] = property;
        }
    }
    catch (const Materials::UnknownValueType&) {
        Base::Console().warning(
            "Material: the carried value '%s' is of a kind this build does not know ('%s'), so "
            "it is not read back.\n",
            name.toStdString().c_str(),
            type.toStdString().c_str());
    }
}

/// Take one carried value back into the material.
void readProperty(Base::XMLReader& reader, Materials::Material& material, bool appearance)
{
    const QString name =
        QString::fromUtf8(reader.getAttribute<const char*>("name", ""));
    if (name.isEmpty()) {
        throw Base::XMLParseException("A carried material value states no name");
    }
    const QString type = QString::fromUtf8(reader.getAttribute<const char*>("type", ""));
    const QString units = QString::fromUtf8(reader.getAttribute<const char*>("units", ""));
    const QString model = QString::fromUtf8(reader.getAttribute<const char*>("model", ""));

    const auto held = [&material, appearance](const QString& of) {
        return appearance ? material.hasAppearanceProperty(of) : material.hasPhysicalProperty(of);
    };

    // The model is asked for first, so a machine that has it gets the property the library
    // defines -- its description, its columns, the rest of the model beside it -- and the
    // carried definition is the fallback rather than the rule.
    if (!held(name) && !model.isEmpty()) {
        if (appearance) {
            material.addAppearance(model);
        }
        else {
            material.addPhysical(model);
        }
    }
    if (!held(name)) {
        makeRoomFor(material, appearance, name, type, units, model);
    }
    if (!held(name)) {
        return;
    }

    if (!reader.hasAttribute("value")) {
        // Either the material states no value for this property, or this form could not write
        // the one it had. Both are read the same way: the property is here and holds nothing.
        return;
    }
    const QString value = QString::fromUtf8(reader.getAttribute<const char*>("value"));

    auto property = appearance ? material.getAppearanceProperty(name)
                               : material.getPhysicalProperty(name);
    switch (property->getType()) {
        case Materials::MaterialValue::Quantity:
            try {
                property->setQuantity(Base::Quantity::parse(value.toStdString()));
            }
            catch (const Base::ParserError&) {
                Base::Console().warning("Material: the carried value '%s' for '%s' is not a "
                                        "quantity this build can read.\n",
                                        value.toStdString().c_str(),
                                        name.toStdString().c_str());
            }
            break;
        case Materials::MaterialValue::Float:
            // Set as a double. The string setter takes a float, and a record read back at less
            // precision than it was written at is a record that changes every time it is opened.
            property->setFloat(value.toDouble());
            break;
        case Materials::MaterialValue::Integer:
            property->setInt(value.toInt());
            break;
        case Materials::MaterialValue::Boolean:
            property->setBoolean(value);
            break;
        default:
            property->setString(value);
            break;
    }
}

/// Build the material the file carries a copy of, from that copy alone.
void readCarried(Base::XMLReader& reader, Materials::Material& material)
{
    const int carried = reader.level();
    while (App::nextChildElement(reader, carried)) {
        const std::string element = reader.localName();
        if (element == "Provenance") {
            material.setAuthor(QString::fromUtf8(reader.getAttribute<const char*>("author", "")));
            material.setLicense(QString::fromUtf8(reader.getAttribute<const char*>("license", "")));
            material.setDescription(
                QString::fromUtf8(reader.getAttribute<const char*>("description", "")));
            material.setURL(QString::fromUtf8(reader.getAttribute<const char*>("url", "")));
            material.setReference(
                QString::fromUtf8(reader.getAttribute<const char*>("reference", "")));
            material.setParentUUID(
                QString::fromUtf8(reader.getAttribute<const char*>("parent", "")));
        }
        else if (element == "Tag") {
            material.addTag(QString::fromUtf8(reader.getAttribute<const char*>("name", "")));
        }
        else if (element == "Physical") {
            readProperty(reader, material, /*appearance=*/false);
        }
        else if (element == "Appearance") {
            readProperty(reader, material, /*appearance=*/true);
        }
        else {
            // Refused rather than skipped: a reader that passes over what it does not recognise
            // is how a document comes back holding less than it was written with.
            throw Base::XMLParseException(
                std::string("A carried material states <") + element
                + ">, which this build does not read");
        }
    }
}

}  // namespace

TYPESYSTEM_SOURCE(Materials::PropertyMaterial, App::Property)

PropertyMaterial::PropertyMaterial() = default;

PropertyMaterial::~PropertyMaterial() = default;

void PropertyMaterial::setValue(const Material& mat)
{
    aboutToSetValue();
    _material = mat;
    // A person has chosen a material, so the copy the document arrived with no longer describes
    // what this object is made of. The next save states the copy of what was just chosen.
    _carried.reset();
    hasSetValue();
}

void PropertyMaterial::setValue(const App::Material& mat)
{
    aboutToSetValue();
    _material = mat;
    _carried.reset();
    hasSetValue();
}

const Material& PropertyMaterial::getValue() const
{
    return _material;
}

PyObject* PropertyMaterial::getPyObject()
{
    return new MaterialPy(new Material(_material));
}

void PropertyMaterial::setPyObject(PyObject* value)
{
    if (PyObject_TypeCheck(value, &(MaterialPy::Type))) {
        setValue(*static_cast<MaterialPy*>(value)->getMaterialPtr());
    }
    else {
        std::string error = std::string("type must be 'Material' not ");
        error += value->ob_type->tp_name;
        throw Base::TypeError(error);
    }
}

void PropertyMaterial::Save(Base::Writer& writer) const
{
    // The identity: where the value came from, and what a later reconciliation matches on. The
    // name is written beside it so that a document opened where the library does not have this
    // material can say what it was called. It is never read in preference to the library: the
    // identifier is the material's identity, the name only a handle for a person.
    writer.Stream() << writer.ind() << "<PropertyMaterial uuid=\""
                    << _material.getUUID().toStdString() << "\" name=\""
                    << encodeAttribute(_material.getName().toStdString()) << "\">" << std::endl;
    writer.incInd();
    // The copy the document came with, where it came with one.
    //
    // Not re-derived from the material in memory. Where this machine HAS the library, the
    // property holds the LIBRARY's material, and writing that back would replace the copy the
    // author saved with whatever the library says today -- silently deciding a question Clause
    // 18.3 deliberately leaves open. The record keeps what it was authored with until a person
    // chooses a material, which is the one moment what this object is made of actually changed.
    writeCarried(writer, _carried ? *_carried : _material);
    writer.decInd();
    writer.Stream() << writer.ind() << "</PropertyMaterial>" << std::endl;
}

void PropertyMaterial::Restore(Base::XMLReader& reader)
{
    reader.readElement("PropertyMaterial");
    const std::string uuid = reader.getAttribute<const char*>("uuid");
    const QString identifier = QString::fromLatin1(uuid.c_str());
    const std::string storedName =
        reader.hasAttribute("name") ? reader.getAttribute<const char*>("name") : "";

    // The copy of the library value the document carries (Amendment 18 Clause 18.3), read into a
    // material of its own -- never over the one in memory, whose values belong to whatever this
    // object was made of before and would show through wherever the copy states nothing.
    std::optional<Material> carried;
    const int property = reader.level();
    while (App::nextChildElement(reader, property)) {
        if (std::string(reader.localName()) != "Carried") {
            throw Base::XMLParseException(std::string("A material states <") + reader.localName()
                                          + ">, which this build does not read");
        }
        Material copy;
        copy.setUUID(identifier);
        if (!storedName.empty()) {
            copy.setName(QString::fromUtf8(storedName.c_str()));
        }
        readCarried(reader, copy);
        carried = copy;
    }

    try {
        const Material& library = *MaterialManager::getManager().getMaterial(identifier);
        setValue(library);
        _carried = carried;
        if (carried && carriedText(*carried) != carriedText(library)) {
            // Disclosed, never resolved quietly (P7). Which of the two governs is the one thing
            // Clause 18.3 reserves, so this build says that they differ and changes neither: the
            // library's material is what the session computes with, as it always has, and the
            // copy the document came with is what the document keeps stating.
            Base::Console().warning(
                "%s carries a copy of material \"%s\" (%s) that differs from the one this "
                "system's library holds. The document keeps its own copy and this session is "
                "computing with the library's.\n",
                getFullName().c_str(),
                storedName.empty() ? "unnamed" : storedName.c_str(),
                uuid.c_str());
        }
        return;
    }
    catch (const MaterialNotFound&) {
        // Handled below. A document that names a material this machine does not have is not a
        // damaged document.
    }

    if (carried) {
        // What the copy is FOR: the document states what the part is made of, so it can be
        // opened, read and computed here without the library the material was looked up from.
        setValue(*carried);
        _carried = carried;
        Base::Console().log("%s refers to material \"%s\" (%s), which is not in any library on "
                            "this system. The copy the document carries is being used.\n",
                            getFullName().c_str(),
                            storedName.empty() ? "unnamed" : storedName.c_str(),
                            uuid.c_str());
        return;
    }

    // The reference is kept, not replaced. Letting the restore fail left the property holding
    // the default material, and the next save wrote the default's own id over the one the
    // document came with -- so a part that travelled to a machine without the right library
    // lost what it was made of permanently, and silently. The values stay at the default, since
    // they are all this machine has to draw and weigh the part with, but the id is the one the
    // author chose, so saving preserves it and reopening on a machine that has the library
    // resolves it.
    // The author's name is kept exactly as written, with no marker added to say it did not
    // resolve. A marker would be written back on the next save and grow on every open after that,
    // corrupting the one record of what the part was made of -- which is the thing this name
    // exists to protect. That the material is unresolved is said in the warning below, and is
    // answerable at any time by asking the library for the identifier.
    Material unresolved = _material;
    unresolved.setUUID(identifier);
    if (!storedName.empty()) {
        unresolved.setName(QString::fromUtf8(storedName.c_str()));
    }
    setValue(unresolved);

    Base::Console().warning(
        "%s refers to material \"%s\" (%s), which is not in any library on this system and "
        "which the document carries no copy of. The reference has been kept and the default "
        "material's values are being used in its place.\n",
        getFullName().c_str(),
        storedName.empty() ? "unnamed" : storedName.c_str(),
        uuid.c_str()
    );
}

const char* PropertyMaterial::getEditorName() const
{
    if (testStatus(MaterialEdit)) {
        return "";  //"Gui::PropertyEditor::PropertyMaterialItem";
    }
    return "";
}

App::Property* PropertyMaterial::Copy() const
{
    PropertyMaterial* p = new PropertyMaterial();
    p->_material = _material;
    p->_carried = _carried;
    return p;
}

void PropertyMaterial::Paste(const App::Property& from)
{
    aboutToSetValue();
    const auto& other = dynamic_cast<const PropertyMaterial&>(from);
    _material = other._material;
    // The copy travels with the value, so an object pasted into another document takes what it
    // is made of with it rather than arriving able to name a material and not describe one.
    _carried = other._carried;
    hasSetValue();
}
