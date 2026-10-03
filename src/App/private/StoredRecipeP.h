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

#pragma once

#include <string>

#include <Base/Persistence.h>
#include <Base/ServiceProvider.h>

#include "../DocumentObject.h"
#include "../Services.h"

namespace App
{

/// The non-bulk file in a source-store entry: the property's own element, naming its side files.
constexpr const char* assetContentFile = "value.xml";

/// ` name="value"`, escaped. Every attribute goes through here: much of it is a person's own text.
inline std::string attribute(const char* name, const std::string& value)
{
    return std::string(" ") + name + "=\"" + Base::Persistence::encodeAttribute(value) + "\"";
}

/// One end of a reference: the target's durable id, and the part of it that was picked.
/// The reference binds by the id; `sub` ("Face6") only rides along.
struct Binding
{
    std::string uuid;
    std::string sub;
    bool external {false};  ///< the target lives in another document
    bool noPart {false};    ///< named with no part at all; written with no `sub`, not `sub=""`
};

/// The container holding an object's chosen appearance, or null when this session has none
/// (headless). Only the view layer knows where it lives, so the file asks.
inline PropertyContainer* appearanceOf(const DocumentObject& obj)
{
    auto* display = Base::provideService<DisplayStateProvider>();
    return display != nullptr ? display->appearanceOf(obj) : nullptr;
}

}  // namespace App
