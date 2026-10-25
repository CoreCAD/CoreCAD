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

#pragma once

#include <string>
#include <vector>

#include <boost/uuid/uuid.hpp>

#include <App/Recipe.h>

namespace Part
{
class Geometry;
}

namespace Sketcher
{
class SketchObject;
}

/// Helpers the sketch recipe's writer, merge and view share.
namespace Sketcher::SketchRecipeInternal
{

std::string tagToString(const boost::uuids::uuid& tag);

const Part::Geometry* findSeedGeometry(
    const std::string& tag,
    const std::vector<const SketchObject*>& seedSources
);

std::string readableGeometryType(const std::string& typeName);

void addAuthoredCoordinates(const Part::Geometry* geo, App::RecipeNode& node);

struct Borrowed
{
    int geoId;
    std::string id;
    const Part::Geometry* geometry;
};

std::vector<Borrowed> borrowedGeometry(const SketchObject& sketch);

std::string nounFor(const std::string& type);

}  // namespace Sketcher::SketchRecipeInternal
