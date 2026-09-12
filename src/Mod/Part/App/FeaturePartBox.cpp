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

#include <BRepPrimAPI_MakeBox.hxx>
#include <Precision.hxx>


#include <Base/Reader.h>

#include "FeaturePartBox.h"


using namespace Part;

PROPERTY_SOURCE(Part::Box, Part::Primitive)


Box::Box()
{
    ADD_PROPERTY_TYPE(Length, (10.0f), "Box", App::Prop_None, "The length of the box");
    ADD_PROPERTY_TYPE(Width, (10.0f), "Box", App::Prop_None, "The width of the box");
    ADD_PROPERTY_TYPE(Height, (10.0f), "Box", App::Prop_None, "The height of the box");
    // The shape-source capability is composed by the Part::ShapeFeature base; Box
    // only overrides getSubObject below to route the query through it.
}

App::DocumentObject* Box::getSubObject(
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* mat,
    bool transform,
    int depth
) const
{
    // Bypass the inherited Part::ShapeFeature override and let the App base
    // dispatch the shape-source query to the composed Part::ShapeExtension.
    return App::DocumentObject::getSubObject(subname, pyObj, mat, transform, depth);
}

short Box::mustExecute() const
{
    if (Length.isTouched() || Height.isTouched() || Width.isTouched()) {
        return 1;
    }
    return Primitive::mustExecute();
}

App::DocumentObjectExecReturn* Box::execute()
{
    double L = Length.getValue();
    double W = Width.getValue();
    double H = Height.getValue();

    if (L < Precision::Confusion()) {
        return new App::DocumentObjectExecReturn("Length of box too small");
    }

    if (W < Precision::Confusion()) {
        return new App::DocumentObjectExecReturn("Width of box too small");
    }

    if (H < Precision::Confusion()) {
        return new App::DocumentObjectExecReturn("Height of box too small");
    }

    try {
        // Build a box using the dimension attributes
        BRepPrimAPI_MakeBox mkBox(L, W, H);
        TopoDS_Shape ResultShape = mkBox.Shape();
        this->Shape.setValue(ResultShape, false);
        return Primitive::execute();
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
}

/**
 * This method was added for backward-compatibility. In former versions
 * of Box we had the properties x,y,z and l,h,w which have changed to
 * Location -- as replacement for x,y and z and Length, Height and Width.
 */
Box::OlderWords& Box::olderWords()
{
    if (!_olderWords) {
        _olderWords = std::make_unique<OlderWords>();
        // The direction the box was built along before a placement said it.
        _olderWords->axis.setValue(0.0F, 0.0F, 1.0F);
    }
    return *_olderWords;
}

bool Box::restoreLegacy(Base::XMLReader& reader, const char* TypeName, App::Property& into)
{
    // Older files name the type without its module: "PropertyDistance", not "App::PropertyDistance".
    std::string stated = TypeName;
    if (stated == "PropertyDistance") {
        stated = "App::" + stated;
    }
    if (into.getTypeId().getName() != stated) {
        return false;
    }
    into.Restore(reader);
    return true;
}

void Box::handleChangedPropertyName(Base::XMLReader& reader, const char* TypeName, const char* PropName)
{
    // In case this comes from an old document we must use the new properties. Width and height
    // were the wrong way round when these names were written, and are read back that way.
    if (strcmp(PropName, "l") == 0) {
        olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().length);
        return;
    }
    if (strcmp(PropName, "w") == 0) {  // by mistake w was considered as height
        olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().height);
        return;
    }
    if (strcmp(PropName, "h") == 0) {  // by mistake h was considered as width
        olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().width);
        return;
    }
    if (strcmp(PropName, "x") == 0) {
        olderWords().positionXyz |= restoreLegacy(reader, TypeName, olderWords().x);
        return;
    }
    if (strcmp(PropName, "y") == 0) {
        olderWords().positionXyz |= restoreLegacy(reader, TypeName, olderWords().y);
        return;
    }
    if (strcmp(PropName, "z") == 0) {
        olderWords().positionXyz |= restoreLegacy(reader, TypeName, olderWords().z);
        return;
    }
    if (strcmp(PropName, "Axis") == 0) {
        olderWords().positionAxis |= restoreLegacy(reader, TypeName, olderWords().axis);
        return;
    }
    if (strcmp(PropName, "Location") == 0) {
        olderWords().positionAxis |= restoreLegacy(reader, TypeName, olderWords().location);
        return;
    }

    Part::Primitive::handleChangedPropertyName(reader, TypeName, PropName);
}

void Box::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop)
{
    // The sizes kept their names and changed their type. Read into the older kind and converted
    // with the rest, so a file that mixes the old names with the new ones arrives the same way.
    if (strcmp(TypeName, "PropertyDistance") == 0) {
        if (prop == &this->Length) {
            olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().length);
            return;
        }
        if (prop == &this->Height) {
            olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().height);
            return;
        }
        if (prop == &this->Width) {
            olderWords().sizes |= restoreLegacy(reader, TypeName, olderWords().width);
            return;
        }
    }

    Part::Primitive::handleChangedPropertyType(reader, TypeName, prop);
}

void Box::Restore(Base::XMLReader& reader)
{
    // A read is answered from the file in front of it, never from what an earlier one left here.
    _olderWords.reset();

    Part::Primitive::Restore(reader);

    if (!_olderWords) {
        return;
    }

    if (_olderWords->sizes) {
        this->Length.setValue(_olderWords->length.getValue());
        this->Height.setValue(_olderWords->height.getValue());
        this->Width.setValue(_olderWords->width.getValue());
    }

    Base::Placement plm;
    // for 0.7 releases or earlier
    if (_olderWords->positionXyz) {
        plm.setPosition(
            Base::Vector3d(_olderWords->x.getValue(), _olderWords->y.getValue(), _olderWords->z.getValue())
        );
        this->Placement.setValue(this->Placement.getValue() * plm);
        this->Shape.setStatus(App::Property::User1, true);  // override the shape's location later on
    }
    // for 0.8 releases
    else if (_olderWords->positionAxis) {
        Base::Vector3d d = _olderWords->axis.getValue();
        Base::Vector3d p = _olderWords->location.getValue();
        Base::Rotation rot(Base::Vector3d(0.0, 0.0, 1.0), Base::Vector3d(d.x, d.y, d.z));
        plm.setRotation(rot);
        plm.setPosition(Base::Vector3d(p.x, p.y, p.z));
        this->Placement.setValue(this->Placement.getValue() * plm);
        this->Shape.setStatus(App::Property::User1, true);  // override the shape's location later on
    }

    _olderWords.reset();
}

void Box::onChanged(const App::Property* prop)
{
    if (prop == &Length || prop == &Width || prop == &Height) {
        if (!isRestoring()) {
            App::DocumentObjectExecReturn* ret = recompute();
            delete ret;
        }
    }
    else if (prop == &this->Shape) {
        // see Box::Restore
        if (this->Shape.testStatus(App::Property::User1)) {
            this->Shape.setStatus(App::Property::User1, false);
            App::DocumentObjectExecReturn* ret = recompute();
            delete ret;
            return;
        }
    }
    Part::Primitive::onChanged(prop);
}
