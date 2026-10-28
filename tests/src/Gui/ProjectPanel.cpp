// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Project.h>
#include <App/VersionHistory.h>
#include <Gui/HistoryPanel.h>
#include <Gui/MainWindow.h>
#include <Gui/ProjectPanel.h>

#include "InitGuiApplication.h"

class TestProjectPanel: public QObject  // NOLINT
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    std::vector<std::string> open;

    QString path(const QString& relative) const
    {
        return dir.filePath(relative);
    }

    App::Document* savedPart(const QString& relative)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("Part").c_str());
        open.emplace_back(doc->getName());
        QDir().mkpath(QFileInfo(path(relative)).absolutePath());
        doc->saveAs(path(relative).toUtf8().constData());
        return doc;
    }

    std::string project(const QString& folder)
    {
        return App::createProject(path(folder).toStdString(), folder.toStdString());
    }

    static QTreeWidget* list()
    {
        return Gui::getMainWindow()->findChild<QTreeWidget*>(QStringLiteral("ProjectDocuments"));
    }

    static QStringList lines()
    {
        QStringList texts;
        for (QTreeWidgetItemIterator it(list()); *it; ++it) {
            texts << (*it)->text(0);
        }
        return texts;
    }

    static QTreeWidgetItem* rowNamed(const QString& name)
    {
        for (QTreeWidgetItemIterator it(list()); *it; ++it) {
            if ((*it)->text(0).startsWith(name)) {
                return *it;
            }
        }
        return nullptr;
    }

    static QPushButton* updateButton()
    {
        return list()->findChild<QPushButton*>(QStringLiteral("ProjectUpdate"));
    }

    static QLabel* problem()
    {
        return Gui::getMainWindow()->findChild<QLabel*>(QStringLiteral("ProjectProblem"));
    }

    static QString secondsAgo(qint64 seconds)
    {
        return Gui::timeAgo(QDateTime::currentSecsSinceEpoch() - seconds);
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
        QVERIFY(dir.isValid());
    }

    void cleanup()  // NOLINT
    {
        for (const std::string& name : open) {
            if (App::GetApplication().getDocument(name.c_str())) {
                App::GetApplication().closeDocument(name.c_str());
            }
        }
        open.clear();
    }

    void anEmptyProjectSaysSo()  // NOLINT
    {
        Gui::ProjectPanel::openProject(QString::fromStdString(project(QStringLiteral("Empty"))));
        QCOMPARE(lines(), QStringList {QStringLiteral("This project has no documents yet.")});
    }

    void theDocumentsInTheFolderAreListedByFileName()  // NOLINT
    {
        const std::string file = project(QStringLiteral("Gearbox"));
        savedPart(QStringLiteral("Gearbox/Housing.cpart"));
        savedPart(QStringLiteral("Gearbox/shafts/Shaft.cpart"));

        Gui::ProjectPanel::openProject(QString::fromStdString(file));
        QCOMPARE(
            lines(),
            (QStringList {
                QStringLiteral("In this project (2)"),
                QStringLiteral("Housing.cpart"),
                QStringLiteral("Shaft.cpart"),
                QStringLiteral("From outside (0)"),
            })
        );
        QCOMPARE(Gui::ProjectPanel::openProjectFile(), QString::fromStdString(file));
    }

    void anOutsideDocumentSaysWhereItIsAndWhichVersionItIsHeldAt()  // NOLINT
    {
        const std::string file = project(QStringLiteral("Rig"));
        savedPart(QStringLiteral("Parts/Clamp.cpart"));
        App::startHistory(path(QStringLiteral("Parts")).toStdString());
        App::commitFile(path(QStringLiteral("Parts/Clamp.cpart")).toStdString(), "First clamp");
        App::addOutsideDocument(file, path(QStringLiteral("Parts/Clamp.cpart")).toStdString());

        Gui::ProjectPanel::openProject(QString::fromStdString(file));
        QTreeWidgetItem* row = rowNamed(QStringLiteral("Clamp.cpart"));
        QVERIFY(row);
        QCOMPARE(row->text(0), QStringLiteral("Clamp.cpart\n../Parts · First clamp · just now"));
        QVERIFY(!updateButton());
    }

    void aNewerVersionOffersUpdateAndUpdateTakesIt()  // NOLINT
    {
        const std::string file = project(QStringLiteral("Press"));
        App::Document* bolt = savedPart(QStringLiteral("Library/Bolt.cpart"));
        const std::string boltFile = path(QStringLiteral("Library/Bolt.cpart")).toStdString();
        App::startHistory(path(QStringLiteral("Library")).toStdString());
        App::commitFile(boltFile, "Bolt");
        App::addOutsideDocument(file, boltFile);
        bolt->addObject("App::DocumentObjectGroup", "Thread");
        QVERIFY(bolt->save());
        App::commitFile(boltFile, "Thread");

        Gui::ProjectPanel::openProject(QString::fromStdString(file));
        QVERIFY2(
            rowNamed(QStringLiteral("Bolt.cpart"))
                ->text(0)
                .endsWith(QStringLiteral("Bolt · just now · newer version available")),
            qPrintable(rowNamed(QStringLiteral("Bolt.cpart"))->text(0))
        );
        QVERIFY(updateButton());

        updateButton()->click();
        QVERIFY(
            rowNamed(QStringLiteral("Bolt.cpart"))->text(0).endsWith(QStringLiteral("Thread · just now"))
        );
        QTRY_VERIFY(!updateButton());
        QCOMPARE(App::outsideDocuments(file).front().version, App::fileVersions(boltFile).front().commit);
    }

    void anOutsideDocumentWhoseFileIsGoneIsMissing()  // NOLINT
    {
        const std::string file = project(QStringLiteral("Jig"));
        savedPart(QStringLiteral("Loose/Pin.cpart"));
        App::addOutsideDocument(file, path(QStringLiteral("Loose/Pin.cpart")).toStdString());
        QVERIFY(QFile::remove(path(QStringLiteral("Loose/Pin.cpart"))));

        Gui::ProjectPanel::openProject(QString::fromStdString(file));
        QCOMPARE(
            rowNamed(QStringLiteral("Pin.cpart"))->text(0),
            QStringLiteral("Pin.cpart\n../Loose · missing")
        );
    }

    void anUnreadableProjectFileIsReportedNotListed()  // NOLINT
    {
        const std::string file = project(QStringLiteral("Broken"));
        QFile text(QString::fromStdString(file));
        QVERIFY(text.open(QIODevice::WriteOnly | QIODevice::Truncate));
        text.write("not a project");
        text.close();

        Gui::ProjectPanel::openProject(QString::fromStdString(file));
        QVERIFY(lines().isEmpty());
        QVERIFY(!problem()->isHidden());
        QVERIFY(problem()->text().contains(QStringLiteral("can no longer be read")));
    }

    void timeAgoCountsUpInMinutesHoursAndDays()  // NOLINT
    {
        QCOMPARE(secondsAgo(30), QStringLiteral("just now"));
        QCOMPARE(secondsAgo(60), QStringLiteral("1 minute ago"));
        QCOMPARE(secondsAgo(59 * 60), QStringLiteral("59 minutes ago"));
        QCOMPARE(secondsAgo(3600), QStringLiteral("1 hour ago"));
        QCOMPARE(secondsAgo(23 * 3600), QStringLiteral("23 hours ago"));
        QCOMPARE(secondsAgo(86400), QStringLiteral("1 day ago"));
        QCOMPARE(secondsAgo(6 * 86400), QStringLiteral("6 days ago"));
    }

    void timeAgoGivesADateOnceItIsOverAWeek()  // NOLINT
    {
        const qint64 then = QDateTime::currentSecsSinceEpoch() - 8 * 86400;
        QCOMPARE(
            Gui::timeAgo(then),
            QLocale().toString(QDateTime::fromSecsSinceEpoch(then).date(), QLocale::ShortFormat)
        );
    }
};

QTEST_MAIN(TestProjectPanel)

#include "ProjectPanel.moc"
