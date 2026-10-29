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

#include <vector>

#include <App/DocumentObjectExtension.h>
#include <Mod/Part/PartGlobal.h>

namespace App
{
class Document;
class DocumentObject;
class GeoFeature;
}  // namespace App

namespace Part
{

class TopoShape;

/**
 * @brief The "carries a shape" capability (Amendment 17).
 *
 * An object composes this extension instead of inheriting a shape base class. It hosts the
 * shape-source half of the getSubObject contract and answers through backingShape(): a normal
 * feature with the Shape it stores, a Body with the shape of its shown step (17.2). Ask
 * Part::hasShape / Part::getShape, not isDerivedFrom<Part::ShapeFeature>, which misses a Body.
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

    /// The shape this object carries: its stored geometry by default. An object that derives
    /// its shape instead of storing one overrides this.
    virtual TopoShape backingShape() const;
};

/// Capability check: does this object provide the shape-source contract?
/// The extension-based successor to isDerivedFrom<Part::ShapeFeature>().
PartExport bool hasShape(const App::DocumentObject* obj);

/// Capability read: the object's ShapeExtension::backingShape() — a stored Shape for a
/// normal feature, the derived shown-step shape for a Body — or an empty TopoShape when
/// the object carries no shape. Returns the raw, local-frame geometry (no placement
/// transform, no sub-element resolution): the extension-based successor to the
/// bare static_cast<Part::ShapeFeature*>(obj)->Shape.getShape() property read.
/// For transform- or sub-element-aware reads, use Part::Feature::getTopoShape().
PartExport TopoShape getShape(const App::DocumentObject* obj);

/// @p obj when it provides the shape-source contract, else null: the extension-based
/// successor to dynamic_cast<Part::ShapeFeature*>(obj).
PartExport App::GeoFeature* shapeCarrier(App::DocumentObject* obj);
PartExport const App::GeoFeature* shapeCarrier(const App::DocumentObject* obj);

/// Capability enumeration: every object in the document that provides the
/// shape-source contract (Part::hasShape). The extension-based successor to
/// doc->getObjectsOfType(Part::ShapeFeature::getClassTypeId()).
PartExport std::vector<App::DocumentObject*> getShapeObjects(const App::Document* doc);

}  // namespace Part
