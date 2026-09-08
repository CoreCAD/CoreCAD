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
#include <Gui/Selection/SoFCSelection.h>

#include <Mod/Assembly/App/ExplodedViewStep.h>

class SoSeparator;

namespace AssemblyGui
{

/**
 * The view provider of an Assembly::ExplodedViewStep: it draws the dashed lines
 * the moved components travelled along.
 *
 * The typed replacement for the former Python ViewProviderExplodedViewStep proxy.
 * The lines are a preview of a move being edited, so they are handed over rather
 * than computed here -- applying a move displaces the components, which a view
 * provider must never do on its own. The task panel calls redrawLines with what
 * ExplodedViewStep::applyStep reported.
 */
class AssemblyGuiExport ViewProviderExplodedViewStep: public Gui::ViewProviderDocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(AssemblyGui::ViewProviderExplodedViewStep);

public:
    ViewProviderExplodedViewStep();
    ~ViewProviderExplodedViewStep() override;

    void attach(App::DocumentObject* obj) override;
    void setDisplayMode(const char* ModeName) override;
    std::vector<std::string> getDisplayModes() const override;

    QIcon getIcon() const override;

    /// Draw one dashed line per component moved, replacing whatever was drawn before.
    void redrawLines(const std::vector<Assembly::ExplosionLine>& lines);

    PyObject* getPyObject() override;

private:
    Gui::SoFCSelection* pcSelectionRoot;
    SoSeparator* pcLineGroup;
};

}  // namespace AssemblyGui
