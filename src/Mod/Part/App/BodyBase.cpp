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


#include <App/Document.h>

#include "BodyBase.h"
#include "BodyBasePy.h"


namespace Part
{

PROPERTY_SOURCE(Part::BodyBase, Part::Feature)

BodyBase::BodyBase()
{
    ADD_PROPERTY(Tip, (nullptr));
    Tip.setScope(App::LinkScope::Child);

    ADD_PROPERTY(BaseFeature, (nullptr));
}

BodyBase* BodyBase::findBodyOf(const App::DocumentObject* f)
{
    App::Document* doc = f->getDocument();
    if (doc) {
        std::vector<App::DocumentObject*> bodies = doc->getObjectsOfType(BodyBase::getClassTypeId());
        for (auto it : bodies) {
            BodyBase* body = static_cast<BodyBase*>(it);
            // De-owned Bodies keep no Group (Cruth §11 step 5e); membership is the derived
            // member list, resolved through the virtual getFullModel() so a PartDesign Body
            // uses its BaseFeature-chain reverse lookup.
            std::vector<App::DocumentObject*> model = body->getFullModel();
            if (std::ranges::find(model, f) != model.end()) {
                return body;
            }
        }
    }

    return nullptr;
}

bool BodyBase::isAfter(const App::DocumentObject* feature, const App::DocumentObject* target) const
{
    assert(feature);

    if (feature == target) {
        return false;
    }

    // De-owned Bodies keep no Group (Cruth §11 step 5e); order the members by the derived
    // member list (getFullModel is solids-first in build order) instead of Group position.
    const std::vector<App::DocumentObject*> features = const_cast<BodyBase*>(this)->getFullModel();

    if (!target || target == BaseFeature.getValue()) {
        return std::ranges::find(features, feature) != features.end();
    }

    const auto featureIt = std::ranges::find(features, feature);
    const auto targetIt = std::ranges::find(features, target);

    return featureIt == features.end() ? false : featureIt > targetIt;
}

void BodyBase::onBeforeChange(const App::Property* prop)
{

    // Tip can't point outside the body, hence no base feature tip
    /*// If we are changing the base feature and tip point to it reset it
    if ( prop == &BaseFeature && BaseFeature.getValue() == Tip.getValue() && BaseFeature.getValue()
    ) { Tip.setValue( nullptr );
    }*/
    Part::Feature::onBeforeChange(prop);
}

void BodyBase::onChanged(const App::Property* prop)
{
    // Tip can't point outside the body, hence no base feature tip
    /*// If the tip is zero and we are adding a base feature to the body set it to be the tip
    if ( prop == &BaseFeature && !Tip.getValue() && BaseFeature.getValue() ) {
        Tip.setValue( BaseFeature.getValue () );
    }*/
    Part::Feature::onChanged(prop);
}

PyObject* BodyBase::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new BodyBasePy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

}  // namespace Part
