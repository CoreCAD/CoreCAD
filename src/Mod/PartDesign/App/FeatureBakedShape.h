// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                 *
 *                                                                         *
 *   This file is part of the Cruth CAx development system,                *
 *   forked from FreeCAD.                                                   *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#pragma once

#include <Mod/Part/App/PropertyTopoShape.h>
#include "Feature.h"


namespace PartDesign
{

/** A frozen, self-contained solid feature (Cruth, §7 BakedShape).
 *
 * Unlike FeatureBase, which re-derives its shape from a BaseFeature link on
 * every recompute (a live link), BakedShape owns its geometry outright. The
 * captured solid — element map included — lives in the StoredShape property
 * and is emitted verbatim. There are no input links, so nothing upstream can
 * flow into it: it is severed from whatever produced it.
 *
 * This is what re-homes an instance broken out of a pattern into its own Body:
 * cutting the cord to the pattern/base is exactly the industry-standard
 * behaviour for breaking a copy out of a pattern.
 */
class PartDesignExport BakedShape: public PartDesign::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::BakedShape);

public:
    BakedShape();

    /// The frozen captured solid, stored with its element map.
    Part::PropertyPartShape StoredShape;

    short int mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;

    /// No upstream object: a BakedShape is severed from its origin.
    Part::Feature* getBaseObject(bool silent = false) const override;
};

}  // namespace PartDesign
