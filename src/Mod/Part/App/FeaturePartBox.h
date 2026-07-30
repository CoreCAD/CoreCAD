// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
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

#include <App/PropertyStandard.h>

#include <Mod/Part/PartGlobal.h>

#include "PrimitiveFeature.h"
#include "ShapeExtension.h"


namespace Part
{

/** First concrete feature to carry its shape as a composed capability (#79).
 *
 * Box mixes in Part::ShapeExtension (the same pattern Cylinder uses for
 * PrismExtension) and routes its getSubObject through the extension rather than
 * the inherited ShapeFeature override, proving the write/ownership side of
 * Amendment 17: a stored-backed feature can source its element-mapped shape
 * from the capability with byte-identical results, transform path included.
 */
class PartExport Box: public Part::Primitive, public Part::ShapeExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Box);

public:
    Box();

    App::PropertyLength Length, Height, Width;


    /** @name methods override feature */
    //@{
    /// recalculate the Feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderBox";
    }

    /// Route the shape-source query through the composed Part::ShapeExtension
    /// (App::DocumentObject::getSubObject dispatches to it) instead of the
    /// inherited Part::ShapeFeature override.
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* mat,
        bool transform,
        int depth
    ) const override;

protected:
    void Restore(Base::XMLReader& reader) override;
    /// get called by the container when a property has changed
    void onChanged(const App::Property* prop) override;
    //@}
};

}  // namespace Part
