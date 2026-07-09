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

#include <App/DocumentObject.h>
#include <App/DocumentObjectExtension.h>
#include <App/ExtensionPython.h>
#include <App/PropertyGeo.h>

namespace App
{
class PlacementExtensionPy;

/**
 * @brief The opt-in "authored position" capability (Amendment 4).
 *
 * A geometric object either *authors* a coordinate frame of its own — a
 * primitive built at the origin then lifted into place, a sketch/datum that
 * defines a frame, an imported shape the user can move — or it is *derived*,
 * its geometry already living in the document world frame with no position of
 * its own to store. Only the former carry this extension. The latter simply
 * answer the world-frame query (App::GeoFeature) from their geometry.
 *
 * This is the structural form of Clause 4.2 ("position belongs only to frame
 * anchors"): position is no longer a birthright bolted onto every GeoFeature,
 * it is a capability the placed classes opt into. The property is named
 * "Placement" and lives in the object's property namespace exactly as before,
 * so Python (obj.Placement), serialization, and expressions are unaffected for
 * the objects that carry it.
 */
class AppExport PlacementExtension: public DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(App::PlacementExtension);
    using inherited = DocumentObjectExtension;

public:
    /// Constructor
    PlacementExtension();
    ~PlacementExtension() override;

    PyObject* getExtensionPyObject() override;

    /// The authored position of the owning object, in its parent coordinate system.
    PropertyPlacement Placement;
};

template<typename ExtensionT>
class PlacementExtensionPythonT: public ExtensionT
{
public:
    PlacementExtensionPythonT() = default;
    ~PlacementExtensionPythonT() override = default;
};

using PlacementExtensionPython = ExtensionPythonT<PlacementExtensionPythonT<PlacementExtension>>;

}  // namespace App
