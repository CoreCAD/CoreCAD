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

/// The one file inside a source-store entry that is not bulk: the property's own element, naming
/// what sits beside it.
constexpr const char* assetContentFile = "value.xml";

/// ` name="value"`, with the value made safe to stand inside the quotes.
///
/// Every attribute this form writes goes through here. Much of what it writes is a person's own
/// text -- a property's tooltip, its group, the name of a picked part -- and that can hold any
/// character. Measured before this: one `&` in a tooltip saved without complaint and the document
/// then refused to open at all (#138).
inline std::string attribute(const char* name, const std::string& value)
{
    return std::string(" ") + name + "=\"" + Base::Persistence::encodeAttribute(value) + "\"";
}

/// One end of a reference: the target's durable id, and the part of it that was picked.
///
/// A sub-element string ("Face6") names a position in a computed shape, not an identity, so it
/// can never be what the reference BINDS by — but leaving it out would lose which face a sketch
/// was drawn on. It rides alongside the durable id, exactly as the document archive already
/// carries a name and a position side by side.
struct Binding
{
    std::string uuid;
    std::string sub;
    bool external {false};  ///< the target lives in another document
    bool noPart {false};    ///< named with no part at all; written with no `sub`, not `sub=""`
};

/// The container holding an object's chosen appearance, or null when this session has none.
///
/// A colour a person chose is authored content and belongs in the file of record, not in the
/// deletable project cache -- but only the view layer knows where that state lives, so the file
/// asks for it rather than reaching for it. A headless session gets no answer and writes no
/// appearance, which is honest: it chose none.
inline PropertyContainer* appearanceOf(const DocumentObject& obj)
{
    auto* display = Base::provideService<DisplayStateProvider>();
    return display != nullptr ? display->appearanceOf(obj) : nullptr;
}

}  // namespace App
