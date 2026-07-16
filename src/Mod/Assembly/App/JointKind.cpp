// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
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

#include "JointKind.h"

using namespace Assembly;

namespace
{
// One row per joint kind, in the exact order of Assembly::Joint::JointTypeEnums:
//   0 Fixed  1 Revolute  2 Cylindrical  3 Slider  4 Ball  5 Distance
//   6 Parallel  7 Perpendicular  8 Angle  9 RackPinion  10 Screw
//   11 Gears  12 Belt
// Columns: {usesPreSolve, forbidsParallel, ignoresVertex}.
constexpr JointKind kindTable[] = {
    /* Fixed         */ {true, false, false},
    /* Revolute      */ {true, false, false},
    /* Cylindrical   */ {true, false, false},
    /* Slider        */ {true, false, false},
    /* Ball          */ {true, false, false},
    /* Distance      */ {false, false, true},
    /* Parallel      */ {false, false, false},
    /* Perpendicular */ {false, true, false},
    /* Angle         */ {false, true, false},
    /* RackPinion    */ {false, false, false},
    /* Screw         */ {false, false, false},
    /* Gears         */ {false, false, false},
    /* Belt          */ {false, false, false},
};

constexpr int kindCount = static_cast<int>(sizeof(kindTable) / sizeof(kindTable[0]));
static_assert(kindCount == 13, "kindTable must stay in sync with Joint::JointTypeEnums");

constexpr JointKind defaultKind {};
}  // namespace

const JointKind& Assembly::jointKindForType(int typeIndex)
{
    if (typeIndex < 0 || typeIndex >= kindCount) {
        return defaultKind;
    }
    return kindTable[typeIndex];
}
