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

void Document::addRecomputeObject(DocumentObject* obj) // NOLINT
{
    if (testStatus(Status::Restoring) && obj) {
        setStatus(Status::RecomputeOnRestore, true);
        d->touchedObjs.insert(obj);
        obj->touch();
    }
}

void Document::getLinksTo(std::set<DocumentObject*>& links,
                          const DocumentObject* obj,
                          const int options,
                          const int maxCount,
                          const std::vector<DocumentObject*>& objs) const
{
    std::map<const DocumentObject*, std::vector<DocumentObject*>> linkMap;

    for (auto o : !objs.empty() ? objs : d->objectArray) {
        if (o == obj) {
            continue;
        }
        auto linked = o;
        if ((options & GetLinkArrayElement) != 0) {
            linked = o->getLinkedObject(false);
        }
        else {
            const auto ext = o->getExtensionByType<LinkBaseExtension>(true);
            linked =
                ext ? ext->getTrueLinkedObject(false, nullptr, 0, true) : o->getLinkedObject(false);
        }

        if (linked && linked != o) {
            if ((options & GetLinkRecursive) != 0) {
                linkMap[linked].push_back(o);
            }
            else if (linked == obj || !obj) {
                if (((options & GetLinkExternal) != 0) && linked->getDocument() == o->getDocument()) {
                    continue;
                }
                if ((options & GetLinkedObject) != 0) {
                    links.insert(linked);
                }
                else {
                    links.insert(o);
                }
                if ((maxCount != 0) && maxCount <= static_cast<int>(links.size())) {
                    return;
                }
            }
        }
    }

    if ((options & GetLinkRecursive) == 0) {
        return;
    }

    std::vector<const DocumentObject*> current(1, obj);
    for (int depth = 0; !current.empty(); ++depth) {
        if (GetApplication().checkLinkDepth(depth, MessageOption::Error) == 0) {
            break;
        }
        std::vector<const DocumentObject*> next;
        for (const DocumentObject* o : current) {
            auto iter = linkMap.find(o);
            if (iter == linkMap.end()) {
                continue;
            }
            for (DocumentObject* link : iter->second) {
                if (links.insert(link).second) {
                    if ((maxCount != 0) && maxCount <= static_cast<int>(links.size())) {
                        return;
                    }
                    next.push_back(link);
                }
            }
        }
        current = std::move(next);
    }
}

bool Document::hasLinksTo(const DocumentObject* obj) const
{
    std::set<DocumentObject*> links;
    getLinksTo(links, obj, 0, 1);
    return !links.empty();
}

std::vector<DocumentObject*> Document::getInList(const DocumentObject* me) const
{
    // result list
    std::vector<DocumentObject*> result;
    // go through all objects
    for (const auto& [name, object] : d->objectMap) {
        // get the outList and search if me is in that list
        std::vector<DocumentObject*> OutList = object->getOutList();
        for (const auto obj : OutList) {
            if (obj && obj == me) {
                // add the parent object
                result.push_back(object);
            }
        }
    }
    return result;
}

// This function unifies the old _rebuildDependencyList() and
// getDependencyList().  The algorithm basically obtains the object dependency
// by recrusivly visiting the OutList of each object in the given object array.
// It makes sure to call getOutList() of each object once and only once, which
// makes it much more efficient than calling getRecursiveOutList() on each
// individual object.
//
// The problem with the original algorithm is that, it assumes the objects
// inside any OutList are all within the given object array, so it does not
// recursively call getOutList() on those dependent objects inside. This
// assumption is broken by the introduction of PropertyXLink which can link to
// external object.
//
static void buildDependencyList(const std::vector<DocumentObject*>& objectArray,
                                const int options,
                                 std::vector<DocumentObject*>* depObjs,
                                 DependencyList* depList,
                                 std::map<DocumentObject*, Vertex>* objectMap,
                                 bool* touchCheck = nullptr)
{
    std::map<DocumentObject*, std::vector<DocumentObject*>> outLists;
    std::deque<DocumentObject*> objs;

    if (objectMap) {
        objectMap->clear();
    }
    if (depList) {
        depList->clear();
    }

    const int op = ((options & Document::DepNoXLinked) != 0) ? DocumentObject::OutListNoXLinked : 0;
    for (auto obj : objectArray) {
        objs.push_back(obj);
        while (!objs.empty()) {
            auto objF = objs.front();
            objs.pop_front();
            if (!objF || !objF->isAttachedToDocument()) {
                continue;
            }

            auto it = outLists.find(objF);
            if (it != outLists.end()) {
                continue;
            }

            if (touchCheck) {
                if (objF->isTouched() || (objF->mustExecute() != 0)) {
                    // early termination on touch check
                    *touchCheck = true;
                    return;
                }
            }
            if (depObjs) {
                depObjs->push_back(objF);
            }
            if (objectMap && depList) {
                (*objectMap)[objF] = add_vertex(*depList);
            }

            auto& outList = outLists[objF];
            outList = objF->getOutList(op);
            objs.insert(objs.end(), outList.begin(), outList.end());
        }
    }

    if (objectMap && depList) {
        for (const auto& [key, objects] : outLists) {
            for (auto obj : objects) {
                if (obj && obj->isAttachedToDocument()) {
                    add_edge((*objectMap)[key], (*objectMap)[obj], *depList);
                }
            }
        }
    }
}

std::vector<DocumentObject*>
Document::getDependencyList(const std::vector<DocumentObject*>& objs, int options)
{
    std::vector<DocumentObject*> ret;
    if ((options & DepSort) == 0) {
        buildDependencyList(objs, options, &ret, nullptr, nullptr);
        return ret;
    }

    DependencyList depList;
    std::map<DocumentObject*, Vertex> objectMap;
    std::map<Vertex, DocumentObject*> vertexMap;

    buildDependencyList(objs, options, nullptr, &depList, &objectMap);

    for (auto& v : objectMap) {
        vertexMap[v.second] = v.first;
    }

    std::list<Vertex> make_order;
    try {
        boost::topological_sort(depList, std::front_inserter(make_order));
    }
    catch (const std::exception& e) {
        if ((options & DepNoCycle) != 0) {
            // Use boost::strong_components to find cycles. It groups strongly
            // connected vertices as components, and therefore each component
            // forms a cycle.
            std::vector<int> c(vertexMap.size());
            std::map<int, std::vector<Vertex>> components;
            boost::strong_components(
                depList,
                boost::make_iterator_property_map(c.begin(),
                                                  boost::get(boost::vertex_index, depList),
                                                  c[0]));
            for (size_t i = 0; i < c.size(); ++i) {
                components[c[i]].push_back(i);
            }

            FC_ERR("Dependency cycles: ");
            std::ostringstream ss;
            ss << '\n';
            for (auto& [key, vertexes] : components) {
                if (vertexes.size() == 1) {
                    // For components with only one member, we still need to
                    // check if there it is self looping.
                    auto it = vertexMap.find(vertexes[0]);
                    if (it == vertexMap.end()) {
                        continue;
                    }
                    // Try search the object in its own out list
                    for (auto obj : it->second->getOutList()) {
                        if (obj == it->second) {
                            ss << '\n' << it->second->getFullName() << '\n';
                            break;
                        }
                    }
                    continue;
                }
                // For components with more than one member, they form a loop together
                for (size_t i = 0; i < vertexes.size(); ++i) {
                    auto it = vertexMap.find(vertexes[i]);
                    if (it == vertexMap.end()) {
                        continue;
                    }
                    if (i % 6 == 0) {
                        ss << '\n';
                    }
                    ss << it->second->getFullName() << ", ";
                }
                ss << '\n';
            }
            FC_ERR(ss.str());
            FC_THROWM(Base::BadGraphError, e.what());
        }
        FC_ERR(e.what());
        ret = DocumentP::partialTopologicalSort(objs);
        std::reverse(ret.begin(), ret.end());
        return ret;
    }

    for (auto i = make_order.rbegin(); i != make_order.rend(); ++i) {
        ret.push_back(vertexMap[*i]);
    }
    return ret;
}

std::vector<Document*> Document::getDependentDocuments(const bool sort)
{
    return getDependentDocuments({this}, sort);
}

std::vector<Document*> Document::getDependentDocuments(std::vector<Document*> docs,
                                                       const bool sort)
{
    DependencyList depList;
    std::map<Document*, Vertex> docMap;
    std::map<Vertex, Document*> vertexMap;

    std::vector<Document*> ret;
    if (docs.empty()) {
        return ret;
    }

    auto outLists = PropertyXLink::getDocumentOutList();
    std::set<Document*> docSet;
    docSet.insert(docs.begin(), docs.end());
    if (sort) {
        for (auto doc : docs) {
            docMap[doc] = add_vertex(depList);
        }
    }
    while (!docs.empty()) {
        auto doc = docs.back();
        docs.pop_back();

        auto it = outLists.find(doc);
        if (it == outLists.end()) {
            continue;
        }

        const auto& vertex = docMap[doc];
        for (auto depDoc : it->second) {
            if (docSet.insert(depDoc).second) {
                docs.push_back(depDoc);
                if (sort) {
                    docMap[depDoc] = add_vertex(depList);
                }
            }
            add_edge(vertex, docMap[depDoc], depList);
        }
    }

    if (!sort) {
        ret.insert(ret.end(), docSet.begin(), docSet.end());
        return ret;
    }

    std::list<Vertex> make_order;
    try {
        boost::topological_sort(depList, std::front_inserter(make_order));
    }
    catch (const std::exception& e) {
        std::string msg("Document::getDependentDocuments: ");
        msg += e.what();
        throw Base::RuntimeError(msg);
    }

    for (auto& v : docMap) {
        vertexMap[v.second] = v.first;
    }
    for (auto rIt = make_order.rbegin(); rIt != make_order.rend(); ++rIt) {
        ret.push_back(vertexMap[*rIt]);
    }
    return ret;
}

/**
 * @brief Signal that object identifiers, typically a property or document object has been renamed.
 *
 * This function iterates through all document object in the document, and calls its
 * renameObjectIdentifiers functions.
 *
 * @param paths Map with current and new names
 */

int Document::recompute(const std::vector<DocumentObject*>& objs,
                        bool force,
                        bool* hasError,
                        int options)
{
    ZoneScoped;

    // Recompute can execute Python-backed features. Keep the GIL for the full
    // recompute so async recompute still serializes Python execution the same
    // way the main-thread path does, preserving compatibility with existing
    // Python-backed objects and addons. Main-thread signal hops such as
    // signalBeforeRecompute() temporarily release it when they need to run
    // Python on the GUI thread to avoid deadlocks.
    Base::PyGILStateLocker locker;

    if (d->undoing || d->rollback) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Ignore document recompute on undo/redo");
        }
        return 0;
    }

    int objectCount = 0;
    if (testStatus(Document::PartialDoc)) {
        if (mustExecute()) {
            FC_WARN("Please reload partial document '" << Label.getValue()
                                                       << "' for recomputation.");
        }
        return 0;
    }
    if (testStatus(Document::Recomputing)) {
        // this is clearly a bug in the calling instance
        FC_ERR("Recursive calling of recompute for document " << getName());
        return 0;
    }
    // The 'SkipRecompute' flag can be (tmp.) set to avoid too many
    // time expensive recomputes
    if (!force && testStatus(Document::SkipRecompute)) {
        signalSkipRecompute(*this, objs);
        return 0;
    }

    // delete recompute log
    d->clearRecomputeLog();

    Base::TimeTracker tracker("Document::recompute");
    std::optional<Base::ObjectStatusLocker<Document::Status, Document>> recomputingStatus;
    recomputingStatus.emplace(Document::Recomputing, this);

    signalBeforeRecompute(*this);

    //////////////////////////////////////////////////////////////////////////
    // FIXME Comment by Realthunder:
    // the topologicalSrot() below cannot handle partial recompute, haven't got
    // time to figure out the code yet, simply use back boost::topological_sort
    // for now, that is, rely on getDependencyList() to do the sorting. The
    // downside is, it didn't take advantage of the ready built InList, nor will
    // it report for cyclic dependency.
    //////////////////////////////////////////////////////////////////////////

    /*   // get the sorted vector of all dependent objects and go though it from the end
       auto depObjs = getDependencyList(objs.empty()?d->objectArray:objs);
       vector<DocumentObject*> topoSortedObjects = topologicalSort(depObjs);
       if (topoSortedObjects.size() != depObjs.size()){
           cerr << "Document::recompute(): cyclic dependency detected" << '\n';
           topoSortedObjects = d->partialTopologicalSort(depObjs);
       }
       std::reverse(topoSortedObjects.begin(),topoSortedObjects.end());
   */

    // alt:
    auto topoSortedObjects =
        getDependencyList(objs.empty() ? d->objectArray : objs, DepSort | options);

    for (auto obj : topoSortedObjects) {
        obj->setStatus(ObjectStatus::PendingRecompute, true);
    }

    ParameterGrp::handle hGrp =
        GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document");
    bool canAbort = hGrp->GetBool("CanAbortRecompute", true);

    tracker.checkpoint("pre-recompute & topo sort");

    try {
        std::set<DocumentObject*> filter;
        size_t idx = 0;
        // maximum two passes to allow some form of dependency inversion
        for (int passes = 0; passes < 2 && idx < topoSortedObjects.size(); ++passes) {
            std::unique_ptr<Base::SequencerLauncher> seq;
            if (canAbort) {
                seq = std::make_unique<Base::SequencerLauncher>("Recompute...",
                                                                topoSortedObjects.size());
            }
            FC_LOG("Recompute pass " << passes);
            for (; idx < topoSortedObjects.size(); ++idx) {
                auto obj = topoSortedObjects[idx];
                if (!obj->isAttachedToDocument() || filter.find(obj) != filter.end()) {
                    continue;
                }
                // ask the object if it should be recomputed
                bool doRecompute = false;
                if (obj->mustRecompute()) {
                    doRecompute = true;
                    ++objectCount;
                    int res = _recomputeFeature(obj);
                    if (res != 0) {
                        if (hasError) {
                            *hasError = true;
                        }
                        if (res < 0) {
                            passes = 2;
                            break;
                        }
                        // if something happened filter all object in its
                        // inListRecursive from the queue then proceed
                        obj->getInListEx(filter, true);
                        filter.insert(obj);
                        continue;
                    }
                }
                if (obj->isTouched() || doRecompute) {
                    signalRecomputedObject(*obj);
                    obj->purgeTouched();
                    // set all dependent object touched to force recompute
                    for (auto inObjIt : obj->getInList()) {
                        inObjIt->enforceRecompute();
                    }
                }
                if (seq) {
                    seq->next(true);
                }
            }
            // check if all objects are recomputed but still thouched
            for (size_t i = 0; i < topoSortedObjects.size(); ++i) {
                auto obj = topoSortedObjects[i];
                obj->setStatus(ObjectStatus::Recompute2, false);
                if (!filter.contains(obj) && obj->isTouched()) {
                    if (passes > 0) {
                        FC_ERR(obj->getFullName() << " still touched after recompute");
                    }
                    else {
                        FC_LOG(obj->getFullName() << " still touched after recompute");
                        if (idx >= topoSortedObjects.size()) {
                            // let's start the next pass on the first touched object
                            idx = i;
                        }
                        obj->setStatus(ObjectStatus::Recompute2, true);
                    }
                }
            }
        }
    }
    catch (Base::Exception& e) {
        e.reportException();
    }

    tracker.checkpoint("Recompute");

    for (auto obj : topoSortedObjects) {
        if (!obj->isAttachedToDocument()) {
            continue;
        }
        obj->setStatus(ObjectStatus::PendingRecompute, false);
        obj->setStatus(ObjectStatus::Recompute2, false);
    }

    // What this document could not honour is said again, because the log this pass emptied held
    // two kinds of entry with different lifetimes. An account of a rebuild belongs to the rebuild
    // that produced it and is rightly gone. What the FILE states and this build could not honour
    // belongs to the file, outlives every rebuild, and was being thrown away by the first one --
    // so a person opening somebody else's document met a part marked bad and a document with
    // nothing to say about why (§3.6, Amendment 19 Clause 19.6).
    //
    // Said again rather than kept across the clear. Both hold: the fact lives on the object and
    // every path that changes it already restates the entry, so keeping the entry would land in
    // the same place -- no test told the two apart, and that is worth saying rather than dressing
    // the choice up as a measurement. It is said again because then the log has ONE source and
    // cannot be left holding a sentence about a state nothing still reports.
    recordWhatIsBlocked();

    // Keep the document marked as Recomputing while signalRecomputed() runs.
    // Those observers may execute Python or GUI code; clearing the status
    // first would let re-entrant code see the document as stable before
    // recompute teardown has finished. signalBecameStable() is the first
    // point where observers may treat the document as stable again.

    // What observers edit in answer to a recompute joins the undo step that prompted it, so one
    // undo reverses both. The step is reopened only when they edit something.
    Base::FlagToggler<> joinLast(d->joinLastTransaction);
    signalRecomputed(*this, topoSortedObjects);
    recomputingStatus.reset();
    signalBecameStable(*this);

    tracker.checkpoint("Recompute total");

    if (!d->_RecomputeLog.empty()) {
        if (!testStatus(Status::IgnoreErrorOnRecompute)) {
            for (auto it : topoSortedObjects) {
                if (it->isError()) {
                    const char* text = getErrorDescription(it);
                    if (text) {
                        Base::Console().error("%s: %s\n", it->Label.getValue(), text);
                    }
                }
            }
        }
    }

    for (auto doc : GetApplication().getDocuments()) {
        decltype(doc->d->pendingRemove) objects;
        objects.swap(doc->d->pendingRemove);
        for (auto& o : objects) {
            try {
                if (auto obj = o.getObject()) {
                    obj->getDocument()->removeObject(obj->getNameInDocument());
                }
            }
            catch (Base::Exception& e) {
                e.reportException();
                FC_ERR("error when removing object " << o.getDocumentName() << '#'
                                                     << o.getObjectName());
            }
        }
    }
    if (d->resumedLastTransaction) {
        commitTransaction();
    }
    return objectCount;
}

/*!
  Does almost the same as topologicalSort() until no object with an input degree of zero
  can be found. It then searches for objects with an output degree of zero until neither
  an object with input or output degree can be found. The remaining objects form one or
  multiple cycles.
  An alternative to this method might be:
  https://en.wikipedia.org/wiki/Tarjan%E2%80%99s_strongly_connected_components_algorithm
 */
std::vector<DocumentObject*>
DocumentP::partialTopologicalSort(const std::vector<DocumentObject*>& objects)
{
    std::vector<DocumentObject*> ret;
    ret.reserve(objects.size());
    // pairs of input and output degree
    std::map<DocumentObject*, std::pair<int, int>> countMap;

    for (auto objectIt : objects) {
        // we need inlist with unique entries
        auto in = objectIt->getInList();
        std::sort(in.begin(), in.end());
        in.erase(std::unique(in.begin(), in.end()), in.end());

        // we need outlist with unique entries
        auto out = objectIt->getOutList();
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());

        countMap[objectIt] = std::make_pair(in.size(), out.size());
    }

    std::list<DocumentObject*> degIn;
    std::list<DocumentObject*> degOut;

    bool removeVertex = true;
    while (removeVertex) {
        removeVertex = false;

        // try input degree
        auto degInIt = find_if(countMap.begin(),
                               countMap.end(),
                               [](std::pair<DocumentObject*, std::pair<int, int>> vertex) -> bool {
                                   return vertex.second.first == 0;
                               });

        if (degInIt != countMap.end()) {
            removeVertex = true;
            degIn.push_back(degInIt->first);
            degInIt->second.first = degInIt->second.first - 1;

            // we need outlist with unique entries
            auto out = degInIt->first->getOutList();
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());

            for (auto outListIt : out) {
                auto outListMapIt = countMap.find(outListIt);
                if (outListMapIt != countMap.end()) {
                    outListMapIt->second.first = outListMapIt->second.first - 1;
                }
            }
        }
    }

    // make the output degree negative if input degree is negative
    // to mark the vertex as processed
    for (auto& [obj, pair] : countMap) {
        if (pair.first < 0) {
            pair.second = -1;
        }
    }

    removeVertex = degIn.size() != objects.size();
    while (removeVertex) {
        removeVertex = false;

        auto degOutIt = std::find_if(countMap.begin(),
                                     countMap.end(),
                                     [](std::pair<DocumentObject*, std::pair<int, int>> vertex) -> bool {
                                         return vertex.second.second == 0;
                                     });

        if (degOutIt != countMap.end()) {
            removeVertex = true;
            degOut.push_front(degOutIt->first);
            degOutIt->second.second = degOutIt->second.second - 1;

            // we need inlist with unique entries
            auto in = degOutIt->first->getInList();
            std::sort(in.begin(), in.end());
            in.erase(std::unique(in.begin(), in.end()), in.end());

            for (auto inListIt : in) {
                auto inListMapIt = countMap.find(inListIt);
                if (inListMapIt != countMap.end()) {
                    inListMapIt->second.second = inListMapIt->second.second - 1;
                }
            }
        }
    }

    // at this point we have no root object any more
    for (auto countIt : countMap) {
        if (countIt.second.first > 0 && countIt.second.second > 0) {
            degIn.push_back(countIt.first);
        }
    }

    ret.insert(ret.end(), degIn.begin(), degIn.end());
    ret.insert(ret.end(), degOut.begin(), degOut.end());

    return ret;
}

std::vector<DocumentObject*>
DocumentP::topologicalSort(const std::vector<DocumentObject*>& objects) const
{
    // topological sort algorithm described here:
    // https://de.wikipedia.org/wiki/Topologische_Sortierung#Algorithmus_f.C3.BCr_das_Topologische_Sortieren
    std::vector<DocumentObject*> ret;
    ret.reserve(objects.size());
    std::map<DocumentObject*, int> countMap;

    for (auto objectIt : objects) {
        // We now support externally linked objects
        // if(!obj->isAttachedToDocument() || obj->getDocument()!=this)
        if (!objectIt->isAttachedToDocument()) {
            continue;
        }
        // we need inlist with unique entries
        auto in = objectIt->getInList();
        std::sort(in.begin(), in.end());
        in.erase(std::unique(in.begin(), in.end()), in.end());

        countMap[objectIt] = in.size();
    }

    auto rootObjeIt = std::find_if(countMap.begin(),
                                   countMap.end(),
                                   [](std::pair<DocumentObject*, int> count) -> bool {
                                       return count.second == 0;
                                   });

    if (rootObjeIt == countMap.end()) {
        std::cerr << "Document::topologicalSort: cyclic dependency detected (no root object)" << '\n';
        return ret;
    }

    while (rootObjeIt != countMap.end()) {
        rootObjeIt->second = rootObjeIt->second - 1;

        // we need outlist with unique entries
        auto out = rootObjeIt->first->getOutList();
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());

        for (auto outListIt : out) {
            auto outListMapIt = countMap.find(outListIt);
            if (outListMapIt != countMap.end()) {
                outListMapIt->second = outListMapIt->second - 1;
            }
        }
        ret.push_back(rootObjeIt->first);

        rootObjeIt = find_if(countMap.begin(),
                             countMap.end(),
                             [](std::pair<DocumentObject*, int> count) -> bool {
                                 return count.second == 0;
                             });
    }

    return ret;
}

std::vector<DocumentObject*> Document::topologicalSort() const
{
    return d->topologicalSort(d->objectArray);
}

const char* Document::getErrorDescription(const DocumentObject* Obj) const
{
    return d->findRecomputeLog(Obj);
}

// call the recompute of the Feature and handle the exceptions and errors.
int Document::_recomputeFeature(DocumentObject* Feat) // NOLINT
{
    FC_LOG("Recomputing " << Feat->getFullName());

    if (Feat->isBlockedByAStatement()) {
        // Cruth (Amendment 19): something this session could not honour states a value here --
        // this object's own file, or another object setting a value on it (Clause 19.6) -- so
        // this build cannot produce what depends on it. Executing the step from the part of its
        // input that happened to be legible produces a shape nobody designed, and a document that
        // presented it as finished would be denying what its own file says. The node is blocked
        // instead, and says which statement blocked it, and where it is stated (§3.6).
        std::string why = "Blocked: this build could not honour what the file states here.";
        for (const auto& [name, reason] : Feat->whatCouldNotBeHonoured()) {
            why += " '" + name + "': " + reason + ".";
        }
        d->addRecomputeLog(why, Feat);
        FC_LOG("Blocked " << Feat->getFullName() << ": " << why);
        return 1;
    }

    DocumentObjectExecReturn* returnCode = nullptr;
    try {
        returnCode = Feat->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteNonOutput);
        if (returnCode == DocumentObject::StdReturn) {
            returnCode = Feat->recompute();
            if (returnCode == DocumentObject::StdReturn) {
                returnCode =
                    Feat->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteOutput);
            }
        }
    }
    catch (Base::AbortException& e) {
        e.reportException();
        FC_LOG("Failed to recompute " << Feat->getFullName() << ": " << e.what());
        d->addRecomputeLog("User abort", Feat);
        return -1;
    }
    catch (const Base::MemoryException& e) {
        FC_ERR("Memory exception in " << Feat->getFullName() << " thrown: " << e.what());
        d->addRecomputeLog("Out of memory exception", Feat);
        return 1;
    }
    catch (Base::Exception& e) {
        e.reportException();
        FC_LOG("Failed to recompute " << Feat->getFullName() << ": " << e.what());
        d->addRecomputeLog(e.what(), Feat);
        return 1;
    }
    catch (std::exception& e) {
        FC_ERR("Exception in " << Feat->getFullName() << " thrown: " << e.what());
        d->addRecomputeLog(e.what(), Feat);
        return 1;
    }
#ifndef FC_DEBUG
    catch (...) {
        FC_ERR("Unknown exception in " << Feat->getFullName() << " thrown");
        d->addRecomputeLog("Unknown exception!", Feat);
        return 1;
    }
#endif

    if (returnCode == DocumentObject::StdReturn) {
        Feat->resetError();
    }
    else {
        returnCode->Which = Feat;
        d->addRecomputeLog(returnCode);
        FC_LOG("Failed to recompute " << Feat->getFullName() << ": " << returnCode->Why);
        return 1;
    }
    return 0;
}

bool Document::recomputeFeature(DocumentObject* feature, bool recursive)
{
    // delete recompute log
    d->clearRecomputeLog(feature);

    // verify that the feature is (active) part of the document
    if (!feature->isAttachedToDocument()) {
        return false;
    }

    if (recursive) {
        bool hasError = false;
        recompute({feature}, true, &hasError);
        return !hasError;
    }
    _recomputeFeature(feature);
    // The same debt as the whole-document pass, on the one object: rebuilding it emptied its half
    // of the log, and what the file states about it does not belong to that rebuild.
    recordWhatIsBlocked();
    signalRecomputedObject(*feature);
    return feature->isValid();
}

void DocumentP::findAllPathsAt(const std::vector<Node>& all_nodes,
                               const size_t id,
                               std::vector<Path>& all_paths,
                               Path tmp)
{
    if (std::ranges::find(tmp, id) != tmp.end()) {
        tmp.push_back(id);
        all_paths.push_back(std::move(tmp));
        return;  // a cycle
    }

    tmp.push_back(id);
    if (all_nodes[id].empty()) {
        all_paths.push_back(std::move(tmp));
        return;
    }

    for (size_t i = 0; i < all_nodes[id].size(); i++) {
        const Path& tmp2(tmp);
        findAllPathsAt(all_nodes, all_nodes[id][i], all_paths, tmp2);
    }
}

std::vector<std::list<DocumentObject*>>
Document::getPathsByOutList(const DocumentObject* from, const DocumentObject* to) const
{
    std::map<const DocumentObject*, size_t> indexMap;
    for (size_t i = 0; i < d->objectArray.size(); ++i) {
        indexMap[d->objectArray[i]] = i;
    }

    std::vector<Node> all_nodes(d->objectArray.size());
    for (size_t i = 0; i < d->objectArray.size(); ++i) {
        const DocumentObject* obj = d->objectArray[i];
        std::vector<DocumentObject*> outList = obj->getOutList();
        for (const auto it : outList) {
            all_nodes[i].push_back(indexMap[it]);
        }
    }

    std::vector<std::list<DocumentObject*>> array;
    if (from == to) {
        return array;
    }

    size_t index_from = indexMap[from];
    size_t index_to = indexMap[to];
    std::vector<Path> all_paths;
    DocumentP::findAllPathsAt(all_nodes, index_from, all_paths, Path());

    for (const Path& it : all_paths) {
        auto jt = std::ranges::find(it, index_to);
        if (jt != it.end()) {
            array.push_back({});
            auto& path = array.back();
            for (auto kt = it.begin(); kt != jt; ++kt) {
                path.push_back(d->objectArray[*kt]);
            }

            path.push_back(d->objectArray[*jt]);
        }
    }

    // remove duplicates
    std::sort(array.begin(), array.end());
    array.erase(std::unique(array.begin(), array.end()), array.end());

    return array;
}

bool Document::mustExecute() const
{
    if (PropertyXLink::hasXLink(this)) {
        bool touched = false;
        buildDependencyList(d->objectArray, 0, nullptr, nullptr, nullptr, &touched);
        return touched;
    }

    for (const auto It : d->objectArray) {
        if (It->isTouched() || It->mustExecute() == 1) {
            return true;
        }
    }
    return false;
}
