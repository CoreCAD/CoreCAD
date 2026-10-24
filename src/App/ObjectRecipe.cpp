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
# include <iomanip>
# include <limits>
# include <locale>
# include <map>
# include <memory>
# include <optional>
# include <set>
# include <sstream>
# include <string>
# include <utility>
# include <vector>
#endif

#include <Base/Unit.h>

#include "ObjectRecipe.h"

#include "Document.h"
#include "ElementNamingUtils.h"
#include "DocumentObject.h"
#include "Expression.h"
#include "ObjectIdentifier.h"
#include "PropertyExpressionEngine.h"
#include "PropertyGeo.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "PropertyUnits.h"
#include "RecipeDetail.h"
#include "StoredRecipe.h"

using namespace App;

namespace
{

/// Full-precision, locale-free rendering of a double, so a field changes iff the authored value
/// changes (display formatting would round distinct values into one string). Mirrors the sketch
/// emitter's canonicalNumber, so the two drivers speak the same value language.
std::string canonicalNumber(double value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return oss.str();
}

/// A full-precision, locale-free rendering of a 3-vector's components, so a change to any
/// component changes the field. Shared by the vector and placement canonicalizers.
std::string canonicalVector(const Base::Vector3d& vec)
{
    return canonicalNumber(vec.x) + " " + canonicalNumber(vec.y) + " " + canonicalNumber(vec.z);
}

/// Position then rotation as a quaternion, full precision, so a move OR a rotation changes the
/// field. Shared by the placement scalar and the placement-list canonicalizers.
std::string canonicalPlacement(const Base::Placement& value)
{
    double q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0;
    value.getRotation().getValue(q0, q1, q2, q3);
    return canonicalVector(value.getPosition()) + " " + canonicalNumber(q0) + " "
        + canonicalNumber(q1) + " " + canonicalNumber(q2) + " " + canonicalNumber(q3);
}

/// A list value type canonicalized as its elements joined by "; ", so a change to any element,
/// to the count, or to the order changes the field. Each element is rendered by the same
/// per-value canonicalizer the scalar path uses, so a list and its scalar sibling speak one
/// value language. An empty list is a valid authored state and renders as the empty string
/// (present-but-empty, distinct from a type this driver does not handle).
template<class ListT, class Render>
std::string canonicalList(const ListT& values, Render render)
{
    std::string out;
    bool first = true;
    for (const auto& element : values) {
        if (!first) {
            out += "; ";
        }
        out += render(element);
        first = false;
    }
    return out;
}

/// Properties that are never part of the authored recipe whatever their type: the durable id
/// itself (it *is* the node's identity, not one of its fields) and whether the object is shown,
/// which is view state. ExpressionEngine is here for the opposite reason: its content is already
/// recorded, each binding emitted as the value of the property it drives, so naming it keeps one
/// expression from being stated twice and keeps every object in the document from reporting a
/// gap it does not have.
/// Shared by the emitter and by unrecordedProperties, so both agree on what is out of scope.
bool isNonRecipeProperty(const std::string& name)
{
    return name == "Uid" || name == "Visibility" || name == "ExpressionEngine";
}


/// The authored value of one property as a canonical string, or nullopt if the property carries
/// no value this driver can yet canonicalize (list value types are a follow-on increment, each
/// gated by its own test). A quantity keeps its unit token so a bare-number change and a unit
/// change are both visible. Most-derived types are matched first (PropertyQuantity derives from
/// PropertyFloat; PropertyPosition/PropertyVectorDistance derive from PropertyVector).
/// Each object a link points at, with the element picked on it ("" for the whole object). A link
/// to one object hands back that object once and all its picked elements, so each element is
/// paired with it; a list of objects hands back one element per object.
std::vector<std::pair<DocumentObject*, std::string>> linkedPairs(const PropertyLinkBase& link,
                                                                 bool newStyle)
{
    std::vector<DocumentObject*> targets;
    std::vector<std::string> subs;
    link.getLinks(targets, /*all=*/true, &subs, newStyle);

    std::vector<std::pair<DocumentObject*, std::string>> pairs;
    if (targets.size() == 1 && subs.size() > 1) {
        for (const std::string& sub : subs) {
            pairs.emplace_back(targets.front(), sub);
        }
        return pairs;
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        pairs.emplace_back(targets[i], i < subs.size() ? subs[i] : std::string());
    }
    return pairs;
}

std::optional<std::string> authoredFieldValue(const Property* prop)
{
    if (const auto* quantity = dynamic_cast<const PropertyQuantity*>(prop)) {
        std::string literal = canonicalNumber(quantity->getValue());
        const std::string unit = quantity->getUnit().getString();
        if (!unit.empty()) {
            literal += " " + unit;
        }
        return literal;
    }
    if (const auto* number = dynamic_cast<const PropertyFloat*>(prop)) {
        return canonicalNumber(number->getValue());
    }
    if (const auto* integer = dynamic_cast<const PropertyInteger*>(prop)) {
        return std::to_string(integer->getValue());
    }
    if (const auto* flag = dynamic_cast<const PropertyBool*>(prop)) {
        return std::string(flag->getValue() ? "true" : "false");
    }
    if (const auto* choice = dynamic_cast<const PropertyEnumeration*>(prop)) {
        const char* current = choice->getValueAsString();
        return current != nullptr ? std::string(current) : std::string();
    }
    if (const auto* text = dynamic_cast<const PropertyString*>(prop)) {
        const char* value = text->getValue();
        return value != nullptr ? std::string(value) : std::string();
    }
    if (const auto* placement = dynamic_cast<const PropertyPlacement*>(prop)) {
        // A move OR a rotation of a datum/attachment placement — previously invisible to the
        // merge — changes the field.
        return canonicalPlacement(placement->getValue());
    }
    if (const auto* vector = dynamic_cast<const PropertyVector*>(prop)) {
        return canonicalVector(vector->getValue());
    }
    if (const auto* matrix = dynamic_cast<const PropertyMatrix*>(prop)) {
        double cells[16] = {};
        matrix->getValue().getMatrix(cells);
        std::string out;
        for (int i = 0; i < 16; ++i) {
            if (i != 0) {
                out += " ";
            }
            out += canonicalNumber(cells[i]);
        }
        return out;
    }
    // List value types. Each derives from PropertyListsT (not from its scalar sibling), so these
    // are matched independently of the scalar cases above; every one renders via canonicalList so
    // a change to any element, the count, or the order is visible.
    if (const auto* list = dynamic_cast<const PropertyFloatList*>(prop)) {
        return canonicalList(list->getValues(), [](double v) { return canonicalNumber(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyIntegerList*>(prop)) {
        return canonicalList(list->getValues(), [](long v) { return std::to_string(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyStringList*>(prop)) {
        return canonicalList(list->getValues(), [](const std::string& v) { return v; });
    }
    if (const auto* list = dynamic_cast<const PropertyBoolList*>(prop)) {
        // Stored as a boost::dynamic_bitset (no range iterators), so index it directly.
        const auto& bits = list->getValues();
        std::string out;
        for (std::size_t i = 0; i < bits.size(); ++i) {
            if (i != 0) {
                out += "; ";
            }
            out += bits[i] ? "true" : "false";
        }
        return out;
    }
    if (const auto* list = dynamic_cast<const PropertyVectorList*>(prop)) {
        return canonicalList(list->getValues(),
                             [](const Base::Vector3d& v) { return canonicalVector(v); });
    }
    if (const auto* list = dynamic_cast<const PropertyPlacementList*>(prop)) {
        return canonicalList(list->getValues(),
                             [](const Base::Placement& v) { return canonicalPlacement(v); });
    }
    return std::nullopt;
}

/// The entries of a name→value property, or nullopt when the property is not map-shaped.
///
/// A drawing's title block is the case that forced this: every box on it — title, drawn by,
/// scale, sheet number — lives as one entry of a single property, so the whole block was
/// reported as unrecorded and a changed drawing title produced no line at all. Each entry
/// becomes a field of its own rather than one joined string, so a changed title is one changed
/// line, and two people filling in different boxes of the same block do not collide.
std::optional<std::map<std::string, std::string>> authoredMapEntries(const Property* prop)
{
    if (const auto* entries = dynamic_cast<const PropertyMap*>(prop)) {
        return entries->getValues();
    }
    return std::nullopt;
}

/// A value driven in parts: the parts it can set apart are marked "*" and every part's formula is
/// listed after it, so neither the formulas nor the rest of the value hide behind each other. A part
/// it cannot set apart keeps its computed number, which can then change with the formula's inputs:
/// a louder report, never a hidden one.
std::string partlyBoundValue(const Property* prop,
                             const std::string& value,
                             const std::map<std::string, std::string>& formulaByPart)
{
    const auto bound = [&formulaByPart](std::initializer_list<const char*> parts) {
        return std::any_of(parts.begin(), parts.end(), [&formulaByPart](const char* part) {
            return formulaByPart.count(part) != 0;
        });
    };
    const auto component = [&bound](double number, std::initializer_list<const char*> parts) {
        return bound(parts) ? std::string("*") : canonicalNumber(number);
    };

    std::string out = value;
    if (const auto* vector = dynamic_cast<const PropertyVector*>(prop)) {
        const Base::Vector3d& v = vector->getValue();
        out = component(v.x, {".x"}) + " " + component(v.y, {".y"}) + " "
            + component(v.z, {".z"});
    }
    else if (const auto* placement = dynamic_cast<const PropertyPlacement*>(prop)) {
        const Base::Vector3d& base = placement->getValue().getPosition();
        out = component(base.x, {".Base", ".Base.x"}) + " "
            + component(base.y, {".Base", ".Base.y"}) + " "
            + component(base.z, {".Base", ".Base.z"});
        if (bound({".Rotation"})) {
            out += " * * * *";
        }
        else {
            const std::string whole = canonicalPlacement(placement->getValue());
            const std::string position = canonicalVector(base);
            out += whole.substr(position.size());
        }
    }
    for (const auto& [part, formula] : formulaByPart) {
        out += " | " + part + " = " + formula;
    }
    return out;
}

/// Each object of a document by its durable Uid.
std::map<std::string, const DocumentObject*> objectsByUid(const Document& doc)
{
    std::map<std::string, const DocumentObject*> found;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj != nullptr) {
            found[obj->Uid.getValueStr()] = obj;
        }
    }
    return found;
}

/** Make a change the emitter cannot write down visible to the merge.
 *
 *  A value with no recipe form (a sketch's lines, a spreadsheet's cells) was simply absent, so
 *  both sides setting it differently read as "merged cleanly", and an edit inside an object the
 *  other side deleted was lost without a word. Each such value now stands in the recipe as a
 *  marker saying only whether it changed, compared whole: one side's change is taken, two
 *  different changes conflict. Where the type's module merges that content by its own
 *  identities, the module's merge decides instead and its findings are returned.
 */
RecipeContentMerge markUncomparedChanges(const Document& ancestor,
                                         const Document& branchA,
                                         const Document& branchB,
                                         RecipeSection& base,
                                         RecipeSection& a,
                                         RecipeSection& b)
{
    static const std::string unchanged = "unchanged";
    static const std::string changed = "changed";
    static const std::string changedAlike = "changed alike";
    static const std::string changedDifferently = "changed differently";
    static const std::string mergedByContent = "merged by its own content";

    RecipeContentMerge found;
    const auto objectsA = objectsByUid(branchA);
    const auto objectsB = objectsByUid(branchB);

    for (const auto& [id, objO] : objectsByUid(ancestor)) {
        const auto itA = objectsA.find(id);
        const auto itB = objectsB.find(id);
        const DocumentObject* objA = itA != objectsA.end() ? itA->second : nullptr;
        const DocumentObject* objB = itB != objectsB.end() ? itB->second : nullptr;
        const RegisteredContentMerge* merger = recipeMerger(*objO);
        bool contentMerged = false;
        std::vector<std::string> covered;

        for (const std::string& name : unrecordedProperties(*objO)) {
            const Property* propO = objO->getPropertyByName(name.c_str());
            const Property* propA = objA != nullptr ? objA->getPropertyByName(name.c_str()) : nullptr;
            const Property* propB = objB != nullptr ? objB->getPropertyByName(name.c_str()) : nullptr;
            const bool changedA = propA != nullptr && !propA->isSame(*propO);
            const bool changedB = propB != nullptr && !propB->isSame(*propO);
            const bool alike = changedA && changedB && propA->isSame(*propB);

            base[id].fields[name] = unchanged;
            if (propA != nullptr) {
                a[id].fields[name] = alike ? changedAlike : changedA ? changed : unchanged;
            }
            if (propB != nullptr) {
                b[id].fields[name] = alike ? changedAlike
                    : !changedB            ? unchanged
                    : changedA             ? changedDifferently
                                           : changed;
            }

            const bool coveredByMerger = merger != nullptr
                && std::find(merger->coveredProperties.begin(),
                             merger->coveredProperties.end(),
                             name)
                    != merger->coveredProperties.end();
            if (coveredByMerger) {
                covered.push_back(name);
                contentMerged = contentMerged || (changedA && changedB && !alike);
            }
        }

        if (!contentMerged) {
            continue;
        }
        for (const std::string& name : covered) {
            a[id].fields[name] = mergedByContent;
            b[id].fields[name] = mergedByContent;
        }
        RecipeContentMerge inside = merger->merge(*objO, *objA, *objB);
        const std::string where = std::string(objA->Label.getValue()) + ": ";
        for (MergeConflict& conflict : inside.conflicts) {
            conflict.detail = where + conflict.detail;
            found.conflicts.push_back(std::move(conflict));
        }
        for (RefResolution& resolution : inside.resolutions) {
            resolution.detail = where + resolution.detail;
            found.resolutions.push_back(std::move(resolution));
        }
    }

    return found;
}

/// A short, readable stand-in for a durable Uid in a report line (the full uuid is exact but
/// unreadable). Never used for identity — only for a person scanning the summary.
std::string shortId(const std::string& id)
{
    return id.size() > 8 ? id.substr(0, 8) : id;
}

}  // namespace

RecipeNode App::emitObjectRecipe(const DocumentObject& obj)
{
    RecipeNode node;
    node.id = obj.Uid.getValueStr();
    node.type = obj.getTypeId().getName();

    // A property driven by an expression is authored by that expression, not by its resolved
    // number; index the bindings by property so the field records the expression text. A formula
    // can drive one part of a value (a placement's x), so each is kept under the part it drives.
    std::map<const Property*, std::map<std::string, std::string>> formulasByProperty;
    for (const auto& [identifier, expression] : obj.ExpressionEngine.getExpressions()) {
        if (expression == nullptr) {
            continue;
        }
        if (const Property* bound = identifier.getProperty()) {
            formulasByProperty[bound][identifier.getSubPathStr()] = formulaStatedByUid(*expression);
        }
    }

    std::set<std::string> seenRefs;
    // getPropertyMap (unlike getPropertyNamedList) is overridden by ExtensionContainer to include
    // properties an extension contributes — Placement chief among them, which Amendment 4 moved
    // off GeoFeature into App::PlacementExtension. Enumerating the map keeps those in the recipe
    // (and orders deterministically by name).
    std::map<std::string, Property*> properties;
    obj.getPropertyMap(properties);
    for (const auto& [propName, prop] : properties) {
        if (prop == nullptr) {
            continue;
        }
        if (isNonRecipeProperty(propName)) {
            continue;
        }
        if (!theRecipeCarries(*prop, obj)) {
            continue;
        }
        if (obj.producesContentOf(*prop)) {
            // Produced again from the merged result, never merged (an attached placement).
            continue;
        }
        if (prop->isDerivedFrom(PropertyLinkBase::getClassTypeId())) {
            // A link addresses another object; the recipe references it by the target's durable
            // Uid, never by name, and a picked face or edge by its durable name. The readable
            // "Edge3" suffix is a position that shifts when the shape renumbers, so it is kept
            // only as a label.
            if (!obj.linkOrderMatters(*prop)) {
                node.unorderedLinks.insert(propName);
            }
            for (const auto& [target, sub] :
                 linkedPairs(*static_cast<const PropertyLinkBase*>(prop), /*newStyle=*/true)) {
                if (target == nullptr) {
                    continue;
                }
                RecipeRef ref;
                ref.link = propName;
                ref.target = target->Uid.getValueStr();
                if (!sub.empty()) {
                    ref.element = Data::newElementName(sub.c_str());
                    ref.elementLabel = Data::oldElementName(sub.c_str());
                }
                if (seenRefs.insert(ref.link + '\n' + ref.target + '\n' + ref.element).second) {
                    node.refs.push_back(ref);
                }
            }
            continue;
        }
        if (const auto entries = authoredMapEntries(prop)) {
            // One field per entry, named "property.key", so the block reads as the list of
            // boxes it is. A map keeps its keys sorted, so the order is a property of the
            // content and not of the walk.
            for (const auto& [key, value] : *entries) {
                node.fields[propName + "." + key] = value;
            }
            continue;
        }
        if (const std::optional<std::string> value = authoredFieldValue(prop)) {
            if (propName == "Label2" && value->empty()) {
                continue;  // Nobody wrote a description; a blank one on every object says nothing.
            }
            const auto bound = formulasByProperty.find(prop);
            if (bound == formulasByProperty.end()) {
                node.fields[propName] = *value;
            }
            else if (const auto whole = bound->second.find(""); whole != bound->second.end()) {
                node.fields[propName] = whole->second;
            }
            else {
                node.fields[propName] = partlyBoundValue(prop, *value, bound->second);
            }
        }
    }

    return node;
}

std::vector<std::string> App::unrecordedProperties(const DocumentObject& obj)
{
    // Deliberately mirrors emitObjectRecipe's walk, minus the value rendering: a property is
    // reported here exactly when the emitter reached it and had nothing to say about it. The
    // two must be read together -- change one filter and this stops telling the truth.
    std::vector<std::string> names;

    std::map<std::string, Property*> properties;
    obj.getPropertyMap(properties);
    for (const auto& [propName, prop] : properties) {
        if (prop == nullptr || isNonRecipeProperty(propName)) {
            continue;
        }
        if (!theRecipeCarries(*prop, obj)) {
            continue;
        }
        // A container of links (a spreadsheet's cells) holds content beyond the links the
        // emitter takes from it, so it is not recorded by them.
        const bool linksOnly = prop->isDerivedFrom(PropertyLinkBase::getClassTypeId())
            && !prop->isDerivedFrom(PropertyXLinkContainer::getClassTypeId());
        if (linksOnly || obj.producesContentOf(*prop)) {
            continue;
        }
        if (!authoredFieldValue(prop).has_value() && !authoredMapEntries(prop).has_value()) {
            names.push_back(propName);
        }
    }

    return names;
}

std::map<std::string, std::vector<std::string>> App::linkedElements(const DocumentObject& obj)
{
    std::map<std::string, std::vector<std::string>> picked;

    // The same walk and the same filters the emitter uses, so the view can never name an element
    // on a link the recipe itself does not carry.
    std::map<std::string, Property*> properties;
    obj.getPropertyMap(properties);
    for (const auto& [propName, prop] : properties) {
        if (prop == nullptr || isNonRecipeProperty(propName)) {
            continue;
        }
        if (!theRecipeCarries(*prop, obj)) {
            continue;
        }
        if (!prop->isDerivedFrom(PropertyLinkBase::getClassTypeId())) {
            continue;
        }

        // Old-style names ("Face6"), not the shape's mapped ones: a mapped name is a hash, and
        // this exists to be read.
        for (const auto& [target, sub] :
             linkedPairs(*static_cast<const PropertyLinkBase*>(prop), /*newStyle=*/false)) {
            if (target != nullptr && !sub.empty()) {
                picked[target->Uid.getValueStr()].push_back(sub);
            }
        }
    }

    for (auto& [id, named] : picked) {
        std::sort(named.begin(), named.end());
        named.erase(std::unique(named.begin(), named.end()), named.end());
    }

    return picked;
}

RecipeSection App::emitDocumentRecipe(const Document& doc)
{
    RecipeSection section;
    for (const DocumentObject* obj : doc.getObjects()) {
        if (obj == nullptr) {
            continue;
        }
        section[obj->Uid.getValueStr()] = emitObjectRecipe(*obj);
    }
    return section;
}

DocumentMergeReport App::mergeDocuments(const Document& ancestor,
                                        const Document& branchA,
                                        const Document& branchB)
{
    DocumentMergeReport report;

    RecipeSection base = emitDocumentRecipe(ancestor);
    RecipeSection a = emitDocumentRecipe(branchA);
    RecipeSection b = emitDocumentRecipe(branchB);
    RecipeContentMerge inside = markUncomparedChanges(ancestor, branchA, branchB, base, a, b);

    report.merged = RecipeMerge::threeWay(base, a, b, report.conflicts);

    // Refine each whole-object conflict to field granularity: where the two branches touched
    // disjoint fields (or links, or type) the edits are auto-merged into report.merged and the
    // conflict dissolves; only genuinely overlapping same-field edits remain. Runs before the
    // referential pass so that pass resolves against the refined links.
    report.conflicts = RecipeMerge::refineConflicts(report.conflicts, base, a, b, report.merged);

    // A step one branch added or changed on an object the other branch deleted is a conflict, kept
    // for the person settling it; §4.7's drop reaches only steps the deleting branch could see.
    RecipeMerge::presenceConflicts(base, a, b, base, a, b, report.merged, report.conflicts);
    report.conflicts.insert(report.conflicts.end(), inside.conflicts.begin(), inside.conflicts.end());

    // An open conflict is held for the person settling it; the reference pass never drops it.
    RecipeSection contested;
    for (const MergeConflict& conflict : report.conflicts) {
        if (const auto node = report.merged.find(conflict.id); node != report.merged.end()) {
            contested.insert(report.merged.extract(node));
        }
    }

    // Objects reference other objects in the same section, so a reference dangles when the merge
    // kept an object whose referent the other branch deleted. Resolve against a snapshot of the
    // survivors (a copy, so dropping a node cannot perturb the target set mid-pass).
    const RecipeSection survivors = report.merged;
    report.resolutions = RecipeMerge::resolveReferences(report.merged, survivors);
    report.merged.merge(contested);

    report.resolutions.insert(
        report.resolutions.end(), inside.resolutions.begin(), inside.resolutions.end());

    return report;
}

std::string App::formatDocumentReport(const DocumentMergeReport& report)
{
    std::ostringstream out;
    const std::size_t objectCount = report.merged.size();

    if (report.conflicts.empty() && report.resolutions.empty()) {
        out << "Merged cleanly: " << objectCount << " objects, no conflicts.\n";
        return out.str();
    }

    out << "Merged " << objectCount << " objects.\n";

    const auto printConflicts = [&](MergeConflict::Kind kind, const char* heading) {
        bool headed = false;
        for (const MergeConflict& conflict : report.conflicts) {
            if (conflict.kind != kind) {
                continue;
            }
            if (!headed) {
                out << heading;
                headed = true;
            }
            out << "  - " << (conflict.type.empty() ? "object" : conflict.type) << " "
                << shortId(conflict.id) << ": " << conflict.detail << "\n";
        }
    };
    printConflicts(MergeConflict::Kind::Value,
                   "\nConflicts — both branches changed the same object differently:\n");
    printConflicts(MergeConflict::Kind::Referential,
                   "\nConflicts — one branch built on an object the other branch deleted:\n");

    if (!report.resolutions.empty()) {
        out << "\nReferences the merge had to settle:\n";
        for (const RefResolution& resolution : report.resolutions) {
            const char* label = "kept";
            if (resolution.outcome == RefResolution::Outcome::Drop) {
                label = "dropped";
            }
            else if (resolution.outcome == RefResolution::Outcome::StopAsk) {
                label = "needs a decision";
            }
            out << "  - " << (resolution.type.empty() ? "object" : resolution.type) << " "
                << shortId(resolution.id) << " " << label << ": " << resolution.detail << "\n";
        }
    }

    return out.str();
}
