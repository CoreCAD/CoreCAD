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

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/SealedArchive.h>

#include "Gui/DocumentRecovery.h"

#include <src/App/InitApplication.h>

/** Recovery calls an original file corrupted only when it is.
 *
 * Recovery compares a recovery file against the document it was taken from, and treats an
 * unreadable original as corrupted. That test knew only the legacy container, so every document
 * this build saves -- a plain-text recipe -- was reported corrupted at startup.
 */
class TestDocumentRecovery: public QObject  // NOLINT
{
    Q_OBJECT

private:
    App::Document* doc {};
    QTemporaryDir dir;

    QString pathOf(const char* name) const
    {
        return dir.filePath(QString::fromLatin1(name));
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initApplication();
    }

    void init()  // NOLINT
    {
        auto& app = App::GetApplication();
        doc = app.newDocument(app.getUniqueDocumentName("recovery").c_str(), "testUser");
        doc->addObject("App::DocumentObjectGroup", "Step");
    }

    void cleanup()  // NOLINT
    {
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
    }

    void aSavedRecipeIsIntact()  // NOLINT
    {
        const QString path = pathOf("saved.cpart");
        QVERIFY(doc->saveAs(path.toUtf8().constData()));
        QVERIFY(Gui::Dialog::originalDocumentIsIntact(QFileInfo(path)));
    }

    void aSealedArchiveIsIntact()  // NOLINT
    {
        const QString path = pathOf("sealed.cpart");
        App::writeSealedArchive(*doc, path.toStdString());
        QVERIFY(Gui::Dialog::originalDocumentIsIntact(QFileInfo(path)));
    }

    void aRecipeCutShortIsCorrupted()  // NOLINT
    {
        const QString path = pathOf("cut.cpart");
        QVERIFY(doc->saveAs(path.toUtf8().constData()));
        QFile file(path);
        QVERIFY(file.open(QFile::ReadWrite));
        QVERIFY(file.resize(file.size() / 2));
        file.close();
        QVERIFY(!Gui::Dialog::originalDocumentIsIntact(QFileInfo(path)));
    }

    void aMissingFileIsNotIntact()  // NOLINT
    {
        QVERIFY(!Gui::Dialog::originalDocumentIsIntact(QFileInfo(pathOf("absent.cpart"))));
    }
};

QTEST_MAIN(TestDocumentRecovery)

#include "DocumentRecovery.moc"
