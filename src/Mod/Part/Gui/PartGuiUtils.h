// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 CoreCAD Contributors

/***************************************************************************
 * Shared utility helpers for Part GUI dialogs.                             *
 ***************************************************************************/

#pragma once

#include <App/GeoFeatureGroupExtension.h>
#include <App/Part.h>
#include <Gui/Application.h>
#include <Gui/MDIView.h>

namespace PartGui
{

/// If an active App::Part context exists and \a obj is not already inside a
/// group (Body, Part, etc.), move \a obj into that Part.  Call this after
/// creating a result object in any Part operation dialog.
inline void addToActivePart(App::DocumentObject* obj)
{
    if (!obj || App::GeoFeatureGroupExtension::getGroupOfObject(obj)) {
        return;
    }
    Gui::MDIView* view = Gui::Application::Instance->activeView();
    if (!view) {
        return;
    }
    auto* part = view->getActiveObject<App::Part*>("part");
    if (part) {
        part->addObject(obj);
    }
}

}  // namespace PartGui
