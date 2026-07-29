// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Cruth (CoreCAD fork of FreeCAD)                     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef PARTDESIGNGUI_SKETCHPICKDIALOG_H
#define PARTDESIGNGUI_SKETCHPICKDIALOG_H

#include <vector>

namespace Part
{
class Part2DObject;
}

namespace PartDesign
{
class Body;
}

namespace PartDesignGui
{

/// Cruth §8.5: a synchronous modal chooser for "which sketch?" when a profile
/// command finds several candidate sketches and the user selected none.
///
/// This replaces the old active-body picker (TaskFeaturePick) for the de-owned
/// model: rather than gatekeeping sketches by which body/part owns them, it simply
/// lists every candidate by name and highlights the current one in the 3D view. It
/// returns the chosen sketch synchronously so it drops straight into
/// resolveSketchFromSelection's slot (the destination Body is decided downstream,
/// unchanged — the picker only picks).
///
/// @returns the sketch the user picked, or nullptr if they cancelled.
Part::Part2DObject* pickSketch(const std::vector<Part::Part2DObject*>& candidates);

/// Cruth §8.5: the same synchronous modal chooser for "which body?" — the target of a
/// combinator (subtractive primitive, Boolean) when several bodies exist and the user
/// selected none. A combinator is told its target explicitly and never silently reads an
/// active body (§4.6); this is the twin of pickSketch, so both pickers share one dialog.
///
/// @returns the body the user picked, or nullptr if they cancelled.
PartDesign::Body* pickBody(const std::vector<PartDesign::Body*>& candidates);

}  // namespace PartDesignGui

#endif  // PARTDESIGNGUI_SKETCHPICKDIALOG_H
