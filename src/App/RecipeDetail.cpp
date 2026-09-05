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
