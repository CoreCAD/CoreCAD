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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <deque>
# include <unordered_set>
#endif

#include "MaterialExtension.h"

#include <App/DocumentObject.h>
#include <Mod/Material/App/MaterialManager.h>

using namespace Part;

EXTENSION_PROPERTY_SOURCE(Part::MaterialExtension, App::DocumentObjectExtension)
EXTENSION_PROPERTY_SOURCE_TEMPLATE(Part::MaterialExtensionPython, Part::MaterialExtension)

MaterialExtension::MaterialExtension()
{
    initExtensionType(MaterialExtension::getExtensionClassTypeId());

    auto mat = Materials::MaterialManager::defaultMaterial();
    EXTENSION_ADD_PROPERTY(Material, (*mat));
}

MaterialExtension::~MaterialExtension() = default;

bool Part::hasMaterial(const App::DocumentObject* obj)
{
    return obj && obj->hasExtension(MaterialExtension::getExtensionClassTypeId());
}

Materials::PropertyMaterial* Part::materialPropertyOf(App::DocumentObject* obj)
{
    if (!obj) {
        return nullptr;
    }
    auto* ext = obj->getExtensionByType<MaterialExtension>(true);
    return ext ? &ext->Material : nullptr;
}

void Part::inheritMaterial(App::DocumentObject* target, const App::DocumentObject* source)
{
    Materials::PropertyMaterial* prop = materialPropertyOf(target);
    if (!prop) {
        return;
    }
    const Materials::Material* inherited = materialOfPart(source);
    if (!inherited) {
        return;
    }
    auto standard = Materials::MaterialManager::defaultMaterial();
    if (prop->getValue().getUUID() == standard->getUUID()
        && inherited->getUUID() != standard->getUUID()) {
        prop->setValue(*inherited);
    }
}

const Materials::Material* Part::materialOfPart(const App::DocumentObject* obj)
{
    if (!obj) {
        return nullptr;
    }
    if (auto* ext = obj->getExtensionByType<MaterialExtension>(true)) {
        return &ext->Material.getValue();
    }

    // A feature inside a Body is not made of anything itself: what it is made of is what the
    // part built from it is made of. Walk the objects that depend on this one -- the feature
    // chain up to its Body -- and take the first that stands as a part. The walk is over
    // dependants rather than a stored owner because membership is derived, never stored.
    std::unordered_set<const App::DocumentObject*> seen {obj};
    std::deque<const App::DocumentObject*> queue {obj};
    while (!queue.empty()) {
        const App::DocumentObject* current = queue.front();
        queue.pop_front();
        for (App::DocumentObject* dependant : current->getInList()) {
            if (!dependant || !seen.insert(dependant).second) {
                continue;
            }
            if (hasMaterial(dependant)) {
                auto* ext = dependant->getExtensionByType<MaterialExtension>(true);
                if (ext) {
                    return &ext->Material.getValue();
                }
            }
            queue.push_back(dependant);
        }
    }

    return nullptr;
}
