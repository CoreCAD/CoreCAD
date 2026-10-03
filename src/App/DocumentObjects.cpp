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

#include <bitset>
#include <stack>
#include <deque>
#include <iostream>
#include <utility>
#include <set>
#include <memory>
#include <new>
#include <string>
#include <map>
#include <vector>
#include <list>
#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>

#include <boost/algorithm/string.hpp>
#include <boost/bimap.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>

#include <boost/regex.hpp>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <QCryptographicHash>
#include <QCoreApplication>

#include <FCConfig.h>

#include <App/DocumentPy.h>
#include <Base/Interpreter.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/TimeInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Profiler.h>
#include <Base/Tools.h>
#include <Base/XMLTools.h>
#include <Base/Uuid.h>
#include <Base/Sequencer.h>
#include <Base/Stream.h>

#include "Document.h"
#include "RecipeText.h"
#include "private/DocumentP.h"
#include "Application.h"
#include "AutoTransaction.h"
#include "BackupPolicy.h"
#include "ExpressionParser.h"
#include "GeoFeature.h"
#include "License.h"
#include "Link.h"
#include "Origin.h"
#include "MergeDocuments.h"
#include "StringHasher.h"
#include "GeometryCache.h"
#include "SealedArchive.h"
#include "StoredRecipe.h"
#include "Transactions.h"



FC_LOG_LEVEL_INIT("App", true, true, true)

using Base::Console;
using Base::streq;
using Base::Writer;
using namespace App;
using namespace boost;

#if FC_DEBUG
#define FC_LOGFEATUREUPDATE
#endif

namespace fs = std::filesystem;

namespace
{

// Derive an object's integer id from its durable UUID.
//
// The integer id is stamped into every element-map name (as the "master tag") and is the
// key `getObjectByID` resolves. Historically it came from a per-document counter, which two
// copies of the same saved file resume identically -- so a feature added on one branch and a
// different feature added on another mint the SAME id, and their computed faces collide by
// name (breaking any branch/merge that pairs sub-shapes by name). The UUID is already branch-
// unique, so we project it onto the integer instead of counting. A compact integer is kept
// (element-map names embed it pervasively and nest it), but its value now carries real
// identity. Deterministic (FNV-1a) so it never depends on std::hash randomization; the value
// is persisted, so it is computed once at birth and travels with the object.
long deriveObjectIdFromUuid(const std::string& uuid)
{
    // FNV-1a over the UUID string.
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char ch : uuid) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    // Tag 0 means "no tag" and negative tags are semantically special in the element map,
    // so force a strictly-positive value by clearing the sign bit and lifting 0 to 1.
    long id = static_cast<long>(hash & 0x7FFFFFFFFFFFFFFFULL);
    return id != 0 ? id : 1;
}

}  // namespace

void Document::renameObjectIdentifiers(
    const std::map<ObjectIdentifier, ObjectIdentifier>& paths,
    const std::function<bool(const DocumentObject*)>& selector) // NOLINT
{
    std::map<ObjectIdentifier, ObjectIdentifier> extendedPaths;

    auto it = paths.begin();
    while (it != paths.end()) {
        extendedPaths[it->first.canonicalPath()] = it->second.canonicalPath();
        ++it;
    }

    for (const auto object : d->objectArray) {
        if (selector(object)) {
            object->renameObjectIdentifiers(extendedPaths);
        }
    }
}

DocumentObject* Document::addObject(const char* sType,
                                    const char* pObjectName,
                                    const bool isNew,
                                    const char* viewType,
                                    const bool isPartial)
{
    // Looked up among what is registered, never imported on the strength of the name. A
    // document states the type of each object it holds, so importing from that name made a
    // file decide what code this session ran. Every module the program installed is loaded at
    // startup (FreeCADInit.py), so there is nothing left for a name to have to fetch.
    const Base::Type type =
        Base::Type::getTypeIfDerivedFrom(sType, DocumentObject::getClassTypeId(), false);
    if (type.isBad()) {
        std::stringstream str;
        str << "Document::addObject: '" << sType << "' is not a document object type";
        throw Base::TypeError(str.str());
    }

    void* typeInstance = type.createInstance();
    if (!typeInstance) {
        return nullptr;
    }

    auto* pcObject = static_cast<DocumentObject*>(typeInstance);
    pcObject->setDocument(this);

    _addObject(pcObject,
               pObjectName,
               AddObjectOption::SetNewStatus
                   | (isPartial ? AddObjectOption::SetPartialStatus : AddObjectOption::UnsetPartialStatus)
                   | (isNew ? AddObjectOption::DoSetup : AddObjectOption::None)
                   | AddObjectOption::ActivateObject,
               viewType);

    // return the Object
    return pcObject;
}

std::vector<DocumentObject*>
Document::addObjects(const char* sType, const std::vector<std::string>& objectNames, bool isNew)
{
    Base::Type type =
        Base::Type::getTypeIfDerivedFrom(sType, DocumentObject::getClassTypeId(), false);
    if (type.isBad()) {
        std::stringstream str;
        str << "'" << sType << "' is not a document object type";
        throw Base::TypeError(str.str());
    }

    std::vector<DocumentObject*> objects;
    objects.resize(objectNames.size());
    std::generate(objects.begin(), objects.end(), [&] {
        return static_cast<DocumentObject*>(type.createInstance());
    });
    // the type instance could be a null pointer, it is enough to check the first element
    if (!objects.empty() && !objects[0]) {
        objects.clear();
        return objects;
    }

    for (auto it = objects.begin(); it != objects.end(); ++it) {
        size_t index = std::distance(objects.begin(), it);
        DocumentObject* pcObject = *it;
        pcObject->setDocument(this);

        // Add the object but only activate the last one
        bool isLast = index == (objects.size() - 1);
        _addObject(pcObject,
                   objectNames[index].c_str(),
                   AddObjectOption::SetNewStatus
                       | (isNew ? AddObjectOption::DoSetup : AddObjectOption::None)
                       | (isLast ? AddObjectOption::ActivateObject : AddObjectOption::None));
    }

    return objects;
}

void Document::addObject(DocumentObject* obj, const char* name)
{
    if (obj->getDocument()) {
        throw Base::RuntimeError("Document object is already added to a document");
    }

    obj->setDocument(this);

    _addObject(obj, name, AddObjectOption::SetNewStatus | AddObjectOption::ActivateObject);
}

namespace
{
const char* contentScopeName(DocumentObject::ContentScope scope)
{
    using CS = DocumentObject::ContentScope;
    switch (scope) {
        case CS::Sketch: return "Sketch";
        case CS::Feature: return "Feature";
        case CS::Body: return "Body";
        case CS::AssemblyItem: return "AssemblyItem";
        case CS::DrawingView: return "DrawingView";
        case CS::Spreadsheet: return "Spreadsheet";
        case CS::Generic: return "Generic";
    }
    return "Generic";
}
}  // namespace

void Document::_addObject(DocumentObject* pcObject, const char* pObjectName, AddObjectOptions options, const char* viewType)
{
    // Content-scope admission door (ARCHITECTURE §7.1/§7.4, Amendment 8 Clause 8.1):
    // a typed document refuses an object whose declared kind its type does not admit,
    // failing loud before the object is woven into the document — never a silent drop,
    // never a partial admit + rollback. Untyped documents and Generic objects pass.
    // This one guard covers all three doors, since creation, transfer (moveObject),
    // and load (readObjects) all funnel through here.
    const DocumentObject::ContentScope scope = pcObject->getContentScope();
    if (!admitsContentScope(scope)) {
        throw DocumentContentScopeError(
            "Document type '" + DocumentType.getStrValue() + "' refuses object '"
            + pcObject->getTypeId().getName() + "' (content scope " + contentScopeName(scope)
            + "): it lies outside this document's content scope.");
    }

    // get unique name
    std::string ObjectName;
    if (!Base::Tools::isNullOrEmpty(pObjectName)) {
        ObjectName = getUniqueObjectName(pObjectName);
    }
    else {
        ObjectName = getUniqueObjectName(pcObject->getTypeId().getName());
    }

    // insert in the name map
    d->objectMap[ObjectName] = pcObject;
    d->objectNameManager.addExactName(ObjectName);
    // cache the pointer to the name string in the Object (for performance of
    // DocumentObject::getNameInDocument())
    pcObject->pcNameInDocument = &(d->objectMap.find(ObjectName)->first);
    // Register the current Label even though it might be about to change
    registerLabel(pcObject->Label.getStrValue());

    // generate object id and add to id map + object array
    if (pcObject->_Id == 0) {
        // For a genuinely new object (DoSetup) whose durable UUID is already final, derive the
        // id from that UUID so it is branch-unique (see deriveObjectIdFromUuid). Restore and
        // import pass isNew=false: they stamp the persisted UUID only AFTER this point and
        // instead prime lastObjectId to reproduce the persisted id, so those paths must keep
        // the legacy counter -- deriving here would hash the constructor's throwaway UUID and
        // drift the id away from the one baked into the restored element-map names.
        const std::string uuid = pcObject->Uid.getValueStr();
        long id;
        if (options.testFlag(AddObjectOption::DoSetup) && !uuid.empty()) {
            id = deriveObjectIdFromUuid(uuid);
            // Perturb on the (astronomically unlikely) in-document hash clash so the id stays
            // a unique key into objectIdMap.
            while (d->objectIdMap.count(id) != 0) {
                id = id > 0 ? id + 1 : 1;  // step forward; wrap negatives to a positive tag
            }
        }
        else {
            id = ++d->lastObjectId;
        }
        pcObject->_Id = id;
    }
    d->objectIdMap[pcObject->_Id] = pcObject;
    d->objectUuidMapDirty = true;
    d->objectArray.push_back(pcObject);

     // do no transactions if we do a rollback!
    if (!d->rollback) {
        // Undo stuff
        _checkTransaction(nullptr, nullptr, __LINE__);
        if (d->activeUndoTransaction) {
            d->activeUndoTransaction->addObjectDel(pcObject);
        }
     }
    // If we are restoring, don't set the Label object now; it will be restored later. This is to
    // avoid potential duplicate label conflicts later.
    if (options.testFlag(AddObjectOption::SetNewStatus) && !d->StatusBits.test(Restoring)) {
        pcObject->Label.setValue(ObjectName);
    }

    // Call the object-specific initialization
    if (!isPerformingTransaction() && options.testFlag(AddObjectOption::DoSetup)) {
        try {
            pcObject->setupObject();
        }
        catch (...) {
            // setupObject() is a validation chokepoint that may legitimately reject
            // the object (e.g. a Body spawned into a document with no world frame).
            // Undo the partial registration so the throw cannot strand a half-created
            // object in the document (issue #31). signalNewObject has not fired yet,
            // so no observer knows about pcObject -- we reverse the internal
            // bookkeeping directly and emit no removal signals. Only the internally
            // owning create paths (addObject-by-type, addObjects) set DoSetup, so
            // pcObject is owned here and deleting it dangles no caller reference.
            if (!d->rollback && d->activeUndoTransaction) {
                // Drops the addObjectDel entry recorded above (Del status -> erased).
                d->activeUndoTransaction->addObjectNew(pcObject);
            }
            for (auto it = d->objectArray.begin(); it != d->objectArray.end(); ++it) {
                if (*it == pcObject) {
                    d->objectArray.erase(it);
                    break;
                }
            }
            d->objectIdMap.erase(pcObject->_Id);
            d->objectUuidMapDirty = true;
            d->objectNameManager.removeExactName(ObjectName);
            unregisterLabel(pcObject->Label.getStrValue());
            pcObject->pcNameInDocument = nullptr;
            d->objectMap.erase(ObjectName);
            delete pcObject;
            throw;
        }
    }

    if (options.testFlag(AddObjectOption::SetNewStatus)) {
        pcObject->setStatus(ObjectStatus::New, true);
    }
    if (options.testFlag(AddObjectOption::SetPartialStatus) || options.testFlag(AddObjectOption::UnsetPartialStatus)) {
        pcObject->setStatus(ObjectStatus::PartialObject, options.testFlag(AddObjectOption::SetPartialStatus));
    }

    if (Base::Tools::isNullOrEmpty(viewType)) {
        viewType = pcObject->getViewProviderNameOverride();
    }
    pcObject->_pcViewProviderName = viewType ? viewType : "";

    if (!testStatus(Restoring)) {
        d->movedOnFromItsFile = true;
    }
    signalNewObject(*pcObject);

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        signalTransactionAppend(*pcObject, d->activeUndoTransaction);
    }

    if (options.testFlag(AddObjectOption::ActivateObject)) {
        d->activeObject = pcObject;
        signalActivatedObject(*pcObject);
    }
}

bool Document::containsObject(const DocumentObject* pcObject) const
{
    // We could look for the object in objectMap (keyed by object name),
    // or search in objectArray (a O(n) vector search) but looking by Id
    // in objectIdMap would be fastest.
    auto found = d->objectIdMap.find(pcObject->getID());
    return found != d->objectIdMap.end() && found->second == pcObject;
}

/// Remove an object out of the document
void Document::removeObject(const DocumentObject* object)
{
    if (object->getDocument() == this) {
        removeObject(object->getNameInDocument());
    }
}

/// Remove an object out of the document
void Document::removeObject(const char* sName)
{
    auto pos = d->objectMap.find(sName);
    if (pos == d->objectMap.end()){
        FC_MSG("Object " << sName << " already deleted in document " << getName());
        return;
    }

    if (pos->second->testStatus(ObjectStatus::Remove)) {
        FC_LOG("Avoid recursive deletion of " << pos->second->getFullName());
        return;
    }

    // Never mutate the object graph synchronously while a recompute is in flight:
    // a removal during signalRecomputed() (e.g. Cruth multi-output marker retirement)
    // tears down objects while the document is still marked Recomputing and crashes.
    // Defer to the pendingRemove queue, which is flushed once recompute settles.
    if (pos->second->testStatus(ObjectStatus::PendingRecompute) || testStatus(Document::Recomputing)) {
        // TODO: shall we allow removal if there is active undo transaction?
        FC_LOG("pending remove of " << sName << " after recomputing document " << getName());
        d->pendingRemove.emplace_back(pos->second);
        return;
    }

    _removeObject(pos->second, RemoveObjectOption::MayRemoveWhileRecomputing | RemoveObjectOption::MayDestroyOutOfTransaction);
}
void Document::_removeObject(DocumentObject* pcObject, RemoveObjectOptions options)
{
    if (!options.testFlag(RemoveObjectOption::MayRemoveWhileRecomputing) && testStatus(Document::Recomputing)) {
        FC_ERR("Cannot delete " << pcObject->getFullName() << " while recomputing");
        return;
    }

    TransactionLocker tlock(this);

    _checkTransaction(pcObject, nullptr, __LINE__);

    auto pos = d->objectMap.find(pcObject->getNameInDocument());
    if (pos == d->objectMap.end()) {
        FC_ERR("Internal error, could not find " << pcObject->getFullName() << " to remove");
        return;
    }

    if (options.testFlag(RemoveObjectOption::PreserveChildrenVisibility)
        && !d->rollback && d->activeUndoTransaction && pcObject->hasChildElement()) {
        // Preserve link group sub object global visibilities. Normally those
        // claimed object should be hidden in global coordinate space. However,
        // when the group is deleted, the user will naturally try to show the
        // children, which may now in the global space. When the parent is
        // undeleted, having its children shown in both the local and global
        // coordinate space is very confusing. Hence, we preserve the visibility
        // here
        for (auto& sub : pcObject->getSubObjects()) {
            if (sub.empty()) {
                continue;
            }
            if (sub[sub.size() - 1] != '.') {
                sub += '.';
            }
            auto sobj = pcObject->getSubObject(sub.c_str());
            if (sobj && sobj->getDocument() == this && !sobj->Visibility.getValue()) {
                d->activeUndoTransaction->addObjectChange(sobj, &sobj->Visibility);
            }
        }
    }

    if (d->activeObject == pcObject) {
        d->activeObject = nullptr;
    }

    // Mark the object as about to be removed
    pcObject->setStatus(ObjectStatus::Remove, true);
    if (!d->undoing && !d->rollback) {
        pcObject->unsetupObject();
    }
    if (!testStatus(Restoring)) {
        d->movedOnFromItsFile = true;
    }
    signalDeletedObject(*pcObject);
    signalTransactionRemove(*pcObject, d->rollback ? nullptr : d->activeUndoTransaction);
    breakDependency(pcObject, true);

    // TODO Check me if it's needed (2015-09-01, Fat-Zer)
    // remove the tip if needed
    if (Tip.getValue() == pcObject) {
        Tip.setValue(nullptr);
        TipName.setValue("");
    }

    // remove from map
    pcObject->setStatus(ObjectStatus::Remove, false);  // Unset the bit to be on the safe side
    d->objectIdMap.erase(pcObject->_Id);
    d->objectUuidMapDirty = true;
    d->objectNameManager.removeExactName(pos->first);
    unregisterLabel(pcObject->Label.getStrValue());

    // do no transactions if we do a rollback!
    if (!d->rollback && d->activeUndoTransaction) {
        d->activeUndoTransaction->addObjectNew(pcObject);
    }

    std::unique_ptr<DocumentObject> tobedestroyed;
    if ((options.testFlag(RemoveObjectOption::MayDestroyOutOfTransaction) && !d->rollback && !d->activeUndoTransaction)
        || (options.testFlag(RemoveObjectOption::DestroyOnRollback) && d->rollback)) {
        // if not saved in undo -> delete object later
        std::unique_ptr<DocumentObject> delobj(pos->second);
        tobedestroyed.swap(delobj);
        tobedestroyed->setStatus(ObjectStatus::Destroy, true);
    }

    for (auto it = d->objectArray.begin();
         it != d->objectArray.end();
         ++it) {
        if (*it == pcObject) {
            d->objectArray.erase(it);
            break;
        }
    }

    // In case the object gets deleted the pointer must be nullified
    if (tobedestroyed) {
        tobedestroyed->pcNameInDocument = nullptr;
    }

    // Erase last to avoid invalidating pcObject->pcNameInDocument
    // when it is still needed in Transaction::addObjectNew
    d->objectMap.erase(pos);
}

void Document::breakDependency(DocumentObject* pcObject, const bool clear) // NOLINT
{
    // Nullify all dependent objects
    PropertyLinkBase::breakLinks(pcObject, d->objectArray, clear);
}

std::vector<DocumentObject*>
Document::copyObject(const std::vector<DocumentObject*>& objs, bool recursive, bool returnAll)
{
    std::vector<DocumentObject*> deps;
    if (!recursive) {
        deps = objs;
    }
    else {
        deps = getDependencyList(objs, DepNoXLinked | DepSort);
    }

    if (!testStatus(TempDoc) && !isSaved() && PropertyXLink::hasXLink(deps)) {
        throw Base::RuntimeError(
            "Document must be saved at least once before link to external objects");
    }

    // A relocation keeps the identities it arrived with and rewires nothing, so it is not bound
    // here; a duplication mints fresh ones and must rewire, which is the thing that cannot be
    // done for a reference that never resolved (Amendment 19 Clause 19.3).
    if (!testStatus(Relocating)) {
        refuseDuplicationThatCannotBeRewired(deps);
    }

    if (deps.empty()) {
        return {};
    }

    // A copy becomes objects in a document, so it is a thing that can become the record, and it
    // is written by the writer that writes the record and read by that reader (Amendment 19
    // Clause 19.5). It used to go out through a second writer -- the legacy archive and each
    // property's own Save -- and an object holding a statement this build could not honour was
    // copied without it, so the copy asserted as authored what the original only failed to honour.
    //
    // Material rides beside the rendering rather than inside it, exactly as it does beside a
    // document on disk. It goes in an envelope of its own and not in either document's project
    // folder: rendering into the source's folder would leave entries there as a side effect of
    // copying, and a document that has never been saved has no folder at all -- which is the
    // ordinary case for the document a person is pasting into.
    const fs::path envelope =
        fs::path(Base::FileInfo::getTempPath()) / ("copy-" + Base::Uuid::createUuid());
    std::error_code ignored;
    fs::create_directories(envelope, ignored);
    const auto discardEnvelope = [&envelope] {
        std::error_code failed;
        fs::remove_all(envelope, failed);
    };

    std::vector<std::pair<std::string, DocumentObject*>> arrived;
    try {
        // The rendering names the document it came from; the objects in it may come from more
        // than one, and each says which is its own.
        const std::vector<const DocumentObject*> carried(deps.begin(), deps.end());
        const std::string rendering =
            formatStoredRecipe(*deps.front()->getDocument(),
                               envelope.string(),
                               RecipeScope {carried, /*withDocumentProperties=*/false});

        std::istringstream text(rendering);
        arrived = acceptStoredRecipeObjects(text, envelope.string());
    }
    catch (...) {
        discardEnvelope();
        throw;
    }
    discardEnvelope();

    // Each copy, found by the durable id its original still wears. The rendering states objects
    // in durable-id order rather than the order they were asked for, so position cannot say which
    // copy came from which original.
    std::map<std::string, DocumentObject*> byOriginal(arrived.begin(), arrived.end());
    std::vector<DocumentObject*> imported;
    imported.reserve(deps.size());
    for (DocumentObject* source : deps) {
        const auto found = byOriginal.find(source->Uid.getValueStr());
        if (found != byOriginal.end()) {
            imported.push_back(found->second);
        }
    }

    if (returnAll || imported.size() != deps.size()) {
        return imported;
    }

    std::unordered_map<DocumentObject*, size_t> indices;
    size_t i = 0;
    for (auto o : deps) {
        indices[o] = i++;
    }
    std::vector<DocumentObject*> result;
    result.reserve(objs.size());
    for (auto o : objs) {
        result.push_back(imported[indices[o]]);
    }
    return result;
}

std::vector<DocumentObject*>
Document::importLinks(const std::vector<DocumentObject*>& objs)
{
    std::set<DocumentObject*> links;
    getLinksTo(links, nullptr, GetLinkExternal, 0, objs);

    std::vector<DocumentObject*> vecObjs;
    vecObjs.insert(vecObjs.end(), links.begin(), links.end());
    std::vector<DocumentObject*> depObjs = getDependencyList(vecObjs);
    if (depObjs.empty()) {
        FC_ERR("nothing to import");
        return depObjs;
    }

    for (auto it = depObjs.begin(); it != depObjs.end();) {
        auto obj = *it;
        if (obj->getDocument() == this) {
            it = depObjs.erase(it);
            continue;
        }
        ++it;
        if (obj->testStatus(PartialObject)) {
            throw Base::RuntimeError(
                "Cannot import partial loaded object. Please reload the current document");
        }
    }

    Base::FileInfo fi(Application::getTempFileName());
    {
        // save stuff to temp file
        Base::ofstream str(fi, std::ios::out | std::ios::binary);
        MergeDocuments mimeView(this);
        exportObjects(depObjs, str);
        str.close();
    }
    Base::ifstream str(fi, std::ios::in | std::ios::binary);
    MergeDocuments mimeView(this);
    depObjs = mimeView.importObjects(str);
    str.close();
    fi.deleteFile();

    const auto& nameMap = mimeView.getNameMap();

    // First, find all link type properties that needs to be changed
    std::map<Property*, std::unique_ptr<Property>> propMap;
    std::vector<Property*> propList;
    for (auto obj : links) {
        propList.clear();
        obj->getPropertyList(propList);
        for (auto prop : propList) {
            auto linkProp = freecad_cast<PropertyLinkBase*>(prop);
            if (linkProp && !prop->testStatus(Property::Immutable) && !obj->isReadOnly(prop)) {
                auto copy = linkProp->CopyOnImportExternal(nameMap);
                if (copy) {
                    propMap[linkProp].reset(copy);
                }
            }
        }
    }

    // Then change them in one go. Note that we don't make change in previous
    // loop, because a changed link property may break other depending link
    // properties, e.g. a link sub referring to some sub object of an xlink, If
    // that sub object is imported with a different name, and xlink is changed
    // before this link sub, it will break.
    for (auto& v : propMap) {
        v.first->Paste(*v.second);
    }

    return depObjs;
}

DocumentObject* Document::moveObject(DocumentObject* obj, const bool recursive)
{
    if (!obj) {
        return nullptr;
    }
    Document* that = obj->getDocument();
    if (that == this) {
        return nullptr;  // nothing todo
    }

    // True object move without copy is only safe when undo is off on both
    // documents.
    if (!recursive && (d->iUndoMode == 0) && (that->d->iUndoMode == 0) && !that->d->rollback) {
        // all object of the other document that refer to this object must be nullified
        that->breakDependency(obj, false);
        const std::string objname = getUniqueObjectName(obj->getNameInDocument());
        that->_removeObject(obj);
        this->_addObject(obj, objname.c_str());
        obj->setDocument(this);
        return obj;
    }

    std::vector<DocumentObject*> deps;
    if (recursive) {
        deps = getDependencyList({obj}, DepNoXLinked | DepSort);
    }
    else {
        deps.push_back(obj);
    }

    // A move is relocation, not duplication (§10.7): the object arrives with the
    // identity it already had and keeps it, down to its contents. Declaring that
    // for the duration of the copy is what stops the import path minting afresh.
    std::vector<DocumentObject*> objs;
    {
        Base::ObjectStatusLocker<Status, Document> relocating(Status::Relocating, this);
        objs = copyObject(deps, false);
    }
    if (objs.empty()) {
        return nullptr;
    }
    // Some object may delete its children if deleted, so we collect the IDs
    // or all depending objects for safety reason.
    std::vector<int> ids;
    ids.reserve(deps.size());
    for (const auto o : deps) {
        ids.push_back(static_cast<int>(o->getID()));
    }

    // We only remove object if it is the moving object or it has no
    // depending objects, i.e. an empty inList, which is why we need to
    // iterate the depending list backwards.
    for (auto iter = ids.rbegin(); iter != ids.rend(); ++iter) {
        const auto o = that->getObjectByID(*iter);
        if (!o) {
            continue;
        }
        if (iter == ids.rbegin()) {
            that->removeObject(o->getNameInDocument());
            continue;
        }
        if (!o->getInList().empty()) {
            continue;
        }
        // An empty inList means nothing that this session could READ references the object. That
        // is not the same as nothing referencing it: a statement the source document could not
        // honour may name anything, including this (Amendment 19 Clause 19.3). So the object
        // stays where it is, and the reason is said rather than left to be noticed.
        if (!that->seesEveryReference()) {
            Base::Console().warning(
                "'%s' was left in '%s' rather than cleaned up: %s, so an absence of references "
                "is not evidence that nothing references it.\n",
                o->getNameInDocument(),
                that->getName(),
                that->whyReferencesAreFrozen().c_str());
            continue;
        }
        that->removeObject(o->getNameInDocument());
    }
    return objs.back();
}
