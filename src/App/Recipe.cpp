// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <optional>
#include <set>
#include <string>
#include <vector>
#endif

#include "Recipe.h"

using namespace App;

RecipeSection RecipeMerge::threeWay(const RecipeSection& base,
                                    const RecipeSection& a,
                                    const RecipeSection& b,
                                    std::vector<MergeConflict>& conflicts)
{
    RecipeSection merged;

    // Every id that appears on any of the three sides.
    std::set<std::string> ids;
    for (const auto& [id, node] : base) {
        ids.insert(id);
    }
    for (const auto& [id, node] : a) {
        ids.insert(id);
    }
    for (const auto& [id, node] : b) {
        ids.insert(id);
    }

    for (const std::string& id : ids) {
        auto itO = base.find(id);
        auto itA = a.find(id);
        auto itB = b.find(id);
        const bool inO = itO != base.end();
        const bool inA = itA != a.end();
        const bool inB = itB != b.end();

        // Treat presence-and-value together: two sides "equal" when both absent, or both
        // present with equal nodes.
        auto sidesEqual = [](bool p1, const RecipeNode* n1, bool p2, const RecipeNode* n2) {
            if (p1 != p2) {
                return false;
            }
            return !p1 || *n1 == *n2;  // both absent, or both present and equal
        };
        const RecipeNode* nO = inO ? &itO->second : nullptr;
        const RecipeNode* nA = inA ? &itA->second : nullptr;
        const RecipeNode* nB = inB ? &itB->second : nullptr;

        if (sidesEqual(inA, nA, inB, nB)) {
            // Both branches agree (both added the same, both deleted, or neither touched).
            if (inA) {
                merged[id] = *nA;
            }
        }
        else if (sidesEqual(inA, nA, inO, nO)) {
            // A is unchanged from the ancestor -> B decides (its edit, or its delete).
            if (inB) {
                merged[id] = *nB;
            }
        }
        else if (sidesEqual(inB, nB, inO, nO)) {
            // B is unchanged from the ancestor -> A decides.
            if (inA) {
                merged[id] = *nA;
            }
        }
        else {
            // Both moved off the ancestor, differently: a genuine conflict. Keep A's side
            // provisionally (if any) so the merged recipe stays inspectable.
            const std::string type = nA ? nA->type : (nB ? nB->type : std::string {});
            conflicts.push_back(
                {MergeConflict::Kind::Value, id, type, "diverging edit to the same entity"});
            if (inA) {
                merged[id] = *nA;
            }
            else if (inB) {
                merged[id] = *nB;
            }
        }
    }

    return merged;
}

void RecipeMerge::checkReferences(const RecipeSection& withRefs,
                                  const RecipeSection& liveTargets,
                                  std::vector<MergeConflict>& conflicts)
{
    for (const auto& [id, node] : withRefs) {
        for (const RecipeRef& ref : node.refs) {
            if (ref.target.empty()) {
                continue;
            }
            if (liveTargets.find(ref.target) == liveTargets.end()) {
                conflicts.push_back({MergeConflict::Kind::Referential,
                                     id,
                                     node.type,
                                     "references deleted entity " + ref.target});
            }
        }
    }
}

std::vector<RefResolution> RecipeMerge::resolveReferences(RecipeSection& withRefs,
                                                          const RecipeSection& liveTargets)
{
    std::vector<RefResolution> resolutions;
    std::vector<std::string> toDrop;

    for (auto& [id, node] : withRefs) {
        bool anyDangling = false;
        bool anySurviving = false;
        std::string danglingTarget;

        for (const RecipeRef& ref : node.refs) {
            if (ref.target.empty()) {
                continue;  // sentinel (sketch axis/external) — not an authored target
            }
            if (liveTargets.find(ref.target) == liveTargets.end()) {
                anyDangling = true;
                if (danglingTarget.empty()) {
                    danglingTarget = ref.target;
                }
            }
            else {
                anySurviving = true;
            }
        }

        if (!anyDangling) {
            continue;  // Carry: geometry still determines it; nothing to decide.
        }

        if (anySurviving) {
            // A live participant survives; a satisfiable target remains but re-targeting
            // off the retired one is the user's choice.
            resolutions.push_back({RefResolution::Outcome::StopAsk,
                                   id,
                                   node.type,
                                   "a live reference survives; retargeting from retired "
                                       + danglingTarget + " is the user's choice"});
        }
        else {
            // No authored ref survives; the subject genuinely ceased to exist and nothing
            // remains to hold this node. Drop it, disclosing what retired.
            resolutions.push_back({RefResolution::Outcome::Drop,
                                   id,
                                   node.type,
                                   "no surviving reference; dropped (target " + danglingTarget
                                       + " retired)"});
            toDrop.push_back(id);
        }
    }

    for (const std::string& id : toDrop) {
        withRefs.erase(id);
    }

    return resolutions;
}

namespace
{

/// A dimension's authored value, or nullopt when the dimension is absent on that side. The
/// three-way rule is the same one `threeWay` applies to a whole node, only over one value:
/// both sides equal -> agree; one side unchanged from the ancestor -> the other decides; both
/// moved off the ancestor differently -> a conflict, keeping A's side provisionally (matching
/// object-granular). `conflict` is set to say which happened; the return is the resolved value.
std::optional<std::string> resolveDimension(const std::optional<std::string>& ancestor,
                                            const std::optional<std::string>& a,
                                            const std::optional<std::string>& b,
                                            bool& conflict)
{
    conflict = false;
    if (a == b) {
        return a;  // agree (both absent -> nullopt, both present and equal, or both added same)
    }
    if (a == ancestor) {
        return b;  // A unchanged from the ancestor -> B decides (its edit or its removal)
    }
    if (b == ancestor) {
        return a;  // B unchanged -> A decides
    }
    conflict = true;
    return a;  // both moved differently: provisionally keep A's side, as object-granular does
}

/// A single canonical string for a node's links, so a set of refs can be reconciled as one
/// dimension (a link edit is disjoint from a field edit; only two differing link sets clash).
std::string linksKey(const std::vector<RecipeRef>& refs)
{
    std::string key;
    for (const RecipeRef& ref : refs) {
        key += ref.target + "#" + std::to_string(ref.pos) + ";";
    }
    return key;
}

/// A readable stand-in for an optional dimension value in a conflict line.
std::string describeValue(const std::optional<std::string>& value)
{
    return value ? *value : std::string("(absent)");
}

}  // namespace

std::vector<MergeConflict> RecipeMerge::refineConflicts(const std::vector<MergeConflict>& conflicts,
                                                        const RecipeSection& base,
                                                        const RecipeSection& a,
                                                        const RecipeSection& b,
                                                        RecipeSection& merged)
{
    std::vector<MergeConflict> refined;

    for (const MergeConflict& conflict : conflicts) {
        // Only object-level Value conflicts refine to field granularity; a Referential conflict
        // is about identity across objects, not a field within one, so it passes through.
        if (conflict.kind != MergeConflict::Kind::Value) {
            refined.push_back(conflict);
            continue;
        }

        const auto itA = a.find(conflict.id);
        const auto itB = b.find(conflict.id);
        // A delete-vs-edit conflict keeps one side absent — there are not two field sets to
        // reconcile, so it stays a whole-object conflict.
        if (itA == a.end() || itB == b.end()) {
            refined.push_back(conflict);
            continue;
        }
        const RecipeNode& nodeA = itA->second;
        const RecipeNode& nodeB = itB->second;

        // An object added on both branches (both-added-differently) has no ancestor; treat the
        // base as an empty node of the same type, so a field only one side added reads as that
        // side's edit and a field both added with different values reads as a genuine clash.
        const auto itO = base.find(conflict.id);
        const RecipeNode emptyBase {conflict.id, nodeA.type, {}, {}};
        const RecipeNode& nodeO = itO != base.end() ? itO->second : emptyBase;

        const auto fieldOf = [](const RecipeNode& node,
                                const std::string& key) -> std::optional<std::string> {
            const auto it = node.fields.find(key);
            return it != node.fields.end() ? std::optional<std::string>(it->second) : std::nullopt;
        };

        RecipeNode result {conflict.id, nodeA.type, {}, {}};
        std::vector<MergeConflict> dimensionConflicts;

        // Type dimension. Normally the same on all three (an object's type is fixed at creation),
        // so this agrees silently; carried for completeness so a hand-built divergence still shows.
        bool typeConflict = false;
        const std::optional<std::string> resolvedType =
            resolveDimension(nodeO.type, nodeA.type, nodeB.type, typeConflict);
        result.type = resolvedType ? *resolvedType : nodeA.type;
        if (typeConflict) {
            dimensionConflicts.push_back({MergeConflict::Kind::Value,
                                          conflict.id,
                                          nodeA.type,
                                          "type: this branch " + nodeA.type + ", other branch "
                                              + nodeB.type});
        }

        // Links dimension — the whole set of refs, reconciled as one value.
        bool linksConflict = false;
        const std::string keyO = linksKey(nodeO.refs);
        const std::string keyA = linksKey(nodeA.refs);
        const std::string keyB = linksKey(nodeB.refs);
        const std::optional<std::string> resolvedLinks =
            resolveDimension(keyO, keyA, keyB, linksConflict);
        result.refs = (resolvedLinks == keyB) ? nodeB.refs : nodeA.refs;
        if (linksConflict) {
            dimensionConflicts.push_back({MergeConflict::Kind::Value,
                                          conflict.id,
                                          nodeA.type,
                                          "references: both branches changed the links "
                                          "differently"});
        }

        // Field dimensions — every authored field present on any of the three sides.
        std::set<std::string> fieldNames;
        for (const auto& [name, value] : nodeO.fields) {
            fieldNames.insert(name);
        }
        for (const auto& [name, value] : nodeA.fields) {
            fieldNames.insert(name);
        }
        for (const auto& [name, value] : nodeB.fields) {
            fieldNames.insert(name);
        }
        for (const std::string& name : fieldNames) {
            bool fieldConflict = false;
            const std::optional<std::string> resolved =
                resolveDimension(fieldOf(nodeO, name), fieldOf(nodeA, name), fieldOf(nodeB, name),
                                 fieldConflict);
            if (resolved) {
                result.fields[name] = *resolved;
            }
            if (fieldConflict) {
                dimensionConflicts.push_back(
                    {MergeConflict::Kind::Value,
                     conflict.id,
                     nodeA.type,
                     "field " + name + ": this branch " + describeValue(fieldOf(nodeA, name))
                         + ", other branch " + describeValue(fieldOf(nodeB, name))});
            }
        }

        // Rebuild the object's merged node so disjoint edits from both sides land (object-granular
        // had kept only A's whole node); clashing dimensions kept A's side above, as before. If no
        // dimension clashed the conflict has dissolved and nothing is reported for this object.
        merged[conflict.id] = result;
        for (const MergeConflict& dimensionConflict : dimensionConflicts) {
            refined.push_back(dimensionConflict);
        }
    }

    return refined;
}
