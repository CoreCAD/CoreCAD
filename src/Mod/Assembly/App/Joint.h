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
#include <App/PropertyGeo.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/SuppressibleExtension.h>

#include "JointKind.h"


namespace Assembly
{

/**
 * A mate constraint between two assembly components.
 *
 * This is the typed replacement for the former Python `Joint` proxy
 * (App::FeaturePython distinguished only by a runtime `JointType` property).
 * As a first-class type it declares its content scope (AssemblyItem), so a
 * typed document admits or refuses it at the load/creation door
 * (ARCHITECTURE §7.1, Amendment 8) instead of relying on a Python duck-type.
 *
 * The property set and names are kept identical to the former proxy: the
 * assembly solver reads joints as a property bag by name
 * (getPropertyByName("Reference1"/"Placement1"/…)), so an unchanged naming
 * scheme keeps the solver working without modification. Per-joint-kind
 * behaviour (Fixed, Revolute, …) is layered on top in a later stage; here the
 * type only owns the data.
 */
class AssemblyExport Joint: public App::DocumentObject, public App::SuppressibleExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::Joint);

public:
    Joint();
    ~Joint() override;

    /// The kind of mate; drives which properties the solver reads.
    App::PropertyEnumeration JointType;

    /** @name First joint connector */
    //@{
    App::PropertyXLinkSub Reference1;
    App::PropertyPlacement Placement1;
    App::PropertyBool Detach1;
    App::PropertyPlacement Offset1;
    //@}

    /** @name Second joint connector */
    //@{
    App::PropertyXLinkSub Reference2;
    App::PropertyPlacement Placement2;
    App::PropertyBool Detach2;
    App::PropertyPlacement Offset2;
    //@}

    /** @name Kind-specific parameters */
    //@{
    App::PropertyAngle Angle;
    App::PropertyLength Distance;
    App::PropertyLength Distance2;
    //@}

    /** @name Motion limits */
    //@{
    App::PropertyBool EnableLengthMin;
    App::PropertyBool EnableLengthMax;
    App::PropertyBool EnableAngleMin;
    App::PropertyBool EnableAngleMax;
    App::PropertyLength LengthMin;
    App::PropertyLength LengthMax;
    App::PropertyAngle AngleMin;
    App::PropertyAngle AngleMax;
    //@}

    App::DocumentObjectExecReturn* execute() override;

    /// The behavioural capabilities of this joint's current kind (JointType).
    /// The single source of truth for per-kind decisions (pre-solve, parallel
    /// handling, vertex handling) that used to be `JointType in [...]` tests.
    const JointKind& getKind() const;

    PyObject* getPyObject() override;

    /// A mate is assembly-scoped content: only an Assembly document admits it.
    App::DocumentObject::ContentScope getContentScope() const override
    {
        return App::DocumentObject::ContentScope::AssemblyItem;
    }

private:
    /// Enumeration values for JointType, in the historical order.
    static const char* JointTypeEnums[];

    /**
     * Recompute the joint-coordinate-system placements (Placement1/Placement2).
     *
     * The reference-to-frame geometry (resolving a face/edge/vertex to a local
     * coordinate system) still lives in the Python helper during the #59 bridge;
     * this invokes it through the transitional `Proxy`. It is a no-op on a bare
     * typed Joint that carries no Proxy — that path arrives once the geometry is
     * ported to C++ in a later sub-stage.
     */
    void updateJointCoordinateSystems();
};

/**
 * Python-proxy-enabled variant of Joint (transitional bridge, #59).
 *
 * The typed Joint above owns the data and declares the content scope. Joint
 * behaviour (JCS recompute in execute, interactive onChanged, the solver's
 * mid-solve call-ins) still lives in the Python `Joint` helper for now. This
 * variant carries that helper via a `Proxy` and dispatches execute/onChanged
 * to it, so the flip to a typed object changes no behaviour. As the behaviour
 * is ported to C++ across later stages, the dispatch is retired and creation
 * moves to the bare typed Joint; this alias is then removed.
 */
using JointPython = App::FeaturePythonT<Joint>;


}  // namespace Assembly
