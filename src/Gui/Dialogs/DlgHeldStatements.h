// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   Cruth is free software: you can redistribute it and/or modify it       *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Cruth is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with Cruth. If not, see                                  *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <QDialog>

class QPushButton;
class QTreeWidgetItem;

namespace App
{
class Document;
}

namespace Gui
{
namespace Dialog
{

class Ui_DlgHeldStatements;

/** What a document is holding that this build could not honour, and the one way out of it.
 *
 * Cruth (Amendment 19 Clause 19.4): a statement nothing here can honour is discarded by a
 * person's deliberate act and by nothing else. The act itself lives on the document
 * (`App::Document::discardStatement`), which is where the refusals and the history entry are.
 * This is the door to it -- the interface's half of P8, which until now ran only one way: the
 * act existed in the API and a person working in the window was told to perform it with no way
 * to do so.
 *
 * The dialog shows and it discards. It never decides: nothing here drops a statement without a
 * person choosing that row and confirming what it drops.
 */
class DlgHeldStatements: public QDialog
{
    Q_OBJECT

public:
    explicit DlgHeldStatements(App::Document* doc, QWidget* parent = nullptr);
    ~DlgHeldStatements() override;

private Q_SLOTS:
    void onSelectionChanged();
    void onDiscard();

private:
    /// Read the document again and show what it is holding now.
    void reload();

    App::Document* _doc;
    Ui_DlgHeldStatements* ui;
    QPushButton* _discard;
};

}  // namespace Dialog
}  // namespace Gui
