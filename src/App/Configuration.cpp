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

#include <cctype>

#include "Configuration.h"
#include "Document.h"

using namespace App;

namespace
{
/// The file's own words for a value, folded onto one line so a report can carry them.
///
/// A person who has to correct an override needs to be told WHAT was stated, not only that it
/// would not read; the words are how they find it in the file.
std::string inOneLine(const std::string& words)
{
    std::string folded;
    bool space = false;
    for (const char character : words) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            space = !folded.empty();
            continue;
        }
        if (space) {
            folded += ' ';
            space = false;
        }
        folded += character;
    }
    return folded;
}
}  // namespace


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
    // Switching the active option re-applies that option's overrides, and so does correcting the
    // table under it -- which is the way back out of an override this build could not honour.
    // Guard against restore (the values are already in the file) and against the pre-attachment
    // construction phase (no document yet).
    if ((prop == &ActiveOption || prop == &Overrides) && !isRestoring() && getDocument()) {
        applyActiveOption();
    }
    App::DocumentObject::onChanged(prop);
}

void Configuration::unsetupObject()
{
    // What is no longer stated no longer blocks. A configuration leaving the document takes its
    // refusals with it, or a part would go on reporting itself blocked by an option that is gone.
    App::Document* doc = getDocument();
    const char* name = getNameInDocument();
    if (doc != nullptr && name != nullptr) {
        for (App::DocumentObject* obj : doc->getObjects()) {
            if (obj != nullptr) {
                obj->forgetStatementsSetFrom(name);
            }
        }
        doc->recordWhatIsBlocked();
    }
    App::DocumentObject::unsetupObject();
}

void Configuration::blockWhatItSetsElsewhere()
{
    stateActiveOption(false);
}

void Configuration::applyActiveOption()
{
    stateActiveOption(true);
    if (App::Document* doc = getDocument()) {
        // Said the moment it is known. An option applied in a running session is the same failure
        // as one found on reading the file, and waiting for a rebuild to disclose it would wait
        // for a rebuild that a part whose geometry is already in hand is never asked for.
        doc->recordWhatIsBlocked();
    }
}

void Configuration::stateActiveOption(bool apply)
{
    App::Document* doc = getDocument();
    const char* holderName = getNameInDocument();
    if (!doc || holderName == nullptr) {
        return;
    }
    const std::string holder = holderName;

    // This pass states what cannot be honoured NOW, so what was stated before goes first --
    // including on objects this option no longer names at all.
    for (App::DocumentObject* obj : doc->getObjects()) {
        if (obj != nullptr) {
            obj->forgetStatementsSetFrom(holder);
        }
    }

    const std::string option = ActiveOption.getValue();
    if (option.empty()) {
        return;
    }

    for (const auto& [address, stated] : Overrides.getValues()) {
        if (address.option != option) {
            continue;
        }

        App::DocumentObject* target = doc->getObject(address.object.c_str());
        if (!target) {
            // The object whose value it would have set is not here, so there is nothing else to
            // block and the holder is all that is left. Named with the object it was looking for:
            // a report that will not say what is missing cannot be acted on (§3.6).
            rememberStatementSetFromElsewhere(
                {holder,
                 address.object + "." + address.property,
                 "option '" + option + "' sets it and this document holds no object '"
                     + address.object + "'"});
            continue;
        }
        App::Property* targetProp = target->getPropertyByName(address.property.c_str());
        if (!targetProp) {
            target->rememberStatementSetFromElsewhere(
                {holder,
                 address.property,
                 "option '" + option + "' sets it and this build has no property of that name"});
            continue;
        }

        // Said whether or not it is applied. A stored value that cannot be read is a value the
        // option never set, and a session that only read the file would otherwise report the part
        // finished at a value nobody chose until somebody happened to switch the option.
        if (!stated.value) {
            const std::string words = inOneLine(stated.words);
            target->rememberStatementSetFromElsewhere(
                {holder,
                 address.property,
                 "option '" + option + "' sets it to " + (words.empty() ? "a value" : words)
                     + ", which is not a value this build can read: " + stated.reason});
            continue;
        }

        // The kinds must be the same kind. A value authored for one kind of property and pasted
        // into another is not a conversion, it is a guess -- and the whole point of storing the
        // value as the property it is for is that nothing has to guess what it meant.
        if (stated.value->getTypeId() != targetProp->getTypeId()) {
            target->rememberStatementSetFromElsewhere(
                {holder,
                 address.property,
                 "option '" + option + "' states it as a '" + stated.type
                     + "' and it is a '" + targetProp->getTypeId().getName() + "'"});
            continue;
        }
        if (!apply) {
            continue;
        }

        try {
            // The property takes its own value back, by the same route undo and redo use.
            targetProp->Paste(*stated.value);
        }
        catch (Base::Exception& e) {
            target->rememberStatementSetFromElsewhere(
                {holder,
                 address.property,
                 "option '" + option + "' sets it to a value it would not take: " + e.what()});
        }
    }
}
