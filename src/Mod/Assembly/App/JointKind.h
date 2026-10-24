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


#pragma once

#include <Mod/Assembly/AssemblyGlobal.h>


namespace Assembly
{

/**
 * The behavioural capabilities of one joint kind (Fixed, Revolute, …).
 *
 * The joint kinds differ today only in a set of capability flags — which of the
 * shared joint behaviours apply to them — rather than in genuinely divergent
 * algorithms. This is therefore modelled as a Type-Object-style value record
 * (one small immutable record per kind) rather than a polymorphic Strategy
 * hierarchy of near-empty classes. It is the single C++ source of truth for the
 * per-kind decisions that used to be scattered as `JointType in [...]`
 * membership tests in JointObject.py.
 *
 * The record is deliberately open to refactoring: if a kind ever needs a
 * genuinely divergent algorithm (not just a flag), this can grow a virtual seam
 * and become a Strategy without changing the call sites that ask a joint for its
 * kind.
 */
struct AssemblyExport JointKind
{
    /// Pre-position the moving part onto the mate before solving, so the solver
    /// does not land in a flipped or otherwise wrong branch.
    /// Kinds: Fixed, Revolute, Cylindrical, Slider, Ball.
    bool usesPreSolve = false;

    /// The solver cannot resolve the two coordinate systems when they are
    /// parallel; one is nudged out of parallel first.
    /// Kinds: Angle, Perpendicular.
    bool forbidsParallel = false;

    /// When resolving a reference to a joint coordinate system, ignore a picked
    /// vertex and use the edge/face frame instead.
    /// Kind: Distance.
    bool ignoresVertex = false;
};

/**
 * The capability record for a joint kind, selected by its JointType index (the
 * position within Assembly::Joint::JointTypeEnums). An out-of-range index yields
 * the all-false default record.
 */
AssemblyExport const JointKind& jointKindForType(int typeIndex);

}  // namespace Assembly
