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
#include <QString>

namespace Gui
{
namespace Dialog
{

class Ui_DlgProjectCleanup;

/** What a project folder holds that nothing in it names any more, and the two ways out.
 *
 * Cruth: a project accumulates two kinds of file that nothing removes. Kept rebuild results are
 * named by a digest of the recipe that produced them, so editing one dimension leaves the old
 * result beside the new one. Source material is named by a digest of its own bytes, so replacing
 * an import leaves the old body in `assets/` -- which is visible and versioned, so the dead
 * weight is handed over with the project.
 *
 * **The two are not one button, and this dialog is where that shows.** Removing a kept result
 * costs a rebuild and loses nothing that was designed, so it is offered whole. Source material is
 * authored content -- a part that quietly loses the body it was built from looks exactly like a
 * part that never had one -- so nothing here is ticked by default and each file is a person's own
 * choice. The acts themselves live in `App::discardUnreferencedRebuildResults` and
 * `App::discardSourceMaterial`, which confirm for themselves that nothing names a file before
 * removing it; this shows what they found and asks.
 *
 * The two halves also need different things of a person, and each says so. Asking what names a
 * kept result means computing its name, which means opening the documents -- so that half cannot
 * answer while a part in the folder is open. The source-material half reads the recipes as text
 * and needs nothing closed.
 */
class DlgProjectCleanup: public QDialog
{
    Q_OBJECT

public:
    explicit DlgProjectCleanup(const QString& projectFolder, QWidget* parent = nullptr);
    ~DlgProjectCleanup() override;

    /// The folder being asked about.
    QString projectFolder() const;

public Q_SLOTS:
    /// Ask the folder both questions again and show what it answered. A person reaches this
    /// after closing a part, which is what the kept-results half was waiting for.
    void scan();

private Q_SLOTS:
    void onBrowse();
    void onTicked();
    void onRemoveRebuildResults();
    void onRemoveSourceMaterial();

private:
    /// Ask about kept rebuild results, which needs the folder's documents closed.
    void scanRebuildResults();
    /// Ask about source material, which needs nothing closed.
    void scanSourceMaterial();

    QString _folder;
    Ui_DlgProjectCleanup* ui;
};

}  // namespace Dialog
}  // namespace Gui
