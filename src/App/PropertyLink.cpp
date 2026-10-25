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

#include <optional>
#include <QDir>
#include <QFileInfo>
#include <boost/algorithm/string/predicate.hpp>
#include <array>
#include <zipios++/zipinputstream.h>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>
#include <Base/Tools.h>
#include <Base/Uuid.h>

#include "PropertyLinks.h"
#include "PropertyLinksDetail.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentObjectPy.h"
#include "DocumentObserver.h"
#include "ObjectIdentifier.h"
#include "ElementNamingUtils.h"
#include "GeoFeature.h"


FC_LOG_LEVEL_INIT("PropertyLinks", true, true)

using namespace App;
using namespace Base;
using namespace std;
namespace sp = std::placeholders;
using namespace App::PropertyLinksDetail;

//**************************************************************************
//**************************************************************************
// PropertyLink
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyLink, App::PropertyLinkBase)
TYPESYSTEM_SOURCE(App::PropertyLinkChild, App::PropertyLink)
TYPESYSTEM_SOURCE(App::PropertyLinkGlobal, App::PropertyLink)
TYPESYSTEM_SOURCE(App::PropertyLinkHidden, App::PropertyLink)

//**************************************************************************
// Construction/Destruction


PropertyLink::PropertyLink() = default;

PropertyLink::~PropertyLink()
{
    resetLink();
}

//**************************************************************************
// Base class implementer

void PropertyLink::resetLink()
{
    // in case this property gets dynamically removed
    // maintain the back link in the DocumentObject class if it is from a document object
    if (_pcScope != LinkScope::Hidden && _pcLink && getContainer()
        && getContainer()->isDerivedFrom<App::DocumentObject>()) {
        App::DocumentObject* parent = static_cast<DocumentObject*>(getContainer());
        // before accessing internals make sure the object is not about to be destroyed
        // otherwise the backlink contains dangling pointers
        if (!parent->testStatus(ObjectStatus::Destroy)) {
            if (_pcLink) {
                _pcLink->_removeBackLink(parent);
            }
        }
    }
    _pcLink = nullptr;
}

bool PropertyLink::statesWhereItPoints(std::vector<Pointing>& pointing) const
{
    if (_pcLink != nullptr) {
        pointing.push_back({_pcLink, {}, /*noPart=*/true});
    }
    return true;
}

bool PropertyLink::pointAt(const std::vector<Pointing>& pointing)
{
    // A plain link points at an object and cannot say which part of one. Refusing is the honest
    // answer: setting the object and dropping the part would keep a reference nobody authored.
    for (const Pointing& one : pointing) {
        if (!one.sub.empty()) {
            return false;
        }
    }
    setValue(pointing.empty() ? nullptr : pointing.front().target);
    return true;
}

void PropertyLink::setValue(App::DocumentObject* lValue)
{
    auto parent = dynamic_cast<App::DocumentObject*>(getContainer());
    if (!testFlag(LinkAllowExternal) && parent && lValue
        && parent->getDocument() != lValue->getDocument()) {
        throw Base::ValueError("PropertyLink does not support external object");
    }

    aboutToSetValue();

    // maintain the back link in the DocumentObject class if it is from a document object
    if (_pcScope != LinkScope::Hidden && parent) {
        // before accessing internals make sure the object is not about to be destroyed
        // otherwise the backlink contains dangling pointers
        if (!parent->testStatus(ObjectStatus::Destroy)) {
            if (_pcLink) {
                _pcLink->_removeBackLink(parent);
            }
            if (lValue) {
                lValue->_addBackLink(parent);
            }
        }
    }

    _pcLink = lValue;
    hasSetValue();
}

App::DocumentObject* PropertyLink::getValue() const
{
    return _pcLink;
}

App::DocumentObject* PropertyLink::getValue(Base::Type t) const
{
    return (_pcLink && _pcLink->isDerivedFrom(t)) ? _pcLink : nullptr;
}

PyObject* PropertyLink::getPyObject()
{
    if (_pcLink) {
        return _pcLink->getPyObject();
    }
    else {
        Py_Return;
    }
}

void PropertyLink::setPyObject(PyObject* value)
{
    Base::PyTypeCheck(&value, &DocumentObjectPy::Type);
    if (value) {
        DocumentObjectPy* pcObject = static_cast<DocumentObjectPy*>(value);
        setValue(pcObject->getDocumentObjectPtr());
    }
    else {
        setValue(nullptr);
    }
}

void PropertyLink::Save(Base::Writer& writer) const
{
    // The durable binding is the target's UUID (Amendment 3, Clause 3.2); the
    // name is written as display/diagnostic and as a resolution fallback.
    writer.Stream() << writer.ind() << "<Link value=\"" << (_pcLink ? _pcLink->getExportName() : "")
                    << "\" uuid=\"" << (_pcLink ? _pcLink->Uid.getValueStr() : "") << "\"/>"
                    << std::endl;
}

void PropertyLink::Restore(Base::XMLReader& reader)
{
    // read my element
    reader.readElement("Link");
    // get the value of my attribute
    std::string name = reader.getName(reader.getAttribute<const char*>("value"));
    std::string uuid = reader.getAttribute<const char*>("uuid", "");

    // Property not in a DocumentObject!
    assert(getContainer()->isDerivedFrom<App::DocumentObject>());

    if (!name.empty() || !uuid.empty()) {
        DocumentObject* parent = static_cast<DocumentObject*>(getContainer());

        App::Document* document = parent->getDocument();
        // Resolve by UUID first (durable binding); fall back to the name only
        // when no UUID was stored (forward interop, not legacy migration).
        DocumentObject* object = nullptr;
        if (document && !uuid.empty()) {
            Base::Uuid targetUuid;
            targetUuid.setValue(uuid);
            object = document->getObjectByUuid(targetUuid);
        }
        if (!object && document && !name.empty()) {
            object = document->getObject(name.c_str());
        }
        if (!object) {
            if (reader.isVerbose()) {
                Base::Console().warning("Lost link to '%s' while loading, maybe "
                                        "an object was not loaded correctly\n",
                                        name.c_str());
            }
        }
        else if (parent == object) {
            if (reader.isVerbose()) {
                Base::Console().warning("Object '%s' links to itself, nullify it\n", name.c_str());
            }
            object = nullptr;
        }

        setValue(object);
    }
    else {
        setValue(nullptr);
    }
}

Property* PropertyLink::Copy() const
{
    PropertyLink* p = new PropertyLink();
    p->_pcLink = _pcLink;
    return p;
}

void PropertyLink::Paste(const Property& from)
{
    if (!from.isDerivedFrom<PropertyLink>()) {
        throw Base::TypeError("Incompatible property to paste to");
    }

    setValue(static_cast<const PropertyLink&>(from)._pcLink);
}

void PropertyLink::getLinks(std::vector<App::DocumentObject*>& objs,
                            bool all,
                            std::vector<std::string>* subs,
                            bool newStyle) const
{
    (void)newStyle;
    (void)subs;
    if ((all || _pcScope != LinkScope::Hidden) && _pcLink && _pcLink->isAttachedToDocument()) {
        objs.push_back(_pcLink);
    }
}

void PropertyLink::getLinksTo(std::vector<App::ObjectIdentifier>& identifiers,
                              App::DocumentObject* obj,
                              const char* subname,
                              bool all) const
{
    (void)subname;
    if (!all && _pcScope == LinkScope::Hidden) {
        return;  // Don't get hidden links unless all is specified.
    }
    if (obj && _pcLink == obj) {
        identifiers.emplace_back(*this);
    }
}

void PropertyLink::breakLink(App::DocumentObject* obj, bool clear)
{
    if (_pcLink == obj || (clear && getContainer() == obj)) {
        setValue(nullptr);
    }
}

bool PropertyLink::adjustLink(const std::set<App::DocumentObject*>& inList)
{
    (void)inList;
    return false;
}

Property* PropertyLink::CopyOnLinkReplace(const App::DocumentObject* parent,
                                          App::DocumentObject* oldObj,
                                          App::DocumentObject* newObj) const
{
    auto res = tryReplaceLink(getContainer(), _pcLink, parent, oldObj, newObj);
    if (res.first) {
        auto p = new PropertyLink();
        p->_pcLink = res.first;
        return p;
    }
    return nullptr;
}
