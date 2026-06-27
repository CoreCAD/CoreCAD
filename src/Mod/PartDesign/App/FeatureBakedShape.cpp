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


#include "FeatureBakedShape.h"


namespace PartDesign
{


PROPERTY_SOURCE(PartDesign::BakedShape, PartDesign::Feature)

BakedShape::BakedShape()
{
    ADD_PROPERTY_TYPE(
        StoredShape,
        (Part::TopoShape()),
        "BakedShape",
        (App::PropertyType)(App::Prop_Hidden),
        "The frozen captured solid (with element map) that this feature emits"
    );
}

short int BakedShape::mustExecute() const
{
    // Frozen: nothing flows in, so the only reason to execute is an explicit
    // change to the stored geometry.
    if (StoredShape.isTouched()) {
        return 1;
    }
    return PartDesign::Feature::mustExecute();
}

App::DocumentObjectExecReturn* BakedShape::execute()
{
    Part::TopoShape shape = StoredShape.getShape();
    if (shape.isNull()) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "BakedShape has no stored shape")
        );
    }

    Shape.setValue(shape);
    return StdReturn;
}

Part::Feature* BakedShape::getBaseObject(bool /*silent*/) const
{
    // Severed from its origin: a BakedShape has no base feature.
    return nullptr;
}

}  // namespace PartDesign
