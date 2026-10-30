// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Project.h>

#include "Gui/ProjectDialogs.h"

#include <src/App/InitApplication.h>

class TestProjectDialogs: public QObject  // NOLINT
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    App::Document* part {};

    static QLineEdit* nameOf(const Gui::NewProjectDialog& dlg)
    {
        return dlg.findChild<QLineEdit*>(QStringLiteral("ProjectName"));
    }

    static QLineEdit* folderOf(const Gui::NewProjectDialog& dlg)
    {
        return dlg.findChild<QLineEdit*>(QStringLiteral("ProjectFolder"));
    }

    static QString statementOf(const Gui::NewProjectDialog& dlg)
    {
        return dlg.findChild<QLabel*>(QStringLiteral("ProjectStatement"))->text();
    }

    static QString problemOf(const Gui::NewProjectDialog& dlg)
    {
        return dlg.findChild<QLabel*>(QStringLiteral("ProjectProblem"))->text();
    }

    static bool canCreate(const Gui::NewProjectDialog& dlg)
    {
        auto* buttons = dlg.findChild<QDialogButtonBox*>();
        for (QAbstractButton* button : buttons->buttons()) {
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) {
                return button->isEnabled();
            }
        }
        return false;
    }

    QString touch(const QString& relative)
    {
        const QString path = dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        return path;
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initApplication();
        QVERIFY(dir.isValid());
    }

    void init()  // NOLINT
    {
        auto& app = App::GetApplication();
        part = app.newDocument(app.getUniqueDocumentName("part").c_str(), "part");
        part->FileName.setValue(touch(QStringLiteral("parts/bracket.FCStd")).toStdString());
    }

    void cleanup()  // NOLINT
    {
        App::GetApplication().closeDocument(part->getName());
        part = nullptr;
    }

    void anEmptyNameCannotBeCreated()  // NOLINT
    {
        Gui::NewProjectDialog dlg(nullptr);
        QVERIFY(!canCreate(dlg));
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        QVERIFY(canCreate(dlg));
    }

    void aNameWithASlashIsRefused()  // NOLINT
    {
        Gui::NewProjectDialog dlg(nullptr);
        nameOf(dlg)->setText(QStringLiteral("a/b"));
        QVERIFY(!canCreate(dlg));
        QVERIFY(problemOf(dlg).contains(QStringLiteral("cannot contain")));
    }

    void aFolderThatIsAlreadyAProjectIsRefused()  // NOLINT
    {
        touch(QStringLiteral("taken/old") + QLatin1String(App::projectFileExtension));
        Gui::NewProjectDialog dlg(nullptr);
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        folderOf(dlg)->clear();
        QTest::keyClicks(folderOf(dlg), dir.filePath(QStringLiteral("taken")));
        QVERIFY(!canCreate(dlg));
        QVERIFY(problemOf(dlg).contains(QStringLiteral("already a project")));
    }

    void theFolderFollowsTheNameUntilTheUserTypesOne()  // NOLINT
    {
        Gui::NewProjectDialog dlg(part);
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        QCOMPARE(
            folderOf(dlg)->text(),
            QDir(dir.filePath(QStringLiteral("parts"))).filePath(QStringLiteral("Rig"))
        );

        folderOf(dlg)->clear();
        QTest::keyClicks(folderOf(dlg), dir.filePath(QStringLiteral("mine")));
        nameOf(dlg)->setText(QStringLiteral("Other"));
        QCOMPARE(folderOf(dlg)->text(), dir.filePath(QStringLiteral("mine")));
    }

    void aPartOutsideTheFolderIsSaidToMove()  // NOLINT
    {
        Gui::NewProjectDialog dlg(part);
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        QVERIFY2(
            statementOf(dlg).contains(QStringLiteral("bracket.FCStd will be moved to")),
            qPrintable(statementOf(dlg))
        );
        QVERIFY(canCreate(dlg));
    }

    void aPartAlreadyInTheFolderStays()  // NOLINT
    {
        Gui::NewProjectDialog dlg(part);
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        folderOf(dlg)->clear();
        QTest::keyClicks(folderOf(dlg), dir.filePath(QStringLiteral("parts")));
        QVERIFY2(
            statementOf(dlg).contains(QStringLiteral("stays where it is")),
            qPrintable(statementOf(dlg))
        );
        QVERIFY(canCreate(dlg));
    }

    void aFileOfTheSameNameInTheFolderIsRefused()  // NOLINT
    {
        touch(QStringLiteral("clash/bracket.FCStd"));
        Gui::NewProjectDialog dlg(part);
        nameOf(dlg)->setText(QStringLiteral("Rig"));
        folderOf(dlg)->clear();
        QTest::keyClicks(folderOf(dlg), dir.filePath(QStringLiteral("clash")));
        QVERIFY(!canCreate(dlg));
        QVERIFY(problemOf(dlg).contains(QStringLiteral("already holds a file named bracket.FCStd")));
        QVERIFY(statementOf(dlg).isEmpty());
    }
};

QTEST_MAIN(TestProjectDialogs)
#include "ProjectDialogs.moc"
