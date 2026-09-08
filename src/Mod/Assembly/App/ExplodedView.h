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
#include <App/GroupExtension.h>

#include <map>
#include <string>
#include <vector>

#include <Mod/Part/App/TopoShape.h>

#include "ExplodedViewStep.h"


namespace Assembly
{

class AssemblyObject;

/**
 * A saved explosion of an assembly: an ordered group of moves that displace
 * components away from each other so the parts can be seen separately.
 *
 * This is the typed replacement for the former Python `ExplodedView` proxy. As a
 * real type it can be asked for its exploded shape directly; TechDraw previously
 * had to test every object it was handed for a Python attribute called
 * "getExplodedShape", and the assembly view provider for one called
 * "explodeTemporarily".
 *
 * An exploded view is a VIEW of the assembly, never an edit of it: the two
 * document-mutating entry points here (explodeTemporarily / restoreAssembly) are
 * paired, and getExplodedShape computes without touching the document at all.
 */
class AssemblyExport ExplodedView: public App::DocumentObject, public App::GroupExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Assembly::ExplodedView);

public:
    ExplodedView();
    ~ExplodedView() override;

    App::DocumentObjectExecReturn* execute() override;

    /// The assembly this view explodes, or nullptr if it belongs to none.
    AssemblyObject* getAssembly() const;

    /// The moves of this view, in order.
    std::vector<ExplodedViewStep*> getSteps() const;

    /**
     * Apply every move to the assembly, in order, and report the lines the
     * components travelled along.
     *
     * The centre and size the radial moves scale against are taken from the
     * assembly when not supplied.
     */
    std::vector<ExplosionLine> applyMoves();
    std::vector<ExplosionLine> applyMoves(const Base::Vector3d& com, double size);

    /// Remember the assembly's placements, then explode it in place. Paired with
    /// restoreAssembly(); calling it twice without restoring keeps the first snapshot.
    void explodeTemporarily();

    /// Put every component back where it was before explodeTemporarily().
    void restoreAssembly();

    /// Explode the assembly and return a compound of the explosion lines.
    Part::TopoShape saveAssemblyAndExplode();

    /**
     * A compound of the assembly as it would look exploded -- every visible
     * component at its exploded placement, plus the explosion lines.
     *
     * Computed without moving anything: the document is left untouched, which is
     * what lets a drawing consume an exploded view without disturbing the model.
     */
    Part::TopoShape getExplodedShape() const;

    /**
     * Present the exploded compound as this object's shape.
     *
     * This is the standard route by which any object offers geometry to the rest of
     * the application, so a consumer -- a drawing, say -- needs to know nothing about
     * exploded views to show one. TechDraw previously had to special-case them.
     */
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* pmat,
        bool transform,
        int depth
    ) const override;

    PyObject* getPyObject() override;

    /// Hosts the (still-Python) ViewProviderExplodedView over the FeaturePython view
    /// provider shell, as Joint does. Ported to C++ with the view provider slice.
    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderFeaturePython";
    }

private:
    /// Placements to restore, keyed by component name. Transient: an explosion in
    /// progress is a session state, never part of what the document says.
    std::map<std::string, Base::Placement> initialPlacements;
};


}  // namespace Assembly
