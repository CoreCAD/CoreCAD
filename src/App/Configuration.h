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

#pragma once

#include "DocumentObject.h"
#include "PropertyStandard.h"

namespace App
{

/** A document-level Variant configuration (ARCHITECTURE §7.7).
 *
 * Carries one configuration *input* (a name plus an ordered list of options)
 * and a per-option *Variant override map* of identity-bearing property values.
 * Switching the active option applies that option's overrides over the live
 * property values and recomputes; the document file is unchanged — the
 * configurations are alternate values, not alternate copies.
 *
 * POC scope (§7.7 "MVP Scope"): one Variant axis on one Body. Representations,
 * feature suppression, and the configuration-table UI are post-POC.
 */
class AppExport Configuration: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Configuration);

public:
    Configuration();
    ~Configuration() override = default;

    /// The configuration input's name, e.g. "Size".
    App::PropertyString InputName;
    /// Ordered option names for the input, e.g. ["Small", "Large"].
    App::PropertyStringList Options;
    /// The currently active option name.
    App::PropertyString ActiveOption;
    /** The per-option Variant override table.
     *
     * Each key is an option/target/property triple joined by the pipe
     * separator: "<option>|<objectName>|<propertyName>"; each value is the
     * serialized override value for that property under that option. Stored as
     * a flat PropertyMap so it round-trips natively. The separator must be an
     * XML-legal character: a control char such as 0x1f is silently stripped
     * when the PropertyMap serialises to the document XML, which corrupts the
     * keys and loses the table on reopen. '|' is safe because object and
     * property names are always identifiers (never contain '|'), and the
     * option is matched as a full prefix, so parsing stays unambiguous even if
     * an option name itself contains '|'.
     */
    App::PropertyMap Overrides;

    const char* getViewProviderName() const override;

protected:
    void onChanged(const App::Property* prop) override;

    /** Apply the active option's overrides over the live property values.
     *
     * For each Overrides entry under ActiveOption, the stored value is
     * evaluated and written into the target object's property. Authoring
     * convention (§7.7): every option — including the base — records a value
     * for each overridden property, so switching always restores cleanly.
     */
    void applyActiveOption();

    /// Separator used to join the Overrides map key fields. Must be XML-legal
    /// (a control char like 0x1f is stripped on serialise and corrupts the keys).
    static const char keySep = '|';
};

}  // namespace App
