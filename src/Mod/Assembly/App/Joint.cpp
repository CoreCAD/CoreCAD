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

#include <App/FeaturePythonPyImp.h>

#include "Joint.h"
#include "JointPy.h"

using namespace Assembly;


const char* Joint::JointTypeEnums[] = {
    "Fixed",
    "Revolute",
    "Cylindrical",
    "Slider",
    "Ball",
    "Distance",
    "Parallel",
    "Perpendicular",
    "Angle",
    "RackPinion",
    "Screw",
    "Gears",
    "Belt",
    nullptr
};


PROPERTY_SOURCE(Assembly::Joint, App::DocumentObject)


Joint::Joint()
{
    ADD_PROPERTY_TYPE(JointType, (long(0)), "Joint", App::Prop_None, "The type of the joint");
    JointType.setEnums(JointTypeEnums);

    // First joint connector.
    ADD_PROPERTY_TYPE(
        Reference1,
        (nullptr),
        "Joint Connector 1",
        App::Prop_None,
        "The first reference of the joint"
    );
    ADD_PROPERTY_TYPE(
        Placement1,
        (Base::Placement()),
        "Joint Connector 1",
        App::Prop_None,
        "The local coordinate system within Reference1's object used for the joint"
    );
    ADD_PROPERTY_TYPE(
        Detach1,
        (false),
        "Joint Connector 1",
        App::Prop_None,
        "Prevents Placement1 from recomputing, enabling custom positioning"
    );
    ADD_PROPERTY_TYPE(
        Offset1,
        (Base::Placement()),
        "Joint Connector 1",
        App::Prop_None,
        "The attachment offset of the first connector of the joint"
    );

    // Second joint connector.
    ADD_PROPERTY_TYPE(
        Reference2,
        (nullptr),
        "Joint Connector 2",
        App::Prop_None,
        "The second reference of the joint"
    );
    ADD_PROPERTY_TYPE(
        Placement2,
        (Base::Placement()),
        "Joint Connector 2",
        App::Prop_None,
        "The local coordinate system within Reference2's object used for the joint"
    );
    ADD_PROPERTY_TYPE(
        Detach2,
        (false),
        "Joint Connector 2",
        App::Prop_None,
        "Prevents Placement2 from recomputing, enabling custom positioning"
    );
    ADD_PROPERTY_TYPE(
        Offset2,
        (Base::Placement()),
        "Joint Connector 2",
        App::Prop_None,
        "The attachment offset of the second connector of the joint"
    );

    // Kind-specific parameters.
    ADD_PROPERTY_TYPE(
        Angle,
        (0.0),
        "Joint",
        App::Prop_None,
        "The angle of the joint. Used only by the Angle joint"
    );
    ADD_PROPERTY_TYPE(
        Distance,
        (0.0),
        "Joint",
        App::Prop_None,
        "The distance of the joint. Used by Distance, RackPinion (pitch radius), "
        "Screw, Gears and Belt (radius1)"
    );
    ADD_PROPERTY_TYPE(
        Distance2,
        (0.0),
        "Joint",
        App::Prop_None,
        "The second distance of the joint. Used by the Gears joint (second radius)"
    );
    Distance.enableNegative(true);
    Distance2.enableNegative(true);

    // Motion limits.
    ADD_PROPERTY_TYPE(
        EnableLengthMin,
        (false),
        "Limits",
        App::Prop_None,
        "Enable the minimum length limit of the joint"
    );
    ADD_PROPERTY_TYPE(
        EnableLengthMax,
        (false),
        "Limits",
        App::Prop_None,
        "Enable the maximum length limit of the joint"
    );
    ADD_PROPERTY_TYPE(
        EnableAngleMin,
        (false),
        "Limits",
        App::Prop_None,
        "Enable the minimum angle limit of the joint"
    );
    ADD_PROPERTY_TYPE(
        EnableAngleMax,
        (false),
        "Limits",
        App::Prop_None,
        "Enable the maximum angle limit of the joint"
    );
    ADD_PROPERTY_TYPE(
        LengthMin,
        (0.0),
        "Limits",
        App::Prop_None,
        "The minimum limit for the length between both coordinate systems (along their z-axis)"
    );
    ADD_PROPERTY_TYPE(
        LengthMax,
        (0.0),
        "Limits",
        App::Prop_None,
        "The maximum limit for the length between both coordinate systems (along their z-axis)"
    );
    ADD_PROPERTY_TYPE(
        AngleMin,
        (0.0),
        "Limits",
        App::Prop_None,
        "The minimum limit for the angle between both coordinate systems (between their x-axis)"
    );
    ADD_PROPERTY_TYPE(
        AngleMax,
        (0.0),
        "Limits",
        App::Prop_None,
        "The maximum limit for the angle between both coordinate systems (between their x-axis)"
    );
    LengthMin.enableNegative(true);
    LengthMax.enableNegative(true);

    App::SuppressibleExtension::initExtension(this);
}

Joint::~Joint() = default;

App::DocumentObjectExecReturn* Joint::execute()
{
    // The solve is driven at the AssemblyObject level, which reads this joint's
    // properties by name. Per-joint execute (joint-coordinate-system geometry)
    // is ported in a later stage; for now recompute is a no-op.
    return App::DocumentObject::StdReturn;
}

PyObject* Joint::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new JointPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

// Transitional Python-proxy variant (#59): typed Joint data + Python behaviour.
namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Assembly::JointPython, Assembly::Joint)
template<>
const char* Assembly::JointPython::getViewProviderName() const
{
    return "Gui::ViewProviderFeaturePython";
}
template<>
PyObject* Assembly::JointPython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<Assembly::JointPy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class AssemblyExport FeaturePythonT<Assembly::Joint>;
}  // namespace App
