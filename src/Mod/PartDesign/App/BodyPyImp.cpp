// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#include "Mod/PartDesign/App/Body.h"
#include "Mod/PartDesign/App/Feature.h"

// inclusion of the generated files (generated out of ItemPy.xml)
#include "BodyPy.h"
#include "BodyPy.cpp"

using namespace PartDesign;

// returns a string which represents the object e.g. when printed in python
std::string BodyPy::representation() const
{
    return {"<body object>"};
}


PyObject* BodyPy::getCustomAttributes(const char* /*attr*/) const
{
    return nullptr;
}

int BodyPy::setCustomAttributes(const char* /*attr*/, PyObject* /*obj*/)
{
    return 0;
}

PyObject* BodyPy::insertObject(PyObject* args)
{
    PyObject* featurePy;
    PyObject* targetPy;
    PyObject* afterPy = Py_False;
    if (!PyArg_ParseTuple(
            args,
            "O!O|O!",
            &(App::DocumentObjectPy::Type),
            &featurePy,
            &targetPy,
            &PyBool_Type,
            &afterPy
        )) {
        return nullptr;
    }

    App::DocumentObject* feature
        = static_cast<App::DocumentObjectPy*>(featurePy)->getDocumentObjectPtr();
    App::DocumentObject* target = nullptr;
    if (PyObject_TypeCheck(targetPy, &(App::DocumentObjectPy::Type))) {
        target = static_cast<App::DocumentObjectPy*>(targetPy)->getDocumentObjectPtr();
    }

    if (!Body::isAllowed(feature)) {
        PyErr_SetString(
            PyExc_SystemError,
            "Only PartDesign features, datum features and sketches can be inserted into a Body"
        );
        return nullptr;
    }

    bool after = Base::asBoolean(afterPy);
    Body* body = this->getBodyPtr();

    try {
        body->insertObject(feature, target, after);
    }
    catch (Base::Exception& e) {
        PyErr_SetString(PyExc_SystemError, e.what());
        return nullptr;
    }

    Py_Return;
}

PyObject* BodyPy::addFeature(PyObject* args)
{
    PyObject* featurePy;
    if (!PyArg_ParseTuple(args, "O!", &(App::DocumentObjectPy::Type), &featurePy)) {
        return nullptr;
    }

    App::DocumentObject* feature
        = static_cast<App::DocumentObjectPy*>(featurePy)->getDocumentObjectPtr();

    if (!Body::isAllowed(feature)) {
        PyErr_SetString(
            PyExc_SystemError,
            "Only PartDesign features, datum features and sketches can be added to a Body"
        );
        return nullptr;
    }

    try {
        this->getBodyPtr()->addFeature(feature);
    }
    catch (Base::Exception& e) {
        PyErr_SetString(PyExc_SystemError, e.what());
        return nullptr;
    }

    return feature->getPyObject();
}

PyObject* BodyPy::removeFeature(PyObject* args)
{
    PyObject* featurePy;
    if (!PyArg_ParseTuple(args, "O!", &(App::DocumentObjectPy::Type), &featurePy)) {
        return nullptr;
    }

    App::DocumentObject* feature
        = static_cast<App::DocumentObjectPy*>(featurePy)->getDocumentObjectPtr();

    try {
        this->getBodyPtr()->removeFeature(feature);
    }
    catch (Base::Exception& e) {
        PyErr_SetString(PyExc_SystemError, e.what());
        return nullptr;
    }

    Py_Return;
}

static bool collectFeatures(PyObject* listPy, std::vector<App::DocumentObject*>& out)
{
    Py::Sequence seq(listPy);
    for (const auto& item : seq) {
        PyObject* obj = item.ptr();
        if (!PyObject_TypeCheck(obj, &(App::DocumentObjectPy::Type))) {
            PyErr_SetString(PyExc_TypeError, "Expected a list of document objects");
            return false;
        }
        App::DocumentObject* feature = static_cast<App::DocumentObjectPy*>(obj)->getDocumentObjectPtr();
        if (!Body::isAllowed(feature)) {
            PyErr_SetString(
                PyExc_SystemError,
                "Only PartDesign features, datum features and sketches can be added to a Body"
            );
            return false;
        }
        out.push_back(feature);
    }
    return true;
}

PyObject* BodyPy::addFeatures(PyObject* args)
{
    PyObject* listPy;
    if (!PyArg_ParseTuple(args, "O", &listPy)) {
        return nullptr;
    }
    std::vector<App::DocumentObject*> features;
    if (!collectFeatures(listPy, features)) {
        return nullptr;
    }
    try {
        this->getBodyPtr()->addFeatures(features);
    }
    catch (Base::Exception& e) {
        PyErr_SetString(PyExc_SystemError, e.what());
        return nullptr;
    }
    Py_Return;
}

PyObject* BodyPy::removeFeatures(PyObject* args)
{
    PyObject* listPy;
    if (!PyArg_ParseTuple(args, "O", &listPy)) {
        return nullptr;
    }
    std::vector<App::DocumentObject*> features;
    if (!collectFeatures(listPy, features)) {
        return nullptr;
    }
    try {
        this->getBodyPtr()->removeFeatures(features);
    }
    catch (Base::Exception& e) {
        PyErr_SetString(PyExc_SystemError, e.what());
        return nullptr;
    }
    Py_Return;
}

PyObject* BodyPy::breakOutInstance(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return nullptr;
    }

    Body* newBody = Body::breakOutInstance(this->getBodyPtr());
    if (!newBody) {
        Py_RETURN_NONE;
    }
    return newBody->getPyObject();
}

Py::Object BodyPy::getVisibleFeature() const
{
    // Derived membership (§9.1-inverse): the Group is empty under de-ownership, so read the
    // Body's features through getFullModel, which computes them from the feature graph.
    for (auto obj : getBodyPtr()->getFullModel()) {
        if (obj->Visibility.getValue() && obj->isDerivedFrom<PartDesign::Feature>()) {
            return Py::Object(obj->getPyObject(), true);
        }
    }
    return Py::Object();
}
