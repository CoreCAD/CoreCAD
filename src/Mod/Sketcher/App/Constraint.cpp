// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <QDateTime>
#include <boost/random.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"


#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <App/Property.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include "Constraint.h"

#include "ConstraintPy.h"


using namespace Sketcher;
using namespace Base;

namespace
{
/// Which point on a geometry a constraint holds, said the way the enumeration names it.
constexpr std::array<const char*, 4> pointPos2str {{"none", "start", "end", "mid"}};

std::string posToString(PointPos pos)
{
    const auto index = static_cast<size_t>(pos);
    return index < pointPos2str.size() ? pointPos2str.at(index) : std::string {"none"};
}

/// The point a file names, or nothing when it names none of them.
std::optional<PointPos> posFromString(const std::string& name)
{
    const auto found = std::ranges::find(pointPos2str, name);
    if (found == pointPos2str.end()) {
        return std::nullopt;
    }
    return static_cast<PointPos>(std::distance(pointPos2str.begin(), found));
}

/// True where a file states a value the way an older, upstream document states it: as a number.
bool statedAsANumber(const std::string& value)
{
    if (value.empty()) {
        return false;
    }
    const size_t first = (value.front() == '-') ? 1 : 0;
    return value.size() > first
        && std::all_of(value.begin() + static_cast<long>(first), value.end(), [](unsigned char c) {
               return std::isdigit(c) != 0;
           });
}

/// The orientations a constraint carries. A set rather than one value, so the file states a set.
const std::array<std::pair<ConstraintOrientations, const char*>, 4> orientation2str {
    {{ConstraintOrientations::CounterClockwise, "CounterClockwise"},
     {ConstraintOrientations::Clockwise, "Clockwise"},
     {ConstraintOrientations::Internal, "Internal"},
     {ConstraintOrientations::External, "External"}}
};

std::string orientationToString(const ConstraintOrientation& orientation)
{
    std::string said;
    for (const auto& [value, name] : orientation2str) {
        if (orientation.testFlag(value)) {
            said.append(said.empty() ? "" : "|").append(name);
        }
    }
    return said.empty() ? std::string {"None"} : said;
}

/// The orientations a file names, or nothing where it names one this build does not have.
std::optional<ConstraintOrientation> orientationFromString(const std::string& stated)
{
    ConstraintOrientation orientation = ConstraintOrientations::None;
    if (stated == "None") {
        return orientation;
    }
    for (const auto& part : std::views::split(stated, '|')) {
        const std::string name(part.begin(), part.end());
        const auto found = std::ranges::find_if(orientation2str, [&name](const auto& known) {
            return name == known.second;
        });
        if (found == orientation2str.end()) {
            return std::nullopt;
        }
        orientation.setFlag(found->first);
    }
    return orientation;
}
}  // namespace


TYPESYSTEM_SOURCE(Sketcher::Constraint, Base::Persistence)

void Constraint::createNewTag()
{
    // Initialize a random number generator, to avoid Valgrind false positives.
    // The random number generator is not threadsafe so we guard it.  See
    // https://www.boost.org/doc/libs/1_62_0/libs/uuid/uuid.html#Design%20notes
    static boost::mt19937 ran;
    static bool seeded = false;
    static boost::mutex random_number_mutex;

    boost::lock_guard<boost::mutex> guard(random_number_mutex);

    if (!seeded) {
        ran.seed(QDateTime::currentMSecsSinceEpoch() & 0xffffffff);
        seeded = true;
    }
    static boost::uuids::basic_random_generator<boost::mt19937> gen(&ran);

    tag = gen();
}

Constraint::Constraint()
{
    createNewTag();
}

// A duplicated constraint must take an identity of its own rather than keep the
// source's. Mirrors Part::Geometry::mintDurableIdentity for the authored-entity layer.
void Constraint::mintDurableIdentity()
{
    createNewTag();
}

Constraint* Constraint::clone() const
{
    return new Constraint(*this);
}

Constraint* Constraint::copy() const
{
    Constraint* temp = new Constraint();
    temp->Value = this->Value;
    temp->Type = this->Type;
    temp->AlignmentType = this->AlignmentType;
    temp->Orientation = this->Orientation;
    temp->Name = this->Name;
    temp->LabelDistance = this->LabelDistance;
    temp->LabelPosition = this->LabelPosition;
    temp->isDriving = this->isDriving;
    temp->InternalAlignmentIndex = this->InternalAlignmentIndex;
    temp->isInVirtualSpace = this->isInVirtualSpace;
    temp->isVisible = this->isVisible;
    temp->isActive = this->isActive;
    temp->elements = this->elements;
    // Do not copy tag, otherwise it is considered a clone, and a "rename" by the expression engine.
    temp->MetaData = this->MetaData;

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    temp->First = this->First;
    temp->FirstPos = this->FirstPos;
    temp->Second = this->Second;
    temp->SecondPos = this->SecondPos;
    temp->Third = this->Third;
    temp->ThirdPos = this->ThirdPos;
#endif

    return temp;
}

PyObject* Constraint::getPyObject()
{
    return new ConstraintPy(new Constraint(*this));
}

Quantity Constraint::getPresentationValue() const
{
    Quantity quantity;
    switch (Type) {
        case Distance:
        case Radius:
        case Diameter:
        case DistanceX:
        case DistanceY:
            quantity.setValue(Value);
            quantity.setUnit(Unit::Length);
            break;
        case Angle:
            quantity.setValue(toDegrees<double>(Value));
            quantity.setUnit(Unit::Angle);
            break;
        case SnellsLaw:
        case Weight:
            quantity.setValue(Value);
            break;
        default:
            quantity.setValue(Value);
            break;
    }

    QuantityFormat format = quantity.getFormat();
    format.option = QuantityFormat::None;
    format.format = QuantityFormat::Default;
    format.setPrecision(6);  // QString's default
    quantity.setFormat(format);
    return quantity;
}

unsigned int Constraint::getMemSize() const
{
    return 0;
}

/// How many of this constraint's elements the file has anything to say about.
///
/// The three-element shape is how the constraint is held in memory, not a fact about the
/// constraint: a Horizontal holds one line and the other two slots are a sentinel standing in for
/// nothing. Padding them into the file made a constraint's line mostly sentinels, which is most of
/// what a person diffing two sketches has to read past. An element that holds nothing at the end
/// is simply not stated; one in the middle is, because its position is what says which reference
/// it is.
size_t Constraint::statedElementCount() const
{
    size_t stated = 0;
    for (size_t i = 0; i < getElementsSize(); ++i) {
        const GeoElementId element = getElement(i);
        if (element.GeoId != GeoEnum::GeoUndef || element.Pos != PointPos::none) {
            stated = i + 1;
        }
    }
    return stated;
}

void Constraint::Save(Writer& writer) const
{
    // No geometry context here: every element resolves to a nil tag, so nothing is
    // written and the reference stays purely positional. The owning
    // PropertyConstraintList calls the geometry-aware overload for real saves.
    Save(writer, [](int) { return boost::uuids::nil_uuid(); });
}

void Constraint::Save(Writer& writer, const GeoIdToTagFn& geoIdToTag) const
{
    std::string encodeName = encodeAttribute(Name);
    std::string encodeMetaData = encodeAttribute(MetaData);
    writer.Stream() << writer.ind() << "<Constrain "
                    << "Name=\"" << encodeName << "\" "
                    << "MetaData=\"" << encodeMetaData << "\" "
                    << "Type=\"" << typeToString(Type) << "\" ";
    if (this->Type == InternalAlignment) {
        writer.Stream() << "InternalAlignmentType=\""
                        << internalAlignmentTypeToString(AlignmentType) << "\" "
                        << "InternalAlignmentIndex=\"" << InternalAlignmentIndex << "\" ";
    }
    writer.Stream() << "Orientation=\"" << orientationToString(Orientation) << "\" ";
    writer.Stream() << "Value=\"" << Value << "\" "
                    << "LabelDistance=\"" << LabelDistance << "\" "
                    << "LabelPosition=\"" << LabelPosition << "\" "
                    << "IsDriving=\"" << (int)isDriving << "\" "
                    << "IsInVirtualSpace=\"" << (int)isInVirtualSpace << "\" "
                    << "IsVisible=\"" << (int)isVisible << "\" "
                    << "IsActive=\"" << (int)isActive << "\" ";

    // The tag is the constraint's durable identity. It is written here so that two
    // versions of a sketch can be lined up on a merge; without it a constraint is
    // located only by its position in the list, which shifts on any edit.
    writer.Stream() << "Tag=\"" << boost::uuids::to_string(tag) << "\"";

    // Each reference the constraint holds, stated once.
    //
    // Cruth: this used to be said three times over -- the deprecated First/FirstPos pair, the
    // parallel ElementIds/ElementPositions lists, and the durable ElementTags -- and the
    // authoritative one was the one that did not win first, since the positional GeoIds loaded on
    // restore were overwritten from the tags afterwards. Three statements of one fact can
    // disagree, and a merge is exactly where they do. Measured before this: a file whose
    // positional statement said geometry 1 while its tag still named geometry 0 opened as
    // geometry 0, with nothing said and the document reporting itself whole.
    //
    // The durable identity is the reference. A GeoId appears only where there is no durable
    // identity to state -- the axes and external geometry, which the sketch does not author.
    writer.Stream() << ">\n";
    writer.incInd();
    for (size_t i = 0; i < statedElementCount(); ++i) {
        const GeoElementId element = getElement(i);
        const boost::uuids::uuid geoTag = geoIdToTag(element.GeoId);
        writer.Stream() << writer.ind() << "<Element ";
        if (!geoTag.is_nil()) {
            writer.Stream() << "tag=\"" << boost::uuids::to_string(geoTag) << "\" ";
        }
        else if (element.GeoId != GeoEnum::GeoUndef) {
            writer.Stream() << "geoId=\"" << element.GeoId << "\" ";
        }
        writer.Stream() << "at=\"" << posToString(element.Pos) << "\"/>\n";
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</Constrain>\n";
}

void Constraint::Restore(XMLReader& reader)
{
    reader.readElement("Constrain");
    Name = reader.getAttribute<const char*>("Name");
    MetaData = reader.hasAttribute("MetaData") ? reader.getAttribute<const char*>("MetaData") : "";
    // A constraint says what it is by name. A document written before the form changed -- one
    // imported from upstream -- states a position in the enumeration instead; that is read
    // positionally, the only way a position can be read, and named on the next save.
    const std::string statedType = reader.getAttribute<const char*>("Type");
    if (const std::optional<ConstraintType> named = Constraint::typeFromString(statedType)) {
        Type = *named;
    }
    else if (statedAsANumber(statedType)) {
        const long position = std::stol(statedType);
        if (position < 0 || position >= NumConstraintTypes) {
            FC_THROWM(Base::ValueError, "constraint type " << position << " is not one this build has");
        }
        Type = static_cast<ConstraintType>(position);
    }
    else {
        // Refused rather than dropped. A constraint this build cannot place is a statement the
        // file makes, and dropping it lets the next save write its absence over the record --
        // the author's constraint gone from their own file with nothing said (Amendment 19).
        FC_THROWM(Base::ValueError, "'" << statedType << "' is not a constraint type this build has");
    }
    Value = reader.getAttribute<double>("Value");

    if (this->Type == InternalAlignment) {
        const std::string statedAlignment = reader.getAttribute<const char*>("InternalAlignmentType");
        if (const std::optional<InternalAlignmentType> named
            = Constraint::internalAlignmentTypeFromString(statedAlignment)) {
            AlignmentType = *named;
        }
        else if (statedAsANumber(statedAlignment)) {
            const long position = std::stol(statedAlignment);
            if (position < 0 || position >= NumInternalAlignmentType) {
                FC_THROWM(
                    Base::ValueError,
                    "internal alignment type " << position << " is not one this build has"
                );
            }
            AlignmentType = static_cast<InternalAlignmentType>(position);
        }
        else {
            FC_THROWM(
                Base::ValueError,
                "'" << statedAlignment << "' is not an internal alignment this build has"
            );
        }

        if (reader.hasAttribute("InternalAlignmentIndex")) {
            InternalAlignmentIndex = reader.getAttribute<long>("InternalAlignmentIndex");
        }
    }
    else {
        AlignmentType = Undef;
    }
    if (reader.hasAttribute("Orientation")) {
        const std::string statedOrientation = reader.getAttribute<const char*>("Orientation");
        if (const std::optional<ConstraintOrientation> named
            = orientationFromString(statedOrientation)) {
            Orientation = *named;
        }
        else if (statedAsANumber(statedOrientation)) {
            Orientation = static_cast<ConstraintOrientations>(std::stol(statedOrientation));
        }
        else {
            FC_THROWM(
                Base::ValueError,
                "'" << statedOrientation << "' is not an orientation this build has"
            );
        }
    }
    else {
        Orientation = ConstraintOrientations::None;
    }

    // Read the distance a constraint label has been moved
    if (reader.hasAttribute("LabelDistance")) {
        LabelDistance = (float)reader.getAttribute<double>("LabelDistance");
    }

    if (reader.hasAttribute("LabelPosition")) {
        LabelPosition = (float)reader.getAttribute<double>("LabelPosition");
    }

    if (reader.hasAttribute("IsDriving")) {
        isDriving = reader.getAttribute<bool>("IsDriving");
    }

    if (reader.hasAttribute("IsInVirtualSpace")) {
        isInVirtualSpace = reader.getAttribute<bool>("IsInVirtualSpace");
    }

    if (reader.hasAttribute("IsVisible")) {
        isVisible = reader.getAttribute<bool>("IsVisible");
    }

    if (reader.hasAttribute("IsActive")) {
        isActive = reader.getAttribute<bool>("IsActive");
    }

    // Restore the durable tag if the file carries one. Files written before tag
    // persistence have no attribute; those constraints keep the fresh tag minted in
    // the constructor.
    if (reader.hasAttribute("Tag")) {
        try {
            tag = boost::uuids::string_generator()(reader.getAttribute<const char*>("Tag"));
        }
        catch (const std::exception&) {
            // Malformed tag: keep the constructor-minted one rather than fail the load.
        }
    }

    // The references this constraint holds.
    //
    // A document written to this form states each one once, as a child element, and the durable
    // identity is the reference itself -- see Save(). A document written by an older program
    // states them as positional attributes instead; that is read the only way a position can be
    // read, and stated by durable identity on the next save.
    if (reader.hasAttribute("First")) {
        restoreElementsStatedPositionally(reader);
    }
    else {
        restoreElementsStatedOnce(reader);
    }

    // Three is the shape the constraint is held in, whatever the file stated: the deprecated
    // First/Second/Third members are the same storage seen through another name, and they have to
    // exist to be read.
    while (getElementsSize() < 3) {
        addElement(GeoElementId(GeoEnum::GeoUndef, PointPos::none));
    }
}

/// One element, stated once, as a document written to this form has it.
///
/// `tag` is the durable identity of geometry the sketch authors; the GeoId it currently occupies
/// is not in the file at all, and is resolved by bindElementsToDurableGeometry once the geometry
/// is loaded. `geoId` appears only for a reference with no durable identity -- an axis, external
/// geometry -- where the number is all there is to state.
void Constraint::restoreElementsStatedOnce(XMLReader& reader)
{
    const int constrain = reader.level();
    elements.clear();
    restoredElementGeoTags.clear();
    boost::uuids::string_generator stringToUuid;

    while (App::nextChildElement(reader, constrain)) {
        App::expectElement(reader, "Element");

        PointPos pos = PointPos::none;
        if (reader.hasAttribute("at")) {
            const std::string statedPos = reader.getAttribute<const char*>("at");
            const std::optional<PointPos> named = posFromString(statedPos);
            if (!named) {
                FC_THROWM(Base::ValueError, "'" << statedPos << "' is not a point on a geometry");
            }
            pos = *named;
        }

        boost::uuids::uuid geoTag = boost::uuids::nil_uuid();
        int geoId = GeoEnum::GeoUndef;
        if (reader.hasAttribute("tag")) {
            const std::string statedTag = reader.getAttribute<const char*>("tag");
            try {
                geoTag = stringToUuid(statedTag);
            }
            catch (const std::exception&) {
                // Refused rather than quietly turned into a reference to nothing. A durable
                // identity that cannot be read is a reference this build cannot honour, and the
                // constraint it belongs to is kept as the file worded it (Amendment 19).
                FC_THROWM(Base::ValueError, "'" << statedTag << "' is not a durable identity");
            }
        }
        else if (reader.hasAttribute("geoId")) {
            geoId = reader.getAttribute<int>("geoId");
        }

        addElement(GeoElementId(geoId, pos));
        restoredElementGeoTags.push_back(geoTag);
    }
}

/// The references as a document written by an older program states them: a GeoId per slot, padded
/// to three with a sentinel, and the durable identity nowhere in the file.
void Constraint::restoreElementsStatedPositionally(XMLReader& reader)
{
    constexpr std::array<const char*, 3> names = {"First", "Second", "Third"};
    constexpr std::array<const char*, 3> posNames = {"FirstPos", "SecondPos", "ThirdPos"};
    static_assert(names.size() == posNames.size());

    for (size_t i = 0; i < names.size(); ++i) {
        if (!reader.hasAttribute(names[i])) {
            continue;
        }
        const int geoId {reader.getAttribute<int>(names[i])};
        const std::string statedPos = reader.getAttribute<const char*>(posNames[i]);
        const std::optional<PointPos> named = posFromString(statedPos);
        if (!named && !statedAsANumber(statedPos)) {
            FC_THROWM(Base::ValueError, "'" << statedPos << "' is not a point on a geometry");
        }
        const PointPos pos = named ? *named : static_cast<PointPos>(std::stoi(statedPos));
        setElement(i, GeoElementId(geoId, pos));
    }
}

bool Constraint::bindElementsToDurableGeometry(const TagToGeoIdFn& tagToGeoId)
{
    bool wentDangling = false;
    for (size_t i = 0; i < elements.size() && i < restoredElementGeoTags.size(); ++i) {
        const boost::uuids::uuid& tag = restoredElementGeoTags[i];
        if (tag.is_nil()) {
            continue;  // no durable handle: the positional GeoId loaded on Restore stands
        }
        if (const std::optional<int> geoId = tagToGeoId(tag)) {
            // The tag is authoritative: overwrite the annotation GeoId with the one the
            // geometry now occupies, keeping the element's PointPos.
            setElement(i, GeoElementId(*geoId, getElement(i).Pos));
        }
        else {
            // Tag present but unresolved => the referenced geometry is genuinely gone. The
            // loaded GeoId is a stale positional index: if the list reordered it may still
            // be in range and now point at a DIFFERENT live element, so keeping it is the
            // silent re-bind §10.1 forbids. Mark the element GeoUndef instead — the loss is
            // then unmistakable to the constraint validator, and disclosed by the caller.
            setElement(i, GeoElementId(GeoEnum::GeoUndef, getElement(i).Pos));
            wentDangling = true;
        }
    }
    restoredElementGeoTags.clear();
    return wentDangling;
}

void Constraint::carryElementReferencesTo(
    const std::map<boost::uuids::uuid, boost::uuids::uuid>& renamed
)
{
    for (boost::uuids::uuid& geoTag : restoredElementGeoTags) {
        const auto found = renamed.find(geoTag);
        if (found != renamed.end()) {
            geoTag = found->second;
        }
    }
}

void Constraint::substituteIndex(int fromGeoId, int toGeoId)
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    for (size_t i = 0; i < elements.size(); ++i) {
        const GeoElementId element = getElement(i);
        if (element.GeoId == fromGeoId) {
            setElement(i, GeoElementId(toGeoId, element.Pos));
        }
    }
#else
    for (auto& element : elements) {
        if (element.GeoId == fromGeoId) {
            element = GeoElementId(toGeoId, element.Pos);
        }
    }
#endif
}

void Constraint::substituteIndexAndPos(int fromGeoId, PointPos fromPosId, int toGeoId, PointPos toPosId)
{
    const GeoElementId from {fromGeoId, fromPosId};

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    for (size_t i = 0; i < elements.size(); ++i) {
        const GeoElementId element = getElement(i);
        if (element == from) {
            setElement(i, GeoElementId(toGeoId, toPosId));
        }
    }
#else
    for (auto& element : elements) {
        if (element == from) {
            element = GeoElementId(toGeoId, toPosId);
        }
    }
#endif
}

std::string Constraint::typeToString(ConstraintType type)
{
    return type2str[type];
}

std::string Constraint::internalAlignmentTypeToString(InternalAlignmentType alignment)
{
    return internalAlignmentType2str[alignment];
}

std::optional<ConstraintType> Constraint::typeFromString(const std::string& name)
{
    const auto found = std::ranges::find(type2str, name);
    if (found == type2str.end()) {
        return std::nullopt;
    }
    return static_cast<ConstraintType>(std::distance(type2str.begin(), found));
}

std::optional<InternalAlignmentType> Constraint::internalAlignmentTypeFromString(const std::string& name)
{
    const auto found = std::ranges::find(internalAlignmentType2str, name);
    if (found == internalAlignmentType2str.end()) {
        return std::nullopt;
    }
    return static_cast<InternalAlignmentType>(std::distance(internalAlignmentType2str.begin(), found));
}

bool Constraint::involvesGeoId(int geoId) const
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    auto elements = std::views::iota(size_t {0}, this->elements.size())
        | std::views::transform([&](size_t i) { return getElement(i); });
#endif
    return std::ranges::any_of(elements, [geoId](const auto& element) {
        return element.GeoId == geoId;
    });
}
/// utility function to check if (`geoId`, `posId`) is one of the points/curves
bool Constraint::involvesGeoIdAndPosId(int geoId, PointPos posId) const
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    auto elements = std::views::iota(size_t {0}, this->elements.size())
        | std::views::transform([&](size_t i) { return getElement(i); });
#endif
    return std::ranges::find(elements, GeoElementId(geoId, posId)) != elements.end();
}

GeoElementId Constraint::getElement(size_t index) const
{
    if (index >= elements.size()) {
        throw Base::IndexError("Constraint::getElement index out of range");
    }

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                return GeoElementId(First, FirstPos);
            case 1:
                return GeoElementId(Second, SecondPos);
            case 2:
                return GeoElementId(Third, ThirdPos);
        }
    }
#endif
    return elements[index];
}

void Constraint::setElement(size_t index, GeoElementId element)
{
    if (ensureElementExists(index)) {
        elements[index] = element;

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
        if (index < 3) {
            switch (index) {
                case 0:
                    First = element.GeoId;
                    FirstPos = element.Pos;
                    break;
                case 1:
                    Second = element.GeoId;
                    SecondPos = element.Pos;
                    break;
                case 2:
                    Third = element.GeoId;
                    ThirdPos = element.Pos;
                    break;
            }
        }
#endif
    }
}

size_t Constraint::getElementsSize() const
{
    return elements.size();
}

void Constraint::addElement(GeoElementId element)
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    int i = elements.size();
    elements.resize(i + 1);
    setElement(i, element);
#else
    elements.push_back(element);
#endif
}

int Constraint::getGeoId(int index) const
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                return First;
            case 1:
                return Second;
            case 2:
                return Third;
        }
    }
#endif
    return hasElement(index) ? elements[index].GeoId : GeoEnum::GeoUndef;
}

PointPos Constraint::getPosId(int index) const
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                return FirstPos;
            case 1:
                return SecondPos;
            case 2:
                return ThirdPos;
        }
    }
#endif
    return hasElement(index) ? elements[index].Pos : PointPos::none;
}

int Constraint::getPosIdAsInt(int index) const
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                return (int)FirstPos;
            case 1:
                return (int)SecondPos;
            case 2:
                return (int)ThirdPos;
        }
    }
#endif
    return hasElement(index) ? elements[index].posIdAsInt() : 0;
}

bool Constraint::hasElement(int index) const
{
    return index >= 0 && static_cast<decltype(elements)::size_type>(index) < elements.size();
}

void Constraint::setGeoId(int index, int geoId)
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                First = geoId;
                break;
            case 1:
                Second = geoId;
                break;
            case 2:
                Third = geoId;
                break;
        }
    }
#endif
    if (ensureElementExists(index)) {
        elements[index].GeoId = geoId;
    }
}

void Constraint::setPosId(int index, PointPos pos)
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                FirstPos = pos;
                break;
            case 1:
                SecondPos = pos;
                break;
            case 2:
                ThirdPos = pos;
                break;
        }
    }
#endif
    if (ensureElementExists(index)) {
        elements[index].Pos = pos;
    }
}

void Constraint::setPosId(int index, int pos)
{
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    if (index < 3) {
        switch (index) {
            case 0:
                FirstPos = static_cast<PointPos>(pos);
                break;
            case 1:
                SecondPos = static_cast<PointPos>(pos);
                break;
            case 2:
                ThirdPos = static_cast<PointPos>(pos);
                break;
        }
    }
#endif
    if (ensureElementExists(index)) {
        elements[index].Pos = static_cast<PointPos>(pos);
    }
}

bool Constraint::ensureElementExists(int index)
{
    if (index < 0) {
        return false;  // Indicate failure for an invalid index
    }
    if (static_cast<decltype(elements)::size_type>(index) >= elements.size()) {
        elements.resize(index + 1);
    }
    return true;
}

void Constraint::swapElements(int index1, int index2)
{
    if (index1 == index2) {
        return;
    }
    if (ensureElementExists(index1) && ensureElementExists(index2)) {
        std::swap(elements[index1], elements[index2]);
    }
}

bool Constraint::isElementsEmpty() const
{
    return elements.empty();
}

void Constraint::truncateElements(size_t newSize)
{
    if (newSize < elements.size()) {
        elements.resize(newSize);
    }
}

std::string Constraint::getText() const
{
    if (MetaData.empty()) {
        return {};
    }
    try {
        auto j = nlohmann::json::parse(MetaData);
        if (j.contains("text")) {
            return j["text"].get<std::string>();
        }
    }
    catch (...) {
        // Handle JSON parsing errors or type mismatches silently
    }
    return {};
}

void Constraint::setText(const std::string& text)
{
    nlohmann::json j;
    if (!MetaData.empty()) {
        try {
            j = nlohmann::json::parse(MetaData);
        }
        catch (...) {
        }
    }
    j["text"] = text;
    MetaData = j.dump();
}

std::string Constraint::getFont() const
{
    if (MetaData.empty()) {
        return {};
    }
    try {
        auto j = nlohmann::json::parse(MetaData);
        if (j.contains("font")) {
            Base::FileInfo fi(j["font"].get<std::string>());
            return fi.fileNamePure();
        }
    }
    catch (...) {
    }
    return {};
}

void Constraint::setFont(const std::string& font)
{
    Base::FileInfo fi(font);
    std::string fontName = fi.fileNamePure();

    nlohmann::json j;
    if (!MetaData.empty()) {
        try {
            j = nlohmann::json::parse(MetaData);
        }
        catch (...) {
        }
    }
    j["font"] = fontName;
    MetaData = j.dump();
}

bool Constraint::getIsTextHeight() const
{
    if (MetaData.empty()) {
        return true;  // Default value
    }
    try {
        auto j = nlohmann::json::parse(MetaData);
        if (j.contains("isTextHeight")) {
            return j["isTextHeight"].get<bool>();
        }
    }
    catch (...) {
    }
    return true;  // Default value
}

void Constraint::setIsTextHeight(bool isHeight)
{
    nlohmann::json j;
    if (!MetaData.empty()) {
        try {
            j = nlohmann::json::parse(MetaData);
        }
        catch (...) {
        }
    }
    j["isTextHeight"] = isHeight;
    MetaData = j.dump();
}
