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
#include <App/PropertyGeo.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>

#include <utility>
#include <vector>

#include <Base/Vector3D.h>


namespace Assembly
{

/// One start/end pair marking where a component travelled during an explosion.
using ExplosionLine = std::pair<Base::Vector3d, Base::Vector3d>;

/**
 * One move within an exploded view: a set of components and the displacement
 * applied to them.
 *
 * This is the typed replacement for the former Python `ExplodedViewStep` proxy
 * (an App::FeaturePython carrying its meaning in an opaque Proxy). As a real
 * type its content is legible to C++ -- the document recipe, TechDraw's shape
 * extraction and the assembly view provider previously had to guess at it by
 * asking a Python object whether it happened to own an attribute of the right
 * name.
 *
 * Property names are unchanged from the proxy so the (still Python) task panel
 * and view provider keep working against the same names.
 */
class AssemblyExport ExplodedViewStep: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::ExplodedViewStep);

public:
    ExplodedViewStep();
    ~ExplodedViewStep() override;

    /// The components this move displaces.
    App::PropertyXLinkSubHidden References;
    /// The displacement itself: end placement = MovementTransform * start placement.
    App::PropertyPlacement MovementTransform;
    /// "Normal" (a rigid displacement) or "Radial" (outward from a centre).
    App::PropertyEnumeration MoveType;

    App::DocumentObjectExecReturn* execute() override;

    /**
     * Apply this move to the components it names, in place.
     *
     * @param com    centre the radial mode explodes away from.
     * @param size   overall assembly size the radial factor is scaled against.
     * @returns the line each moved component travelled along, for the view to draw.
     *
     * Returns an empty list when the reference is unresolved or broken; a move
     * that cannot say what it acts on moves nothing.
     */
    std::vector<ExplosionLine> applyStep(const Base::Vector3d& com, double size);

    /// The components named by References, resolved. Empty if the reference is broken.
    std::vector<App::DocumentObject*> movedComponents() const;

    PyObject* getPyObject() override;

    /// Hosts the (still-Python) ViewProviderExplodedViewStep over the FeaturePython
    /// view provider shell, as Joint does. Ported to C++ with the view provider slice.
    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderFeaturePython";
    }

private:
    static const char* MoveTypeEnums[];
};


}  // namespace Assembly
