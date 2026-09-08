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

#include <cstring>

#include <App/Document.h>
#include <App/PropertyGeo.h>
#include <Base/Placement.h>

#include "AssemblyUtils.h"
#include "ExplodedViewStep.h"
#include "ExplodedViewStepPy.h"

using namespace Assembly;


const char* ExplodedViewStep::MoveTypeEnums[] = {"Normal", "Radial", nullptr};

PROPERTY_SOURCE(Assembly::ExplodedViewStep, App::DocumentObject)

ExplodedViewStep::ExplodedViewStep()
{
    ADD_PROPERTY_TYPE(
        References,
        (nullptr),
        "Exploded Move",
        App::Prop_None,
        "The objects moved by the move"
    );
    ADD_PROPERTY_TYPE(
        MovementTransform,
        (Base::Placement()),
        "Exploded Move",
        App::Prop_None,
        "This is the movement of the move. The end placement is the result of the start "
        "placement * this placement."
    );
    ADD_PROPERTY_TYPE(MoveType, (0L), "Exploded Move", App::Prop_None, "The type of the move");
    MoveType.setEnums(MoveTypeEnums);
}

ExplodedViewStep::~ExplodedViewStep() = default;

App::DocumentObjectExecReturn* ExplodedViewStep::execute()
{
    // A move holds no geometry of its own: it is a displacement applied on demand by
    // the exploded view that owns it. Nothing to recompute.
    return App::DocumentObject::StdReturn;
}

std::vector<App::DocumentObject*> ExplodedViewStep::movedComponents() const
{
    if (!isRefValid(&References, 1)) {
        return {};
    }

    App::DocumentObject* owner = References.getValue();
    std::vector<App::DocumentObject*> components;

    for (const std::string& sub : References.getSubValues()) {
        if (auto* obj = getObjFromRef(owner, sub)) {
            components.push_back(obj);
        }
    }

    return components;
}

std::vector<ExplosionLine> ExplodedViewStep::applyStep(const Base::Vector3d& com, double size)
{
    std::vector<ExplosionLine> lines;

    const std::vector<App::DocumentObject*> components = movedComponents();
    if (components.empty()) {
        return lines;
    }

    // A radial move pushes each component away from the assembly centre by an amount
    // scaled to the assembly's overall size, so the same stored displacement reads the
    // same on a small part and a large one.
    double factor = 1.0;
    const bool radial = std::strcmp(MoveType.getValueAsString(), "Radial") == 0;
    if (radial) {
        if (size <= 0.0) {
            return lines;
        }
        factor = 4.0 * MovementTransform.getValue().getPosition().Length() / size;
    }

    for (auto* obj : components) {
        auto* placementProp = obj->getPropertyByName<App::PropertyPlacement>("Placement");
        if (!placementProp) {
            continue;
        }

        const Base::Vector3d startPos = getGlobalBoundBoxCenter(obj);
        const Base::Placement current = placementProp->getValue();

        if (radial) {
            const Base::Vector3d outward = startPos - com;
            Base::Placement moved = current;
            moved.setPosition(current.getPosition() + outward * factor);
            placementProp->setValue(moved);
        }
        else {
            placementProp->setValue(MovementTransform.getValue() * current);
        }

        lines.emplace_back(startPos, getGlobalBoundBoxCenter(obj));

        // The move is a view of the assembly, not an edit of it: a component whose
        // placement was displaced for the explosion must not be left marked as changed.
        obj->purgeTouched();
    }

    return lines;
}

PyObject* ExplodedViewStep::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new ExplodedViewStepPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
