// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Cruth contributors

#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>

#include <App/Application.h>
#include <App/Document.h>

#include "DuplicateDocumentQuestion.h"
#include "MainWindow.h"

using namespace Gui;

App::DuplicateAnswer DuplicateDocumentQuestion::ask(
    const std::string& copyPath,
    const App::Document& open
) const
{
    const QString copyName = QFileInfo(QString::fromStdString(copyPath)).fileName();
    const QString openName = QString::fromStdString(open.Label.getStrValue());

    QMessageBox box(getMainWindow());
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QObject::tr("Copy of an open part"));
    box.setText(QObject::tr("“%1” is a copy of “%2”, which is already open.").arg(copyName, openName));
    box.setInformativeText(
        QObject::tr("Is it the same part, or a new part that should get its own identity?")
    );
    // No button is preselected: neither answer is a safe guess.
    QPushButton* same = box.addButton(QObject::tr("Same part"), QMessageBox::ActionRole);
    QPushButton* fresh = box.addButton(QObject::tr("New part"), QMessageBox::ActionRole);
    QPushButton* dontOpen = box.addButton(QObject::tr("Don't open"), QMessageBox::RejectRole);
    for (QPushButton* button : {same, fresh, dontOpen}) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    box.setEscapeButton(dontOpen);
    box.exec();

    if (box.clickedButton() == same) {
        return App::DuplicateAnswer::SamePart;
    }
    if (box.clickedButton() == fresh) {
        return App::DuplicateAnswer::NewPart;
    }
    return App::DuplicateAnswer::DontOpen;
}
