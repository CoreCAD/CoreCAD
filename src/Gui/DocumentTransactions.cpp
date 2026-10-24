/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
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


#include <string>
#include <map>
#include <set>
#include <vector>
#include <QMessageBox>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Transactions.h>
#include <Base/Tools.h>

#include "Document.h"
#include "Application.h"
#include "Command.h"
#include "MainWindow.h"
#include "ViewProviderDocumentObject.h"
#include "private/DocumentP.h"

using namespace Gui;

//--------------------------------------------------------------------------
// UNDO REDO transaction handling
//--------------------------------------------------------------------------
/** Open a new Undo transaction on the active document
 *  This method opens a new UNDO transaction on the active document. This transaction
 *  will later appear in the UNDO/REDO dialog with the name of the command. If the user
 *  recall the transaction everything changed on the document between OpenCommand() and
 *  CommitCommand will be undone (or redone). You can use an alternative name for the
 *  operation default is the command name.
 *  @see CommitCommand(),AbortCommand()
 */
int Document::openCommand(const char* sName)
{
    return getDocument()->openTransaction(App::TransactionName {.name = sName, .temporary = false});
}

void Document::commitCommand()
{
    getDocument()->commitTransaction();
}

void Document::abortCommand()
{
    getDocument()->abortTransaction();
}

bool Document::hasPendingCommand() const
{
    return getDocument()->hasPendingTransaction();
}

/// Get a string vector with the 'Undo' actions
std::vector<std::string> Document::getUndoVector() const
{
    return getDocument()->getAvailableUndoNames();
}

/// Get a string vector with the 'Redo' actions
std::vector<std::string> Document::getRedoVector() const
{
    return getDocument()->getAvailableRedoNames();
}

bool Document::checkTransactionID(bool undo, int iSteps)
{
    if (!iSteps) {
        return false;
    }

    std::vector<int> ids;
    for (int i = 0; i < iSteps; i++) {
        int id = getDocument()->getTransactionID(undo, i);
        if (!id) {
            break;
        }
        ids.push_back(id);
    }
    std::set<App::Document*> prompts;
    std::map<App::Document*, int> dmap;
    for (auto doc : App::GetApplication().getDocuments()) {
        if (doc == getDocument()) {
            continue;
        }
        for (auto id : ids) {
            int steps = undo ? doc->getAvailableUndos(id) : doc->getAvailableRedos(id);
            if (!steps) {
                continue;
            }
            int& currentSteps = dmap[doc];
            if (currentSteps + 1 != steps) {
                prompts.insert(doc);
            }
            if (currentSteps < steps) {
                currentSteps = steps;
            }
        }
    }
    if (!prompts.empty()) {
        std::ostringstream str;
        int i = 0;
        for (auto doc : prompts) {
            if (i++ == 5) {
                str << "...\n";
                break;
            }
            str << "    " << doc->getName() << "\n";
        }
        int ret = QMessageBox::warning(
            getMainWindow(),
            undo ? QObject::tr("Undo") : QObject::tr("Redo"),
            QStringLiteral("%1,\n%2%3")
                .arg(
                    QObject::tr(
                        "There are grouped transactions in the following documents with "
                        "other preceding transactions"
                    ),
                    QString::fromStdString(str.str()),
                    QObject::tr(
                        "Choose 'Yes' to roll back all preceding transactions.\n"
                        "Choose 'No' to roll back in the active document only.\n"
                        "Choose 'Abort' to abort"
                    )
                ),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Abort,
            QMessageBox::Yes
        );
        if (ret == QMessageBox::Abort) {
            return false;
        }
        if (ret == QMessageBox::No) {
            return true;
        }
    }
    for (auto& v : dmap) {
        for (int i = 0; i < v.second; ++i) {
            if (undo) {
                v.first->undo();
            }
            else {
                v.first->redo();
            }
        }
    }
    return true;
}

bool Document::isPerformingTransaction() const
{
    return d->_isTransacting;
}

/// Will UNDO one or more steps
void Document::undo(int iSteps)
{
    Base::FlagToggler<> flag(d->_isTransacting);

    if (!checkTransactionID(true, iSteps)) {
        return;
    }

    for (int i = 0; i < iSteps; i++) {
        getDocument()->undo();
    }
    App::GetApplication().signalUndo();
}

/// Will REDO one or more steps
void Document::redo(int iSteps)
{
    Base::FlagToggler<> flag(d->_isTransacting);

    if (!checkTransactionID(false, iSteps)) {
        return;
    }

    for (int i = 0; i < iSteps; i++) {
        getDocument()->redo();
    }
    App::GetApplication().signalRedo();

    for (auto it : d->_redoViewProviders) {
        handleChildren3D(it);
    }
    d->_redoViewProviders.clear();
}
