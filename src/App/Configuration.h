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
#include "PropertyOverrideTable.h"
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
     * Each entry names the option that states it, the object whose value it is and which of that
     * object's properties, and holds the value itself as a property of the kind it is for -- so a
     * length is stored as a length and read back by the reader that wrote it (§7.7, Amendment 18
     * Clause 18.5).
     *
     * It was a flat map of TEXT, with the three fields joined by a separator and the value stored
     * as source for the Python interpreter to turn back into a value. Both halves of that are
     * gone: the fields are their own fields, and a value never passes through a language.
     */
    App::PropertyOverrideTable Overrides;

    const char* getViewProviderName() const override;

    /** Block the objects whose values this configuration states and could not honour.
     *
     * Cruth (Amendment 19 Clause 19.6): a configuration sets values on OTHER objects from outside
     * the dependency graph, so an override that cannot be honoured has to block the object whose
     * value it would have set. Blocking only this holder would leave that part rebuilding at its
     * base value and reporting success under the name of the option that failed to apply.
     *
     * Checks without applying: a document is read with its values already in it, and the option
     * they were written under is the option it is still under.
     */
    void blockWhatItSetsElsewhere() override;

protected:
    void onChanged(const App::Property* prop) override;

    /// Release what this configuration states about other objects -- it is leaving the document.
    void unsetupObject() override;

    /** Apply the active option's overrides over the live property values.
     *
     * For each Overrides entry under ActiveOption, the stored value is
     * evaluated and written into the target object's property. Authoring
     * convention (§7.7): every option — including the base — records a value
     * for each overridden property, so switching always restores cleanly.
     */
    void applyActiveOption();

    /** Walk the active option's overrides, applying them or only asking whether they can be.
     *
     * One walk for both, because an override that is honoured when applied and refused when
     * merely read -- or the reverse -- would make a document's report of itself depend on which
     * of the two last happened.
     *
     * Whatever it cannot honour it states on the object whose value it would have set, or, where
     * this document holds no such object, on itself: nothing else is left to block.
     */
    void stateActiveOption(bool apply);
};

}  // namespace App
