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

bool Document::undo(const int id)
{
    if (d->iUndoMode != 0) {
        if (id != 0) {
            const auto it = mUndoMap.find(id);
            if (it == mUndoMap.end()) {
                return false;
            }
            if (it->second != d->activeUndoTransaction) {
                while (!mUndoTransactions.empty() && mUndoTransactions.back() != it->second) {
                    undo(0);
                }
            }
        }

        if (d->activeUndoTransaction) {
            _commitTransaction(true);
        }
        if (mUndoTransactions.empty()) {
            return false;
        }
        // redo
        d->activeUndoTransaction = new Transaction(mUndoTransactions.back()->getID());
        d->activeUndoTransaction->Name = mUndoTransactions.back()->Name;

        {
            Base::FlagToggler<bool> flag(d->undoing);
            // applying the undo
            mUndoTransactions.back()->apply(*this, false);

            // save the redo
            mRedoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
            mRedoTransactions.push_back(d->activeUndoTransaction);
            d->activeUndoTransaction = nullptr;
            d->bookedTransaction = 0;

            mUndoMap.erase(mUndoTransactions.back()->getID());
            delete mUndoTransactions.back();
            mUndoTransactions.pop_back();
        }

        for (const auto& obj : d->objectArray) {
            if (obj->testStatus(ObjectStatus::PendingTransactionUpdate)) {
                obj->onUndoRedoFinished();
                obj->setStatus(ObjectStatus::PendingTransactionUpdate, false);
            }
        }

        signalUndo(*this);  // now signal the undo
        signalBecameStable(*this);

        return true;
    }

    return false;
}

bool Document::redo(const int id)
{
    if (d->iUndoMode != 0) {
        if (id != 0) {
            const auto it = mRedoMap.find(id);
            if (it == mRedoMap.end()) {
                return false;
            }
            while (!mRedoTransactions.empty() && mRedoTransactions.back() != it->second) {
                redo(0);
            }
        }

        if (d->activeUndoTransaction) {
            _commitTransaction(true);
        }

        assert(mRedoTransactions.size() != 0);

        // undo
        d->activeUndoTransaction = new Transaction(mRedoTransactions.back()->getID());
        d->activeUndoTransaction->Name = mRedoTransactions.back()->Name;

        // do the redo
        {
            Base::FlagToggler<bool> flag(d->undoing);
            mRedoTransactions.back()->apply(*this, true);

            mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
            mUndoTransactions.push_back(d->activeUndoTransaction);
            d->activeUndoTransaction = nullptr;
            d->bookedTransaction = 0;

            mRedoMap.erase(mRedoTransactions.back()->getID());
            delete mRedoTransactions.back();
            mRedoTransactions.pop_back();
        }

        for (const auto& obj : d->objectArray) {
            if (obj->testStatus(ObjectStatus::PendingTransactionUpdate)) {
                obj->onUndoRedoFinished();
                obj->setStatus(ObjectStatus::PendingTransactionUpdate, false);
            }
        }

        signalRedo(*this);
        signalBecameStable(*this);
        return true;
    }

    return false;
}

void Document::changePropertyOfObject(TransactionalObject* obj,
                                      const Property* prop,
                                      const std::function<void()>& changeFunc)
{
    if (!prop || !obj || !obj->isAttachedToDocument()) {
        return;
    }
    if ((d->iUndoMode != 0) && !isPerformingTransaction() && !d->activeUndoTransaction) {
        if (!testStatus(Restoring) || testStatus(Importing)) {
            if (d->bookedTransaction == NullTransaction) {
                d->bookedTransaction = GetApplication().getGlobalTransaction();
            } else {
                _openTransaction(GetApplication().getTransactionName(d->bookedTransaction), d->bookedTransaction);
            }
        }
    }
    if (d->activeUndoTransaction && !d->rollback) {
        changeFunc();
    }
}

void Document::renamePropertyOfObject(TransactionalObject* obj,
                                      const Property* prop, const char* oldName)
{
    changePropertyOfObject(obj, prop, [this, obj, prop, oldName]() {
        d->activeUndoTransaction->renameProperty(obj, prop, oldName);
    });
}

void Document::addOrRemovePropertyOfObject(TransactionalObject* obj,
                                           const Property* prop, const bool add)
{
    changePropertyOfObject(obj, prop, [this, obj, prop, add]() {
        d->activeUndoTransaction->addOrRemoveProperty(obj, prop, add);
    });
}

bool Document::isPerformingTransaction() const
{
    return d->undoing || d->rollback;
}

std::vector<std::string> Document::getAvailableUndoNames() const
{
    std::vector<std::string> vList;
    if (d->activeUndoTransaction) {
        vList.push_back(d->activeUndoTransaction->Name);
    }
    for (auto It = mUndoTransactions.rbegin();
         It != mUndoTransactions.rend();
         ++It) {
        vList.push_back((*It)->Name);
    }
    return vList;
}

std::vector<std::string> Document::getAvailableRedoNames() const
{
    std::vector<std::string> vList;
    for (auto It = mRedoTransactions.rbegin();
         It != mRedoTransactions.rend();
         ++It) {
        vList.push_back((*It)->Name);
    }
    return vList;
}

int Document::openTransaction(TransactionName name, int tid) // NOLINT
{
    if (tid != NullTransaction && tid == d->bookedTransaction) {
        return tid; // Early exit without warning
    }
    if (isTransactionLocked()) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Transaction locked, ignore new transaction '" << name.name << "'");
        }
        return 0;
    }
    if (isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot open transaction while transacting");
        }
        return 0;
    }
    if (name.name.empty()) {
        name.name = "<empty>";
    }
    return setActiveTransaction(name, tid);
}
int Document::openTransaction(std::string name, int tid)
{
    return openTransaction(TransactionName {.name = name, .temporary = false}, tid);
}

int Document::_openTransaction(std::string name, int id)
{
    if (isTransactionLocked() && id != d->bookedTransaction) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Transaction locked, ignore new transaction '" << name << "'");
        }
    }
    if (isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot open transaction while transacting");
        }
        return 0;
    }
    if (d->iUndoMode == 0) {
        return 0;
    }

    // Avoid recursive calls that is possible while
    // clearing the redo transactions and will cause
    // a double deletion of some transaction and thus
    // a segmentation fault
    if (d->opentransaction) {
        return 0;
    }
    Base::FlagToggler<> flag(d->opentransaction);

    if ((id != 0) && mUndoMap.find(id) != mUndoMap.end()) {
        throw Base::RuntimeError("invalid transaction id");
    }
    if (d->activeUndoTransaction) {
        _commitTransaction(true);
    }
    _clearRedos();

    // When id == 0, this creates a new id
    // for instance, when there is no global transaction
    // from the application to stick to
    d->activeUndoTransaction = new Transaction(id);
    if (name.empty()) {
        name = "<empty>";
    }
    d->activeUndoTransaction->Name = name;
    mUndoMap[d->activeUndoTransaction->getID()] = d->activeUndoTransaction;
    id = d->activeUndoTransaction->getID();

    signalOpenTransaction(*this, name);

    Document* transactionInitiator = GetApplication().transactionInitiator(id);
    if (transactionInitiator && transactionInitiator != this && !transactionInitiator->hasPendingTransaction()) {
        std::string aname = std::format("-> {}", d->activeUndoTransaction->Name);
        FC_LOG("auto transaction " << getName() << " -> " << transactionInitiator->getName());
        transactionInitiator->_openTransaction(aname, id);
    }
    return id;
}

void Document::renameTransaction(const std::string& name, const int id) const
{
    if (!name.empty() && d->activeUndoTransaction && d->activeUndoTransaction->getID() == id) {
        if (boost::starts_with(d->activeUndoTransaction->Name, "-> ")) {
            d->activeUndoTransaction->Name.resize(3);
        }
        else {
            d->activeUndoTransaction->Name.clear();
        }
        d->activeUndoTransaction->Name += name;
    }
}
int Document::setActiveTransaction(TransactionName name, int tid)
{
    // Probably a group transaction situation
    if (tid != NullTransaction) {
        if (!GetApplication().transactionIsActive(tid)) {
            FC_LOG("Could not set active transaction to inactive ID");
            return NullTransaction;
        }
        if (d->bookedTransaction != NullTransaction && d->bookedTransaction != tid && !_commitTransaction(true)) {
            FC_LOG("Could not book transaction for document");
            return NullTransaction;
        }
        d->bookedTransaction = tid;

        if (GetApplication().transactionTmpName(d->bookedTransaction)) {
            GetApplication().setTransactionName(d->bookedTransaction, name);
        }
        return d->bookedTransaction;
    }

    // Rename the transaction if it had a tmp name
    if (d->bookedTransaction != NullTransaction && GetApplication().transactionTmpName(d->bookedTransaction)) {
        GetApplication().setTransactionName(d->bookedTransaction, name);
        return d->bookedTransaction;
    }
    if (d->bookedTransaction != NullTransaction && !_commitTransaction(true)) {
        FC_LOG("Could not book transaction for document");
        return NullTransaction;
    }
    d->bookedTransaction = Transaction::getNewID();

    GetApplication().setTransactionDescription(d->bookedTransaction, TransactionDescription {.initiator = this, .name = name});
    return d->bookedTransaction;
}

void Document::lockTransaction()
{
    d->TransactionLock++;
}
void Document::unlockTransaction()
{
    if (d->TransactionLock > 0) {
        d->TransactionLock--;
    }
}
bool Document::isTransactionLocked() const
{
    return d->TransactionLock > 0;
}
bool Document::transacting() const
{
    return isPerformingTransaction() || d->committing;
}

void Document::_checkTransaction(DocumentObject* pcDelObj, const Property* What, int line)
{
    // if the undo is active but no transaction open, open one!
    if (d->iUndoMode == 0 || isPerformingTransaction() || d->activeUndoTransaction) {
        return;
    }
    const bool mayModify = !What || !What->testStatus(Property::NoModify);
    if (d->joinLastTransaction && mayModify && !testStatus(Restoring) && _resumeLastTransaction()) {
        return;
    }

    if (!testStatus(Restoring) || testStatus(Importing)) {

        // Priority to a transaction that has been booked
        // explicitly for this document, it there are none
        // get a sticky transaction from application
        if (!d->bookedTransaction) {
            d->bookedTransaction = GetApplication().getGlobalTransaction();
        }

        if (d->bookedTransaction != NullTransaction) {
            std::string name = GetApplication().getTransactionName(d->bookedTransaction);
            bool ignore = false;
            if (What && What->testStatus(Property::NoModify)) {
                ignore = true;
            }
            if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
                if (What) {
                    FC_LOG((ignore ? "ignore" : "auto")
                            << " transaction (" << line << ") '" << What->getFullName());
                }
                else {
                    FC_LOG((ignore ? "ignore" : "auto") << " transaction (" << line << ") '"
                                                        << name << "' in " << getName());
                }
            }
            if (!ignore) {
                _openTransaction(name, d->bookedTransaction);
            }
            return;
        }
    }
    if (!pcDelObj) {
        return;
    }
    // When the object is going to be deleted we have to check if it has already been added
    // to the undo transactions
    std::list<Transaction*>::iterator it;
    for (it = mUndoTransactions.begin(); it != mUndoTransactions.end(); ++it) {
        if ((*it)->hasObject(pcDelObj)) {
            _openTransaction("Delete");
            break;
        }
    }
}

void Document::_clearRedos()
{
    if (isPerformingTransaction() || d->committing) {
        FC_ERR("Cannot clear redo while transacting");
        return;
    }

    mRedoMap.clear();
    while (!mRedoTransactions.empty()) {
        delete mRedoTransactions.back();
        mRedoTransactions.pop_back();
    }
}

void Document::commitTransaction() // NOLINT
{
    if (isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot commit transaction while transacting");
        }
        return;
    }

    if (d->activeUndoTransaction) {
        // This will iterate over all documents and ask them to
        // commit their transaction if their ID matches
        GetApplication().commitTransaction(d->activeUndoTransaction->getID());
    } else {
        const bool wasRecoveryWriteBlocked = transactionStateBlocksRecoveryWrite(*d);
        d->bookedTransaction = 0; // Reset booked transaction even if it was not used
        if (wasRecoveryWriteBlocked) {
            signalBecameStable(*this);
        }
    }
}

bool Document::_commitTransaction(const bool notify)
{
    if (isPerformingTransaction()) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot commit transaction while transacting");
        }
        return false;
    }
    if (d->committing) {
        // for a recursive call return without printing a warning
        return false;
    }

    d->bookedTransaction = 0;
    d->resumedLastTransaction = false;
    bool committed = false;
    if (d->activeUndoTransaction) {
        {
            Base::FlagToggler<> flag(d->committing);
            Application::TransactionSignaller signaller(false, true);
            const int id = d->activeUndoTransaction->getID();

            mUndoTransactions.push_back(d->activeUndoTransaction);
            d->activeUndoTransaction = nullptr;

            // check the stack for the limits
            if (mUndoTransactions.size() > d->UndoMaxStackSize) {
                mUndoMap.erase(mUndoTransactions.front()->getID());
                delete mUndoTransactions.front();
                mUndoTransactions.pop_front();
            }
            signalCommitTransaction(*this);

            // commitTransaction() may call again _commitTransaction()
            if (notify) {
                GetApplication().commitTransaction(id);
            }
        }
        committed = true;
    }
    if (committed) {
        signalBecameStable(*this);
    }
    return true;
}

void Document::abortTransaction() const
{
    if (isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot abort transaction while transacting");
        }
        return;
    }
    if (d->activeUndoTransaction) {
        GetApplication().abortTransaction(d->activeUndoTransaction->getID());
    } else {
        const bool wasRecoveryWriteBlocked = transactionStateBlocksRecoveryWrite(*d);
        d->bookedTransaction = 0; // Reset booked transaction even if it was not used
        if (wasRecoveryWriteBlocked) {
            signalBecameStable(*this);
        }
    }
}

void Document::_abortTransaction()
{
    if (isPerformingTransaction() || d->committing) {
        if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
            FC_WARN("Cannot abort transaction while transacting");
        }
    }

    d->bookedTransaction = 0;
    d->resumedLastTransaction = false;
    bool aborted = false;
    if (d->activeUndoTransaction) {
        {
            Base::FlagToggler<bool> flag(d->rollback);
            Application::TransactionSignaller signaller(true, true);

            // applying the so far made changes
            d->activeUndoTransaction->apply(*this, false);

            // destroy the undo
            mUndoMap.erase(d->activeUndoTransaction->getID());
            delete d->activeUndoTransaction;
            d->activeUndoTransaction = nullptr;
            signalAbortTransaction(*this);
        }
        aborted = true;
    }
    if (aborted) {
        signalBecameStable(*this);
    }
}

bool Document::_resumeLastTransaction()
{
    if (d->iUndoMode == 0 || d->activeUndoTransaction || d->bookedTransaction != 0 || transacting()
        || GetApplication().getGlobalTransaction() != 0 || mUndoTransactions.empty()
        || !mRedoTransactions.empty()) {
        return false;
    }
    d->activeUndoTransaction = mUndoTransactions.back();
    mUndoTransactions.pop_back();
    d->bookedTransaction = d->activeUndoTransaction->getID();
    d->resumedLastTransaction = true;
    GetApplication().setTransactionDescription(
        d->bookedTransaction,
        TransactionDescription {.initiator = this, .name = d->activeUndoTransaction->Name}
    );
    return true;
}

bool Document::hasPendingTransaction() const
{
    return d->activeUndoTransaction != nullptr;
}

int Document::getTransactionID(const bool undo, unsigned pos) const
{
    if (undo) {
        if (d->activeUndoTransaction) {
            if (pos == 0) {
                return d->activeUndoTransaction->getID();
            }
            --pos;
        }
        if (pos >= mUndoTransactions.size()) {
            return 0;
        }
        auto rit = mUndoTransactions.rbegin();
        for (; pos != 0U; ++rit, --pos) {}
        return (*rit)->getID();
    }
    if (pos >= mRedoTransactions.size()) {
        return 0;
    }
    auto rit = mRedoTransactions.rbegin();
    for (; pos != 0U; ++rit, --pos) {}
    return (*rit)->getID();
}
int Document::getBookedTransactionID() const
{
    return d->bookedTransaction;
}
bool Document::isTransactionEmpty() const
{
    return !d->activeUndoTransaction;
        // Transactions are now only created when there are actual changes.
        // Empty transaction is now significant for marking external changes. It
        // is used to match ID with transactions in external documents and
        // trigger undo/redo there.

        // return d->activeUndoTransaction->isEmpty();

}

void Document::clearUndos()
{
    if (isPerformingTransaction() || d->committing) {
        FC_ERR("Cannot clear undos while transacting");
        return;
    }

    if (d->activeUndoTransaction) {
        _commitTransaction(true);
    }

    mUndoMap.clear();

    // When cleaning up the undo stack we must delete the transactions from front
    // to back because a document object can appear in several transactions but
    // once removed from the document the object can never ever appear in any later
    // transaction. Since the document object may be also deleted when the transaction
    // is deleted we must make sure not access an object once it's destroyed. Thus, we
    // go from front to back and not the other way round.
    while (!mUndoTransactions.empty()) {
        delete mUndoTransactions.front();
        mUndoTransactions.pop_front();
    }
    // while (!mUndoTransactions.empty()) {
    //     delete mUndoTransactions.back();
    //     mUndoTransactions.pop_back();
    // }

    _clearRedos();
}

int Document::getAvailableUndos(const int id) const
{
    if (id != 0) {
        const auto it = mUndoMap.find(id);
        if (it == mUndoMap.end()) {
            return 0;
        }
        int i = 0;
        if (d->activeUndoTransaction) {
            ++i;
            if (d->activeUndoTransaction->getID() == id) {
                return i;
            }
        }
        auto rit = mUndoTransactions.rbegin();
        for (; rit != mUndoTransactions.rend() && *rit != it->second; ++rit) {
            ++i;
        }
        assert(rit != mUndoTransactions.rend());
        return i + 1;
    }
    if (d->activeUndoTransaction) {
        return static_cast<int>(mUndoTransactions.size() + 1);
    }
    return static_cast<int>(mUndoTransactions.size());
}

int Document::getAvailableRedos(const int id) const
{
    if (id != 0) {
        const auto it = mRedoMap.find(id);
        if (it == mRedoMap.end()) {
            return 0;
        }
        int i = 0;
        for (auto rit = mRedoTransactions.rbegin(); *rit != it->second; ++rit) {
            ++i;
        }
        assert(i < static_cast<int>(mRedoTransactions.size()));
        return i + 1;
    }
    return static_cast<int>(mRedoTransactions.size());
}

void Document::setUndoMode(const int iMode)
{
    if ((d->iUndoMode != 0) && (iMode == 0)) {
        clearUndos();
    }

    d->iUndoMode = iMode;
}

int Document::getUndoMode() const
{
    return d->iUndoMode;
}

unsigned int Document::getUndoMemSize() const
{
    return d->UndoMemSize;
}

void Document::setUndoLimit(const unsigned int UndoMemSize) // NOLINT
{
    d->UndoMemSize = UndoMemSize;
}

void Document::setMaxUndoStackSize(const unsigned int UndoMaxStackSize) // NOLINT
{
    d->UndoMaxStackSize = UndoMaxStackSize;
}

unsigned int Document::getMaxUndoStackSize() const
{
    return d->UndoMaxStackSize;
}

bool Document::openTransactionForAct(const std::string& title)
{
    if (d->iUndoMode == 0 || isPerformingTransaction() || d->activeUndoTransaction != nullptr) {
        return false;
    }
    const int booked = openTransaction(title);
    _openTransaction(title, booked);
    return d->activeUndoTransaction != nullptr;
}
