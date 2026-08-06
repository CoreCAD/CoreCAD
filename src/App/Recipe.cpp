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
#include <set>
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
            conflicts.push_back({MergeConflict::Kind::Value, id, "diverging edit to the same entity"});
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
                                     "references deleted entity " + ref.target});
            }
        }
    }
}
