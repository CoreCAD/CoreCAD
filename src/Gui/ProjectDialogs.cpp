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


#include "PreCompiled.h"

#ifndef _PreComp_
# include <QDialogButtonBox>
# include <QDir>
# include <QFileInfo>
# include <QFormLayout>
# include <QHBoxLayout>
# include <QLabel>
# include <QLineEdit>
# include <QPushButton>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/Project.h>
#include <App/VersionHistory.h>
#include <Base/Exception.h>

#include "ProjectDialogs.h"

#include "Application.h"
#include "Document.h"
#include "FileDialog.h"
#include "MainWindow.h"
#include "ProjectPanel.h"
#include "WaitCursor.h"

using namespace Gui;

namespace
{

QString partFile(const App::Document* part)
{
    return part ? QString::fromUtf8(part->FileName.getValue()) : QString();
}

QString cleanFolder(const QString& folder)
{
    return QDir::cleanPath(QDir(folder).absolutePath());
}

bool holdsAProject(const QString& folder)
{
    const QString extension = QStringLiteral("*") + QLatin1String(App::projectFileExtension);
    return !QDir(folder).entryList({extension}, QDir::Files).isEmpty();
}

QLabel* wrappedLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    return label;
}

}  // namespace

NewProjectDialog::NewProjectDialog(App::Document* part, QWidget* parent)
    : QDialog(parent)
    , part(part)
{
    setWindowTitle(tr("New project"));
    setObjectName(QStringLiteral("NewProjectDialog"));

    nameEdit = new QLineEdit(this);
    nameEdit->setObjectName(QStringLiteral("ProjectName"));
    folderEdit = new QLineEdit(this);
    folderEdit->setObjectName(QStringLiteral("ProjectFolder"));
    auto* browse = new QPushButton(tr("Choose…"), this);

    auto* folderRow = new QHBoxLayout();
    folderRow->addWidget(folderEdit, 1);
    folderRow->addWidget(browse);
    auto* form = new QFormLayout();
    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Folder"), folderRow);

    statement = wrappedLabel(this);
    statement->setObjectName(QStringLiteral("ProjectStatement"));
    problem = wrappedLabel(this);
    problem->setObjectName(QStringLiteral("ProjectProblem"));

    auto* buttons = new QDialogButtonBox(this);
    createButton = buttons->addButton(tr("Create project"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(statement);
    layout->addWidget(problem);
    layout->addWidget(buttons);
    setMinimumWidth(480);

    connect(nameEdit, &QLineEdit::textChanged, this, &NewProjectDialog::folderFollowsName);
    connect(folderEdit, &QLineEdit::textEdited, this, [this]() {
        folderChosen = true;
        describe();
    });
    connect(browse, &QPushButton::clicked, this, &NewProjectDialog::chooseFolder);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewProjectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewProjectDialog::reject);

    folderFollowsName();
}

void NewProjectDialog::folderFollowsName()
{
    if (!folderChosen) {
        const QString base = part ? QFileInfo(partFile(part)).absolutePath() : QDir::homePath();
        folderEdit->setText(QDir(base).filePath(nameEdit->text().trimmed()));
    }
    describe();
}

void NewProjectDialog::chooseFolder()
{
    const QString folder
        = FileDialog::getExistingDirectory(this, tr("Project folder"), folderEdit->text());
    if (!folder.isEmpty()) {
        folderChosen = true;
        folderEdit->setText(QDir::toNativeSeparators(folder));
        describe();
    }
}

void NewProjectDialog::describe()
{
    const QString name = nameEdit->text().trimmed();
    const QString folder = cleanFolder(folderEdit->text().trimmed());
    QString blocked;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        blocked = tr("A project name cannot contain / or \\.");
    }
    else if (!name.isEmpty() && holdsAProject(folder)) {
        blocked = tr("This folder is already a project.");
    }

    statement->clear();
    if (part && !name.isEmpty()) {
        const QFileInfo file(partFile(part));
        if (cleanFolder(file.absolutePath()) == folder) {
            statement->setText(tr("%1 stays where it is.").arg(file.fileName()));
        }
        else if (QFileInfo::exists(QDir(folder).filePath(file.fileName()))) {
            blocked = tr("This folder already holds a file named %1.").arg(file.fileName());
        }
        else {
            statement->setText(
                tr("%1 will be moved to %2.").arg(file.fileName(), QDir::toNativeSeparators(folder))
            );
        }
    }
    problem->setText(blocked);
    problem->setVisible(!blocked.isEmpty());
    createButton->setEnabled(
        !name.isEmpty() && folderEdit->text().trimmed().size() > 0 && blocked.isEmpty()
    );
}

void NewProjectDialog::accept()
{
    const std::string name = nameEdit->text().trimmed().toStdString();
    const std::string folder = cleanFolder(folderEdit->text().trimmed()).toStdString();
    try {
        WaitCursor wait;
        created = QString::fromStdString(App::createProject(folder, name));
    }
    catch (const Base::Exception& e) {
        e.reportException();
        problem->setText(tr("The project was not made: %1").arg(QString::fromUtf8(e.what())));
        problem->show();
        return;
    }
    const bool moved = !part || movePartIntoProject(part, created);
    ProjectPanel::openProject(created);
    if (!moved) {
        problem->setText(tr("The project was made, but %1 was not moved into it.")
                             .arg(QFileInfo(partFile(part)).fileName()));
        problem->show();
        return;
    }
    QDialog::accept();
}

bool Gui::movePartIntoProject(App::Document* part, const QString& projectFile)
{
    try {
        WaitCursor wait;
        const std::string moved = App::moveIntoProject(*part, projectFile.toStdString());
        if (auto* guiDocument = Application::Instance->getDocument(part)) {
            guiDocument->setModified(false);
        }
        getMainWindow()->appendRecentFile(QString::fromStdString(moved));
    }
    catch (const Base::Exception& e) {
        e.reportException();
        return false;
    }
    return true;
}

bool Gui::addToProject(App::Document* part, QWidget* parent)
{
    const QString chosen = FileDialog::getOpenFileName(
        parent,
        QObject::tr("Add to project"),
        QFileInfo(partFile(part)).absolutePath(),
        {FileDialog::Filter(
            QObject::tr("Projects"),
            {QStringLiteral("*") + QLatin1String(App::projectFileExtension)}
        )}
    );
    if (chosen.isEmpty()) {
        return false;
    }

    const QFileInfo file(partFile(part));
    const QString folder = cleanFolder(QFileInfo(chosen).absolutePath());
    QString blocked;
    QString project;
    try {
        project = QString::fromStdString(App::projectName(chosen.toStdString()));
    }
    catch (const Base::Exception&) {
        blocked = QObject::tr("%1 is not a project file.").arg(QFileInfo(chosen).fileName());
    }
    if (blocked.isEmpty() && App::historyFolderOf(chosen.toStdString()).empty()) {
        blocked = QObject::tr("That folder is not kept in version control, so it is not a project.");
    }
    else if (blocked.isEmpty() && QFileInfo::exists(QDir(folder).filePath(file.fileName()))) {
        blocked = QObject::tr("The project already holds a file named %1.").arg(file.fileName());
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Add to project"));
    dialog.setObjectName(QStringLiteral("AddToProjectDialog"));
    auto* statement = wrappedLabel(&dialog);
    statement->setObjectName(QStringLiteral("ProjectStatement"));
    statement->setText(
        blocked.isEmpty() ? QObject::tr("%1 will be moved to %2, in the project %3.")
                                .arg(file.fileName(), QDir::toNativeSeparators(folder), project)
                          : blocked
    );
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* move
        = buttons->addButton(QObject::tr("Move to project"), QDialogButtonBox::AcceptRole);
    move->setEnabled(blocked.isEmpty());
    buttons->addButton(QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(statement);
    layout->addWidget(buttons);
    dialog.setMinimumWidth(420);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (!movePartIntoProject(part, chosen)) {
        return false;
    }
    ProjectPanel::openProject(chosen);
    return true;
}
