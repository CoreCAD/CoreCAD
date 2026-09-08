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

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Precision.hxx>
#include <gp_Pnt.hxx>

#include <cstring>

#include <App/Document.h>
#include <App/FeaturePythonPyImp.h>
#include <Base/Placement.h>

#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShapePy.h>

#include "AssemblyObject.h"
#include "AssemblyUtils.h"
#include "ExplodedView.h"
#include "ExplodedViewPy.h"

using namespace Assembly;

namespace
{
/// An explosion line between two points, or a null shape when the component did
/// not actually travel (a zero-length edge is not a line).
Part::TopoShape makeExplosionLine(const Base::Vector3d& start, const Base::Vector3d& end)
{
    if ((start - end).Length() <= Precision::Confusion()) {
        return {};
    }

    const gp_Pnt p1(start.x, start.y, start.z);
    const gp_Pnt p2(end.x, end.y, end.z);

    return Part::TopoShape(BRepBuilderAPI_MakeEdge(p1, p2).Shape());
}
}  // namespace


PROPERTY_SOURCE_WITH_EXTENSIONS(Assembly::ExplodedView, App::DocumentObject)

ExplodedView::ExplodedView()
{
    App::GroupExtension::initExtension(this);
}

ExplodedView::~ExplodedView() = default;

App::DocumentObjectExecReturn* ExplodedView::execute()
{
    // An exploded view produces no geometry of its own into the document: it is a
    // way of looking at the assembly, applied on demand. Nothing to recompute.
    return App::DocumentObject::StdReturn;
}

AssemblyObject* ExplodedView::getAssembly() const
{
    // An exploded view is not held by the assembly directly: it sits inside the
    // assembly's ViewGroup. The former Python version looked only one level up and so
    // never found the assembly at all -- which left a double-click on an exploded
    // view doing nothing, and a temporary explosion raising. Walk up through whatever
    // containers stand between, with a depth bound so a cycle cannot hang the search.
    constexpr int maxDepth = 8;

    const App::DocumentObject* current = this;
    for (int depth = 0; depth < maxDepth && current; ++depth) {
        const App::DocumentObject* parent = nullptr;

        for (auto* obj : current->getInList()) {
            if (auto* assembly = freecad_cast<AssemblyObject*>(obj)) {
                return assembly;
            }
            if (!parent && obj && obj->hasExtension(App::GroupExtension::getExtensionClassTypeId())) {
                parent = obj;
            }
        }

        current = parent;
    }

    return nullptr;
}

std::vector<ExplodedViewStep*> ExplodedView::getSteps() const
{
    std::vector<ExplodedViewStep*> steps;
    for (auto* obj : Group.getValues()) {
        if (auto* step = freecad_cast<ExplodedViewStep*>(obj)) {
            steps.push_back(step);
        }
    }
    return steps;
}

std::vector<ExplosionLine> ExplodedView::applyMoves()
{
    const auto [com, size] = getComAndSize(getAssembly());
    return applyMoves(com, size);
}

std::vector<ExplosionLine> ExplodedView::applyMoves(const Base::Vector3d& com, double size)
{
    std::vector<ExplosionLine> lines;

    for (auto* step : getSteps()) {
        const std::vector<ExplosionLine> stepLines = step->applyStep(com, size);
        lines.insert(lines.end(), stepLines.begin(), stepLines.end());
    }

    return lines;
}

void ExplodedView::explodeTemporarily()
{
    auto* assembly = getAssembly();
    if (!assembly) {
        return;
    }

    // Only snapshot on the way in. Exploding an already-exploded assembly a second
    // time must still restore to where the model actually was.
    if (initialPlacements.empty()) {
        for (auto* component : getAssemblyComponents(assembly)) {
            const auto* plc = component
                ? component->getPropertyByName<App::PropertyPlacement>("Placement")
                : nullptr;
            if (plc) {
                initialPlacements[component->getNameInDocument()] = plc->getValue();
            }
        }
    }

    applyMoves();

    for (auto* step : getSteps()) {
        step->Visibility.setValue(true);
    }
}

void ExplodedView::restoreAssembly()
{
    auto* assembly = getAssembly();
    if (!assembly || initialPlacements.empty()) {
        return;
    }

    for (auto* component : getAssemblyComponents(assembly)) {
        if (!component) {
            continue;
        }

        const auto found = initialPlacements.find(component->getNameInDocument());
        if (found == initialPlacements.end()) {
            continue;
        }

        auto* plc = component->getPropertyByName<App::PropertyPlacement>("Placement");
        if (plc) {
            plc->setValue(found->second);
            component->purgeTouched();
        }
    }

    initialPlacements.clear();

    for (auto* step : getSteps()) {
        step->Visibility.setValue(false);
    }
}

Part::TopoShape ExplodedView::saveAssemblyAndExplode()
{
    auto* assembly = getAssembly();
    if (!assembly) {
        return {};
    }

    if (initialPlacements.empty()) {
        for (auto* component : getAssemblyComponents(assembly)) {
            const auto* plc = component
                ? component->getPropertyByName<App::PropertyPlacement>("Placement")
                : nullptr;
            if (plc) {
                initialPlacements[component->getNameInDocument()] = plc->getValue();
            }
        }
    }

    std::vector<Part::TopoShape> lines;
    for (const auto& [start, end] : applyMoves()) {
        Part::TopoShape line = makeExplosionLine(start, end);
        if (!line.isNull()) {
            lines.push_back(line);
        }
    }

    if (lines.empty()) {
        return {};
    }

    Part::TopoShape compound;
    compound.makeElementCompound(lines);
    return compound;
}

Part::TopoShape ExplodedView::getExplodedShape() const
{
    auto* assembly = getAssembly();
    if (!assembly) {
        return {};
    }

    const auto [com, size] = getComAndSize(assembly);

    // Work out where each component would end up WITHOUT moving anything: the
    // running map carries each component's placement forward through the moves, so a
    // component displaced twice accumulates both displacements exactly as it would
    // if the moves were applied to the document in turn.
    std::map<const App::DocumentObject*, Base::Placement> explodedPlacements;
    std::vector<ExplosionLine> lines;

    for (auto* step : getSteps()) {
        const bool radial = std::strcmp(step->MoveType.getValueAsString(), "Radial") == 0;
        double factor = 1.0;
        if (radial) {
            if (size <= 0.0) {
                continue;
            }
            factor = 4.0 * step->MovementTransform.getValue().getPosition().Length() / size;
        }

        for (auto* obj : step->movedComponents()) {
            const auto* plcProp = obj->getPropertyByName<App::PropertyPlacement>("Placement");
            if (!plcProp) {
                continue;
            }

            const auto known = explodedPlacements.find(obj);
            const Base::Placement current = known == explodedPlacements.end() ? plcProp->getValue()
                                                                              : known->second;

            const Base::Vector3d startPos = getGlobalBoundBoxCenter(obj);

            Base::Placement moved = current;
            if (radial) {
                moved.setPosition(current.getPosition() + (startPos - com) * factor);
            }
            else {
                moved = step->MovementTransform.getValue() * current;
            }

            explodedPlacements[obj] = moved;

            // The line runs from where the component sits to where the same point
            // lands under the displacement that was just worked out.
            const Base::Placement delta = moved * current.inverse();
            Base::Vector3d endPos;
            delta.multVec(startPos, endPos);
            lines.emplace_back(startPos, endPos);
        }
    }

    std::vector<Part::TopoShape> shapes;

    for (auto* component : getAssemblyComponents(assembly)) {
        if (!component || !component->Visibility.getValue()) {
            continue;
        }

        Part::TopoShape shape = Part::Feature::getTopoShape(
            component,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (shape.isNull()) {
            continue;
        }

        const auto placed = explodedPlacements.find(component);
        if (placed != explodedPlacements.end()) {
            const auto* plcProp = component->getPropertyByName<App::PropertyPlacement>("Placement");
            if (plcProp) {
                // The shape already carries the component's current placement, so
                // apply only the difference between current and exploded.
                const Base::Placement delta = placed->second * plcProp->getValue().inverse();
                shape.move(Part::TopoShape::convert(delta.toMatrix()));
            }
        }

        shapes.push_back(shape);
    }

    for (const auto& [start, end] : lines) {
        Part::TopoShape line = makeExplosionLine(start, end);
        if (!line.isNull()) {
            shapes.push_back(line);
        }
    }

    if (shapes.empty()) {
        return {};
    }

    Part::TopoShape compound;
    compound.makeElementCompound(shapes);
    return compound;
}

App::DocumentObject* ExplodedView::getSubObject(
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* pmat,
    bool transform,
    int depth
) const
{
    while (subname && *subname == '.') {
        ++subname;  // skip leading .
    }

    // An exploded view owns children (its moves), so a dotted subname is a reference
    // into the group rather than a sub-element of a shape.
    if (subname && *subname && strchr(subname, '.')) {
        return App::DocumentObject::getSubObject(subname, pyObj, pmat, transform, depth);
    }

    if (!pyObj) {
        return const_cast<ExplodedView*>(this);
    }

    // The shape is computed, never stored: asking for it must not disturb the model.
    // The components already come back in the world frame, so no placement of this
    // object's own is folded in.
    Part::TopoShape shape = getExplodedShape();
    if (shape.isNull()) {
        return App::DocumentObject::getSubObject(subname, pyObj, pmat, transform, depth);
    }

    if (subname && *subname) {
        shape = shape.getSubTopoShape(subname, true);
        if (shape.isNull()) {
            return nullptr;
        }
    }

    *pyObj = new Part::TopoShapePy(new Part::TopoShape(shape));
    return const_cast<ExplodedView*>(this);
}

PyObject* ExplodedView::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new ExplodedViewPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
