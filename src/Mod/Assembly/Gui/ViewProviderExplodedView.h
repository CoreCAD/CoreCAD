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
 * The view provider of an Assembly::ExplodedView.
 *
 * The typed replacement for the former Python ViewProviderExplodedView proxy. Most
 * of what that class held was boilerplate a Python view provider is obliged to
 * write: an empty display mode holding nothing, dumps/loads returning None, and a
 * claimChildren that repeated what a group already knows. The group extension
 * supplies the children and the deletion of them; the empty scene graph is simply
 * gone.
 *
 * Editing still opens the (Python) task panel, the same hop AssemblyGui already
 * makes for a bill of materials; it goes away when the panels are ported.
 */
class AssemblyGuiExport ViewProviderExplodedView: public Gui::ViewProviderDocumentObject,
                                                  public Gui::ViewProviderGroupExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(AssemblyGui::ViewProviderExplodedView);

public:
    ViewProviderExplodedView();
    ~ViewProviderExplodedView() override;

    QIcon getIcon() const override;

    bool doubleClicked() override;

    /// A move belongs to the view it was made in: it is not dragged elsewhere.
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
