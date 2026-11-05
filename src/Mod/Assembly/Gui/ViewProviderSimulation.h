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

#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/ViewProviderGroupExtension.h>


namespace AssemblyGui
{

/**
 * The view provider of an Assembly::Simulation.
 *
 * The typed replacement for the former Python ViewProviderSimulation proxy. As with
 * the exploded view, the group extension supplies the children and their deletion,
 * and the empty display mode the Python class was obliged to declare is gone.
 *
 * The proxy also added a view property "Decimals" ("the number of decimals to use
 * for calculated texts"). Nothing in the workbench ever read it, so it was written
 * into every saved document for nothing; it is not carried over.
 */
class AssemblyGuiExport ViewProviderSimulation: public Gui::ViewProviderDocumentObject,
                                                public Gui::ViewProviderGroupExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(AssemblyGui::ViewProviderSimulation);

public:
    ViewProviderSimulation();
    ~ViewProviderSimulation() override;

    QIcon getIcon() const override;

    bool doubleClicked() override;

    /// A motion belongs to the simulation it was made in: it is not dragged elsewhere.
    bool canDragObjects() const override
    {
        return false;
    }
    bool canDropObjects() const override
    {
        return false;
    }
    bool canDragAndDropObject(App::DocumentObject*) const override
    {
        return false;
    }
};

}  // namespace AssemblyGui
