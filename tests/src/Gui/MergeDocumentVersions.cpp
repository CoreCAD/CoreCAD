// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <fstream>
#include <functional>
#include <iterator>

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QTest>
#include <QTimer>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/FeatureTest.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>

#include "InitGuiApplication.h"

class TestMergeDocumentVersions: public QObject  // NOLINT
{
    Q_OBJECT

private:
    std::vector<std::string> open;

    App::Document* version(const char* label)
    {
        App::Document* doc = tests::newViewlessDocument(label);
        doc->Label.setValue(label);
        open.emplace_back(doc->getName());
        return doc;
    }

    // The ancestor, A and B as three versions of one counter, A having changed it.
    void threeVersions()
    {
        auto* ancestor = version("Ancestor")->addObject<App::FeatureTest>("Counter");
        auto* mine = version("Mine")->addObject<App::FeatureTest>("Counter");
        auto* theirs = version("Theirs")->addObject<App::FeatureTest>("Counter");
        mine->Uid.setValue(ancestor->Uid.getValue());
        theirs->Uid.setValue(ancestor->Uid.getValue());
        mine->Integer.setValue(7);
    }

    // A version written by a build that knew a property type this one does not, so the read
    // holds that statement unhonoured.
    void openVersionWithAnUnknownProperty(const char* label)
    {
        auto& app = App::GetApplication();
        App::Document* written = version(label);
        written->addObject("App::VarSet", "Alpha");
        const std::string path = App::Application::getTempPath() + written->getName() + ".cpart";
        QVERIFY(written->saveAs(path.c_str()));
        app.closeDocument(written->getName());

        std::string text;
        {
            std::ifstream in(path, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const std::string::size_type at = text.find("<Properties>", text.find("name=\"Alpha\""));
        QVERIFY(at != std::string::npos);
        text.insert(
            at + std::string("<Properties>").size(),
            "\n<Property name=\"Sparkle\" type=\"Addon::PropertyGlitter\" dynamic=\"1\">\n"
            "<Glitter value=\"lots\"/>\n</Property>"
        );
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        App::Document* reopened = tests::openViewlessDocument(path.c_str());
        QVERIFY(reopened);
        QVERIFY(!reopened->heldStatements().empty());
        open.emplace_back(reopened->getName());
    }

    template<class Widget>
    static void whenShown(const std::function<void(Widget*)>& act)
    {
        QTimer::singleShot(10, [act]() {
            if (auto* widget = qobject_cast<Widget*>(QApplication::activeModalWidget())) {
                act(widget);
            }
            else {
                whenShown<Widget>(act);
            }
        });
    }

    static QList<QComboBox*> boxes(QDialog* dialog)
    {
        return dialog->findChildren<QComboBox*>();
    }

    static void runMerge()
    {
        Gui::Application::Instance->commandManager().runCommandByName("Std_MergeDocumentVersions");
    }

    static QString statusMessage()
    {
        QStringList texts;
        for (auto* label : Gui::getMainWindow()->statusBar()->findChildren<QLabel*>()) {
            texts << label->text();
        }
        return texts.join(QString()).trimmed();
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
    }

    void cleanup()  // NOLINT
    {
        for (const std::string& name : open) {
            if (App::GetApplication().getDocument(name.c_str())) {
                App::GetApplication().closeDocument(name.c_str());
            }
        }
        open.clear();
        Gui::getMainWindow()->showMessage(QString());
    }

    void theDialogOffersEachOpenDocumentAndDefaultsToThreeDifferentOnes()  // NOLINT
    {
        threeVersions();
        const QStringList labels {
            QStringLiteral("Ancestor"),
            QStringLiteral("Mine"),
            QStringLiteral("Theirs"),
        };
        QList<int> chosen;
        QStringList offered;
        whenShown<QDialog>([&](QDialog* dialog) {
            for (QComboBox* box : boxes(dialog)) {
                chosen << box->currentIndex();
                for (int i = 0; i < box->count(); ++i) {
                    offered << box->itemText(i);
                }
            }
            dialog->reject();
        });

        runMerge();
        QCOMPARE(chosen, (QList<int> {0, 1, 2}));
        QCOMPARE(offered, labels + labels + labels);
    }

    void cancelMergesNothing()  // NOLINT
    {
        threeVersions();
        whenShown<QDialog>([](QDialog* dialog) { dialog->reject(); });

        runMerge();
        QVERIFY(statusMessage().isEmpty());
    }

    void okMergesAndSaysWhereTheReportWent()  // NOLINT
    {
        threeVersions();
        whenShown<QDialog>([](QDialog* dialog) { dialog->accept(); });

        runMerge();
        QCOMPARE(statusMessage(), QStringLiteral("Merge report written to the Report view."));
    }

    void oneDocumentChosenTwiceIsRefusedBeforeAnyMerge()  // NOLINT
    {
        threeVersions();
        QString warning;
        whenShown<QDialog>([&](QDialog* dialog) {
            boxes(dialog).at(2)->setCurrentIndex(1);
            whenShown<QMessageBox>([&](QMessageBox* box) {
                warning = box->text();
                box->accept();
            });
            dialog->accept();
        });

        runMerge();
        QCOMPARE(
            warning,
            QStringLiteral("Choose three different documents: one ancestor and two edited copies.")
        );
        QVERIFY(statusMessage().isEmpty());
    }

    void aVersionTheMergeCannotHonourIsReportedNotMerged()  // NOLINT
    {
        version("Ancestor");
        version("Mine");
        openVersionWithAnUnknownProperty("Theirs");
        QString error;
        whenShown<QDialog>([&](QDialog* dialog) {
            whenShown<QMessageBox>([&](QMessageBox* box) {
                error = box->text();
                box->accept();
            });
            dialog->accept();
        });

        runMerge();
        QVERIFY2(error.contains(QStringLiteral("Sparkle")), qPrintable(error));
        QVERIFY(statusMessage().isEmpty());
    }
};

QTEST_MAIN(TestMergeDocumentVersions)

#include "MergeDocumentVersions.moc"
