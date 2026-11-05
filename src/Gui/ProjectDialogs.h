// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                 *
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
#include <QString>

#include <FCGlobal.h>

class QLabel;
class QLineEdit;
class QPushButton;

namespace App
{
class Document;
}

namespace Gui
{

/** New project: asks for a name and a folder, and says what will move before anything does.
 *
 *  With a part, the part is moved into the new project's folder. Moving cannot be undone, so the
 *  dialog states it before it happens and that is its confirmation; no second question follows.
 *  Without one, an empty project is made.
 */
class GuiExport NewProjectDialog: public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(App::Document* part, QWidget* parent = nullptr);

    /// The project file made, once the dialog has been accepted.
    QString projectFile() const
    {
        return created;
    }

    void accept() override;

private:
    void folderFollowsName();
    void chooseFolder();
    void describe();

    App::Document* part;
    QLineEdit* nameEdit;
    QLineEdit* folderEdit;
    QLabel* statement;
    QLabel* problem;
    QPushButton* createButton;
    bool folderChosen = false;
    QString created;
};

/// Add to project: choose a project file, see what will move, and move the part there.
/// Returns whether the part was moved.
GuiExport bool addToProject(App::Document* part, QWidget* parent = nullptr);

/// Move a saved part into a project's folder, as a command, and update what the window knows
/// of the part's file. Reports and returns false when the move did not happen.
GuiExport bool movePartIntoProject(App::Document* part, const QString& projectFile);

}  // namespace Gui
