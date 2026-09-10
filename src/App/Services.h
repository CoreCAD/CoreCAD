// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include "DocumentObject.h"

#include <optional>
#include <Base/Placement.h>

namespace App
{

/**
* This service should provide placement of given sub object (like for example face).
* This feature is not implemented in the core and so it must be provided by module.
*/
class SubObjectPlacementProvider
{
public:
    virtual ~SubObjectPlacementProvider() = default;

    /**
    * Returns placement of sub object relative to the base placement.
    */
    virtual Base::Placement calculate(SubObjectT object, Base::Placement basePlacement) const = 0;
};

/**
* This service should provide center of mass calculation;
*/
class CenterOfMassProvider
{
public:
    virtual ~CenterOfMassProvider() = default;

    virtual bool supports(DocumentObject* object) const = 0;
    virtual std::optional<Base::Vector3d> ofDocumentObject(DocumentObject* object) const = 0;
};

/**
* Default implementation for the center of mass contract
* It always returns empty optional
*/
class NullCenterOfMass final : public CenterOfMassProvider
{
public:
    std::optional<Base::Vector3d> ofDocumentObject(DocumentObject* object) const override;
    bool supports(DocumentObject* object) const override;
};

/**
* This service should provide custom attribute access of a Python object
*/
class CustomAttributeProvider
{
public:
    virtual ~CustomAttributeProvider() = default;

    virtual std::optional<PyObject*> getAttribute(DocumentObject* object, const char* attr) const = 0;
};

/**
* The container holding an object's chosen appearance -- the colours, the draw style, the
* transparency a person picked for it.
*
* A colour a person chose is authored content: nothing in the document produces it, and a record
* that dropped it would come back a different-looking part. It therefore belongs in the file of
* record rather than in the deletable project cache. But where that state lives is a question only
* the view layer can answer, and the file of record is written in App -- so the file asks through
* this service. With no GUI nothing answers, and the file simply carries no appearance, which is
* the honest result: a headless session chose none.
*/
class DisplayStateProvider
{
public:
    virtual ~DisplayStateProvider() = default;

    /// The object's appearance container, or null when this session has none for it.
    virtual PropertyContainer* appearanceOf(const DocumentObject& object) const = 0;
};

/**
* This service should provide access to shape elements
*/
class PseudoShapeProvider
{
public:
    virtual ~PseudoShapeProvider() = default;

    virtual Py::Object getElement(const Py::Object& module, const Py::Object& object, const std::string& subname) const = 0;
};

}
