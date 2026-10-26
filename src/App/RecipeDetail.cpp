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
# include <map>
#endif

#include "RecipeDetail.h"

#include "DocumentObject.h"

using namespace App;

namespace
{

std::map<Base::Type, RecipeDetailProvider>& providers()
{
    static std::map<Base::Type, RecipeDetailProvider> registry;
    return registry;
}

std::map<Base::Type, RecipeConsequenceProvider>& consequenceProviders()
{
    static std::map<Base::Type, RecipeConsequenceProvider> registry;
    return registry;
}

std::map<Base::Type, RegisteredContentMerge>& contentMergers()
{
    static std::map<Base::Type, RegisteredContentMerge> registry;
    return registry;
}

std::vector<MergeRebaser>& rebasers()
{
    static std::vector<MergeRebaser> registry;
    return registry;
}

}  // namespace

void App::registerRecipeDetail(Base::Type type, RecipeDetailProvider provider)
{
    providers()[type] = std::move(provider);
}

RecipeDetail App::recipeDetail(const DocumentObject& obj)
{
    // Nearest registered ancestor wins, so a module can claim a whole family with one provider
    // and a subtype can still override it.
    for (Base::Type type = obj.getTypeId(); !type.isBad(); type = type.getParent()) {
        const auto found = providers().find(type);
        if (found != providers().end() && found->second) {
            return found->second(obj);
        }
    }

    return {};
}

void App::registerRecipeConsequences(Base::Type type, RecipeConsequenceProvider provider)
{
    consequenceProviders()[type] = std::move(provider);
}

RecipeConsequences App::recipeConsequences(const DocumentObject& before,
                                           const DocumentObject& after)
{
    for (Base::Type type = after.getTypeId(); !type.isBad(); type = type.getParent()) {
        const auto found = consequenceProviders().find(type);
        if (found != consequenceProviders().end() && found->second) {
            return found->second(before, after);
        }
    }

    return {};
}

void App::registerRecipeMerge(Base::Type type,
                              std::vector<std::string> coveredProperties,
                              RecipeContentMerger merger,
                              RecipeContentApplier applier)
{
    contentMergers()[type] = {std::move(coveredProperties), std::move(merger), std::move(applier)};
}

const RegisteredContentMerge* App::recipeMerger(const DocumentObject& obj)
{
    for (Base::Type type = obj.getTypeId(); !type.isBad(); type = type.getParent()) {
        const auto found = contentMergers().find(type);
        if (found != contentMergers().end() && found->second.merge) {
            return &found->second;
        }
    }

    return nullptr;
}

void App::registerMergeRebase(MergeRebaser rebaser)
{
    rebasers().push_back(std::move(rebaser));
}

const std::vector<App::MergeRebaser>& App::mergeRebasers()
{
    return rebasers();
}
