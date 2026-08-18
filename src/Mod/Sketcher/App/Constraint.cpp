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

#include <fmt/ranges.h>

#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Writer.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>
#include "Constraint.h"

#include "ConstraintPy.h"


using namespace Sketcher;
using namespace Base;


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
                    << "Type=\"" << (int)Type << "\" ";
    if (this->Type == InternalAlignment) {
        writer.Stream() << "InternalAlignmentType=\"" << (int)AlignmentType << "\" "
                        << "InternalAlignmentIndex=\"" << InternalAlignmentIndex << "\" ";
    }
    writer.Stream() << "Orientation=\"" << Orientation.toUnderlyingType() << "\" ";
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
    writer.Stream() << "Tag=\"" << boost::uuids::to_string(tag) << "\" ";

    // Save elements
    {
        // Ensure backwards compatibility with old versions
        writer.Stream() << "First=\"" << getElement(0).GeoId << "\" "
                        << "FirstPos=\"" << getElement(0).posIdAsInt() << "\" "
                        << "Second=\"" << getElement(1).GeoId << "\" "
                        << "SecondPos=\"" << getElement(1).posIdAsInt() << "\" "
                        << "Third=\"" << getElement(2).GeoId << "\" "
                        << "ThirdPos=\"" << getElement(2).posIdAsInt() << "\" ";
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
        auto elements = std::views::iota(size_t {0}, this->elements.size())
            | std::views::transform([&](size_t i) { return getElement(i); });
#endif
        auto geoIds = elements | std::views::transform([](const GeoElementId& e) { return e.GeoId; });
        auto posIds = elements
            | std::views::transform([](const GeoElementId& e) { return e.posIdAsInt(); });

        const std::string ids = fmt::format("{}", fmt::join(geoIds, " "));
        const std::string positions = fmt::format("{}", fmt::join(posIds, " "));

        writer.Stream() << "ElementIds=\"" << ids << "\" "
                        << "ElementPositions=\"" << positions << "\" ";

        // The durable geometry handle for each element: the authoritative reference the
        // GeoId above only annotates. A "-" marks an element with no durable identity
        // (axes, external geometry, undefined), whose GeoId stays as written.
        auto tagStrings = geoIds | std::views::transform([&](int geoId) {
                              const boost::uuids::uuid tag = geoIdToTag(geoId);
                              return tag.is_nil() ? std::string("-") : boost::uuids::to_string(tag);
                          });
        const std::string tags = fmt::format("{}", fmt::join(tagStrings, " "));
        writer.Stream() << "ElementTags=\"" << tags << "\" ";
    }

    writer.Stream() << "/>\n";
}

void Constraint::Restore(XMLReader& reader)
{
    reader.readElement("Constrain");
    Name = reader.getAttribute<const char*>("Name");
    MetaData = reader.hasAttribute("MetaData") ? reader.getAttribute<const char*>("MetaData") : "";
    Type = reader.getAttribute<ConstraintType>("Type");
    Value = reader.getAttribute<double>("Value");

    if (this->Type == InternalAlignment) {
        AlignmentType = reader.getAttribute<InternalAlignmentType>("InternalAlignmentType");

        if (reader.hasAttribute("InternalAlignmentIndex")) {
            InternalAlignmentIndex = reader.getAttribute<long>("InternalAlignmentIndex");
        }
    }
    else {
        AlignmentType = Undef;
    }
    if (reader.hasAttribute("Orientation")) {
        Orientation = reader.getAttribute<ConstraintOrientations>("Orientation");
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

    if (reader.hasAttribute("ElementIds") && reader.hasAttribute("ElementPositions")) {
        auto splitAndClean = [](std::string_view input) {
            const char delimiter = ' ';

            auto tokens = input | std::views::split(delimiter)
                | std::views::transform([](auto&& subrange) {
                              // workaround due to lack of std::ranges::to in c++20
                              std::string token;
                              auto size = std::ranges::distance(subrange);
                              token.reserve(size);
                              for (char c : subrange) {
                                  token.push_back(c);
                              }
                              return token;
                          })
                | std::views::filter([](const std::string& s) { return !s.empty(); });

            return std::vector<std::string>(tokens.begin(), tokens.end());
        };

        const std::string elementIds = reader.getAttribute<const char*>("ElementIds");
        const std::string elementPositions = reader.getAttribute<const char*>("ElementPositions");

        const auto ids = splitAndClean(elementIds);
        const auto positions = splitAndClean(elementPositions);

        if (ids.size() != positions.size()) {
            throw Base::ParserError(
                fmt::format(
                    "ElementIds and ElementPositions do not match in "
                    "size. Got {} ids and {} positions.",
                    ids.size(),
                    positions.size()
                )
            );
        }

        elements.clear();
        for (size_t i = 0; i < std::min(ids.size(), positions.size()); ++i) {
            const int geoId {std::stoi(ids[i])};
            const PointPos pos {static_cast<PointPos>(std::stoi(positions[i]))};
            addElement(GeoElementId(geoId, pos));
        }

        // The durable geometry handle per element, if the file carries it. Held until
        // the owning SketchObject re-binds GeoIds from these tags after restore.
        // Files written before tag persistence have no attribute; those elements keep
        // the positional GeoId loaded above.
        restoredElementGeoTags.clear();
        if (reader.hasAttribute("ElementTags")) {
            const auto tags = splitAndClean(reader.getAttribute<const char*>("ElementTags"));
            boost::uuids::string_generator stringToUuid;
            for (const std::string& t : tags) {
                restoredElementGeoTags.push_back(t == "-" ? boost::uuids::nil_uuid() : stringToUuid(t));
            }
        }
    }

    // Ensure we have at least 3 elements
    while (getElementsSize() < 3) {
        addElement(GeoElementId(GeoEnum::GeoUndef, PointPos::none));
    }

    // Load deprecated First, Second, Third elements
    // These take precedence over the new elements
    // Even though these are deprecated, we still need to read them
    // for compatibility with old files.
    {
        constexpr std::array<const char*, 3> names = {"First", "Second", "Third"};
        constexpr std::array<const char*, 3> posNames = {"FirstPos", "SecondPos", "ThirdPos"};
        static_assert(names.size() == posNames.size());

        for (size_t i = 0; i < names.size(); ++i) {
            if (reader.hasAttribute(names[i])) {
                const int geoId {reader.getAttribute<int>(names[i])};
                const PointPos pos {reader.getAttribute<PointPos>(posNames[i])};
                setElement(i, GeoElementId(geoId, pos));
            }
        }
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
