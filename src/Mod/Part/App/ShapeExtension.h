// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/***************************************************************************
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

#pragma once

#include <App/DocumentObjectExtension.h>
#include <Mod/Part/PartGlobal.h>

namespace App
{
class GeoFeature;
}

namespace Part
{

/**
 * @brief The opt-in "is a shape source" capability (spike for #79).
 *
 * FreeCAD models "this object carries a shape" as inheritance from the fat
 * Part::Feature / Part::ShapeFeature base, which bundles the stored geometry,
 * the topological-naming element map, material, and — the load-bearing part —
 * the type identity that ~88 sites test with isDerivedFrom<Part::ShapeFeature>
 * to mean "can I pull a shape from this object?".
 *
 * This extension is the same peel Amendment 4 already performed for placement
 * (App::PlacementExtension): it makes "is a shape source" a capability an object
 * *composes and answers* rather than a rung it inherits. The extension hosts the
 * shape-source half of the getSubObject contract, sourcing the backing geometry
 * through the object's existing App::GeoFeature::getPropertyOfGeometry() hook —
 * so a normal feature backs it with a stored Shape it computes, and a Body backs
 * it with its derived Tip shape, with no special-casing on either side.
 *
 * Spike scope: additive only. It does not yet replace any concrete class's own
 * getSubObject override, nor migrate any consumer off isDerivedFrom; it exists to
 * prove the extension can carry the element map faithfully before the full
 * migration is drafted as an amendment.
 */
class PartExport ShapeExtension: public App::DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::ShapeExtension);
    using inherited = App::DocumentObjectExtension;

public:
    ShapeExtension();
    ~ShapeExtension() override;

    /** The shape-source half of the getSubObject contract, hosted as a capability.
     *
     * Resolves a sub-element reference against the backing geometry (obtained from
     * the extended object's getPropertyOfGeometry()) and returns the element-mapped
     * sub-shape, mirroring Part::ShapeFeature::getSubObject. Returns false — letting
     * the base DocumentObject::getSubObject continue — when the reference navigates
     * to a child object rather than a sub-element of this object's own shape.
     */
    bool extensionGetSubObject(
        App::DocumentObject*& ret,
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* mat,
        bool transform,
        int depth
    ) const override;
};

/// Capability check: does this object provide the shape-source contract?
/// The extension-based successor to isDerivedFrom<Part::ShapeFeature>().
PartExport bool hasShape(const App::DocumentObject* obj);

}  // namespace Part
