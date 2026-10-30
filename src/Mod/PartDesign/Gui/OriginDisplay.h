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

#ifndef PARTDESIGNGUI_ORIGINDISPLAY_H
#define PARTDESIGNGUI_ORIGINDISPLAY_H

#include <App/DocumentObserver.h>
#include <Gui/ViewProviderCoordinateSystem.h>

namespace App
{
class DocumentObject;
}

namespace PartDesignGui
{

/// Shows the document Origin's planes or axes for picking while a panel is open, and hides
/// them when it closes. It remembers the Origin it showed, so hiding never reads the feature,
/// which a Cancel or bare close has already rolled back and deleted (#131).
class OriginDisplay
{
public:
    OriginDisplay() = default;
    ~OriginDisplay();
    OriginDisplay(const OriginDisplay&) = delete;
    OriginDisplay& operator=(const OriginDisplay&) = delete;

    void show(const App::DocumentObject* feature, Gui::DatumElements elements);
    void hide();

private:
    App::DocumentObjectT shown;
};

}  // namespace PartDesignGui

#endif  // PARTDESIGNGUI_ORIGINDISPLAY_H
