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

#include <memory>
#include <string>
#include <vector>

#include <Mod/Assembly/AssemblyGlobal.h>

#include <Base/Placement.h>

#include <Gui/Selection/SoFCSelection.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/ViewProviderSuppressibleExtension.h>


namespace AssemblyGui
{

class JcsMarker;

/**
 * The view provider of an Assembly::Joint: it draws the two joint frames where the
 * joint meets its components, plus a third preview frame while one is being placed.
 *
 * The typed replacement for the former Python ViewProviderJoint proxy. Two things
 * needed that proxy gone: the App layer had to reach a joint's drawing through
 * ViewObject.Proxy at runtime whenever the solver moved something, and the
 * assembly's own 3D double-click had to build a line of Python to open a joint for
 * editing. Both are direct calls now.
 *
 * A frame's world position is the component's global placement combined with the
 * joint's own stored placement, so it has to be redrawn when the component moves --
 * a property change on the joint alone does not cover it.
 */
class AssemblyGuiExport ViewProviderJoint: public Gui::ViewProviderDocumentObject,
                                           public Gui::ViewProviderSuppressibleExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(AssemblyGui::ViewProviderJoint);

public:
    ViewProviderJoint();
    ~ViewProviderJoint() override;

    void attach(App::DocumentObject* obj) override;
    void setDisplayMode(const char* ModeName) override;
    std::vector<std::string> getDisplayModes() const override;

    void updateData(const App::Property* prop) override;

    QIcon getIcon() const override;
    QIcon mergeColorfulOverlayIcons(const QIcon& orig) const override;

    bool doubleClicked() override;

    /// Redraw both joint frames from the joint's references and placements.
    void redrawMarkers();

    /// Show the preview frame at a placement expressed in a reference's own frame.
    void showPreviewJcs(
        const Base::Placement& placement,
        App::DocumentObject* refObj,
        const std::vector<std::string>& subs
    );
    void hidePreviewJcs();

    /// Whether the joint frames can be picked in the 3D view.
    void setPickableState(bool state);

    PyObject* getPyObject() override;

private:
    /// Place one frame from a reference and the joint placement stored against it.
    void redrawMarker(JcsMarker& marker, const char* placementName, const char* referenceName);

    Gui::SoFCSelection* pcSelectionRoot;
    std::unique_ptr<JcsMarker> jcs1;
    std::unique_ptr<JcsMarker> jcs2;
    std::unique_ptr<JcsMarker> jcsPreview;
};

}  // namespace AssemblyGui
