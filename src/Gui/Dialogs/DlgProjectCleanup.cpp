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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QApplication>
# include <QDir>
# include <QFileDialog>
# include <QHeaderView>
# include <QLocale>
# include <QMessageBox>
# include <QPushButton>
# include <QTreeWidgetItem>
# include <string>
# include <vector>
#endif

#include <App/UnreferencedFiles.h>
#include <Base/Exception.h>

#include "Dialogs/DlgProjectCleanup.h"
#include "ui_DlgProjectCleanup.h"
#include "Command.h"


using namespace Gui::Dialog;

namespace
{
/// The full path a row stands for, carried on the row rather than rebuilt from what it displays.
constexpr int PathRole = Qt::UserRole;

/// What a file holds, said the way a person reads it rather than in bytes.
QString asSize(qulonglong bytes)
{
    return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}
}  // namespace


DlgProjectCleanup::DlgProjectCleanup(const QString& projectFolder, QWidget* parent)
    : QDialog(parent)
    , _folder(projectFolder)
    , ui(new Ui_DlgProjectCleanup)
{
    ui->setupUi(this);
    ui->folder->setText(_folder);
    ui->material->header()->setStretchLastSection(false);
    ui->material->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->material->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // The kept-results half often cannot answer the first time it is asked, because a part in
    // the folder is open. Closing that part and asking again is the whole of the fix, so the way
    // to ask again is here rather than through closing and reopening this.
    QPushButton* again = ui->buttonBox->addButton(tr("Scan Again"), QDialogButtonBox::ActionRole);
    again->setObjectName(QStringLiteral("scanAgain"));
    again->setAutoDefault(false);
    connect(again, &QPushButton::clicked, this, &DlgProjectCleanup::scan);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(ui->browse, &QPushButton::clicked, this, &DlgProjectCleanup::onBrowse);
    connect(ui->material, &QTreeWidget::itemChanged, this, &DlgProjectCleanup::onTicked);
    connect(ui->removeRebuild, &QPushButton::clicked, this, &DlgProjectCleanup::onRemoveRebuildResults);
    connect(ui->removeMaterial, &QPushButton::clicked, this, &DlgProjectCleanup::onRemoveSourceMaterial);

    scan();
}

DlgProjectCleanup::~DlgProjectCleanup()
{
    delete ui;
}

QString DlgProjectCleanup::projectFolder() const
{
    return _folder;
}

void DlgProjectCleanup::onBrowse()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this,
        tr("Project Folder"),
        _folder.isEmpty() ? QDir::homePath() : _folder
    );
    if (chosen.isEmpty()) {
        return;
    }
    _folder = chosen;
    ui->folder->setText(_folder);
    scan();
}

void DlgProjectCleanup::scan()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    scanRebuildResults();
    scanSourceMaterial();
    QApplication::restoreOverrideCursor();
}

void DlgProjectCleanup::scanRebuildResults()
{
    ui->removeRebuild->setEnabled(false);
    if (_folder.isEmpty()) {
        ui->rebuildSummary->setText(tr("Choose a project folder to ask about."));
        return;
    }

    try {
        const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.toUtf8().constData());
        if (found.unreferenced.empty()) {
            ui->rebuildSummary->setText(
                tr("Nothing kept here is out of date. %n part(s) read.",
                   "",
                   static_cast<int>(found.recipesRead.size()))
            );
            return;
        }
        ui->rebuildSummary->setText(tr("%n result(s) that nothing names any more, holding %1.",
                                       "",
                                       static_cast<int>(found.unreferenced.size()))
                                        .arg(asSize(found.bytes)));
        ui->removeRebuild->setEnabled(true);
    }
    catch (const Base::Exception& refused) {
        // The refusal is the answer, not a failure to give one, so it is shown where the answer
        // would have been. Nearly always this is a part being open, and a person can act on that.
        ui->rebuildSummary->setText(
            tr("This cannot be answered yet: %1").arg(QString::fromUtf8(refused.what()))
        );
    }
}

void DlgProjectCleanup::scanSourceMaterial()
{
    // Off while the rows are put in, or filling the list counts as a person ticking things.
    const QSignalBlocker quiet(ui->material);
    ui->material->clear();
    ui->removeMaterial->setEnabled(false);

    if (_folder.isEmpty()) {
        ui->materialSummary->setText(QString());
        return;
    }

    try {
        const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.toUtf8().constData());
        for (const App::UnreferencedFile& file : found.unreferenced) {
            const QString path = QString::fromUtf8(file.path.c_str());
            auto* row = new QTreeWidgetItem(ui->material);
            row->setText(0, QFileInfo(path).fileName());
            row->setText(1, asSize(file.bytes));
            row->setToolTip(0, path);
            row->setData(0, PathRole, path);
            // Nothing is ticked by a program. Every one of these is authored content, and the
            // whole point of this half is that a person chooses them one at a time.
            row->setCheckState(0, Qt::Unchecked);
        }
        ui->materialSummary->setText(
            found.unreferenced.empty()
                ? tr("Everything this project was handed is still used by something in it.")
                : tr("%n file(s) that nothing names any more, holding %1 in total.",
                     "",
                     static_cast<int>(found.unreferenced.size()))
                      .arg(asSize(found.bytes))
        );
    }
    catch (const Base::Exception& refused) {
        ui->materialSummary->setText(
            tr("This cannot be answered yet: %1").arg(QString::fromUtf8(refused.what()))
        );
    }
}

void DlgProjectCleanup::onTicked()
{
    for (int i = 0; i < ui->material->topLevelItemCount(); ++i) {
        if (ui->material->topLevelItem(i)->checkState(0) == Qt::Checked) {
            ui->removeMaterial->setEnabled(true);
            return;
        }
    }
    ui->removeMaterial->setEnabled(false);
}

void DlgProjectCleanup::onRemoveRebuildResults()
{
    // Through the same call a script would make, so a recorded macro says what was done here and
    // the one act has one meaning (P8).
    try {
        Gui::Command::doCommand(
            Gui::Command::Doc,
            "App.discardUnreferencedRebuildResults('%s')",
            _folder.toUtf8().constData()
        );
    }
    catch (const Base::Exception& refused) {
        QMessageBox::warning(this, tr("Remove Kept Results"), QString::fromUtf8(refused.what()));
    }
    scan();
}

void DlgProjectCleanup::onRemoveSourceMaterial()
{
    QStringList ticked;
    qulonglong holding = 0;
    for (int i = 0; i < ui->material->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* row = ui->material->topLevelItem(i);
        if (row->checkState(0) == Qt::Checked) {
            ticked << row->data(0, PathRole).toString();
            holding += QFileInfo(row->data(0, PathRole).toString()).size();
        }
    }
    if (ticked.isEmpty()) {
        return;
    }

    QMessageBox ask(this);
    ask.setIcon(QMessageBox::Warning);
    ask.setWindowTitle(tr("Remove Source Material"));
    ask.setText(tr("Remove %n file(s) this project was handed?", "", ticked.size()));
    ask.setInformativeText(
        tr("Nothing in this project names them any more, and each was checked again just now.\n\n"
           "This is content the project was given rather than content it produced: a scan, an "
           "imported body, a measured surface. If no copy exists anywhere else, it cannot be "
           "produced again.")
    );
    ask.setDetailedText(ticked.join(QLatin1String("\n")));
    QPushButton* remove = ask.addButton(tr("Remove"), QMessageBox::DestructiveRole);
    QPushButton* cancel = ask.addButton(QMessageBox::Cancel);
    ask.setDefaultButton(cancel);
    ask.exec();
    if (ask.clickedButton() != remove) {
        return;
    }

    QStringList quoted;
    for (const QString& path : ticked) {
        quoted << QStringLiteral("'%1'").arg(path);
    }
    try {
        Gui::Command::doCommand(
            Gui::Command::Doc,
            "App.discardSourceMaterial('%s', [%s])",
            _folder.toUtf8().constData(),
            quoted.join(QLatin1String(", ")).toUtf8().constData()
        );
    }
    catch (const Base::Exception& refused) {
        QMessageBox::warning(this, tr("Remove Source Material"), QString::fromUtf8(refused.what()));
    }
    scan();
}

#include "moc_DlgProjectCleanup.cpp"
