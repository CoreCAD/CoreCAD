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
// PropertyXLinkSubList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyXLinkSubList, App::PropertyLinkBase)

//**************************************************************************
// Construction/Destruction


PropertyXLinkSubList::PropertyXLinkSubList()
{
    _pcScope = LinkScope::Global;
    setSyncSubObject(true);
}

PropertyXLinkSubList::~PropertyXLinkSubList() = default;

void PropertyXLinkSubList::setSyncSubObject(bool enable)
{
    _Flags.set((std::size_t)LinkSyncSubObject, enable);
}

int PropertyXLinkSubList::getSize() const
{
    return static_cast<int>(_Links.size());
}

void PropertyXLinkSubList::setValue(DocumentObject* lValue, const char* SubName)
{
    std::map<DocumentObject*, std::vector<std::string>> values;
    if (lValue) {
        auto& subs = values[lValue];
        if (SubName) {
            subs.emplace_back(SubName);
        }
    }
    setValues(std::move(values));
}

bool PropertyXLinkSubList::statesWhereItPoints(std::vector<Pointing>& pointing) const
{
    for (const PropertyXLinkSub& link : _Links) {
        DocumentObject* target = link.getValue();
        if (target == nullptr || !target->isAttachedToDocument()) {
            continue;
        }
        const std::vector<std::string>& subs = link.getSubValues();
        if (subs.empty()) {
            pointing.push_back({target, {}, /*noPart=*/true});
            continue;
        }
        for (std::size_t i = 0; i < subs.size(); ++i) {
            pointing.push_back(
                {target, subs[i], false, fullNameOf(target, link.getShadowSubs(), i)});
        }
    }
    return true;
}

bool PropertyXLinkSubList::pointAt(const std::vector<Pointing>& pointing)
{
    // Gathered per target, because this property holds the parts of one object together rather
    // than as a flat list of pairs, and in the order the targets were stated.
    struct Picked
    {
        DocumentObject* target;
        std::vector<std::string> subs;
        std::vector<ShadowSub> shadows;
    };
    std::vector<Picked> picked;
    for (const Pointing& one : pointing) {
        if (one.target == nullptr || !one.target->isAttachedToDocument()) {
            FC_THROWM(Base::ValueError, "invalid document object");
        }
        auto it = std::find_if(picked.begin(), picked.end(), [&](const Picked& p) {
            return p.target == one.target;
        });
        if (it == picked.end()) {
            it = picked.insert(picked.end(), {one.target, {}, {}});
        }
        if (!one.noPart) {
            it->subs.push_back(one.sub);
            it->shadows.push_back({one.name, one.name.empty() ? std::string() : one.sub});
        }
    }
    atomic_change guard(*this);
    _Links.clear();
    for (Picked& one : picked) {
        _Links.emplace_back(testFlag(LinkAllowPartial), this);
        _Links.back().setValue(one.target, std::move(one.subs), std::move(one.shadows));
    }
    guard.tryInvoke();
    return true;
}

void PropertyXLinkSubList::setValues(const std::vector<DocumentObject*>& lValue,
                                     const std::vector<const char*>& lSubNames)
{
#define CHECK_SUB_SIZE(_l, _r)                                                                     \
    do {                                                                                           \
        if (_l.size() != _r.size())                                                                \
            FC_THROWM(Base::ValueError, "object and subname size mismatch");                       \
    } while (0)
    CHECK_SUB_SIZE(lValue, lSubNames);
    std::map<DocumentObject*, std::vector<std::string>> values;
    int i = 0;
    for (auto& obj : lValue) {
        const char* sub = lSubNames[i++];
        if (sub) {
            values[obj].emplace_back(sub);
        }
    }
    setValues(std::move(values));
}

void PropertyXLinkSubList::setValues(const std::vector<DocumentObject*>& lValue,
                                     const std::vector<std::string>& lSubNames)
{
    CHECK_SUB_SIZE(lValue, lSubNames);
    std::map<DocumentObject*, std::vector<std::string>> values;
    int i = 0;
    for (auto& obj : lValue) {
        values[obj].push_back(lSubNames[i++]);
    }
    setValues(std::move(values));
}

void PropertyXLinkSubList::setSubListValues(const std::vector<PropertyLinkSubList::SubSet>& svalues)
{
    std::map<DocumentObject*, std::vector<std::string>> values;
    for (auto& v : svalues) {
        auto& s = values[v.first];
        s.reserve(s.size() + v.second.size());
        s.insert(s.end(), v.second.begin(), v.second.end());
    }
    setValues(std::move(values));
}

void PropertyXLinkSubList::setValues(
    const std::map<App::DocumentObject*, std::vector<std::string>>& values)
{
    setValues(std::map<App::DocumentObject*, std::vector<std::string>>(values));
}

void PropertyXLinkSubList::setValues(
    std::map<App::DocumentObject*, std::vector<std::string>>&& values)
{
    for (auto& v : values) {
        if (!v.first || !v.first->isAttachedToDocument()) {
            FC_THROWM(Base::ValueError, "invalid document object");
        }
    }

    atomic_change guard(*this);

    for (auto it = _Links.begin(), itNext = it; it != _Links.end(); it = itNext) {
        ++itNext;
        auto iter = values.find(it->getValue());
        if (iter == values.end()) {
            _Links.erase(it);
            continue;
        }
        it->setSubValues(std::move(iter->second));
        values.erase(iter);
    }

    for (auto& v : values) {
        _Links.emplace_back(testFlag(LinkAllowPartial), this);
        _Links.back().setValue(v.first, std::move(v.second));
    }
    guard.tryInvoke();
}

void PropertyXLinkSubList::addValue(App::DocumentObject* obj,
                                    const std::vector<std::string>& subs,
                                    bool reset)
{
    addValue(obj, std::vector<std::string>(subs), reset);
}

void PropertyXLinkSubList::addValue(App::DocumentObject* obj,
                                    std::vector<std::string>&& subs,
                                    bool reset)
{

    if (!obj || !obj->isAttachedToDocument()) {
        FC_THROWM(Base::ValueError, "invalid document object");
    }

    for (auto& l : _Links) {
        if (l.getValue() == obj) {
            auto s = l.getSubValues();
            if (s.empty() || reset) {
                l.setSubValues(std::move(subs));
            }
            else {
                s.reserve(s.size() + subs.size());
                std::move(subs.begin(), subs.end(), std::back_inserter(s));
                l.setSubValues(std::move(s));
            }
            return;
        }
    }
    atomic_change guard(*this);
    _Links.emplace_back(testFlag(LinkAllowPartial), this);
    _Links.back().setValue(obj, std::move(subs));
    guard.tryInvoke();
}

void PropertyXLinkSubList::setValue(DocumentObject* lValue, const std::vector<std::string>& SubList)
{
    std::map<DocumentObject*, std::vector<std::string>> values;
    if (lValue) {
        values[lValue] = SubList;
    }
    setValues(std::move(values));
}

void PropertyXLinkSubList::setValues(const std::vector<DocumentObject*>& values)
{
    atomic_change guard(*this);
    _Links.clear();
    for (auto obj : values) {
        _Links.emplace_back(testFlag(LinkAllowPartial), this);
        _Links.back().setValue(obj);
    }
    guard.tryInvoke();
}

void PropertyXLinkSubList::set1Value(int idx,
                                     DocumentObject* value,
                                     const std::vector<std::string>& SubList)
{
    if (idx < -1 || idx > getSize()) {
        throw Base::RuntimeError("index out of bound");
    }

    if (idx < 0 || idx + 1 == getSize()) {
        if (SubList.empty()) {
            addValue(value, SubList);
            return;
        }
        atomic_change guard(*this);
        _Links.emplace_back(testFlag(LinkAllowPartial), this);
        _Links.back().setValue(value);
        guard.tryInvoke();
        return;
    }

    auto it = _Links.begin();
    for (; idx; --idx) {
        ++it;
    }
    it->setValue(value, SubList);
}

const string PropertyXLinkSubList::getPyReprString() const
{
    if (_Links.empty()) {
        return std::string("None");
    }
    std::ostringstream ss;
    ss << '[';
    for (auto& link : _Links) {
        auto obj = link.getValue();
        if (!obj || !obj->isAttachedToDocument()) {
            continue;
        }
        ss << "(App.getDocument('" << obj->getDocument()->getName() << "').getObject('"
           << obj->getNameInDocument() << "'),  (";
        const auto& subs = link.getSubValues();
        if (subs.empty()) {
            ss << "''";
        }
        else {
            for (auto& sub : subs) {
                ss << "'" << sub << "',";
            }
        }
        ss << ")), ";
    }
    ss << ']';
    return ss.str();
}

DocumentObject* PropertyXLinkSubList::getValue() const
{
    if (!_Links.empty()) {
        return _Links.begin()->getValue();
    }
    return nullptr;
}

int PropertyXLinkSubList::removeValue(App::DocumentObject* lValue)
{
    atomic_change guard(*this, false);
    int ret = 0;
    for (auto it = _Links.begin(); it != _Links.end();) {
        if (it->getValue() != lValue) {
            ++it;
        }
        else {
            guard.aboutToChange();
            it = _Links.erase(it);
            ++ret;
        }
    }
    guard.tryInvoke();
    return ret;
}

PyObject* PropertyXLinkSubList::getPyObject()
{
    Py::List list;
    for (auto& link : _Links) {
        auto obj = link.getValue();
        if (!obj || !obj->isAttachedToDocument()) {
            continue;
        }

        Py::Tuple tup(2);
        tup[0] = Py::asObject(obj->getPyObject());

        const auto& subs = link.getSubValues();
        Py::Tuple items(subs.size());
        for (std::size_t j = 0; j < subs.size(); j++) {
            items[j] = Py::String(subs[j]);
        }
        tup[1] = items;
        list.append(tup);
    }
    return Py::new_reference_to(list);
}

void PropertyXLinkSubList::setPyObject(PyObject* value)
{
    try {  // try PropertyLinkSub syntax
        PropertyLinkSub dummy;
        dummy.setAllowExternal(true);
        dummy.setPyObject(value);
        this->setValue(dummy.getValue(), dummy.getSubValues());
        return;
    }
    catch (Base::Exception&) {
    }

    if (!PyTuple_Check(value) && !PyList_Check(value)) {
        throw Base::TypeError(
            "Invalid type. Accepts (DocumentObject, (subname...)) or sequence of such type.");
    }
    Py::Sequence seq(value);
    std::map<DocumentObject*, std::vector<std::string>> values;
    try {
        for (Py_ssize_t i = 0; i < seq.size(); ++i) {
            PropertyLinkSub link;
            link.setAllowExternal(true);
            link.setPyObject(seq[i].ptr());
            const auto& subs = link.getSubValues();
            auto& s = values[link.getValue()];
            s.reserve(s.size() + subs.size());
            s.insert(s.end(), subs.begin(), subs.end());
        }
    }
    catch (Base::Exception&) {
        throw Base::TypeError(
            "Invalid type inside sequence. Must be type of (DocumentObject, (subname...))");
    }
    setValues(std::move(values));
}

void PropertyXLinkSubList::afterRestore()
{
    for (auto& l : _Links) {
        l.afterRestore();
    }
}

void PropertyXLinkSubList::onContainerRestored()
{
    for (auto& l : _Links) {
        l.onContainerRestored();
    }
}

void PropertyXLinkSubList::updateElementReference(DocumentObject* feature,
                                                  bool reverse,
                                                  bool notify)
{
    for (auto& l : _Links) {
        l.updateElementReference(feature, reverse, notify);
    }
}

bool PropertyXLinkSubList::referenceChanged() const
{
    for (auto& l : _Links) {
        if (l.referenceChanged()) {
            return true;
        }
    }
    return false;
}

void PropertyXLinkSubList::Save(Base::Writer& writer) const
{
    // Cruth: the links are stated, never counted -- see the note in PropertyXLink::Save.
    writer.Stream() << writer.ind() << "<XLinkSubList";
    if (testFlag(LinkAllowPartial)) {
        writer.Stream() << " partial=\"1\"";
    }
    writer.Stream() << ">" << endl;
    writer.incInd();
    for (auto& l : _Links) {
        l.Save(writer);
    }
    writer.decInd();
    writer.Stream() << writer.ind() << "</XLinkSubList>" << endl;
}

void PropertyXLinkSubList::Restore(Base::XMLReader& reader)
{
    reader.readElement("XLinkSubList");
    setFlag(LinkAllowPartial,
            reader.hasAttribute("partial") && reader.getAttribute<bool>("partial"));
    const int list = reader.level();
    atomic_change guard(*this, false);
    _Links.clear();
    while (nextChildElement(reader, list)) {
        expectElement(reader, "XLink");
        _Links.emplace_back(false, this);
        _Links.back().restoreFromCurrentElement(reader);
    }
    guard.tryInvoke();
}

Property*
PropertyXLinkSubList::CopyOnImportExternal(const std::map<std::string, std::string>& nameMap) const
{
    std::unique_ptr<Property> copy;
    auto it = _Links.begin();
    for (; it != _Links.end(); ++it) {
        copy.reset(it->CopyOnImportExternal(nameMap));
        if (copy) {
            break;
        }
    }
    if (!copy) {
        return nullptr;
    }
    std::unique_ptr<PropertyXLinkSubList> p(new PropertyXLinkSubList);
    for (auto iter = _Links.begin(); iter != it; ++iter) {
        p->_Links.emplace_back();
        iter->copyTo(p->_Links.back());
    }
    p->_Links.emplace_back();
    static_cast<PropertyXLinkSub&>(*copy).copyTo(p->_Links.back());
    for (++it; it != _Links.end(); ++it) {
        p->_Links.emplace_back();
        copy.reset(it->CopyOnImportExternal(nameMap));
        if (copy) {
            static_cast<PropertyXLinkSub&>(*copy).copyTo(p->_Links.back());
        }
        else {
            it->copyTo(p->_Links.back());
        }
    }
    return p.release();
}

Property* PropertyXLinkSubList::CopyOnLabelChange(App::DocumentObject* obj,
                                                  const std::string& ref,
                                                  const char* newLabel) const
{
    std::unique_ptr<Property> copy;
    auto it = _Links.begin();
    for (; it != _Links.end(); ++it) {
        copy.reset(it->CopyOnLabelChange(obj, ref, newLabel));
        if (copy) {
            break;
        }
    }
    if (!copy) {
        return nullptr;
    }
    std::unique_ptr<PropertyXLinkSubList> p(new PropertyXLinkSubList);
    for (auto iter = _Links.begin(); iter != it; ++iter) {
        p->_Links.emplace_back();
        iter->copyTo(p->_Links.back());
    }
    p->_Links.emplace_back();
    static_cast<PropertyXLinkSub&>(*copy).copyTo(p->_Links.back());
    for (++it; it != _Links.end(); ++it) {
        p->_Links.emplace_back();
        copy.reset(it->CopyOnLabelChange(obj, ref, newLabel));
        if (copy) {
            static_cast<PropertyXLinkSub&>(*copy).copyTo(p->_Links.back());
        }
        else {
            it->copyTo(p->_Links.back());
        }
    }
    return p.release();
}

Property* PropertyXLinkSubList::CopyOnLinkReplace(const App::DocumentObject* parent,
                                                  App::DocumentObject* oldObj,
                                                  App::DocumentObject* newObj) const
{
    std::unique_ptr<Property> copy;
    PropertyXLinkSub* copied = nullptr;
    std::set<std::string> subs;
    auto it = _Links.begin();
    for (; it != _Links.end(); ++it) {
        copy.reset(it->CopyOnLinkReplace(parent, oldObj, newObj));
        if (copy) {
            copied = static_cast<PropertyXLinkSub*>(copy.get());
            if (copied->getValue() == newObj) {
                for (auto& sub : copied->getSubValues()) {
                    subs.insert(sub);
                }
            }
            break;
        }
    }
    if (!copy) {
        return nullptr;
    }
    std::unique_ptr<PropertyXLinkSubList> p(new PropertyXLinkSubList);
    for (auto iter = _Links.begin(); iter != it; ++iter) {
        if (iter->getValue() == newObj && copied->getValue() == newObj) {
            // merge subnames in case new object already exists
            for (auto& sub : iter->getSubValues()) {
                if (subs.insert(sub).second) {
                    copied->_SubList.push_back(sub);
                }
            }
        }
        else {
            p->_Links.emplace_back();
            iter->copyTo(p->_Links.back());
        }
    }
    p->_Links.emplace_back();
    copied->copyTo(p->_Links.back());
    copied = &p->_Links.back();
    for (++it; it != _Links.end(); ++it) {
        if ((it->getValue() == newObj || it->getValue() == oldObj)
            && copied->getValue() == newObj) {
            // merge subnames in case new object already exists
            for (auto& sub : it->getSubValues()) {
                if (subs.insert(sub).second) {
                    copied->_SubList.push_back(sub);
                }
            }
            continue;
        }
        p->_Links.emplace_back();
        copy.reset(it->CopyOnLinkReplace(parent, oldObj, newObj));
        if (copy) {
            static_cast<PropertyXLinkSub&>(*copy).copyTo(p->_Links.back());
        }
        else {
            it->copyTo(p->_Links.back());
        }
    }
    return p.release();
}

Property* PropertyXLinkSubList::Copy() const
{
    PropertyXLinkSubList* p = new PropertyXLinkSubList();
    for (auto& l : _Links) {
        p->_Links.emplace_back(testFlag(LinkAllowPartial), p);
        l.copyTo(p->_Links.back());
    }
    return p;
}

void PropertyXLinkSubList::Paste(const Property& from)
{
    if (!from.isDerivedFrom<PropertyXLinkSubList>()) {
        throw Base::TypeError("Incompatible property to paste to");
    }

    aboutToSetValue();
    _Links.clear();
    for (auto& l : static_cast<const PropertyXLinkSubList&>(from)._Links) {
        _Links.emplace_back(testFlag(LinkAllowPartial), this);
        _Links.back().Paste(l);
    }
    hasSetValue();
}

unsigned int PropertyXLinkSubList::getMemSize() const
{
    unsigned int size = 0;
    for (auto& l : _Links) {
        size += l.getMemSize();
    }
    return size;
}

const std::vector<std::string>& PropertyXLinkSubList::getSubValues(App::DocumentObject* obj) const
{
    for (auto& l : _Links) {
        if (l.getValue() == obj) {
            return l.getSubValues();
        }
    }
    FC_THROWM(Base::RuntimeError, "object not found");
}

std::vector<std::string> PropertyXLinkSubList::getSubValues(App::DocumentObject* obj,
                                                            bool newStyle) const
{
    for (auto& l : _Links) {
        if (l.getValue() == obj) {
            return l.getSubValues(newStyle);
        }
    }
    return {};
}

void PropertyXLinkSubList::getLinks(std::vector<App::DocumentObject*>& objs,
                                    bool all,
                                    std::vector<std::string>* subs,
                                    bool newStyle) const
{
    if (all || _pcScope != LinkScope::Hidden) {
        if (!subs) {
            objs.reserve(objs.size() + _Links.size());
            for (auto& l : _Links) {
                auto obj = l.getValue();
                if (obj && obj->isAttachedToDocument()) {
                    objs.push_back(obj);
                }
            }
            return;
        }
        size_t count = 0;
        for (auto& l : _Links) {
            auto obj = l.getValue();
            if (obj && obj->isAttachedToDocument()) {
                count += std::max((int)l.getSubValues().size(), 1);
            }
        }
        if (!count) {
            objs.reserve(objs.size() + _Links.size());
            for (auto& l : _Links) {
                auto obj = l.getValue();
                if (obj && obj->isAttachedToDocument()) {
                    objs.push_back(obj);
                }
            }
            return;
        }

        objs.reserve(objs.size() + count);
        subs->reserve(subs->size() + count);
        for (auto& l : _Links) {
            auto obj = l.getValue();
            if (obj && obj->isAttachedToDocument()) {
                // we use to run this method everytime the program needed to access the sub-elements in
                // a property link, but it caused multiple issues (#23441 and #23402) so it has been
                // commented out.
                // updateElementReferences(obj);

                auto subnames = l.getSubValues(newStyle);
                if (subnames.empty()) {
                    subnames.emplace_back("");
                }
                for (auto& sub : subnames) {
                    objs.push_back(obj);
                    subs->push_back(std::move(sub));
                }
            }
        }
    }
}

// Same algorithm as _getLinksTo above, but returns all matches
void PropertyXLinkSubList::_getLinksToList(
    std::vector<App::ObjectIdentifier>& identifiers,
    App::DocumentObject* obj,
    const char* subname,
    const std::vector<std::string>& subs,
    const std::vector<PropertyLinkBase::ShadowSub>& shadows) const
{
    if (!subname) {
        identifiers.emplace_back(*this);
        return;
    }
    App::SubObjectT objT(obj, subname);
    auto subObject = objT.getSubObject();
    auto subElement = objT.getOldElementName();

    int i = -1;
    for (const auto& sub : subs) {
        ++i;
        if (sub == subname) {
            identifiers.emplace_back(*this, i);
            continue;
        }
        if (!subObject) {
            continue;
        }
        // There is a subobject and the subname doesn't match our current entry
        App::SubObjectT sobjT(obj, sub.c_str());
        if (sobjT.getSubObject() == subObject && sobjT.getOldElementName() == subElement) {
            identifiers.emplace_back(*this, i);
            continue;
        }
        // The oldElementName ( short, I.E. "Edge5" ) doesn't match.
        if (i < (int)shadows.size()) {
            const auto& [shadowNewName, shadowOldName] = shadows[i];
            if (shadowNewName == subname || shadowOldName == subname) {
                identifiers.emplace_back(*this, i);
                continue;
            }
            if (!subObject) {
                continue;
            }
            App::SubObjectT shadowobjT(obj,
                                       shadowNewName.empty() ? shadowOldName.c_str()
                                                             : shadowNewName.c_str());
            if (shadowobjT.getSubObject() == subObject
                && shadowobjT.getOldElementName() == subElement) {
                identifiers.emplace_back(*this, i);
                continue;
            }
        }
    }
}

void PropertyXLinkSubList::getLinksTo(std::vector<App::ObjectIdentifier>& identifiers,
                                      App::DocumentObject* obj,
                                      const char* subname,
                                      bool all) const
{
    if (!all && _pcScope != LinkScope::Hidden) {
        return;
    }
    for (auto& l : _Links) {
        if (obj && obj == l._pcLink) {
            _getLinksToList(identifiers, obj, subname, l._SubList, l._ShadowSubList);
        }
    }
}

void PropertyXLinkSubList::breakLink(App::DocumentObject* obj, bool clear)
{
    if (clear && getContainer() == obj) {
        setValue(nullptr);
        return;
    }
    atomic_change guard(*this, false);
    for (auto& l : _Links) {
        if (l.getValue() == obj) {
            guard.aboutToChange();
            l.setValue(nullptr);
        }
    }
    guard.tryInvoke();
}

bool PropertyXLinkSubList::adjustLink(const std::set<App::DocumentObject*>& inList)
{
    if (_pcScope == LinkScope::Hidden) {
        return false;
    }
    std::map<App::DocumentObject*, std::vector<std::string>> values;
    bool touched = false;
    int count = 0;
    for (auto& l : _Links) {
        auto obj = l.getValue();
        if (!obj || !obj->isAttachedToDocument()) {
            ++count;
            continue;
        }
        if (inList.contains(obj) && adjustLinkSubs(this, inList, obj, l._SubList, &values)) {
            touched = true;
        }
    }
    if (touched) {
        decltype(_Links) tmp;
        if (count) {
            // XLink allows detached state, i.e. with closed external document. So
            // we need to preserve empty link
            for (auto it = _Links.begin(), itNext = it; it != _Links.end(); it = itNext) {
                ++itNext;
                if (!it->getValue()) {
                    tmp.splice(tmp.end(), _Links, it);
                }
            }
        }
        setValues(std::move(values));
        _Links.splice(_Links.end(), tmp);
    }
    return touched;
}

int PropertyXLinkSubList::checkRestore(std::string* msg) const
{
    for (auto& l : _Links) {
        int res;
        if ((res = l.checkRestore(msg))) {
            return res;
        }
    }
    return 0;
}

bool PropertyXLinkSubList::upgrade(Base::XMLReader& reader, const char* typeName)
{
    if (strcmp(typeName, PropertyLinkListGlobal::getClassTypeId().getName()) == 0
        || strcmp(typeName, PropertyLinkList::getClassTypeId().getName()) == 0
        || strcmp(typeName, PropertyLinkListChild::getClassTypeId().getName()) == 0) {
        PropertyLinkList linkProp;
        linkProp.setContainer(getContainer());
        linkProp.Restore(reader);
        setValues(linkProp.getValues());
        return true;
    }
    else if (strcmp(typeName, PropertyLinkSubListGlobal::getClassTypeId().getName()) == 0
             || strcmp(typeName, PropertyLinkSubList::getClassTypeId().getName()) == 0
             || strcmp(typeName, PropertyLinkSubListChild::getClassTypeId().getName()) == 0) {
        PropertyLinkSubList linkProp;
        linkProp.setContainer(getContainer());
        linkProp.Restore(reader);
        std::map<DocumentObject*, std::vector<std::string>> values;
        const auto& objs = linkProp.getValues();
        const auto& subs = linkProp.getSubValues();
        assert(objs.size() == subs.size());
        for (size_t i = 0; i < objs.size(); ++i) {
            values[objs[i]].push_back(subs[i]);
        }
        setValues(std::move(values));
        return true;
    }
    _Links.clear();
    _Links.emplace_back(testFlag(LinkAllowPartial), this);
    if (!_Links.back().upgrade(reader, typeName)) {
        _Links.clear();
        return false;
    }
    return true;
}

void PropertyXLinkSubList::setAllowPartial(bool enable)
{
    setFlag(LinkAllowPartial, enable);
    for (auto& l : _Links) {
        l.setAllowPartial(enable);
    }
}

void PropertyXLinkSubList::hasSetChildValue(Property&)
{
    if (!signalCounter) {
        hasSetValue();
    }
}

void PropertyXLinkSubList::aboutToSetChildValue(Property&)
{
    if (!signalCounter || !hasChanged) {
        aboutToSetValue();
        if (signalCounter) {
            hasChanged = true;
        }
    }
}

std::vector<App::DocumentObject*> PropertyXLinkSubList::getValues() const
{
    std::vector<DocumentObject*> xLinks;
    getLinks(xLinks);
    return (xLinks);
}

//**************************************************************************
// PropertyXLinkList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyXLinkList, App::PropertyXLinkSubList)

//**************************************************************************
// Construction/Destruction

PropertyXLinkList::PropertyXLinkList() = default;

PropertyXLinkList::~PropertyXLinkList() = default;

PyObject* PropertyXLinkList::getPyObject()
{
    for (auto& link : _Links) {
        auto obj = link.getValue();
        if (!obj || !obj->isAttachedToDocument()) {
            continue;
        }
        if (link.hasSubName()) {
            return PropertyXLinkSubList::getPyObject();
        }
    }

    Py::List list;
    for (auto& link : _Links) {
        auto obj = link.getValue();
        if (!obj || !obj->isAttachedToDocument()) {
            continue;
        }
        list.append(Py::asObject(obj->getPyObject()));
    }
    return Py::new_reference_to(list);
}

void PropertyXLinkList::setPyObject(PyObject* value)
{
    try {  // try PropertyLinkList syntax
        PropertyLinkList dummy;
        dummy.setAllowExternal(true);
        dummy.setPyObject(value);
        this->setValues(dummy.getValues());
        return;
    }
    catch (Base::Exception&) {
    }

    PropertyXLinkSubList::setPyObject(value);
}
