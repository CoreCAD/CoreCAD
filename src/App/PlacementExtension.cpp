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


#include <Base/Tools.h>

#include "Extension.h"
#include "PlacementExtension.h"
#include "PlacementExtensionPy.h"


namespace App
{

EXTENSION_PROPERTY_SOURCE(App::PlacementExtension, App::DocumentObjectExtension)


EXTENSION_PROPERTY_SOURCE_TEMPLATE(App::PlacementExtensionPython, App::PlacementExtension)

// explicit template instantiation
template class AppExport ExtensionPythonT<PlacementExtensionPythonT<PlacementExtension>>;


PlacementExtension::PlacementExtension()
{
    initExtensionType(PlacementExtension::getExtensionClassTypeId());
    EXTENSION_ADD_PROPERTY_TYPE(Placement, (Base::Placement()), nullptr, Prop_NoRecompute, nullptr);
}

PlacementExtension::~PlacementExtension() = default;

PyObject* PlacementExtension::getExtensionPyObject()
{
    if (ExtensionPythonObject.is(Py::_None())) {
        // ref counter is set to 1
        auto ext = new PlacementExtensionPy(this);
        ExtensionPythonObject = Py::Object(ext, true);
    }
    return Py::new_reference_to(ExtensionPythonObject);
}

}  // namespace App
