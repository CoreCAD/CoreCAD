// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <string>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Interpreter.h>

namespace tests
{

/// A container with its own placement that owns what it holds (a geo-feature group). App::Part
/// used to be the stock example; the type is retired, so tests that exercise geo-feature-group
/// behaviour build the same thing from the generic extensions.
inline App::DocumentObject* addPlacedGroup(App::Document* doc, const char* name = "Part")
{
    auto* grp = doc->addObject("App::GeometryPython", name);
    const std::string cmd = std::string("_grp = App.getDocument('") + doc->getName()
        + "').getObject('" + grp->getNameInDocument()
        + "')\n"
          "_grp.addExtension('App::GeoFeatureGroupExtensionPython')\n"
          "_grp.addExtension('App::PlacementExtensionPython')\n"
          "del _grp\n";
    Base::Interpreter().runString(cmd.c_str());
    return grp;
}

}  // namespace tests
