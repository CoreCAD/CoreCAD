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
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#endif

#include "DocumentChanges.h"

#include "Datums.h"
#include "Document.h"
#include "DocumentObject.h"
#include "ObjectRecipe.h"
#include "RecipeDetail.h"

using namespace App;

namespace
{

using LabelById = std::map<std::string, std::string>;

LabelById labelsOf(const Document& doc)
{
    LabelById labels;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        const char* label = obj->Label.getValue();
        labels[obj->Uid.getValueStr()] =
            (label != nullptr && *label != '\0') ? label : obj->getNameInDocument();
    }
    return labels;
}

/// A sub-entity has no name, so it is shown by the head of its durable tag. Anything else is
/// left whole.
std::string shortId(const std::string& id)
{
    constexpr std::string::size_type tagLength = 36;
    constexpr std::string::size_type shown = 6;
    return id.size() == tagLength ? id.substr(0, shown) : id;
}

std::string valueText(const std::string& value)
{
    return value.empty() ? std::string("(empty)") : value;
}

/// A link target is named by its label in either version, since a removed object is only in
/// the older one.
std::string refsText(const std::vector<RecipeRef>& refs,
                     const LabelById& newLabels,
                     const LabelById& oldLabels)
{
    std::string text;
    for (const RecipeRef& ref : refs) {
        if (ref.target.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += ", ";
        }
        if (const auto it = newLabels.find(ref.target); it != newLabels.end()) {
            text += it->second;
        }
        else if (const auto old = oldLabels.find(ref.target); old != oldLabels.end()) {
            text += old->second;
        }
        else {
            text += shortId(ref.target);
        }
        if (ref.pos != 0) {
            text += ":" + std::to_string(ref.pos);
        }
    }
    return text.empty() ? std::string("nothing") : text;
}

std::string describe(const RecipeChange& change,
                     const std::string& subject,
                     const LabelById& newLabels,
                     const LabelById& oldLabels)
{
    switch (change.kind) {
        case RecipeChange::Kind::Added:
            return subject + " added";
        case RecipeChange::Kind::Removed:
            return subject + " removed";
        case RecipeChange::Kind::TypeChanged:
            return subject + ": type " + *change.before + " → " + *change.after;
        case RecipeChange::Kind::LinksChanged:
            return subject + ": references " + refsText(change.refsBefore, newLabels, oldLabels)
                + " → " + refsText(change.refsAfter, newLabels, oldLabels);
        case RecipeChange::Kind::FieldChanged:
            if (!change.before) {
                return subject + ": " + change.field + " set to " + valueText(*change.after);
            }
            if (!change.after) {
                return subject + ": " + change.field + " cleared (was " + valueText(*change.before)
                    + ")";
            }
            return subject + ": " + change.field + " " + valueText(*change.before) + " → "
                + valueText(*change.after);
    }
    return subject;
}

/// Put changes in the order a person met the entities: the newer version's order, then
/// whatever only the older one had.
void orderByAuthoring(std::vector<RecipeChange>& changes,
                      const std::vector<std::string>& newOrder,
                      const std::vector<std::string>& oldOrder)
{
    std::map<std::string, std::size_t> rank;
    for (const std::string& id : newOrder) {
        rank.emplace(id, rank.size());
    }
    for (const std::string& id : oldOrder) {
        rank.emplace(id, rank.size());
    }
    std::stable_sort(changes.begin(),
                     changes.end(),
                     [&rank](const RecipeChange& lhs, const RecipeChange& rhs) {
                         return rank[lhs.id] < rank[rhs.id];
                     });
}

RecipeSection sectionOf(const std::vector<RecipeNode>& nodes, std::vector<std::string>& order)
{
    RecipeSection section;
    for (const RecipeNode& node : nodes) {
        section[node.id] = node;
        order.push_back(node.id);
    }
    return section;
}

const RecipeDetailSection* findSection(const RecipeDetail& detail, const std::string& name)
{
    for (const RecipeDetailSection& section : detail.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

/// Changes inside an object's interior, such as a sketch's lines and dimensions, which the
/// object's own node cannot see.
void appendInteriorChanges(const DocumentObject& oldObj,
                           const DocumentObject& newObj,
                           const DocumentChange& about,
                           const LabelById& newLabels,
                           const LabelById& oldLabels,
                           std::vector<DocumentChange>& out)
{
    const RecipeDetail oldDetail = recipeDetail(oldObj);
    const RecipeDetail newDetail = recipeDetail(newObj);
    const RecipeConsequences consequences = recipeConsequences(oldObj, newObj);

    std::vector<std::string> names;
    for (const RecipeDetailSection& section : newDetail.sections) {
        names.push_back(section.name);
    }
    for (const RecipeDetailSection& section : oldDetail.sections) {
        if (std::find(names.begin(), names.end(), section.name) == names.end()) {
            names.push_back(section.name);
        }
    }

    for (const std::string& name : names) {
        const RecipeDetailSection* oldSection = findSection(oldDetail, name);
        const RecipeDetailSection* newSection = findSection(newDetail, name);
        std::vector<std::string> oldOrder;
        std::vector<std::string> newOrder;
        const RecipeSection oldNodes = oldSection ? sectionOf(oldSection->nodes, oldOrder)
                                                  : RecipeSection {};
        const RecipeSection newNodes = newSection ? sectionOf(newSection->nodes, newOrder)
                                                  : RecipeSection {};

        std::vector<RecipeChange> changes = RecipeDiff::twoWay(oldNodes, newNodes);
        orderByAuthoring(changes, newOrder, oldOrder);
        for (const RecipeChange& change : changes) {
            if (change.kind == RecipeChange::Kind::FieldChanged
                && consequences.count({change.id, change.field}) != 0) {
                continue;
            }
            DocumentChange entry = about;
            entry.section = name;
            entry.change = change;
            const std::string subject = about.label + ", " + change.type + " " + shortId(change.id);
            entry.text = describe(change, subject, newLabels, oldLabels);
            out.push_back(entry);
        }
    }
}

void appendAfterDependencies(const DocumentObject* obj,
                             std::set<const DocumentObject*>& seen,
                             std::vector<const DocumentObject*>& order)
{
    if (!seen.insert(obj).second) {
        return;
    }
    for (const DocumentObject* used : obj->getOutList()) {
        if (used != nullptr && used->getDocument() == obj->getDocument()) {
            appendAfterDependencies(used, seen, order);
        }
    }
    order.push_back(obj);
}

/// The world frame a document makes for itself: its origin and that origin's planes, axes and
/// point. It arrives with the document, not as a step anyone took, so its arrival or removal is
/// not a change worth listing; an edit to it still is.
bool isWorldFrame(const DocumentObject& obj)
{
    if (const auto* frame = dynamic_cast<const LocalCoordinateSystem*>(&obj)) {
        return frame->isOrigin();
    }
    if (const auto* datum = dynamic_cast<const DatumElement*>(&obj)) {
        return datum->isOriginFeature();
    }
    return false;
}

/// Objects in the order the model is built, a sketch before the step that uses it. A document's
/// own list cannot serve: one read back from its file is in id order. A loop between objects, as a
/// body and its steps make, is broken where it is met rather than reported: an order for reading
/// is not a recompute.
std::vector<const DocumentObject*> buildOrder(const Document& doc)
{
    std::vector<const DocumentObject*> order;
    std::set<const DocumentObject*> seen;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr) {
            appendAfterDependencies(obj, seen, order);
        }
    }
    return order;
}

}  // namespace

std::vector<DocumentChange> App::diffDocuments(const Document& before, const Document& after)
{
    const LabelById oldLabels = labelsOf(before);
    const LabelById newLabels = labelsOf(after);

    std::map<std::string, const DocumentObject*> oldObjects;
    std::map<std::string, const DocumentObject*> newObjects;
    std::vector<std::string> order;
    for (const DocumentObject* obj : buildOrder(after)) {
        newObjects[obj->Uid.getValueStr()] = obj;
        order.push_back(obj->Uid.getValueStr());
    }
    for (const DocumentObject* obj : buildOrder(before)) {
        oldObjects[obj->Uid.getValueStr()] = obj;
        if (newObjects.count(obj->Uid.getValueStr()) == 0) {
            order.push_back(obj->Uid.getValueStr());
        }
    }

    std::map<std::string, std::vector<RecipeChange>> nodeChanges;
    for (const RecipeChange& change :
         RecipeDiff::twoWay(emitDocumentRecipe(before), emitDocumentRecipe(after))) {
        nodeChanges[change.id].push_back(change);
    }

    std::vector<DocumentChange> changes;
    for (const std::string& id : order) {
        const auto itOld = oldObjects.find(id);
        const auto itNew = newObjects.find(id);
        const bool inOld = itOld != oldObjects.end();
        const bool inNew = itNew != newObjects.end();

        DocumentChange about;
        about.objectId = id;
        about.label = inNew ? newLabels.at(id) : oldLabels.at(id);
        about.objectType = (inNew ? itNew->second : itOld->second)->getTypeId().getName();

        if (inOld && inNew && oldLabels.at(id) != newLabels.at(id)) {
            DocumentChange entry = about;
            entry.change = {RecipeChange::Kind::FieldChanged,
                            id,
                            about.objectType,
                            "Label",
                            oldLabels.at(id),
                            newLabels.at(id)};
            entry.text = oldLabels.at(id) + " renamed to " + newLabels.at(id);
            changes.push_back(entry);
        }

        const DocumentObject& object = inNew ? *itNew->second : *itOld->second;
        for (const RecipeChange& change : nodeChanges[id]) {
            const bool arrivedOrWent = change.kind == RecipeChange::Kind::Added
                || change.kind == RecipeChange::Kind::Removed;
            if (arrivedOrWent && isWorldFrame(object)) {
                continue;
            }
            DocumentChange entry = about;
            entry.change = change;
            entry.text = describe(change, about.label, newLabels, oldLabels);
            changes.push_back(entry);
        }

        if (inOld && inNew) {
            appendInteriorChanges(*itOld->second, *itNew->second, about, newLabels, oldLabels,
                                  changes);
        }
    }

    return changes;
}

std::string App::formatDocumentChanges(const std::vector<DocumentChange>& changes)
{
    if (changes.empty()) {
        return "No changes.\n";
    }
    std::ostringstream out;
    for (const DocumentChange& change : changes) {
        out << change.text << "\n";
    }
    return out.str();
}
