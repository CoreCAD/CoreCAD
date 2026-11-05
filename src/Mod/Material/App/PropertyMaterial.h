// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023-2024 David Carter <dcarter@david.carter.ca>        *
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

#pragma once

#include <optional>

#include <App/Property.h>
#include <Base/Reader.h>

#include "Materials.h"

namespace App
{
class Material;
}

namespace Materials
{

/** Material properties
 * This is the father of all properties handling colors.
 */
class MaterialsExport PropertyMaterial: public App::Property
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    /**
     * A constructor.
     * A more elaborate description of the constructor.
     */
    PropertyMaterial();

    /**
     * A destructor.
     * A more elaborate description of the destructor.
     */
    ~PropertyMaterial() override;

    /** Sets the property
     */
    void setValue(const Material& mat);

    /** Sets the appearance properties
     */
    void setValue(const App::Material& mat);

    /** This method returns a string representation of the property
     */
    const Material& getValue() const;

    PyObject* getPyObject() override;
    void setPyObject(PyObject*) override;

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;

    const char* getEditorName() const override;

    Property* Copy() const override;
    void Paste(const Property& from) override;

    unsigned int getMemSize() const override
    {
        return sizeof(_material) + (_carried ? sizeof(*_carried) : 0);
    }

    bool isSame(const Property& other) const override
    {
        if (&other == this) {
            return true;
        }
        return getTypeId() == other.getTypeId()
            && getValue() == static_cast<decltype(this)>(&other)->getValue();
    }

private:
    Material _material;
    /** The copy of the library value the document carries, as the file stated it.
     *
     * Kept rather than re-derived (Amendment 18 Clause 18.3). Where this machine has the library,
     * the material in memory is the LIBRARY's, and a save that wrote that back would replace the
     * copy the author saved with whatever the library holds today -- deciding, silently and in
     * the file, which of the two governs when they differ. That is the one question the clause
     * reserves. So the words the document came with stand until a person chooses a material,
     * which is the only moment what this object is made of actually changed.
     */
    std::optional<Material> _carried;
};

}  // namespace Materials