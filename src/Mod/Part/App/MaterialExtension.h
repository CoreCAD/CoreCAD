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

#include <App/DocumentObjectExtension.h>
#include <App/ExtensionPython.h>

#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Part/PartGlobal.h>

namespace App
{
class DocumentObject;
}

namespace Part
{

/**
 * @brief The opt-in "is made of something" capability (#121).
 *
 * What a part is made of and how it is drawn were one property
 * (`Part::ShapeFeature::ShapeMaterial`) on the base every shape-carrying object
 * inherits, so a sketch and every intermediate feature nobody ever sees were
 * made of steel, and a document held one copy of the same material per object.
 * The two halves belong to different things: density, stiffness and cost are
 * facts about a *part* — only something standing as a solid in its own right can
 * answer them — while ambient, diffuse and specular colour are authored view
 * state, which anything visible carries, down to a single face.
 *
 * This is the same peel Amendment 4 performed for position and Amendment 17 for
 * geometry, applied to substance: being made of something is a capability an
 * object *composes and answers* (Clause 17.1), not a rung it inherits. A Body
 * composes it because a Body is what stands as a part; a standalone Part feature
 * composes it because it stands on its own until it spawns one (§4.6); a
 * PartDesign feature inside a Body composes nothing — the Body it belongs to is
 * what the steel is.
 *
 * The appearance half is not here and is not on the object at all: it is the
 * view provider's authored `ShapeAppearance`.
 */
class PartExport MaterialExtension: public App::DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::MaterialExtension);
    using inherited = App::DocumentObjectExtension;

public:
    MaterialExtension();
    ~MaterialExtension() override;

    /// What the part this object stands for is made of. A library value carried
    /// in full (Amendment 18 Clause 18.3), not a reference to one.
    Materials::PropertyMaterial Material;
};

using MaterialExtensionPython = App::ExtensionPythonT<MaterialExtension>;

/// Capability check: does this object stand as a part that can be made of something?
PartExport bool hasMaterial(const App::DocumentObject* obj);

/// Capability read, writable: the object's own material property, or nullptr when it
/// carries no material capability.
PartExport Materials::PropertyMaterial* materialPropertyOf(App::DocumentObject* obj);

/// Inherit what the source is made of: a cut of a steel block is steel. Applies only
/// while the target is still carrying the default material, so it never overwrites a
/// material a person chose, and does nothing when either side carries no material.
PartExport void inheritMaterial(App::DocumentObject* target, const App::DocumentObject* source);

/// The material of the part the object belongs to: its own when it stands as a part,
/// otherwise the material of the single part that is built from it — a Body for one of
/// its features, since a feature inside a Body is not made of anything itself. Returns
/// nullptr when no part in reach carries a material.
PartExport const Materials::Material* materialOfPart(const App::DocumentObject* obj);

}  // namespace Part
