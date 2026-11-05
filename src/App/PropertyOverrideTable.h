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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "Property.h"

namespace App
{

/** The values a document's configuration options state for other objects' properties (§7.7).
 *
 * Cruth: an override is a VALUE a person authored, and the format has one dialect for those --
 * the property's own serializer, the same one every other authored value in the document is
 * written by (Amendment 18 Clause 18.5). This table holds each stated value as a property of the
 * kind it is for, so a length is stored as a length, in its own unit, and read back by the reader
 * that wrote it.
 *
 * It replaces a map of TEXT. The text was handed to the Python interpreter to turn it into a
 * value, which made opening a document an act of running whatever it carried; that was closed
 * first by reading the text as a literal instead, and closed here by there being no text to
 * interpret at all. The value never passes through a language on its way in or out.
 *
 * Whether an override may be a COMPUTED value rather than a stated one is a separate question and
 * deliberately not answered here. If it may, it is a formula, and the program already has an
 * expression grammar that binds its references by durable identity -- which is where a formula
 * would belong, and it is still not a general-purpose language stored in a file.
 *
 * What a value is FOR is addressed by the in-document name of the object and the name of its
 * property, which is what a configuration has always used: it sets values from outside the
 * dependency graph rather than linking to what it sets.
 */
class AppExport PropertyOverrideTable: public Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /// Which option states a value, and whose value it is.
    struct Address
    {
        std::string option;
        std::string object;
        std::string property;

        bool operator<(const Address& other) const
        {
            return std::tie(option, object, property)
                < std::tie(other.option, other.object, other.property);
        }
        bool operator==(const Address& other) const
        {
            return option == other.option && object == other.object && property == other.property;
        }
    };

    /** One stated value: either a value this build read, or the words it could not.
     *
     * Never neither, and never both. A value this build cannot read is kept exactly as the file
     * states it and named as unread, so a save writes back what it was given rather than quietly
     * publishing the loss over the file (Amendment 19 Clause 19.1) -- and so the object whose
     * value it would have set can be blocked, rather than left rebuilding at a value nobody
     * chose under the name of an option that failed to apply.
     */
    struct Stated
    {
        /// The kind of property the value was authored for, as the file names it.
        std::string type;
        /// The value, held as the kind of property it is for. Null when it could not be read.
        std::shared_ptr<Property> value;
        /// The file's own words for the value, kept only when `value` is null.
        std::string words;
        /// Why it could not be read. Empty exactly when `value` is not null.
        std::string reason;
    };

    PropertyOverrideTable();
    ~PropertyOverrideTable() override;

    /// Every entry, in the one order this table is written and read in.
    const std::map<Address, Stated>& getValues() const
    {
        return _stated;
    }

    /// Declare the table empty. The property macros ask every property to state a starting
    /// value, and an override table's is that nothing is overridden.
    void setValue()
    {}

    /// What is stated there, or null if nothing is.
    const Stated* stated(const Address& address) const;

    /** State a value, taken from a property of the kind it is for.
     *
     * The value is copied, so the table holds its own and does not follow later edits to the
     * property it came from. A property that keeps its value in a file of its own is refused: an
     * override is a value small enough to sit in the recipe, and a table that pushed the whole
     * document's overrides into the source store would name none of them.
     */
    void stateValue(const Address& address, const Property& takenFrom);

    /// Stop stating it. True if there was something to stop stating.
    bool forgetValue(const Address& address);
    /// Stop stating anything under that option. Returns how many entries went.
    int forgetOption(const std::string& option);
    /// Stop stating anything at all.
    void clear();

    PyObject* getPyObject() override;
    void setPyObject(PyObject* value) override;

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;

    Property* Copy() const override;
    void Paste(const Property& from) override;

    unsigned int getMemSize() const override;
    bool isSame(const Property& other) const override;

private:
    /// Deep copy of the table -- each stated value is a property of its own, never shared.
    std::map<Address, Stated> copyOfWhatIsStated() const;

    std::map<Address, Stated> _stated;
};

}  // namespace App
