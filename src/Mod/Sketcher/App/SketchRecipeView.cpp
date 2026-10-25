// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
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
# include <algorithm>
# include <cctype>
# include <cstdlib>
# include <iomanip>
# include <limits>
# include <locale>
# include <map>
# include <memory>
# include <numeric>
# include <set>
# include <sstream>

# include <boost/uuid/uuid.hpp>
# include <boost/uuid/uuid_io.hpp>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <App/ElementNamingUtils.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyLinks.h>
#include <App/RecipeDetail.h>
#include <Mod/Part/App/Geometry.h>

#include "SketchRecipe.h"
#include "SketchRecipeInternal.h"

#include "Constraint.h"
#include "ExternalGeometryFacade.h"
#include "GeoEnum.h"
#include "GeometryFacade.h"
#include "SolverGeometryExtension.h"

using namespace Sketcher;
using namespace Sketcher::SketchRecipeInternal;

namespace
{

/// The name of a sketch element that carries no durable tag of its own: the sketch's own axes
/// and root point, and an edge borrowed from another object. The merge cannot bind any of these
/// -- they are not entities of this sketch -- and so drops them, which left the view saying
/// "coincident" without ever saying with what. A person reading the file needs the other half
/// of the sentence, so the view names them. Returns empty for an element the sketch cannot
/// account for at all.
/// The readable name of every borrowed element, keyed by the reference the sketch stamps on the
/// projected curve. That stored reference carries the shape's mapped element name -- a hash no
/// one can read, which says nothing about what was picked -- while the link the person actually
/// made still holds "Pad" and "Edge1". Both are built from the same two halves, so the readable
/// one can be found by the other.
std::map<std::string, std::string> readableBorrowedNames(const SketchObject& sketch)
{
    std::map<std::string, std::string> names;

    const std::vector<App::DocumentObject*>& objects = sketch.ExternalGeometry.getValues();
    const std::vector<std::string>& subs = sketch.ExternalGeometry.getSubValues();
    const std::vector<App::PropertyLinkBase::ShadowSub>& shadows
        = sketch.ExternalGeometry.getShadowSubs();

    for (std::size_t i = 0; i < objects.size() && i < subs.size(); ++i) {
        if (objects[i] == nullptr || objects[i]->getNameInDocument() == nullptr) {
            continue;
        }
        const std::string owner = std::string(objects[i]->getNameInDocument()) + ".";
        const bool mapped = i < shadows.size() && !shadows[i].newName.empty();
        const std::string& sub = mapped ? shadows[i].newName : subs[i];

        // The same two halves the sketch itself puts together when it stamps a reference on the
        // projected curve, so the key here is the key there.
        const std::string plain = Data::oldElementName(subs[i].c_str());
        names[owner + Data::newElementName(sub.c_str())] = owner + (plain.empty() ? subs[i] : plain);
    }

    return names;
}

std::string borrowedElementName(
    const SketchObject& sketch,
    int geoId,
    const std::map<std::string, std::string>& readable
)
{
    if (geoId == GeoEnum::HAxis) {
        return "H_Axis";
    }
    if (geoId == GeoEnum::VAxis) {
        return "V_Axis";
    }
    // A negative GeoId is a position in the borrowed list, counting back from -1; anything
    // past its end is an unused constraint slot (a constraint carries a fixed number of
    // element places and leaves the spare ones undefined), which names nothing.
    const std::vector<Part::Geometry*>& borrowed = sketch.getExternalGeometry();
    const int index = -geoId - 1;
    if (index < 0 || index >= static_cast<int>(borrowed.size())) {
        return {};
    }

    const std::string reference = ExternalGeometryFacade::getFacade(borrowed[index])->getRef();
    const auto found = readable.find(reference);
    if (found != readable.end()) {
        return found->second;
    }
    // A reference the link list no longer accounts for: say so rather than print the hash.
    return reference.empty() ? std::string("external") : std::string("external (unresolved)");
}

/// The sketch's origin, addressed as the start point of the horizontal axis. The two share a
/// GeoId, so only the point selector tells them apart, and "RootPoint:1" would say the same
/// thing twice.
bool isRootPoint(int geoId, int pos)
{
    return geoId == GeoEnum::RtPnt && pos != static_cast<int>(PointPos::none);
}

}  // namespace

App::RecipeDetail Sketcher::sketchRecipeDetail(const App::DocumentObject& obj)
{
    App::RecipeDetail detail;

    const auto* sketch = dynamic_cast<const SketchObject*>(&obj);
    if (sketch == nullptr) {
        return detail;
    }

    // The two properties the generic emitter can only see as opaque values. Claimed here, so
    // the view stops reporting them missing now that it prints what is in them.
    detail.coveredProperties = {"Geometry", "Constraints"};

    const SketchRecipe recipe = emitSketchRecipe(*sketch);

    // Walked in the sketch's own order rather than the recipe's id order: the merge is keyed by
    // durable tag and indifferent to sequence, but a person reads a sketch in the order it was
    // drawn.
    App::RecipeDetailSection geometry;
    geometry.name = "geometry";
    const std::vector<Part::Geometry*>& internals = sketch->getInternalGeometry();
    for (const Part::Geometry* geo : internals) {
        const auto found = recipe.geometry.find(tagToString(geo->getTag()));
        if (found == recipe.geometry.end()) {
            continue;
        }
        App::RecipeNode node = found->second;
        node.type = readableGeometryType(node.type);
        // A flag left at its default is not something anyone did. The merge carries it on every
        // entity because it needs a value to rebuild with; the view prints it only where it
        // departs from ordinary geometry, the same way a constraint prints "driving" only when
        // the answer is no. Repeated once per line, an untouched flag would outnumber the
        // authored content of the sketch.
        const auto construction = node.fields.find("construction");
        if (construction != node.fields.end() && construction->second == "false") {
            node.fields.erase(construction);
        }
        addAuthoredCoordinates(geo, node);
        geometry.nodes.push_back(std::move(node));
    }

    // What the sketch borrows from elsewhere in the document. The link itself is already an
    // object-level reference, but that only names the object; which edge or face was picked is
    // the authored fact, and it lived nowhere in the view until now.
    App::RecipeDetailSection external;
    external.name = "external";
    const std::map<std::string, std::string> readable = readableBorrowedNames(*sketch);
    for (const Part::Geometry* geo : sketch->getExternalGeometry()) {
        const auto facade = ExternalGeometryFacade::getFacade(geo);
        if (facade->getRef().empty()) {
            continue;  // the sketch's own two axes, which are not borrowed from anywhere
        }
        App::RecipeNode node;
        const auto found = readable.find(facade->getRef());
        node.id = found != readable.end() ? found->second : facade->getRef();
        node.type = readableGeometryType(geo->getTypeId().getName());
        for (int flag = 0; flag < ExternalGeometryExtension::NumFlags; ++flag) {
            if (facade->testFlag(flag)) {
                // A reference the sketch has frozen, detached or lost is a state someone put it
                // in (or a breakage they need to see), never the ordinary case.
                node.fields[ExternalGeometryExtension::flag2str.at(flag)] = "true";
            }
        }
        external.nodes.push_back(std::move(node));
    }

    // The GeoId a constraint stores is a position in the sketch's own geometry list, so the
    // view needs the same index the merge built to turn one back into a durable tag.
    std::map<int, std::string> geoIdToTag;
    for (int geoId = 0; geoId < static_cast<int>(internals.size()); ++geoId) {
        geoIdToTag[geoId] = tagToString(internals[geoId]->getTag());
    }

    App::RecipeDetailSection constraints;
    constraints.name = "constraints";
    for (const Constraint* constraint : sketch->Constraints.getValues()) {
        const auto found = recipe.constraints.find(tagToString(constraint->getTag()));
        if (found == recipe.constraints.end()) {
            continue;
        }
        App::RecipeNode node = found->second;
        if (!constraint->Name.empty()) {
            // A named constraint is the handle an expression binds to, so the name is authored
            // content and a rename is a real change to the model.
            node.fields["name"] = constraint->Name;
        }
        if (!constraint->isDriving) {
            node.fields["driving"] = "false";
        }

        // Rebuilt rather than inherited: the merge keeps only what it can bind by durable tag,
        // which silently dropped every reference to an axis, to the origin, or to a borrowed
        // edge -- so "point on object" arrived naming one side of a two-sided fact.
        node.refs.clear();
        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int element = static_cast<int>(i);
            const int geoId = constraint->getGeoId(element);
            const int pos = constraint->getPosIdAsInt(element);

            App::RecipeRef ref;
            const auto own = geoIdToTag.find(geoId);
            if (own != geoIdToTag.end()) {
                ref.target = own->second;
                ref.pos = pos;
            }
            else if (isRootPoint(geoId, pos)) {
                ref.target = "RootPoint";
            }
            else {
                ref.target = borrowedElementName(*sketch, geoId, readable);
                if (ref.target.empty()) {
                    continue;  // an element the sketch itself cannot account for
                }
                ref.pos = geoId <= GeoEnum::RefExt ? pos : 0;
            }
            node.refs.push_back(ref);
        }

        constraints.nodes.push_back(std::move(node));
    }

    detail.sections.push_back(std::move(geometry));
    if (!external.nodes.empty()) {
        detail.sections.push_back(std::move(external));
    }
    detail.sections.push_back(std::move(constraints));

    return detail;
}

namespace
{

/// The geometry named by a constraint that was added, removed or changed, on either side.
std::set<std::string> geometryOfChangedConstraints(
    const SketchObject& oldSketch,
    const SketchObject& newSketch
)
{
    const SketchRecipe was = emitSketchRecipe(oldSketch);
    const SketchRecipe now = emitSketchRecipe(newSketch);
    std::set<std::string> touched;
    for (const App::RecipeChange& change : App::RecipeDiff::twoWay(was.constraints, now.constraints)) {
        for (const App::RecipeSection* side : {&was.constraints, &now.constraints}) {
            const auto found = side->find(change.id);
            if (found == side->end()) {
                continue;
            }
            for (const App::RecipeRef& ref : found->second.refs) {
                touched.insert(ref.target);
            }
        }
    }
    return touched;
}

/// For each of the sketch's own geometry, by GeoId, one GeoId standing for everything its
/// constraints tie it to.
std::vector<int> connectedGroups(const SketchObject& sketch)
{
    std::vector<int> group(sketch.getInternalGeometry().size());
    std::iota(group.begin(), group.end(), 0);
    const auto root = [&group](int geoId) {
        while (group[geoId] != geoId) {
            group[geoId] = group[group[geoId]];
            geoId = group[geoId];
        }
        return geoId;
    };

    for (const Constraint* constraint : sketch.Constraints.getValues()) {
        int first = -1;
        for (size_t i = 0; i < constraint->getElementsSize(); ++i) {
            const int geoId = constraint->getGeoId(static_cast<int>(i));
            if (geoId < 0 || geoId >= static_cast<int>(group.size())) {
                continue;  // an axis or borrowed edge holds things in place; it joins nothing
            }
            if (first < 0) {
                first = geoId;
            }
            else {
                group[root(geoId)] = root(first);
            }
        }
    }

    for (int geoId = 0; geoId < static_cast<int>(group.size()); ++geoId) {
        group[geoId] = root(geoId);
    }
    return group;
}

}  // namespace

App::RecipeConsequences Sketcher::sketchConsequences(
    const App::DocumentObject& before,
    const App::DocumentObject& after
)
{
    App::RecipeConsequences consequences;

    const auto* oldSketch = dynamic_cast<const SketchObject*>(&before);
    const auto* newSketch = dynamic_cast<const SketchObject*>(&after);
    if (oldSketch == nullptr || newSketch == nullptr) {
        return consequences;
    }

    std::map<std::string, const Part::Geometry*> oldByTag;
    for (const Part::Geometry* geo : oldSketch->getInternalGeometry()) {
        oldByTag[tagToString(geo->getTag())] = geo;
    }

    // Geometry tied to a changed constraint went where the solver put it, so its moves follow
    // from that change. Replaying the solve to tell a drag from a solver move cannot work: on
    // geometry the constraints do not fix, the solver lands in a different place from run to run.
    const std::set<std::string> touched = geometryOfChangedConstraints(*oldSketch, *newSketch);
    const std::vector<int> group = connectedGroups(*newSketch);
    const std::vector<Part::Geometry*>& internals = newSketch->getInternalGeometry();
    std::set<int> resolved;
    for (size_t i = 0; i < internals.size(); ++i) {
        if (touched.count(tagToString(internals[i]->getTag())) != 0) {
            resolved.insert(group[i]);
        }
    }

    for (size_t i = 0; i < internals.size(); ++i) {
        const std::string tag = tagToString(internals[i]->getTag());
        const auto found = oldByTag.find(tag);
        if (resolved.count(group[i]) == 0 || found == oldByTag.end()) {
            continue;
        }
        App::RecipeNode was;
        App::RecipeNode now;
        addAuthoredCoordinates(found->second, was);
        addAuthoredCoordinates(internals[i], now);
        for (const App::RecipeNode* side : {&was, &now}) {
            for (const auto& entry : side->fields) {
                consequences.emplace(tag, entry.first);
            }
        }
    }

    return consequences;
}
