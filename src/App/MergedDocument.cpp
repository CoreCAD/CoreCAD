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
# include <map>
# include <optional>
# include <set>
# include <sstream>
# include <string>
# include <vector>
#endif

#include "ObjectRecipe.h"

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "ElementNamingUtils.h"
#include "Expression.h"
#include "ObjectIdentifier.h"
#include "PropertyExpressionEngine.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "RecipeDetail.h"
#include "StoredRecipe.h"

using namespace App;

namespace
{

std::map<std::string, const DocumentObject*> byUid(const Document& doc)
{
    std::map<std::string, const DocumentObject*> found;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr) {
            found[obj->Uid.getValueStr()] = obj;
        }
    }
    return found;
}

DocumentObject* objectWithUid(const Document& doc, const std::string& id)
{
    for (DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr && obj->Uid.getValueStr() == id) {
            return obj;
        }
    }
    return nullptr;
}

const DocumentObject* find(const std::map<std::string, const DocumentObject*>& objects,
                           const std::string& id)
{
    const auto found = objects.find(id);
    return found != objects.end() ? found->second : nullptr;
}

/// The formulas driving a property, or a part of it, each by the path it drives.
std::map<std::string, std::string> formulasOn(const DocumentObject* obj, const std::string& name)
{
    std::map<std::string, std::string> formulas;
    const Property* prop = obj != nullptr ? obj->getPropertyByName(name.c_str()) : nullptr;
    if (prop == nullptr) {
        return formulas;
    }
    for (const auto& [path, expression] : obj->ExpressionEngine.getExpressions()) {
        if (expression != nullptr && path.getProperty() == prop) {
            formulas[path.toString()] = formulaStatedByUid(*expression);
        }
    }
    return formulas;
}

/// Whether one version of the object states this property differently from another: its value,
/// or a formula on it. A version that lacks the object states it all differently.
bool changed(const DocumentObject* side, const DocumentObject* other, const std::string& name)
{
    const Property* mine = side->getPropertyByName(name.c_str());
    const Property* theirs = other != nullptr ? other->getPropertyByName(name.c_str()) : nullptr;
    if (mine == nullptr || theirs == nullptr) {
        return mine != theirs;
    }
    return !mine->isSame(*theirs) || formulasOn(side, name) != formulasOn(other, name);
}

/// Give `into` the formulas `from` puts on the property, in place of its own.
void takeFormulas(DocumentObject& into, const DocumentObject& from, const std::string& name)
{
    for (const auto& [path, formula] : formulasOn(&into, name)) {
        into.setExpression(ObjectIdentifier::parse(&into, path), nullptr);
    }
    for (const auto& [path, formula] : formulasOn(&from, name)) {
        into.setExpression(ObjectIdentifier::parse(&into, path),
                           std::shared_ptr<Expression>(Expression::parse(&into, formula)));
    }
}

/// The names the stored form could carry for either version of the object.
std::set<std::string> carriedNames(const DocumentObject& a, const DocumentObject* b)
{
    std::set<std::string> names;
    for (const DocumentObject* version : {&a, b}) {
        if (version == nullptr) {
            continue;
        }
        std::map<std::string, Property*> properties;
        version->getPropertyMap(properties);
        for (const auto& [name, prop] : properties) {
            if (prop != nullptr && theRecipeCarries(*prop, *version)) {
                names.insert(name);
            }
        }
    }
    return names;
}

/// Point a link the merge combined from both versions at the merged references. Each reference
/// is stated as the version that holds it states it, by the element's full name, never by the
/// short form a name takes inside one document. Returns why it could not, or nothing.
std::optional<std::string> applyMergedLink(DocumentObject& into,
                                           const std::string& name,
                                           const RecipeNode& merged,
                                           const DocumentObject& objA,
                                           const DocumentObject& objB)
{
    auto* link = dynamic_cast<PropertyLinkBase*>(into.getPropertyByName(name.c_str()));
    if (link == nullptr) {
        return "combining both versions of " + name + " is not built yet";
    }

    struct Side
    {
        RecipeNode node;
        std::vector<PropertyLinkBase::Pointing> pointing;
    };
    std::vector<Side> sides;
    for (const DocumentObject* version : {&objA, &objB}) {
        Side side {emitObjectRecipe(*version), {}};
        const auto* theirs = dynamic_cast<const PropertyLinkBase*>(
            version->getPropertyByName(name.c_str()));
        if (theirs == nullptr || !theirs->statesWhereItPoints(side.pointing)) {
            return "combining both versions of " + name + " is not built yet";
        }
        sides.push_back(std::move(side));
    }

    std::vector<PropertyLinkBase::Pointing> pointing;
    for (const RecipeRef& ref : merged.refs) {
        if (ref.link != name) {
            continue;
        }
        const PropertyLinkBase::Pointing* stated = nullptr;
        for (const Side& side : sides) {
            if (std::find(side.node.refs.begin(), side.node.refs.end(), ref)
                == side.node.refs.end()) {
                continue;
            }
            for (const PropertyLinkBase::Pointing& one : side.pointing) {
                if (one.target != nullptr && one.target->Uid.getValueStr() == ref.target
                    && Data::oldElementName(one.sub.c_str()) == ref.elementLabel) {
                    stated = &one;
                    break;
                }
            }
            if (stated != nullptr) {
                break;
            }
        }
        DocumentObject* target = objectWithUid(*into.getDocument(), ref.target);
        if (stated == nullptr || target == nullptr) {
            return "a merged reference in " + name + " could not be stated";
        }
        pointing.push_back({target, stated->sub, stated->noPart, stated->name});
    }

    if (!link->pointAt(pointing)) {
        return "combining both versions of " + name + " is not built yet";
    }
    return std::nullopt;
}

/// The fields a node states for one property: the property's own, or one per entry of a map.
std::map<std::string, std::string> fieldsOf(const RecipeNode& node, const std::string& name)
{
    std::map<std::string, std::string> fields;
    for (const auto& [field, value] : node.fields) {
        if (field == name || field.rfind(name + ".", 0) == 0) {
            fields[field] = value;
        }
    }
    return fields;
}

/// Give the merged document the values the merge took for the document itself; it starts as A
/// states it. Returns why it could not, or nothing.
std::optional<std::string> takeDocumentValues(Document& into,
                                              const Document& branchA,
                                              const Document& branchB,
                                              const RecipeNode& merged)
{
    const RecipeNode stateA = emitDocumentValues(branchA, merged.id);
    const RecipeNode stateB = emitDocumentValues(branchB, merged.id);
    std::map<std::string, Property*> properties;
    into.getPropertyMap(properties);
    for (const auto& [name, prop] : properties) {
        const std::map<std::string, std::string> wanted = fieldsOf(merged, name);
        if (prop == nullptr || wanted == fieldsOf(stateA, name)) {
            continue;
        }
        const Property* fromB = branchB.getPropertyByName(name.c_str());
        if (fromB != nullptr && wanted == fieldsOf(stateB, name)) {
            prop->Paste(*fromB);
            continue;
        }
        if (auto* entries = dynamic_cast<PropertyMap*>(prop)) {
            std::map<std::string, std::string> values;
            for (const auto& [field, value] : wanted) {
                values[field.substr(name.size() + 1)] = value;
            }
            entries->setValues(values);
            continue;
        }
        return "combining both versions of the document's " + name + " is not built yet";
    }
    return std::nullopt;
}

/// The ids of the steps that fail when a version is rebuilt from its recipe, as the merged part is.
/// What a version shows in this session may predate its last edit, so it is not asked.
std::set<std::string> failingWhenRebuilt(const Document& version)
{
    DocumentInitFlags flags;
    flags.createView = false;
    flags.temporary = true;
    Document* copy = GetApplication().newDocument("Rebuilt", nullptr, flags);
    std::istringstream recipe(formatStoredRecipe(version));
    restoreStoredRecipe(*copy, recipe);
    copy->recompute();
    std::set<std::string> failing;
    for (const DocumentObject* obj : copy->getObjects()) {
        if (obj != nullptr && !obj->isValid()) {
            failing.insert(obj->Uid.getValueStr());
        }
    }
    GetApplication().closeDocument(copy->getName());
    return failing;
}

}  // namespace

MergedDocument App::buildMergedDocument(const Document& ancestor,
                                        const Document& branchA,
                                        const Document& branchB,
                                        bool createView)
{
    MergedDocument result;
    result.report = mergeDocuments(ancestor, branchA, branchB);
    if (!result.report.conflicts.empty()) {
        result.refusal = "conflicts are open";
        return result;
    }

    const auto objectsO = byUid(ancestor);
    const auto objectsA = byUid(branchA);
    const auto objectsB = byUid(branchB);

    // Each kept object is stated as A states it, with each value only B changed stated as B
    // does; an object only B holds is stated as B states it. A value both changed, which the
    // merge combined, is set afterwards from the merged result.
    RecipeScope scope;
    std::map<std::string, std::vector<std::string>> combined;
    for (const auto& [id, node] : result.report.merged) {
        const DocumentObject* objO = find(objectsO, id);
        const DocumentObject* objA = find(objectsA, id);
        const DocumentObject* objB = find(objectsB, id);
        if (objA == nullptr) {
            scope.objects.push_back(objB);
            continue;
        }
        scope.objects.push_back(objA);
        if (objB == nullptr) {
            continue;
        }
        for (const std::string& name : carriedNames(*objA, objB)) {
            if (name == "ExpressionEngine") {
                continue;  // Each formula is compared and taken with the value it drives.
            }
            const bool changedA = changed(objA, objO, name);
            const bool changedB = changed(objB, objO, name);
            if (changedB && !changedA) {
                scope.valuesFrom[objA][name] = objB;
            }
            else if (changedA && changedB && changed(objB, objA, name)) {
                combined[id].push_back(name);
            }
        }
    }
    std::erase(scope.objects, nullptr);

    const std::set<std::string> failingA = failingWhenRebuilt(branchA);
    const std::set<std::string> failingB = failingWhenRebuilt(branchB);

    DocumentInitFlags flags;
    flags.createView = createView;
    Document* merged = GetApplication().newDocument(
        "Merged",
        (std::string(branchA.Label.getValue()) + " (merged)").c_str(),
        flags);
    std::istringstream recipe(formatStoredRecipe(branchA, {}, scope));
    restoreStoredRecipe(*merged, recipe);
    if (const std::optional<std::string> refusal =
            takeDocumentValues(*merged, branchA, branchB, result.report.document)) {
        result.refusal = *refusal;
        GetApplication().closeDocument(merged->getName());
        return result;
    }
    merged->Label.setValue(std::string(merged->Label.getValue()) + " (merged)");

    for (const auto& [objA, taken] : scope.valuesFrom) {
        DocumentObject* into = objectWithUid(*merged, objA->Uid.getValueStr());
        for (const auto& [name, objB] : taken) {
            takeFormulas(*into, *objB, name);
        }
    }

    for (const auto& [id, names] : combined) {
        DocumentObject* into = objectWithUid(*merged, id);
        const RegisteredContentMerge* merger = recipeMerger(*into);
        bool contentApplied = false;
        for (const std::string& name : names) {
            const bool covered = merger != nullptr && merger->apply
                && std::find(merger->coveredProperties.begin(),
                             merger->coveredProperties.end(),
                             name)
                    != merger->coveredProperties.end();
            std::optional<std::string> refusal;
            if (covered) {
                if (!contentApplied) {
                    refusal = merger->apply(*into,
                                            *find(objectsO, id),
                                            *find(objectsA, id),
                                            *find(objectsB, id));
                    contentApplied = true;
                }
            }
            else if (Property* prop = into->getPropertyByName(name.c_str());
                     prop != nullptr && prop->isDerivedFrom(PropertyLinkBase::getClassTypeId())) {
                refusal = applyMergedLink(*into,
                                          name,
                                          result.report.merged.at(id),
                                          *find(objectsA, id),
                                          *find(objectsB, id));
            }
            else {
                refusal = "combining both versions of " + name + " is not built yet";
            }
            if (refusal) {
                result.refusal = std::string(into->Label.getValue()) + ": " + *refusal;
                GetApplication().closeDocument(merged);
                return result;
            }
        }
    }

    merged->recompute();

    for (DocumentObject* obj : merged->getObjects()) {
        if (obj == nullptr || obj->isValid()) {
            continue;
        }
        const std::string id = obj->Uid.getValueStr();
        if (failingA.count(id) == 0 && failingB.count(id) == 0) {
            result.newlyFailing.push_back(obj->Label.getValue());
        }
    }

    result.document = merged;
    return result;
}
