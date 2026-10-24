// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2014 Yorik van Havre <yorik@uncreated.net>              *
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

#include <App/DocumentObject.h>
#include <App/GeoFeature.h>
#include <App/PlacementExtension.h>
#include <App/FeaturePython.h>

#include "PropertyPath.h"


namespace Path
{

class PathExport Feature: public App::GeoFeature, public App::PlacementExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Path::Feature);

public:
    /// Constructor
    Feature();
    ~Feature() override;

    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override
    {
        return "PathGui::ViewProviderPath";
    }
    App::DocumentObjectExecReturn* execute() override
    {
        return App::DocumentObject::StdReturn;
    }
    short mustExecute() const override;
    PyObject* getPyObject() override;

    PropertyPath Path;

    /// The toolpath is the OUTPUT of the operation that generated it: the stock, the tool and
    /// the parameters are the source, and the cutting moves are what they produce. It used to be
    /// kept as though a person had authored it, because the rule looked at the property's class
    /// and only a geometric one could be output (Amendment 18 Clause 18.2).
    ///
    /// Path::Feature itself is the plain holder -- its execute does nothing, so a toolpath parked
    /// on one is a toolpath nothing will produce again, exactly as Part::Feature is for shapes.
    /// The operations CAM really builds are scripted subclasses, and a script that rebuilds says
    /// so through FeaturePythonT.
    bool producesContentOf(const App::Property& prop) const override
    {
        return &prop == &Path && getTypeId() != Feature::getClassTypeId();
    }

protected:
    /// get called by the container when a property has changed
    void onChanged(const App::Property* prop) override;
};

using FeaturePython = App::FeaturePythonT<Feature>;

}  // namespace Path
