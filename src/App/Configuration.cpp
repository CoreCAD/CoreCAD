// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                 *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#include <Base/Interpreter.h>

#include "Configuration.h"
#include "Document.h"

using namespace App;

PROPERTY_SOURCE(App::Configuration, App::DocumentObject)

Configuration::Configuration()
{
    ADD_PROPERTY_TYPE(InputName, (""), "Configuration", Prop_None, "Name of the configuration input");
    ADD_PROPERTY_TYPE(Options, (), "Configuration", Prop_None, "Ordered option names for the input");
    ADD_PROPERTY_TYPE(ActiveOption, (""), "Configuration", Prop_None, "Currently active option");
    ADD_PROPERTY_TYPE(Overrides, (), "Configuration", Prop_Hidden,
                      "Per-option Variant override table (option/object/property -> value)");
}

const char* Configuration::getViewProviderName() const
{
    return "Gui::ViewProviderDocumentObject";
}

void Configuration::onChanged(const App::Property* prop)
{
    // Switching the active option re-applies that option's overrides. Guard
    // against restore (the values are already in the file) and against the
    // pre-attachment construction phase (no document yet).
    if (prop == &ActiveOption && !isRestoring() && getDocument()) {
        applyActiveOption();
    }
    App::DocumentObject::onChanged(prop);
}

void Configuration::applyActiveOption()
{
    App::Document* doc = getDocument();
    if (!doc) {
        return;
    }

    std::string prefix = ActiveOption.getValue();
    if (prefix.empty()) {
        return;
    }
    prefix += keySep;

    for (const auto& entry : Overrides.getValues()) {
        const std::string& key = entry.first;
        if (key.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        // key tail is "<objectName>\x1f<propertyName>"
        std::string rest = key.substr(prefix.size());
        std::string::size_type sep = rest.find(keySep);
        if (sep == std::string::npos) {
            continue;
        }
        std::string objName = rest.substr(0, sep);
        std::string propName = rest.substr(sep + 1);

        App::DocumentObject* target = doc->getObject(objName.c_str());
        if (!target) {
            continue;
        }
        App::Property* targetProp = target->getPropertyByName(propName.c_str());
        if (!targetProp) {
            continue;
        }

        try {
            // Evaluate the stored value literal to a typed Python object, then
            // let the property apply it (units/type handled by setPyObject).
            Py::Object value = Base::Interpreter().runStringObject(entry.second.c_str());
            targetProp->setPyObject(value.ptr());
        }
        catch (Base::Exception& e) {
            Base::Console().warning("Configuration '%s': failed to apply override %s = %s: %s\n",
                                    getNameInDocument(), key.c_str(), entry.second.c_str(),
                                    e.what());
        }
    }
}
