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

#include <Mod/Assembly/AssemblyGlobal.h>

#include <App/DocumentObject.h>
#include <App/FeaturePython.h>
#include <App/PropertyLinks.h>


namespace Assembly
{

/**
 * A grounded joint: it pins one assembly component in place.
 *
 * Structurally distinct from a mate (Assembly::Joint): it carries only the
 * link to the grounded component, no connectors. The assembly solver
 * recognises a grounded joint by the presence of an `ObjectToGround` link
 * property (AssemblyObject::getGroundedJoints), so keeping that property name
 * — and keeping this a separate type — means the solver needs no change.
 *
 * Like a mate it is assembly-scoped content, so a typed document admits or
 * refuses it at the door (ARCHITECTURE §7.1, Amendment 8).
 */
class AssemblyExport GroundedJoint: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::GroundedJoint);

public:
    GroundedJoint();
    ~GroundedJoint() override;

    /// The component this joint fixes in place.
    App::PropertyLinkGlobal ObjectToGround;

    App::DocumentObjectExecReturn* execute() override;

    PyObject* getPyObject() override;

    /// Host the (still-Python) ViewProviderJoint over the FeaturePython view
    /// provider shell; see Assembly::Joint::getViewProviderName. Ported to a
    /// C++ view provider with #60.
    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderFeaturePython";
    }

    /// A grounded joint is assembly-scoped content: only an Assembly document admits it.
    App::DocumentObject::ContentScope getContentScope() const override
    {
        return App::DocumentObject::ContentScope::AssemblyItem;
    }
};

/**
 * Python-proxy-enabled variant of GroundedJoint (transitional bridge, #59).
 * See Assembly::JointPython for the rationale; retired the same way.
 */
using GroundedJointPython = App::FeaturePythonT<GroundedJoint>;


}  // namespace Assembly
