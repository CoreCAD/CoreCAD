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
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>
#endif

#include "Recipe.h"

using namespace App;

namespace
{

std::optional<RecipeNode> statedIn(const RecipeSection& section, const std::string& id)
{
    const auto it = section.find(id);
    return it != section.end() ? std::optional<RecipeNode>(it->second) : std::nullopt;
}

void recordStatements(MergeConflict& conflict,
                      const RecipeSection& base,
                      const RecipeSection& a,
                      const RecipeSection& b)
{
    conflict.ancestor = statedIn(base, conflict.id);
    conflict.branchA = statedIn(a, conflict.id);
    conflict.branchB = statedIn(b, conflict.id);
}

}  // namespace

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
            recordStatements(conflicts.back(), base, a, b);
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

std::vector<RecipeChange> RecipeDiff::twoWay(const RecipeSection& before,
                                             const RecipeSection& after)
{
    std::vector<RecipeChange> changes;

    std::set<std::string> ids;
    for (const auto& [id, node] : before) {
        ids.insert(id);
    }
    for (const auto& [id, node] : after) {
        ids.insert(id);
    }

    for (const std::string& id : ids) {
        const auto itOld = before.find(id);
        const auto itNew = after.find(id);

        if (itOld == before.end()) {
            changes.push_back({RecipeChange::Kind::Added, id, itNew->second.type});
            continue;
        }
        if (itNew == after.end()) {
            changes.push_back({RecipeChange::Kind::Removed, id, itOld->second.type});
            continue;
        }

        const RecipeNode& oldNode = itOld->second;
        const RecipeNode& newNode = itNew->second;
        if (oldNode == newNode) {
            continue;
        }

        if (oldNode.type != newNode.type) {
            changes.push_back({RecipeChange::Kind::TypeChanged,
                               id,
                               newNode.type,
                               {},
                               oldNode.type,
                               newNode.type});
        }

        std::set<std::string> fieldNames;
        for (const auto& [name, value] : oldNode.fields) {
            fieldNames.insert(name);
        }
        for (const auto& [name, value] : newNode.fields) {
            fieldNames.insert(name);
        }
        for (const std::string& name : fieldNames) {
            const auto valueOf = [&name](const RecipeNode& node) -> std::optional<std::string> {
                const auto it = node.fields.find(name);
                return it != node.fields.end() ? std::optional<std::string>(it->second)
                                               : std::nullopt;
            };
            const std::optional<std::string> oldValue = valueOf(oldNode);
            const std::optional<std::string> newValue = valueOf(newNode);
            if (oldValue != newValue) {
                changes.push_back({RecipeChange::Kind::FieldChanged,
                                   id,
                                   newNode.type,
                                   name,
                                   oldValue,
                                   newValue});
            }
        }

        if (oldNode.refs != newNode.refs) {
            RecipeChange change {RecipeChange::Kind::LinksChanged, id, newNode.type};
            change.refsBefore = oldNode.refs;
            change.refsAfter = newNode.refs;
            changes.push_back(change);
        }
    }

    return changes;
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

void RecipeMerge::presenceConflicts(const RecipeSection& baseRefs,
                                    const RecipeSection& aRefs,
                                    const RecipeSection& bRefs,
                                    const RecipeSection& baseTargets,
                                    const RecipeSection& aTargets,
                                    const RecipeSection& bTargets,
                                    const RecipeSection& merged,
                                    std::vector<MergeConflict>& conflicts)
{
    const struct
    {
        const RecipeSection& refs;
        const RecipeSection& otherTargets;
    } sides[] {{aRefs, bTargets}, {bRefs, aTargets}};

    for (const auto& [id, node] : merged) {
        const bool alreadyConflicted =
            std::any_of(conflicts.begin(), conflicts.end(), [&id = id](const MergeConflict& c) {
                return c.id == id;
            });
        if (alreadyConflicted) {
            continue;
        }
        for (const auto& side : sides) {
            const auto mine = side.refs.find(id);
            if (mine == side.refs.end()) {
                continue;
            }
            const auto ancestral = baseRefs.find(id);
            if (ancestral != baseRefs.end() && ancestral->second == mine->second) {
                continue;
            }
            const auto deleted = std::find_if(
                mine->second.refs.begin(), mine->second.refs.end(), [&](const RecipeRef& ref) {
                    return baseTargets.count(ref.target) != 0
                        && side.otherTargets.count(ref.target) == 0;
                });
            if (deleted != mine->second.refs.end()) {
                conflicts.push_back({MergeConflict::Kind::Referential,
                                     id,
                                     node.type,
                                     "points at " + deleted->target
                                         + ", which the other version deleted"});
                recordStatements(conflicts.back(), baseRefs, aRefs, bRefs);
                break;
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
        key += ref.target + "#" + std::to_string(ref.pos) + "#" + ref.element + ";";
    }
    return key;
}

std::vector<RecipeRef> refsOfLink(const RecipeNode& node, const std::string& link)
{
    std::vector<RecipeRef> refs;
    std::copy_if(node.refs.begin(), node.refs.end(), std::back_inserter(refs),
                 [&](const RecipeRef& ref) { return ref.link == link; });
    return refs;
}

/// A link whose order means nothing, merged one reference at a time: a reference stays unless a
/// side removed it, and one either side added joins it. Membership cannot clash.
std::vector<RecipeRef> mergeAsSet(const std::vector<RecipeRef>& base,
                                  const std::vector<RecipeRef>& a,
                                  const std::vector<RecipeRef>& b)
{
    const auto holds = [](const std::vector<RecipeRef>& refs, const RecipeRef& ref) {
        return std::find(refs.begin(), refs.end(), ref) != refs.end();
    };
    std::vector<RecipeRef> merged;
    for (const RecipeRef& ref : a) {
        if (holds(b, ref) || !holds(base, ref)) {
            merged.push_back(ref);
        }
    }
    for (const RecipeRef& ref : b) {
        if (!holds(a, ref) && !holds(base, ref)) {
            merged.push_back(ref);
        }
    }
    return merged;
}

/// A link whose order may mean something, both sides having changed it. Membership is merged as
/// for a set; each pair of references keeps the order a side that changed it gave them, the
/// ancestor's where neither did. The result stands only where exactly one sequence keeps every
/// such order: none (the sides contradict) or several (nothing places two references against
/// each other, as two inserted at one place) is a conflict, nullopt.
std::optional<std::vector<RecipeRef>> mergeAsSequence(const std::vector<RecipeRef>& base,
                                                      const std::vector<RecipeRef>& a,
                                                      const std::vector<RecipeRef>& b)
{
    const std::vector<RecipeRef> members = mergeAsSet(base, a, b);
    const std::size_t count = members.size();

    // Whether `first` comes before `second` in a sequence, or nullopt where it lacks either.
    const auto precedes = [](const std::vector<RecipeRef>& sequence,
                             const RecipeRef& first,
                             const RecipeRef& second) -> std::optional<bool> {
        const auto x = std::find(sequence.begin(), sequence.end(), first);
        const auto y = std::find(sequence.begin(), sequence.end(), second);
        if (x == sequence.end() || y == sequence.end()) {
            return std::nullopt;
        }
        return x < y;
    };

    std::vector<std::vector<bool>> mustPrecede(count, std::vector<bool>(count, false));
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            const std::optional<bool> inBase = precedes(base, members[i], members[j]);
            const std::optional<bool> inA = precedes(a, members[i], members[j]);
            const std::optional<bool> inB = precedes(b, members[i], members[j]);
            const bool aChanged = inA && inA != inBase;
            const bool bChanged = inB && inB != inBase;
            std::optional<bool> order = inBase;
            if (aChanged && bChanged && inA != inB) {
                return std::nullopt;
            }
            if (aChanged) {
                order = inA;
            }
            else if (bChanged) {
                order = inB;
            }
            if (order) {
                mustPrecede[*order ? i : j][*order ? j : i] = true;
            }
        }
    }

    std::vector<std::size_t> waitingOn(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < count; ++j) {
            waitingOn[j] += mustPrecede[i][j] ? 1 : 0;
        }
    }
    std::vector<RecipeRef> sequence;
    std::vector<bool> placed(count, false);
    while (sequence.size() < count) {
        std::optional<std::size_t> next;
        for (std::size_t i = 0; i < count; ++i) {
            if (!placed[i] && waitingOn[i] == 0) {
                if (next) {
                    return std::nullopt;  // two could come next: more than one sequence
                }
                next = i;
            }
        }
        if (!next) {
            return std::nullopt;  // every one waits on another: no sequence
        }
        placed[*next] = true;
        sequence.push_back(members[*next]);
        for (std::size_t j = 0; j < count; ++j) {
            waitingOn[j] -= mustPrecede[*next][j] ? 1 : 0;
        }
    }
    return sequence;
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

        // Links dimension — each link on its own. A link whose order means nothing takes each
        // side's added and removed references; any other link is one value.
        std::set<std::string> linkNames;
        for (const RecipeNode* node : {&nodeO, &nodeA, &nodeB}) {
            for (const RecipeRef& ref : node->refs) {
                linkNames.insert(ref.link);
            }
        }
        for (const std::string& link : linkNames) {
            const std::vector<RecipeRef> refsO = refsOfLink(nodeO, link);
            const std::vector<RecipeRef> refsA = refsOfLink(nodeA, link);
            const std::vector<RecipeRef> refsB = refsOfLink(nodeB, link);
            std::vector<RecipeRef> resolvedRefs;
            if (nodeA.unorderedLinks.count(link) != 0 && nodeB.unorderedLinks.count(link) != 0) {
                resolvedRefs = mergeAsSet(refsO, refsA, refsB);
                std::set<std::string> targets;
                for (const RecipeRef& ref : resolvedRefs) {
                    targets.insert(ref.target);
                }
                if (targets.size() > 1 && nodeA.oneObjectLinks.count(link) != 0) {
                    // One side moved the link to another object while the other changed its picks
                    // on the first; the link holds one object, so both cannot stand.
                    resolvedRefs = refsA;
                    dimensionConflicts.push_back(
                        {MergeConflict::Kind::Value,
                         conflict.id,
                         nodeA.type,
                         "references in " + link
                             + ": one branch moved it to another object, the other changed it"});
                }
            }
            else {
                bool linksConflict = false;
                const std::string keyB = linksKey(refsB);
                const std::optional<std::string> resolvedLinks =
                    resolveDimension(linksKey(refsO), linksKey(refsA), keyB, linksConflict);
                resolvedRefs = (resolvedLinks == keyB) ? refsB : refsA;
                // A named link's references each carry an identity; a node's unnamed list (a
                // constraint's element places) is one value.
                std::optional<std::vector<RecipeRef>> sequence;
                if (linksConflict && !link.empty()) {
                    sequence = mergeAsSequence(refsO, refsA, refsB);
                }
                if (sequence) {
                    resolvedRefs = *sequence;
                }
                else if (linksConflict) {
                    dimensionConflicts.push_back(
                        {MergeConflict::Kind::Value,
                         conflict.id,
                         nodeA.type,
                         "references" + (link.empty() ? std::string() : " in " + link)
                             + ": both branches changed the links differently"});
                }
            }
            result.refs.insert(result.refs.end(), resolvedRefs.begin(), resolvedRefs.end());
        }
        result.unorderedLinks = nodeA.unorderedLinks;
        result.oneObjectLinks = nodeA.oneObjectLinks;

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
        for (MergeConflict& dimensionConflict : dimensionConflicts) {
            recordStatements(dimensionConflict, base, a, b);
            refined.push_back(dimensionConflict);
        }
    }

    return refined;
}
