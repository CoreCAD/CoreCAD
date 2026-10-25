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
// DocInfo
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// Key on absolute path.
// Because of possible symbolic links, multiple entry may refer to the same
// file. We used to rely on QFileInfo::canonicalFilePath to resolve it, but
// has now been changed to simply use the absoluteFilePath(), and rely on user
// to be aware of possible duplicated file location. The reason being that
// some user (especially Linux user) use symlink to organize file tree.
using DocInfoMap = std::map<QString, DocInfoPtr>;
DocInfoMap _DocInfoMap;

class App::DocInfo: public std::enable_shared_from_this<App::DocInfo>
{
public:
    using Connection = fastsignals::scoped_connection;
    Connection connFinishRestoreDocument;
    Connection connPendingReloadDocument;
    Connection connDeleteDocument;
    Connection connSaveDocument;
    Connection connDeletedObject;

    DocInfoMap::iterator myPos;
    std::string myPath;
    App::Document* pcDoc {nullptr};
    std::set<PropertyXLink*> links;

    static std::string getDocPath(const char* filename,
                                  App::Document* pDoc,
                                  bool relative,
                                  QString* fullPath = nullptr)
    {
        bool absolute;
        // The path could be an URI, in that case
        // TODO: build a far much more resilient approach to test for an URI
        QString path = QString::fromUtf8(filename);
        if (path.startsWith(QLatin1String("https://"))) {
            // We do have an URI
            if (fullPath) {
                *fullPath = path;
            }
            return std::string(filename);
        }

        // make sure the filename is absolute path
        path = QDir::cleanPath(path);
        if ((absolute = QFileInfo(path).isAbsolute())) {
            if (fullPath) {
                *fullPath = path;
            }
            if (!relative) {
                return std::string(path.toUtf8().constData());
            }
        }

        const char* docPath = pDoc->getFileName();
        if (!docPath || *docPath == 0) {
            throw Base::RuntimeError("Owner document not saved");
        }

        QFileInfo docFileInfo{QString::fromUtf8(docPath)};
        QDir docDir(docFileInfo.canonicalPath());
        if (!absolute) {
            path = QDir::cleanPath(docDir.absoluteFilePath(path));
            if (fullPath) {
                *fullPath = path;
            }
        }

        if (relative) {
            return std::string(docDir.relativeFilePath(path).toUtf8().constData());
        }
        else {
            return std::string(path.toUtf8().constData());
        }
    }

    static DocInfoPtr
    get(const char* filename, App::Document* pDoc, PropertyXLink* l, const char* objName)
    {
        QString path;
        l->filePath = getDocPath(filename, pDoc, true, &path);

        FC_LOG("finding doc " << filename);

        auto it = _DocInfoMap.find(path);
        DocInfoPtr info;
        if (it != _DocInfoMap.end()) {
            info = it->second;
            if (!info->pcDoc) {
                QString fullpath(info->getFullPath());
                if (fullpath.size()
                    && App::GetApplication().addPendingDocument(
                           fullpath.toUtf8().constData(),
                           objName,
                           l->testFlag(PropertyLinkBase::LinkAllowPartial))
                        == 0) {
                    for (App::Document* doc : App::GetApplication().getDocuments()) {
                        if (getFullPath(doc->getFileName()) == fullpath) {
                            info->attach(doc);
                            break;
                        }
                    }
                }
            }
        }
        else {
            info = std::make_shared<DocInfo>();
            auto ret = _DocInfoMap.insert(std::make_pair(path, info));
            info->init(ret.first, objName, l);
        }

        if (info->pcDoc) {
            // make sure to attach only external object
            auto owner = freecad_cast<DocumentObject*>(l->getContainer());
            if (owner && owner->getDocument() == info->pcDoc) {
                return info;
            }
        }

        info->links.insert(l);
        return info;
    }

    static QString getFullPath(const char* p)
    {
        QString path = QString::fromUtf8(p);
        if (path.isEmpty()) {
            return path;
        }

        if (path.startsWith(QLatin1String("https://"))) {
            return path;
        }
        else {
            return QFileInfo(path).absoluteFilePath();
        }
    }

    QString getFullPath() const
    {
        QString path = myPos->first;
        if (path.startsWith(QLatin1String("https://"))) {
            return path;
        }
        else {
            return QFileInfo(myPos->first).absoluteFilePath();
        }
    }

    const char* filePath() const
    {
        return myPath.c_str();
    }

    void deinit()
    {
        FC_LOG("deinit " << (pcDoc ? pcDoc->getName() : filePath()));
        assert(links.empty());
        connFinishRestoreDocument.disconnect();
        connPendingReloadDocument.disconnect();
        connDeleteDocument.disconnect();
        connSaveDocument.disconnect();
        connDeletedObject.disconnect();

        auto me = shared_from_this();
        _DocInfoMap.erase(myPos);
        myPos = _DocInfoMap.end();
        myPath.clear();
        pcDoc = nullptr;
    }

    void init(DocInfoMap::iterator pos, const char* objName, PropertyXLink* l)
    {
        myPos = pos;
        myPath = myPos->first.toUtf8().constData();
        App::Application& app = App::GetApplication();
        // NOLINTBEGIN
        connFinishRestoreDocument = app.signalFinishRestoreDocument.connect(
            std::bind(&DocInfo::slotFinishRestoreDocument, this, sp::_1));
        connPendingReloadDocument = app.signalPendingReloadDocument.connect(
            std::bind(&DocInfo::slotFinishRestoreDocument, this, sp::_1));
        connDeleteDocument =
            app.signalDeleteDocument.connect(std::bind(&DocInfo::slotDeleteDocument, this, sp::_1));
        connSaveDocument =
            app.signalSaveDocument.connect(std::bind(&DocInfo::slotSaveDocument, this, sp::_1));
        // NOLINTEND

        QString fullpath(getFullPath());
        if (fullpath.isEmpty()) {
            FC_ERR("document not found " << filePath());
        }
        else {
            for (App::Document* doc : App::GetApplication().getDocuments()) {
                if (getFullPath(doc->getFileName()) == fullpath) {
                    if (doc->testStatus(App::Document::PartialDoc) && !doc->getObject(objName)) {
                        break;
                    }
                    attach(doc);
                    return;
                }
            }
            FC_LOG("document pending " << filePath());
            app.addPendingDocument(fullpath.toUtf8().constData(),
                                   objName,
                                   l->testFlag(PropertyLinkBase::LinkAllowPartial));
        }
    }

    void attach(Document* doc)
    {
        assert(!pcDoc);
        // When a link at this path
        // expects a specific target-document UUID, refuse to bind a file that
        // carries a different one. Leaving pcDoc unset keeps the info
        // unresolved so path recovery can locate the right file by UUID.
        for (auto* link : links) {
            if (!link->docUuid.empty() && doc->Uid.getValueStr() != link->docUuid) {
                FC_WARN("document at '" << doc->getFileName() << "' has UUID "
                                        << doc->Uid.getValueStr() << ", expected " << link->docUuid
                                        << " -- refusing to bind");
                return;
            }
        }
        pcDoc = doc;
        FC_LOG("attaching " << doc->getName() << ", " << doc->getFileName());
        std::map<App::PropertyLinkBase*, std::vector<App::PropertyXLink*>> parentLinks;
        for (auto it = links.begin(), itNext = it; it != links.end(); it = itNext) {
            ++itNext;
            auto link = *it;
            if (link->_pcLink) {
                continue;
            }
            if (link->parentProp) {
                parentLinks[link->parentProp].push_back(link);
                continue;
            }
            auto obj = doc->getObject(link->objectName.c_str());
            if (obj) {
                link->restoreLink(obj);
            }
            else if (doc->testStatus(App::Document::PartialDoc)) {
                App::GetApplication().addPendingDocument(doc->FileName.getValue(),
                                                         link->objectName.c_str(),
                                                         false);
                FC_WARN("reloading partial document '" << doc->FileName.getValue()
                                                       << "' due to object " << link->objectName);
            }
            else {
                FC_WARN("object '" << link->objectName << "' not found in document '"
                                   << doc->getName() << "'");
            }
        }
        for (auto& v : parentLinks) {
            v.first->setFlag(PropertyLinkBase::LinkRestoring);
            v.first->aboutToSetValue();
            for (auto link : v.second) {
                auto obj = doc->getObject(link->objectName.c_str());
                if (obj) {
                    link->restoreLink(obj);
                }
                else if (doc->testStatus(App::Document::PartialDoc)) {
                    App::GetApplication().addPendingDocument(doc->FileName.getValue(),
                                                             link->objectName.c_str(),
                                                             false);
                    FC_WARN("reloading partial document '"
                            << doc->FileName.getValue() << "' due to object " << link->objectName);
                }
                else {
                    FC_WARN("object '" << link->objectName << "' not found in document '"
                                       << doc->getName() << "'");
                }
            }
            v.first->hasSetValue();
            v.first->setFlag(PropertyLinkBase::LinkRestoring, false);
        }
    }

    void remove(PropertyXLink* l)
    {
        auto it = links.find(l);
        if (it != links.end()) {
            links.erase(it);
            if (links.empty()) {
                deinit();
            }
        }
    }

    static void restoreDocument(const App::Document& doc)
    {
        auto it = _DocInfoMap.find(getFullPath(doc.FileName.getValue()));
        if (it == _DocInfoMap.end()) {
            return;
        }
        it->second->slotFinishRestoreDocument(doc);
    }

    void slotFinishRestoreDocument(const App::Document& doc)
    {
        if (pcDoc) {
            return;
        }
        QString fullpath(getFullPath());
        if (!fullpath.isEmpty() && getFullPath(doc.getFileName()) == fullpath) {
            attach(const_cast<App::Document*>(&doc));
        }
    }

    void slotSaveDocument(const App::Document& doc)
    {
        if (!pcDoc) {
            slotFinishRestoreDocument(doc);
            return;
        }
        if (&doc != pcDoc) {
            return;
        }

        QFileInfo info(myPos->first);
        QString path(info.absoluteFilePath());
        const char* filename = doc.getFileName();
        QString docPath(getFullPath(filename));

        if (path.isEmpty() || path != docPath) {
            FC_LOG("document '" << doc.getName() << "' path changed");
            auto me = shared_from_this();
            auto ret = _DocInfoMap.insert(std::make_pair(docPath, me));
            if (!ret.second) {
                // is that even possible?
                FC_WARN("document '" << doc.getName() << "' path exists, detach");
                slotDeleteDocument(doc);
                return;
            }
            _DocInfoMap.erase(myPos);
            myPos = ret.first;

            std::set<PropertyXLink*> tmp;
            tmp.swap(links);
            for (auto link : tmp) {
                auto owner = static_cast<DocumentObject*>(link->getContainer());
                // adjust file path for each PropertyXLink
                DocInfo::get(filename, owner->getDocument(), link, link->objectName.c_str());
            }
        }

        // time stamp changed, touch the linking document.
        std::set<Document*> docs;
        for (auto link : links) {
            auto linkdoc = static_cast<DocumentObject*>(link->getContainer())->getDocument();
            auto ret = docs.insert(linkdoc);
            if (ret.second) {
                // This will signal the Gui::Document to call setModified();
                FC_LOG("touch document " << linkdoc->getName() << " on time stamp change of "
                                         << link->getFullName());
                linkdoc->Comment.touch();
            }
        }
    }

    void slotDeleteDocument(const App::Document& doc)
    {
        for (auto it = links.begin(), itNext = it; it != links.end(); it = itNext) {
            ++itNext;
            auto link = *it;
            auto obj = freecad_cast<DocumentObject*>(link->getContainer());
            if (obj && obj->getDocument() == &doc) {
                links.erase(it);
                // must call unlink here, so that PropertyLink::resetLink can
                // remove back link before the owner object is marked as being
                // destroyed
                link->unlink();
            }
        }
        if (links.empty()) {
            deinit();
            return;
        }
        if (pcDoc != &doc) {
            return;
        }
        std::map<App::PropertyLinkBase*, std::vector<App::PropertyXLink*>> parentLinks;
        for (auto link : links) {
            link->setFlag(PropertyLinkBase::LinkDetached);
            if (link->parentProp) {
                parentLinks[link->parentProp].push_back(link);
            }
            else {
                parentLinks[nullptr].push_back(link);
            }
        }
        for (auto& v : parentLinks) {
            if (v.first) {
                v.first->setFlag(PropertyLinkBase::LinkDetached);
                v.first->aboutToSetValue();
            }
            for (auto l : v.second) {
                l->detach();
            }
            if (v.first) {
                v.first->hasSetValue();
                v.first->setFlag(PropertyLinkBase::LinkDetached, false);
            }
        }
        pcDoc = nullptr;
    }

    bool hasXLink(const App::Document* doc) const
    {
        for (auto link : links) {
            auto obj = freecad_cast<DocumentObject*>(link->getContainer());
            if (obj && obj->getDocument() == doc) {
                return true;
            }
        }
        return false;
    }

    static void breakLinks(App::DocumentObject* obj, bool clear)
    {
        auto doc = obj->getDocument();
        for (auto itD = _DocInfoMap.begin(), itDNext = itD; itD != _DocInfoMap.end();
             itD = itDNext) {
            ++itDNext;
            auto docInfo = itD->second;
            if (docInfo->pcDoc != doc) {
                continue;
            }
            auto& links = docInfo->links;
            std::set<PropertyLinkBase*> parentLinks;
            for (auto it = links.begin(), itNext = it; it != links.end(); it = itNext) {
                ++itNext;
                auto link = *it;
                if (link->_pcLink != obj && !(clear && link->getContainer() == obj)) {
                    continue;
                }
                if (link->parentProp) {
                    parentLinks.insert(link->parentProp);
                }
                else {
                    link->breakLink(obj, clear);
                }
            }
            for (auto link : parentLinks) {
                link->breakLink(obj, clear);
            }
        }
    }
};

void PropertyLinkBase::breakLinks(App::DocumentObject* link,
                                  const std::vector<App::DocumentObject*>& objs,
                                  bool clear)
{
    std::vector<Property*> props;
    for (auto obj : objs) {
        props.clear();
        obj->getPropertyList(props);
        for (auto prop : props) {
            auto linkProp = freecad_cast<PropertyLinkBase*>(prop);
            if (linkProp) {
                linkProp->breakLink(link, clear);
            }
        }
    }
    DocInfo::breakLinks(link, clear);
}

//**************************************************************************
// PropertyXLink
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyXLink, App::PropertyLink)

PropertyXLink::PropertyXLink(bool _allowPartial, PropertyLinkBase* parent)
    : parentProp(parent)
{
    setAllowPartial(_allowPartial);
    setAllowExternal(true);
    setSyncSubObject(true);
    if (parent) {
        setContainer(parent->getContainer());
    }
}

PropertyXLink::~PropertyXLink()
{
    try {
        unlink();
    } catch (std::bad_weak_ptr &) {
        FC_WARN("Bad pointer exception caught when destroying PropertyXLink\n");
    }
}

void PropertyXLink::setSyncSubObject(bool enable)
{
    _Flags.set((std::size_t)LinkSyncSubObject, enable);
}

void PropertyXLink::unlink()
{
    if (docInfo) {
        docInfo->remove(this);
        docInfo.reset();
    }
    objectName.clear();
    resetLink();
}

void PropertyXLink::detach()
{
    if (docInfo && _pcLink) {
        aboutToSetValue();
        resetLink();
        updateElementReference(nullptr);
        hasSetValue();
    }
}

void PropertyXLink::aboutToSetValue()
{
    if (parentProp) {
        parentProp->aboutToSetChildValue(*this);
    }
    else {
        PropertyLinkBase::aboutToSetValue();
    }
}

void PropertyXLink::hasSetValue()
{
    if (parentProp) {
        parentProp->hasSetChildValue(*this);
    }
    else {
        PropertyLinkBase::hasSetValue();
    }
}

void PropertyXLink::setSubName(const char* subname)
{
    std::vector<std::string> subs;
    if (!Base::Tools::isNullOrEmpty(subname)) {
        subs.emplace_back(subname);
    }
    aboutToSetValue();
    setSubValues(std::move(subs));
    hasSetValue();
}

void PropertyXLink::setSubValues(std::vector<std::string>&& subs, std::vector<ShadowSub>&& shadows)
{
    _SubList = std::move(subs);
    _ShadowSubList.clear();
    if (shadows.size() == _SubList.size()) {
        _ShadowSubList = std::move(shadows);
        onContainerRestored();  // re-register element references
    }
    else {
        updateElementReference(nullptr);
    }
    checkLabelReferences(_SubList);
}

bool PropertyXLink::statesWhereItPoints(std::vector<Pointing>& pointing) const
{
    if (_pcLink == nullptr) {
        return true;
    }
    if (_SubList.empty()) {
        pointing.push_back({_pcLink, {}, /*noPart=*/true});
        return true;
    }
    for (std::size_t i = 0; i < _SubList.size(); ++i) {
        pointing.push_back({_pcLink, _SubList[i], false, fullNameOf(_pcLink, _ShadowSubList, i)});
    }
    return true;
}

bool PropertyXLink::pointAt(const std::vector<Pointing>& pointing)
{
    std::vector<std::string> subs;
    std::vector<ShadowSub> shadows;
    for (const Pointing& one : pointing) {
        if (!one.noPart) {
            subs.push_back(one.sub);
            shadows.push_back({one.name, one.name.empty() ? std::string() : one.sub});
        }
    }
    setValue(pointing.empty() ? nullptr : pointing.front().target,
             std::move(subs),
             std::move(shadows));
    return true;
}

void PropertyXLink::setValue(App::DocumentObject* lValue)
{
    setValue(lValue, nullptr);
}

void PropertyXLink::setValue(App::DocumentObject* lValue, const char* subname)
{
    std::vector<std::string> subs;
    if (!Base::Tools::isNullOrEmpty(subname)) {
        subs.emplace_back(subname);
    }
    setValue(lValue, std::move(subs));
}

void PropertyXLink::restoreLink(App::DocumentObject* lValue)
{
    assert(!_pcLink && lValue && docInfo);

    auto owner = freecad_cast<DocumentObject*>(getContainer());
    if (!owner || !owner->isAttachedToDocument()) {
        throw Base::RuntimeError("invalid container");
    }

    bool touched = owner->isTouched();
    setFlag(LinkDetached, false);
    setFlag(LinkRestoring);
    aboutToSetValue();

    if (!owner->testStatus(ObjectStatus::Destroy) && _pcScope != LinkScope::Hidden) {
        lValue->_addBackLink(owner);
    }

    _pcLink = lValue;
    updateElementReference(nullptr);
    hasSetValue();
    setFlag(LinkRestoring, false);

    if (!touched && owner->isTouched() && docInfo && docInfo->pcDoc
        && stamp == docInfo->pcDoc->LastModifiedDate.getValue()) {
        owner->purgeTouched();
    }
}

void PropertyXLink::setValue(App::DocumentObject* lValue,
                             std::vector<std::string>&& subs,
                             std::vector<ShadowSub>&& shadows)
{
    if (_pcLink == lValue && _SubList == subs) {
        return;
    }

    if (lValue && (!lValue->isAttachedToDocument() || !lValue->getDocument())) {
        throw Base::ValueError("Invalid object");
    }

    auto owner = freecad_cast<DocumentObject*>(getContainer());
    if (!owner || !owner->isAttachedToDocument()) {
        throw Base::RuntimeError("invalid container");
    }

    if (lValue == owner) {
        throw Base::ValueError("self linking");
    }

    aboutToSetValue();

    DocInfoPtr info;
    const char* name = "";
    if (lValue) {
        name = lValue->getNameInDocument();
        if (lValue->getDocument() != owner->getDocument()) {
            if (!docInfo || lValue->getDocument() != docInfo->pcDoc) {
                const char* filename = lValue->getDocument()->getFileName();
                if (!filename || *filename == 0) {
                    throw Base::RuntimeError("Linked document not saved");
                }
                FC_LOG("xlink set to new document " << lValue->getDocument()->getName());
                info = DocInfo::get(filename, owner->getDocument(), this, name);
                assert(info && info->pcDoc == lValue->getDocument());
            }
            else {
                info = docInfo;
            }
        }
    }

    setFlag(LinkDetached, false);

    if (!owner->testStatus(ObjectStatus::Destroy) && _pcScope != LinkScope::Hidden) {
        if (_pcLink) {
            _pcLink->_removeBackLink(owner);
        }
        if (lValue) {
            lValue->_addBackLink(owner);
        }
    }

    if (docInfo != info) {
        unlink();
        docInfo = info;
    }
    if (!docInfo) {
        filePath.clear();
    }
    _pcLink = lValue;
    if (docInfo && docInfo->pcDoc) {
        stamp = docInfo->pcDoc->LastModifiedDate.getValue();
    }
    objectName = name;
    setSubValues(std::move(subs), std::move(shadows));
    hasSetValue();
}

namespace
{
// Read a document file's own durable UUID without opening the whole
// document. A document written by this program is a recipe in plain text and names its uuid in
// the first few lines; a sealed archive -- a release, or a file from an older version -- keeps
// the same fact inside Document.xml. Both are read here, because a link has to find its target
// whichever shape the target happens to have. Returns an empty string if the file cannot be
// read or carries no UUID.
std::string peekDocumentUuid(const QString& filePath)
{
    try {
        Base::FileInfo fi(filePath.toUtf8().constData());
        if (!fi.exists() || !fi.isFile()) {
            return {};
        }
        Base::ifstream file(fi, std::ios::in | std::ios::binary);
        if (!file) {
            return {};
        }
        const bool sealedArchive = file.peek() == 'P';
        std::unique_ptr<zipios::ZipInputStream> zip;
        std::istream* source = &file;
        if (sealedArchive) {
            // positioned at the first entry, Document.xml
            zip = std::make_unique<zipios::ZipInputStream>(file);
            source = zip.get();
        }

        // The recipe says it outright; the archive says it as the first Uid property, which
        // precedes any object data.
        const std::string recipeTag = "<Document uuid=\"";
        const std::string uidTag = "name=\"Uid\"";
        const std::string valueTag = "<Uuid value=\"";

        const auto found = [&](const std::string& content) {
            if (content.find(recipeTag) != std::string::npos) {
                return true;
            }
            const auto uidPos = content.find(uidTag);
            return uidPos != std::string::npos
                && content.find(valueTag, uidPos) != std::string::npos;
        };
        const auto valueAfter = [](const std::string& content, std::string::size_type pos) {
            const auto end = content.find('"', pos);
            return end == std::string::npos ? std::string() : content.substr(pos, end - pos);
        };

        std::string content;
        constexpr std::string::size_type cap = 1u << 20;  // 1 MiB safety cap
        std::array<char, 4096> buf {};
        while (source->good() && content.size() < cap) {
            source->read(buf.data(), static_cast<std::streamsize>(buf.size()));
            content.append(buf.data(), static_cast<std::size_t>(source->gcount()));
            if (found(content)) {
                break;
            }
        }

        auto pos = content.find(recipeTag);
        if (pos != std::string::npos) {
            return valueAfter(content, pos + recipeTag.size());
        }
        pos = content.find(uidTag);
        if (pos == std::string::npos) {
            return {};
        }
        pos = content.find(valueTag, pos);
        if (pos == std::string::npos) {
            return {};
        }
        return valueAfter(content, pos + valueTag.size());
    }
    catch (const std::exception&) {
        return {};
    }
}

// Recover the path of a cross-document reference target that has moved.
// If the file at the stored path is absent or now carries
// a different document UUID, scan the referring document's directory for a file
// whose document UUID matches docUuid; on a hit return its absolute path (the
// caller adopts it and the stored path locator refreshes). Returns empty when no
// recovery is needed or none is found. Searches the referring document's
// directory only.
std::string recoverXLinkPath(App::Document* ownerDoc,
                             const std::string& filename,
                             const std::string& docUuid)
{
    if (docUuid.empty() || !ownerDoc) {
        return {};
    }
    // Resolve the stored path to absolute; if it already carries the right UUID
    // there is nothing to recover.
    QString stored;
    try {
        App::DocInfo::getDocPath(filename.c_str(), ownerDoc, false, &stored);
    }
    catch (const Base::Exception&) {
        stored.clear();
    }
    if (!stored.isEmpty() && peekDocumentUuid(stored) == docUuid) {
        return {};
    }
    const char* ownerFile = ownerDoc->getFileName();
    if (Base::Tools::isNullOrEmpty(ownerFile)) {
        return {};
    }
    QDir dir = QFileInfo(QString::fromUtf8(ownerFile)).absoluteDir();
    QStringList filters;
    for (const std::string& ext : Document::nativeFormatExtensions()) {
        filters.append(QStringLiteral("*.") + QString::fromStdString(ext));
    }
    for (const QFileInfo& cand : dir.entryInfoList(filters, QDir::Files)) {
        const QString candPath = cand.absoluteFilePath();
        if (candPath == stored) {
            continue;  // already ruled out above
        }
        if (peekDocumentUuid(candPath) == docUuid) {
            return std::string(candPath.toUtf8().constData());
        }
    }
    return {};
}
}  // namespace

void PropertyXLink::setValue(std::string&& filename,
                             std::string&& name,
                             std::string&& docUuid,
                             std::string&& objUuid,
                             std::vector<std::string>&& subs,
                             std::vector<ShadowSub>&& shadows)
{
    if (name.empty()) {
        setValue(nullptr, std::move(subs), std::move(shadows));
        return;
    }
    auto owner = freecad_cast<DocumentObject*>(getContainer());
    if (!owner || !owner->isAttachedToDocument()) {
        throw Base::RuntimeError("invalid container");
    }

    // Remember the target-document UUID: it is the real
    // binding, validated against the file the path locator resolves to.
    this->docUuid = docUuid;

    DocumentObject* pObject = nullptr;
    DocInfoPtr info;
    if (!filename.empty()) {
        owner->getDocument()->signalLinkXsetValue(filename);
        info = DocInfo::get(filename.c_str(), owner->getDocument(), this, name.c_str());
        // Bind the object through its UUID
        // within the target document, name as fallback. Prefer the document
        // opened via the stored path; when that path yields nothing, fall back
        // to a document already open under the target UUID.
        App::Document* targetDoc = info->pcDoc;
        // A path that resolves to the WRONG document UUID must not bind
        // treat it as unresolved and defer to the UUID.
        if (targetDoc && !docUuid.empty() && targetDoc->Uid.getValueStr() != docUuid) {
            targetDoc = nullptr;
        }
        if (!targetDoc && !docUuid.empty()) {
            Base::Uuid targetDocUuid;
            targetDocUuid.setValue(docUuid);
            targetDoc = App::GetApplication().getDocumentByUuid(targetDocUuid);
        }
        // The stored path is gone or now
        // holds a different document, and no open document carries the target
        // UUID. Search the referring document's directory for the moved target
        // by UUID and adopt its path, so the correct file is opened and the
        // stored path locator is refreshed. Runs only on the cold path -- a
        // valid, matching stored path never reaches here.
        if (!targetDoc && !docUuid.empty()) {
            std::string recovered = recoverXLinkPath(owner->getDocument(), filename, docUuid);
            if (!recovered.empty()) {
                FC_WARN("cross-document reference target moved; recovered by UUID at '" << recovered
                                                                                        << "'");
                filename = std::move(recovered);
                info = DocInfo::get(filename.c_str(), owner->getDocument(), this, name.c_str());
                targetDoc = info->pcDoc;
            }
        }
        if (targetDoc) {
            pObject = resolveLinkTarget(targetDoc, objUuid, name);
        }
    }
    else {
        pObject = owner->getDocument()->getObject(name.c_str());
    }

    if (pObject) {
        setValue(pObject, std::move(subs), std::move(shadows));
        return;
    }
    setFlag(LinkDetached, false);
    aboutToSetValue();

    if (_pcLink && !owner->testStatus(ObjectStatus::Destroy) && _pcScope != LinkScope::Hidden) {
        _pcLink->_removeBackLink(owner);
    }

    _pcLink = nullptr;
    if (docInfo != info) {
        unlink();
        docInfo = info;
    }
    if (!docInfo) {
        filePath.clear();
    }
    if (docInfo && docInfo->pcDoc) {
        stamp = docInfo->pcDoc->LastModifiedDate.getValue();
    }
    objectName = std::move(name);
    setSubValues(std::move(subs), std::move(shadows));
    hasSetValue();
}

void PropertyXLink::setValue(App::DocumentObject* link,
                             const std::vector<std::string>& subs,
                             std::vector<ShadowSub>&& shadows)
{
    setValue(link, std::vector<std::string>(subs), std::move(shadows));
}

App::Document* PropertyXLink::getDocument() const
{
    return docInfo ? docInfo->pcDoc : nullptr;
}

const char* PropertyXLink::getDocumentPath() const
{
    return docInfo ? docInfo->filePath() : filePath.c_str();
}

const char* PropertyXLink::getObjectName() const
{
    return objectName.c_str();
}

bool PropertyXLink::upgrade(Base::XMLReader& reader, const char* typeName)
{
    if (strcmp(typeName, App::PropertyLinkGlobal::getClassTypeId().getName()) == 0
        || strcmp(typeName, App::PropertyLink::getClassTypeId().getName()) == 0
        || strcmp(typeName, App::PropertyLinkChild::getClassTypeId().getName()) == 0) {
        PropertyLink::Restore(reader);
        return true;
    }
    FC_ERR("Cannot upgrade from " << typeName);
    return false;
}

int PropertyXLink::checkRestore(std::string* msg) const
{
    if (!docInfo) {
        if (!_pcLink && !objectName.empty()) {
            // this condition means linked object not found
            if (msg) {
                std::ostringstream ss;
                ss << "Link not restored" << std::endl;
                ss << "Object: " << objectName;
                if (!filePath.empty()) {
                    ss << std::endl << "File: " << filePath;
                }
                *msg = ss.str();
            }
            return 2;
        }
        return 0;
    }
    if (!_pcLink) {
        if (testFlag(LinkSilentRestore)) {
            return 0;
        }
        if (testFlag(LinkAllowPartial)
            && (!docInfo->pcDoc || docInfo->pcDoc->testStatus(App::Document::PartialDoc))) {
            return 0;
        }
        if (msg) {
            std::ostringstream ss;
            ss << "Link not restored" << std::endl;
            ss << "Linked object: " << objectName;
            if (docInfo->pcDoc) {
                ss << std::endl << "Linked document: " << docInfo->pcDoc->Label.getValue();
            }
            else if (!filePath.empty()) {
                ss << std::endl << "Linked file: " << filePath;
            }
            *msg = ss.str();
        }
        return 2;
    }
    if (!docInfo->pcDoc || stamp == docInfo->pcDoc->LastModifiedDate.getValue()) {
        return 0;
    }

    if (msg) {
        std::ostringstream ss;
        ss << "Time stamp changed on link " << _pcLink->getFullName();
        *msg = ss.str();
    }
    return 1;
}

void PropertyXLink::afterRestore()
{
    assert(_SubList.size() == _ShadowSubList.size());
    if (!testFlag(LinkRestoreLabel) || !_pcLink || !_pcLink->isAttachedToDocument()) {
        return;
    }

    setFlag(LinkRestoreLabel, false);
    for (size_t i = 0; i < _SubList.size(); ++i) {
        restoreLabelReference(_pcLink, _SubList[i], &_ShadowSubList[i]);
    }
}

void PropertyXLink::onContainerRestored()
{
    if (!_pcLink || !_pcLink->isAttachedToDocument()) {
        return;
    }
    for (size_t i = 0; i < _SubList.size(); ++i) {
        _registerElementReference(_pcLink, _SubList[i], _ShadowSubList[i]);
    }
}

void PropertyXLink::updateElementReference(DocumentObject* feature, bool reverse, bool notify)
{
    if (!updateLinkReference(this,
                             feature,
                             reverse,
                             notify,
                             _pcLink,
                             _SubList,
                             _mapped,
                             _ShadowSubList)) {
        return;
    }
    if (notify) {
        hasSetValue();
    }
}

bool PropertyXLink::referenceChanged() const
{
    return !_mapped.empty();
}

void PropertyXLink::Save(Base::Writer& writer) const
{
    auto owner = dynamic_cast<const DocumentObject*>(getContainer());
    if (!owner || !owner->getDocument()) {
        return;
    }

    assert(_SubList.size() == _ShadowSubList.size());

    auto exporting = owner->isExporting();
    if (_pcLink && exporting && _pcLink->isExporting()) {
        // this means, we are exporting the owner and the linked object together.
        // Lets save the export name
        writer.Stream() << writer.ind() << "<XLink name=\"" << _pcLink->getExportName();
    }
    else {
        const char* path = filePath.c_str();
        std::string _path;
        if (exporting) {
            // Here means we are exporting the owner but not exporting the
            // linked object.  Try to use absolute file path for easy transition
            // into document at different directory
            if (docInfo) {
                _path = docInfo->filePath();
            }
            else {
                auto pDoc = owner->getDocument();
                const char* docPath = pDoc->getFileName();
                if (!Base::Tools::isNullOrEmpty(docPath)) {
                    if (!filePath.empty()) {
                        _path = DocInfo::getDocPath(filePath.c_str(), pDoc, false);
                    }
                    else {
                        _path = docPath;
                    }
                }
                else {
                    FC_WARN("PropertyXLink export without saving the document");
                }
            }
            if (!_path.empty()) {
                path = _path.c_str();
            }
        }
        writer.Stream() << writer.ind() << "<XLink file=\"" << encodeAttribute(path)
                        << "\" stamp=\""
                        << (docInfo && docInfo->pcDoc ? docInfo->pcDoc->LastModifiedDate.getValue()
                                                      : "")
                        << "\" name=\"" << objectName;

        // Persist the target document's and
        // object's UUIDs as the real binding; file/name stay locator hints.
        std::string docUuid;
        std::string objUuid;
        if (_pcLink && _pcLink->isAttachedToDocument()) {
            objUuid = _pcLink->Uid.getValueStr();
            if (auto* linkedDoc = _pcLink->getDocument()) {
                docUuid = linkedDoc->Uid.getValueStr();
            }
        }
        writer.Stream() << "\" docUuid=\"" << docUuid << "\" uuid=\"" << objUuid;
    }

    if (testFlag(LinkAllowPartial)) {
        writer.Stream() << "\" partial=\"1";
    }

    if (_SubList.empty()) {
        writer.Stream() << "\"/>" << std::endl;
    }
    else if (_SubList.size() == 1) {
        const auto& subName = _SubList[0];
        const auto& shadowSub = _ShadowSubList[0];
        const auto& sub = shadowSub.oldName.empty() ? subName : shadowSub.oldName;
        if (exporting) {
            std::string exportName;
            writer.Stream() << "\" sub=\""
                            << encodeAttribute(exportSubName(exportName, _pcLink, sub.c_str()));
            if (!shadowSub.oldName.empty() && shadowSub.newName == subName) {
                writer.Stream() << "\" " ATTR_MAPPED "=\"1";
            }
        }
        else {
            writer.Stream() << "\" sub=\"" << encodeAttribute(sub);
            if (!sub.empty()) {
                if (sub != subName) {
                    writer.Stream() << "\" " ATTR_SHADOWED "=\"" << encodeAttribute(subName);
                }
                else if (!shadowSub.newName.empty()) {
                    writer.Stream()
                        << "\" " ATTR_SHADOW "=\"" << encodeAttribute(shadowSub.newName);
                }
            }
        }
        writer.Stream() << "\"/>" << std::endl;
    }
    else {
        // Cruth: the subs are stated, never counted. A declared length is a second statement of
        // what the same element holds, and a textual merge takes the same raised number from both
        // sides without a conflict, after which a reader that loops it drops one side's addition.
        writer.Stream() << "\">" << std::endl;
        writer.incInd();
        for (unsigned int i = 0; i < _SubList.size(); i++) {
            const auto& shadow = _ShadowSubList[i];
            // shadow.oldName stores the old style element name. For backward
            // compatibility reason, we shall store the old name into attribute
            // 'value' whenever possible.
            const auto& sub = shadow.oldName.empty() ? _SubList[i] : shadow.oldName;
            writer.Stream() << writer.ind() << "<Sub value=\"";
            if (exporting) {
                std::string exportName;
                writer.Stream() << encodeAttribute(exportSubName(exportName, _pcLink, sub.c_str()));
                if (!shadow.oldName.empty() && shadow.newName == _SubList[i]) {
                    writer.Stream() << "\" " ATTR_MAPPED "=\"1";
                }
            }
            else {
                writer.Stream() << encodeAttribute(sub);
                if (!_SubList[i].empty()) {
                    if (sub != _SubList[i]) {
                        writer.Stream()
                            << "\" " ATTR_SHADOWED "=\"" << encodeAttribute(_SubList[i]);
                    }
                    else if (!shadow.newName.empty()) {
                        writer.Stream()
                            << "\" " ATTR_SHADOW "=\"" << encodeAttribute(shadow.newName);
                    }
                }
            }
            writer.Stream() << "\"/>" << endl;
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</XLink>" << endl;
    }
}

void PropertyXLink::Restore(Base::XMLReader& reader)
{
    reader.readElement("XLink");
    restoreFromCurrentElement(reader);
}

void PropertyXLink::restoreFromCurrentElement(Base::XMLReader& reader)
{
    const int link = reader.level();
    std::string stampAttr, file;
    if (reader.hasAttribute("stamp")) {
        stampAttr = reader.getAttribute<const char*>("stamp");
    }
    if (reader.hasAttribute("file")) {
        file = reader.getAttribute<const char*>("file");
    }
    // The target document / object UUIDs are the
    // real binding, resolved ahead of the path / name locator hints below.
    std::string docUuid = reader.getAttribute<const char*>("docUuid", "");
    std::string objUuid = reader.getAttribute<const char*>("uuid", "");

    setFlag(LinkAllowPartial,
            reader.hasAttribute("partial") && reader.getAttribute<bool>("partial"));
    std::string name;
    if (file.empty()) {
        name = reader.getName(reader.getAttribute<const char*>("name"));
    }
    else {
        name = reader.getAttribute<const char*>("name");
    }

    DocumentObject* object = nullptr;
    if (!name.empty() && file.empty()) {
        if (auto parent = freecad_cast<DocumentObject*>(getContainer())) {
            Document* document = parent->getDocument();
            object = document ? document->getObject(name.c_str()) : nullptr;
            if (!object && reader.isVerbose()) {
                FC_WARN("Lost link to '" << name
                                         << "' while loading, maybe "
                                            "an object was not loaded correctly");
            }
        }
    }

    std::vector<std::string> subs;
    std::vector<ShadowSub> shadows;
    std::vector<int> mapped;
    bool restoreLabel = false;
    if (reader.hasAttribute("sub")) {
        if (reader.hasAttribute(ATTR_MAPPED)) {
            mapped.push_back(0);
        }
        subs.emplace_back();
        auto& subname = subs.back();
        shadows.emplace_back();
        auto& shadow = shadows.back();
        shadow.oldName = importSubName(reader, reader.getAttribute<const char*>("sub"), restoreLabel);
        if (reader.hasAttribute(ATTR_SHADOWED) && !IGNORE_SHADOW) {
            subname = shadow.newName =
                importSubName(reader, reader.getAttribute<const char*>(ATTR_SHADOWED), restoreLabel);
        }
        else {
            subname = shadow.oldName;
            if (reader.hasAttribute(ATTR_SHADOW) && !IGNORE_SHADOW) {
                shadow.newName =
                    importSubName(reader, reader.getAttribute<const char*>(ATTR_SHADOW), restoreLabel);
            }
        }
    }
    else {
        while (nextChildElement(reader, link)) {
            expectElement(reader, "Sub");
            const int i = static_cast<int>(subs.size());
            subs.emplace_back();
            shadows.emplace_back();
            shadows[i].oldName = importSubName(reader, reader.getAttribute<const char*>("value"), restoreLabel);
            if (reader.hasAttribute(ATTR_SHADOWED) && !IGNORE_SHADOW) {
                subs[i] = shadows[i].newName =
                    importSubName(reader, reader.getAttribute<const char*>(ATTR_SHADOWED), restoreLabel);
            }
            else {
                subs[i] = shadows[i].oldName;
                if (reader.hasAttribute(ATTR_SHADOW) && !IGNORE_SHADOW) {
                    shadows[i].newName =
                        importSubName(reader, reader.getAttribute<const char*>(ATTR_SHADOW), restoreLabel);
                }
            }
            if (reader.hasAttribute(ATTR_MAPPED)) {
                mapped.push_back(i);
            }
        }
    }
    setFlag(LinkRestoreLabel, restoreLabel);

    if (name.empty()) {
        setValue(nullptr);
        return;
    }

    if (!file.empty() || (!object && !name.empty())) {
        this->stamp = stampAttr;
        setValue(std::move(file),
                 std::move(name),
                 std::move(docUuid),
                 std::move(objUuid),
                 std::move(subs),
                 std::move(shadows));
    }
    else {
        setValue(object, std::move(subs), std::move(shadows));
    }
    _mapped = std::move(mapped);
}

Property*
PropertyXLink::CopyOnImportExternal(const std::map<std::string, std::string>& nameMap) const
{
    auto owner = freecad_cast<const DocumentObject*>(getContainer());
    if (!owner || !owner->getDocument() || !_pcLink || !_pcLink->isAttachedToDocument()) {
        return nullptr;
    }

    auto subs = updateLinkSubs(_pcLink, _SubList, &tryImportSubName, owner->getDocument(), nameMap);
    auto linked = tryImport(owner->getDocument(), _pcLink, nameMap);
    if (subs.empty() && linked == _pcLink) {
        return nullptr;
    }

    std::unique_ptr<PropertyXLink> p(new PropertyXLink);
    copyTo(*p, linked, &subs);
    return p.release();
}

Property* PropertyXLink::CopyOnLinkReplace(const App::DocumentObject* parent,
                                           App::DocumentObject* oldObj,
                                           App::DocumentObject* newObj) const
{
    auto res = tryReplaceLinkSubs(getContainer(), _pcLink, parent, oldObj, newObj, _SubList);
    if (!res.first) {
        return nullptr;
    }
    std::unique_ptr<PropertyXLink> p(new PropertyXLink);
    copyTo(*p, res.first, &res.second);
    return p.release();
}

Property* PropertyXLink::CopyOnLabelChange(App::DocumentObject* obj,
                                           const std::string& ref,
                                           const char* newLabel) const
{
    auto owner = dynamic_cast<const DocumentObject*>(getContainer());
    if (!owner || !owner->getDocument() || !_pcLink || !_pcLink->isAttachedToDocument()) {
        return nullptr;
    }
    auto subs = updateLinkSubs(_pcLink, _SubList, &updateLabelReference, obj, ref, newLabel);
    if (subs.empty()) {
        return nullptr;
    }
    std::unique_ptr<PropertyXLink> p(new PropertyXLink);
    copyTo(*p, _pcLink, &subs);
    return p.release();
}

void PropertyXLink::copyTo(PropertyXLink& other,
                           DocumentObject* linked,
                           std::vector<std::string>* subs) const
{
    if (!linked) {
        linked = _pcLink;
    }
    if (linked && linked->isAttachedToDocument()) {
        other.docName = linked->getDocument()->getName();
        other.objectName = linked->getNameInDocument();
        other.docInfo.reset();
        other.filePath.clear();
    }
    else {
        other.objectName = objectName;
        other.docName.clear();
        other.docInfo = docInfo;
        other.filePath = filePath;
    }
    if (subs) {
        other._SubList = std::move(*subs);
    }
    else {
        other._SubList = _SubList;
        other._ShadowSubList = _ShadowSubList;
    }
    other._Flags = _Flags;
}

Property* PropertyXLink::Copy() const
{
    std::unique_ptr<PropertyXLink> p(new PropertyXLink);
    copyTo(*p);
    return p.release();
}

void PropertyXLink::Paste(const Property& from)
{
    if (!from.isDerivedFrom<PropertyXLink>()) {
        throw Base::TypeError("Incompatible property to paste to");
    }

    const auto& other = static_cast<const PropertyXLink&>(from);
    if (!other.docName.empty()) {
        auto doc = GetApplication().getDocument(other.docName.c_str());
        if (!doc) {
            FC_WARN("Document '" << other.docName << "' not found");
            return;
        }
        auto obj = doc->getObject(other.objectName.c_str());
        if (!obj) {
            FC_WARN("Object '" << other.docName << '#' << other.objectName << "' not found");
            return;
        }
        setValue(obj,
                 std::vector<std::string>(other._SubList),
                 std::vector<ShadowSub>(other._ShadowSubList));
    }
    else {
        std::string docUuid;
        std::string objUuid;
        if (other._pcLink && other._pcLink->isAttachedToDocument()) {
            objUuid = other._pcLink->Uid.getValueStr();
            if (auto* linkedDoc = other._pcLink->getDocument()) {
                docUuid = linkedDoc->Uid.getValueStr();
            }
        }
        setValue(std::string(other.filePath),
                 std::string(other.objectName),
                 std::move(docUuid),
                 std::move(objUuid),
                 std::vector<std::string>(other._SubList),
                 std::vector<ShadowSub>(other._ShadowSubList));
    }
    setFlag(LinkAllowPartial, other.testFlag(LinkAllowPartial));
}

bool PropertyXLink::supportXLink(const App::Property* prop)
{
    return prop->isDerivedFrom<PropertyXLink>()
        || prop->isDerivedFrom<PropertyXLinkSubList>()
        || prop->isDerivedFrom<PropertyXLinkContainer>();
}

bool PropertyXLink::hasXLink(const App::Document* doc)
{
    for (auto& v : _DocInfoMap) {
        if (v.second->hasXLink(doc)) {
            return true;
        }
    }
    return false;
}

bool PropertyXLink::hasXLink(const std::vector<App::DocumentObject*>& objs,
                             std::vector<App::Document*>* unsaved)
{
    std::set<App::Document*> docs;
    bool ret = false;
    for (auto o : objs) {
        if (o && o->isAttachedToDocument() && docs.insert(o->getDocument()).second) {
            if (!hasXLink(o->getDocument())) {
                continue;
            }
            if (!unsaved) {
                return true;
            }
            ret = true;
            if (!o->getDocument()->isSaved()) {
                unsaved->push_back(o->getDocument());
            }
        }
    }
    return ret;
}

void PropertyXLink::restoreDocument(const App::Document& doc)
{
    DocInfo::restoreDocument(doc);
}

std::map<App::Document*, std::set<App::Document*>>
PropertyXLink::getDocumentOutList(App::Document* doc)
{
    std::map<App::Document*, std::set<App::Document*>> ret;
    for (auto& v : _DocInfoMap) {
        for (auto link : v.second->links) {
            if (!v.second->pcDoc || link->getScope() == LinkScope::Hidden
                || link->testStatus(Property::PropTransient)
                || link->testStatus(Property::Transient)
                || link->testStatus(Property::PropNoPersist)) {
                continue;
            }
            auto obj = dynamic_cast<App::DocumentObject*>(link->getContainer());
            if (!obj || !obj->isAttachedToDocument() || !obj->getDocument()) {
                continue;
            }
            if (doc && obj->getDocument() != doc) {
                continue;
            }
            ret[obj->getDocument()].insert(v.second->pcDoc);
        }
    }
    return ret;
}

std::map<App::Document*, std::set<App::Document*>>
PropertyXLink::getDocumentInList(App::Document* doc)
{
    std::map<App::Document*, std::set<App::Document*>> ret;
    for (auto& v : _DocInfoMap) {
        if (!v.second->pcDoc || (doc && doc != v.second->pcDoc) || v.second->links.empty()) {
            continue;
        }
        auto& docs = ret[v.second->pcDoc];
        for (auto link : v.second->links) {
            if (link->getScope() == LinkScope::Hidden || link->testStatus(Property::PropTransient)
                || link->testStatus(Property::Transient)
                || link->testStatus(Property::PropNoPersist)) {
                continue;
            }
            auto obj = dynamic_cast<App::DocumentObject*>(link->getContainer());
            if (obj && obj->isAttachedToDocument() && obj->getDocument()) {
                docs.insert(obj->getDocument());
            }
        }
    }
    return ret;
}

PyObject* PropertyXLink::getPyObject()
{
    if (!_pcLink) {
        Py_Return;
    }
    const auto& subs = getSubValues(false);
    if (subs.empty()) {
        return _pcLink->getPyObject();
    }
    Py::Tuple ret(2);
    ret.setItem(0, Py::Object(_pcLink->getPyObject(), true));
    PropertyString propString;
    if (subs.size() == 1) {
        propString.setValue(subs.front());
        ret.setItem(1, Py::asObject(propString.getPyObject()));
    }
    else {
        Py::List list(subs.size());
        int i = 0;
        for (auto& sub : subs) {
            propString.setValue(sub);
            list[i++] = Py::asObject(propString.getPyObject());
        }
        ret.setItem(1, list);
    }
    return Py::new_reference_to(ret);
}

void PropertyXLink::setPyObject(PyObject* value)
{
    if (PySequence_Check(value)) {
        Py::Sequence seq(value);
        if (seq.size() != 2) {
            throw Base::ValueError("Expect input sequence of size 2");
        }
        std::vector<std::string> subs;
        Py::Object pyObj(seq[0].ptr());
        Py::Object pySub(seq[1].ptr());
        if (pyObj.isNone()) {
            setValue(nullptr);
            return;
        }
        else if (!PyObject_TypeCheck(pyObj.ptr(), &DocumentObjectPy::Type)) {
            throw Base::TypeError("Expect the first element to be of 'DocumentObject'");
        }
        PropertyString propString;
        if (pySub.isString()) {
            propString.setPyObject(pySub.ptr());
            subs.push_back(propString.getStrValue());
        }
        else if (pySub.isSequence()) {
            Py::Sequence seq(pySub);
            subs.reserve(seq.size());
            for (Py_ssize_t i = 0; i < seq.size(); ++i) {
                Py::Object sub(seq[i]);
                if (!sub.isString()) {
                    throw Base::TypeError("Expect only string inside second argument");
                }
                propString.setPyObject(sub.ptr());
                subs.push_back(propString.getStrValue());
            }
        }
        else {
            throw Base::TypeError("Expect the second element to be a string or sequence of string");
        }
        setValue(static_cast<DocumentObjectPy*>(pyObj.ptr())->getDocumentObjectPtr(),
                 std::move(subs));
    }
    else if (PyObject_TypeCheck(value, &(DocumentObjectPy::Type))) {
        setValue(static_cast<DocumentObjectPy*>(value)->getDocumentObjectPtr());
    }
    else if (Py_None == value) {
        setValue(nullptr);
    }
    else {
        throw Base::TypeError(
            "type must be 'DocumentObject', 'None', or '(DocumentObject, SubName)' or "
            "'DocumentObject, [SubName..])");
    }
}

const char* PropertyXLink::getSubName(bool newStyle) const
{
    if (_SubList.empty() || _ShadowSubList.empty()) {
        return "";
    }
    return getSubNameWithStyle(_SubList[0], _ShadowSubList[0], newStyle, tmpShadow).c_str();
}

void PropertyXLink::getLinks(std::vector<App::DocumentObject*>& objs,
                             bool all,
                             std::vector<std::string>* subs,
                             bool newStyle) const
{
    if ((all || _pcScope != LinkScope::Hidden) && _pcLink && _pcLink->isAttachedToDocument()) {
        // we use to run this method everytime the program needed to access the sub-elements in
        // a property link, but it caused multiple issues (#23441 and #23402) so it has been
        // commented out.
        // updateElementReferences(_pcLink, false);
        objs.push_back(_pcLink);
        if (subs && _SubList.size() == _ShadowSubList.size()) {
            *subs = getSubValues(newStyle);
        }
    }
}

void PropertyXLink::getLinksTo(std::vector<App::ObjectIdentifier>& identifiers,
                               App::DocumentObject* obj,
                               const char* subname,
                               bool all) const
{
    if (all || _pcScope != LinkScope::Hidden) {
        if (obj && obj == _pcLink) {
            _getLinksTo(identifiers, obj, subname, _SubList, _ShadowSubList);
        }
    }
}

bool PropertyXLink::adjustLink(const std::set<App::DocumentObject*>& inList)
{
    if (_pcScope == LinkScope::Hidden) {
        return false;
    }
    if (!_pcLink || !_pcLink->isAttachedToDocument() || !inList.contains(_pcLink)) {
        return false;
    }
    auto subs = _SubList;
    auto link = adjustLinkSubs(this, inList, _pcLink, subs);
    if (link) {
        setValue(link, std::move(subs));
        return true;
    }
    return false;
}

std::vector<std::string> PropertyXLink::getSubValues(bool newStyle) const
{
    assert(_SubList.size() == _ShadowSubList.size());
    std::vector<std::string> ret;
    ret.reserve(_SubList.size());
    std::string tmp;
    for (size_t i = 0; i < _ShadowSubList.size(); ++i) {
        ret.push_back(getSubNameWithStyle(_SubList[i], _ShadowSubList[i], newStyle, tmp));
    }
    return ret;
}

std::vector<std::string> PropertyXLink::getSubValuesStartsWith(const char* starter,
                                                               bool newStyle) const
{
    (void)newStyle;

    std::vector<std::string> temp;
    for (const auto& it : _SubList) {
        if (strncmp(starter, it.c_str(), strlen(starter)) == 0) {
            temp.push_back(it);
        }
    }
    return temp;
}

void PropertyXLink::setAllowPartial(bool enable)
{
    setFlag(LinkAllowPartial, enable);
    if (enable) {
        return;
    }
    auto owner = dynamic_cast<const DocumentObject*>(getContainer());
    if (!owner) {
        return;
    }
    if (!App::GetApplication().isRestoring() && !owner->getDocument()->isPerformingTransaction()
        && !_pcLink && docInfo && !filePath.empty() && !objectName.empty()
        && (!docInfo->pcDoc || docInfo->pcDoc->testStatus(Document::PartialDoc))) {
        auto path = docInfo->getDocPath(filePath.c_str(), owner->getDocument(), false);
        if (!path.empty()) {
            App::GetApplication().openDocument(path.c_str());
        }
    }
}

//**************************************************************************
// PropertyXLinkSub
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyXLinkSub, App::PropertyXLink)
TYPESYSTEM_SOURCE(App::PropertyXLinkSubHidden, App::PropertyXLinkSub)

PropertyXLinkSub::PropertyXLinkSub(bool allowPartial, PropertyLinkBase* parent)
    : PropertyXLink(allowPartial, parent)
{}

PropertyXLinkSub::~PropertyXLinkSub() = default;

bool PropertyXLinkSub::upgrade(Base::XMLReader& reader, const char* typeName)
{
    if (strcmp(typeName, PropertyLinkSubGlobal::getClassTypeId().getName()) == 0
        || strcmp(typeName, PropertyLinkSub::getClassTypeId().getName()) == 0
        || strcmp(typeName, PropertyLinkSubChild::getClassTypeId().getName()) == 0) {
        App::PropertyLinkSub linkProp;
        linkProp.setContainer(getContainer());
        linkProp.Restore(reader);
        setValue(linkProp.getValue(), linkProp.getSubValues());
        return true;
    }
    return PropertyXLink::upgrade(reader, typeName);
}

PyObject* PropertyXLinkSub::getPyObject()
{
    if (!_pcLink) {
        Py_Return;
    }
    Py::Tuple ret(2);
    ret.setItem(0, Py::Object(_pcLink->getPyObject(), true));
    const auto& subs = getSubValues(false);
    Py::List list(subs.size());
    int i = 0;
    PropertyString propString;
    for (auto& sub : subs) {
        propString.setValue(sub);
        list[i++] = Py::asObject(propString.getPyObject());
    }
    ret.setItem(1, list);
    return Py::new_reference_to(ret);
}
