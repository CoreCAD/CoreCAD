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

void Document::keepUnreadObject(std::string uuid, std::string type, std::string text)
{
    // Kept in durable-id order, which is the order the file states objects in, so a block goes
    // back exactly where it was rather than being appended -- a save that changed nothing has to
    // change nothing, or the file stops being diffable (Amendment 18 Clause 18.1).
    std::array<std::string, 3> kept {std::move(uuid), std::move(type), std::move(text)};
    const auto at = std::lower_bound(_unreadObjects.begin(),
                                     _unreadObjects.end(),
                                     kept,
                                     [](const auto& left, const auto& right) {
                                         return left[0] < right[0];
                                     });
    _unreadObjects.insert(at, std::move(kept));
}

bool Document::holdsUnreadContent() const
{
    if (!_unreadObjects.empty()) {
        return true;
    }
    // A fragment is the worst case this asks about, and it holds nothing at all of what is
    // missing from it: the content never arrived to be kept. Measured -- a truncated document
    // used to answer that it was whole.
    if (!_unkeptStatements.empty()) {
        return true;
    }
    // The document states properties of its own, and one of those may be a statement this build
    // had no place for -- a tracking code from a records system, a field an add-on added. It is
    // kept like any other, and it counts like any other.
    if (holdsUnhonouredStatement()) {
        return true;
    }
    // An object whose file stated something this session could not honour -- source material that
    // would not load, a reference whose target is not here -- is a document that is not whole.
    // A script that opens documents and saves them has to be able to ask (Amendment 19).
    return std::any_of(d->objectArray.begin(), d->objectArray.end(), [](const DocumentObject* obj) {
        return obj != nullptr && obj->holdsUnhonouredStatement();
    });
}

namespace
{
/// The refusal Clause 19.4 requires, said once wherever it is needed.
[[noreturn]] void notAPerson(const char* doing)
{
    throw Base::RuntimeError(
        std::string("A statement this build cannot honour is discarded only by a person's "
                    "deliberate act (Amendment 19 Clause 19.4). This document is ")
        + doing
        + ", and a read, a rebuild, an import or an undo may not discard on a person's behalf.");
}
}  // namespace

bool Document::isWhole() const
{
    if (holdsUnreadContent()) {
        return false;
    }
    // A value one object states for another and this session could not apply leaves nothing
    // unread -- the words are the holder's and are written back with it -- but the object whose
    // value it would have set is not the object its file describes, and the document may not
    // present it as finished (Amendment 19 Clause 19.6).
    return std::none_of(d->objectArray.begin(),
                        d->objectArray.end(),
                        [](const DocumentObject* obj) {
                            return obj != nullptr && !obj->statementsSetFromElsewhere().empty();
                        });
}

bool Document::seesEveryReference() const
{
    return !holdsUnreadContent();
}

std::string Document::whyReferencesAreFrozen() const
{
    const std::vector<std::array<std::string, 3>> held = heldStatements();
    if (held.empty()) {
        // A fragment holds nothing -- the content never arrived to be kept -- and is exactly the
        // document that has seen the fewest of its own references.
        return "it did not come back whole";
    }
    std::string why = "it holds ";
    why += held.front()[0].empty() ? "'" + held.front()[1] + "'"
                                   : "'" + held.front()[1] + "' on " + held.front()[0];
    why += ": " + held.front()[2];
    if (held.size() > 1) {
        why += ", and " + std::to_string(held.size() - 1) + " more";
    }
    return why;
}

void Document::refuseDuplicationThatCannotBeRewired(const std::vector<DocumentObject*>& objs)
{
    for (const DocumentObject* obj : objs) {
        if (obj == nullptr || !obj->holdsUnhonouredStatement()) {
            continue;
        }
        // Not every kept statement stands in the way. A value this build had no place for is
        // carried across verbatim and states on the copy exactly what it states on the original,
        // which is Clause 19.1's whole point and must keep working. What cannot be carried is a
        // statement that NAMES something: duplication answers, for every reference among the
        // copied objects, whether it should now point at the copy or still at the original, and
        // for a name that never resolved there is no way to answer it.
        const std::vector<std::string> unresolved = obj->unresolvedReferenceNames();
        std::string naming = unresolved.empty() ? std::string {} : unresolved.front();
        if (naming.empty()) {
            for (const auto& [name, stated] : obj->statedProperties()) {
                // The kept words are in this program's own form, so whether they name anything is
                // read rather than guessed at.
                if (stated.words.find("<Target ") != std::string::npos) {
                    naming = name;
                    break;
                }
            }
        }
        if (naming.empty()) {
            continue;
        }
        throw Base::RuntimeError(
            std::string("This duplication is refused: '")
            + (obj->isAttachedToDocument() ? obj->getNameInDocument() : "?") + "' states '" + naming
            + "', a reference this session could not resolve. A copy mints new identities and "
              "rewires the references between the copies, and a reference that never resolved "
              "cannot be rewired -- nothing can say whether it should follow the copy or stay "
              "with the original, so the copy would quietly go on naming the original "
              "(Amendment 19 Clause 19.3). Discard the statement first, deliberately, if it is no "
              "longer wanted.");
    }
}

std::vector<std::array<std::string, 3>> Document::heldStatements() const
{
    std::vector<std::array<std::string, 3>> held;
    for (const auto& [name, why] : PropertyContainer::unhonouredStatements()) {
        held.push_back({std::string {}, name, why});
    }
    for (const auto& kept : _unreadObjects) {
        held.push_back({std::string {},
                        kept[0],
                        "this build cannot construct an object of type '" + kept[1] + "'"});
    }
    for (const DocumentObject* obj : d->objectArray) {
        if (obj == nullptr || !obj->isAttachedToDocument()) {
            continue;
        }
        for (const auto& [name, why] : obj->unhonouredStatements()) {
            held.push_back({obj->getNameInDocument(), name, why});
        }
    }
    return held;
}

PropertyContainer* Document::holderOfStatements(const std::string& holder)
{
    if (holder.empty()) {
        return this;
    }
    return getObject(holder.c_str());
}

void Document::recordStatementBeforeDiscard(const std::string& holder,
                                            const std::string& name,
                                            bool wholeObject)
{
    if (d->activeUndoTransaction == nullptr || d->rollback) {
        return;
    }
    Transaction::DiscardedStatement record;
    record.holder = holder;
    record.name = name;
    record.wholeObject = wholeObject;
    if (wholeObject) {
        const auto at = std::find_if(_unreadObjects.begin(),
                                     _unreadObjects.end(),
                                     [&](const auto& kept) {
                                         return kept[0] == name;
                                     });
        if (at != _unreadObjects.end()) {
            record.block = *at;
        }
    }
    else if (PropertyContainer* held = holderOfStatements(holder); held != nullptr) {
        record.kept = held->keptStatementFor(name);
    }
    d->activeUndoTransaction->recordDiscard(std::move(record));
}

void Document::putBackUnreadObject(const std::string& uuid,
                                   const std::array<std::string, 3>& block)
{
    const auto at = std::find_if(_unreadObjects.begin(),
                                 _unreadObjects.end(),
                                 [&](const auto& kept) {
                                     return kept[0] == uuid;
                                 });
    if (at != _unreadObjects.end()) {
        _unreadObjects.erase(at);
    }
    if (!block[0].empty()) {
        keepUnreadObject(block[0], block[1], block[2]);
    }
}

/// What every discard does around the drop itself: refuse the callers that are not a person, name
/// the act in the document's history, and record what it is about to replace.
bool Document::discardStatement(DocumentObject* holder, const char* name)
{
    if (name == nullptr || *name == '\0') {
        return false;
    }
    if (holder != nullptr && holder->getDocument() != this) {
        return false;
    }
    PropertyContainer* held = holder != nullptr ? static_cast<PropertyContainer*>(holder) : this;
    if (!held->keptStatementFor(name).holdsAnything()) {
        return false;
    }
    if (testStatus(Restoring) || isAnyRestoring()) {
        notAPerson("being read");
    }
    if (testStatus(Recomputing)) {
        notAPerson("rebuilding");
    }
    if (testStatus(Importing)) {
        notAPerson("importing objects");
    }
    if (isPerformingTransaction()) {
        notAPerson("undoing or redoing an edit");
    }

    const std::string holderName =
        holder != nullptr ? std::string(holder->getNameInDocument()) : std::string {};
    // Named by what it drops, because a history entry that says only "discard" cannot be read
    // back a week later, and the whole point of the act is that it was deliberate.
    const bool ownTransaction = openTransactionForAct("Discard '" + std::string(name) + "'");
    recordStatementBeforeDiscard(holderName, name, false);
    held->dropStatement(name);
    if (ownTransaction) {
        commitTransaction();
    }
    Base::Console().message("Discarded the statement kept for '%s'%s%s.\n",
                            name,
                            holderName.empty() ? "" : " on ",
                            holderName.c_str());
    return true;
}

bool Document::discardUnreadObject(const char* uuid)
{
    if (uuid == nullptr || *uuid == '\0') {
        return false;
    }
    const auto at = std::find_if(_unreadObjects.begin(),
                                 _unreadObjects.end(),
                                 [&](const auto& kept) {
                                     return kept[0] == uuid;
                                 });
    if (at == _unreadObjects.end()) {
        return false;
    }
    if (testStatus(Restoring) || isAnyRestoring()) {
        notAPerson("being read");
    }
    if (testStatus(Recomputing)) {
        notAPerson("rebuilding");
    }
    if (testStatus(Importing)) {
        notAPerson("importing objects");
    }
    if (isPerformingTransaction()) {
        notAPerson("undoing or redoing an edit");
    }

    const std::string type = (*at)[1];
    const bool ownTransaction = openTransactionForAct("Discard the kept '" + type + "'");
    recordStatementBeforeDiscard({}, uuid, true);
    putBackUnreadObject(uuid, {});
    if (ownTransaction) {
        commitTransaction();
    }
    Base::Console().message("Discarded the block kept for the '%s' this build cannot "
                            "construct.\n",
                            type.c_str());
    return true;
}

void Document::recordUnkeptStatement(std::string said)
{
    if (said.empty()) {
        return;
    }
    // Said once, however many times the file states it.
    if (std::find(_unkeptStatements.begin(), _unkeptStatements.end(), said)
        == _unkeptStatements.end()) {
        _unkeptStatements.push_back(std::move(said));
    }
}

std::vector<std::string> Document::whatASaveWouldLose() const
{
    // What the read could not bring back and could not keep, and nothing else. A document holding
    // statements it could not honour keeps them and gives them back, so its save costs nothing and
    // is not gated here -- gating it would trap a person's work to protect content that is not in
    // danger.
    return _unkeptStatements;
}

bool Document::mayWrite()
{
    const std::vector<std::string> losing = whatASaveWouldLose();
    if (losing.empty() || _acceptedLoss) {
        return true;
    }

    // Cruth (Amendment 19 Clause 19.3): this document is what could be read before the read
    // failed, not what its file says -- measured on a truncated file and on one left holding the
    // markers of an unfinished merge, both of which open as their own beginning. Writing it
    // anywhere publishes that beginning as a document, and the file on disk is the only remaining
    // copy of what is missing from it. Refused unless the caller says what it is losing.
    std::string why = "'" + std::string(Label.getValue()) + "' did not come back whole. A save "
        "would lose:";
    for (const std::string& loss : losing) {
        why += "\n  " + loss;
    }
    why += "\nTo write it anyway, accept that by name (saveAcceptingLoss).";
    FC_ERR(why);
    return false;
}

bool Document::saveAcceptingLoss(const std::vector<std::string>& losing, const std::string& path)
{
    const std::vector<std::string> cost = whatASaveWouldLose();
    if (losing != cost) {
        // An answer to a question nobody asked is not an acceptance. Named back, so a caller that
        // asked in good faith can see what it should have said.
        std::string why = "This write was not allowed through: what was accepted is not what it "
                          "would lose. It would lose:";
        for (const std::string& loss : cost) {
            why += "\n  " + loss;
        }
        if (cost.empty()) {
            why += " nothing";
        }
        FC_ERR(why);
        return false;
    }

    // For this write and no other. A standing permission is the formality this clause forbids.
    const std::string mine = FileName.getStrValue();
    _acceptedLoss = true;
    bool written = false;
    try {
        written = path.empty() ? save() : saveAs(path.c_str());
    }
    catch (...) {
        _acceptedLoss = false;
        throw;
    }
    _acceptedLoss = false;

    // The cost was real until this write. Where the write landed on the very file the losses were
    // measured against, that file no longer holds them and the account is settled: going on to
    // refuse every later save would be refusing over content that no longer exists anywhere.
    // Where it landed anywhere else, the file that holds them is still out there and every write
    // from here still has to be accepted on its own.
    if (written && !_unkeptAgainst.empty() && (path.empty() ? mine : path) == _unkeptAgainst) {
        _unkeptStatements.clear();
        _unkeptAgainst.clear();
    }
    return written;
}

void Document::blockWhatCouldNotBeHonoured()
{
    // A statement that sets a value on another object is known only to the object that makes it,
    // and the object it blocks is the one whose value it would have set (Clause 19.6). So the
    // holders are asked before anything is reported, or a part left at its base value by an
    // option that failed to apply would pass for finished.
    for (DocumentObject* obj : d->objectArray) {
        if (obj != nullptr) {
            obj->blockWhatItSetsElsewhere();
        }
    }
    recordWhatIsBlocked();
}

void Document::recordWhatIsBlocked()
{
    std::set<long> blocked;
    for (DocumentObject* obj : d->objectArray) {
        if (obj == nullptr || !obj->isBlockedByAStatement()) {
            continue;
        }
        blocked.insert(obj->getID());
        std::string why = "Blocked: this build could not honour what the file states here.";
        for (const auto& [name, reason] : obj->whatCouldNotBeHonoured()) {
            why += " '" + name + "': " + reason + ".";
        }
        d->clearRecomputeLog(obj);
        d->addRecomputeLog(why, obj);
    }

    // What a holder no longer states no longer blocks. The record has to go with it: an object
    // left reporting a failure that has been dealt with is as dishonest as one reporting none.
    for (long was : d->blockedByStatement) {
        if (blocked.count(was) != 0) {
            continue;
        }
        DocumentObject* obj = getObjectByID(was);
        if (obj == nullptr) {
            continue;  // gone from the document since; nothing of it to correct
        }
        d->clearRecomputeLog(obj);
        obj->setStatus(ObjectStatus::Error, false);
        // Released, not resolved: the value it was denied is the value it was never built with,
        // so it is built again rather than left standing at what the block froze it at.
        obj->touch();
    }
    d->blockedByStatement = std::move(blocked);
}
