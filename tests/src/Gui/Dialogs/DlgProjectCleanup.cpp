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

#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

#include <filesystem>
#include <fstream>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectFileIncluded.h>
#include <Base/FileInfo.h>

#include "Gui/Dialogs/DlgProjectCleanup.h"

#include <src/App/InitApplication.h>

namespace fs = std::filesystem;

/** A person can see what a project no longer names, and the two halves are not one button.
 *
 * Cruth: removing a kept rebuild result costs a rebuild and loses nothing that was designed.
 * Removing source material can cost a design -- it is content the project was handed, and a part
 * that quietly loses the body it was built from looks exactly like a part that never had one.
 * The acts themselves are proved at the App layer, where they confirm for themselves that nothing
 * names a file before removing it. What is proved here is the half that only the interface can
 * answer for: that a person is shown what was found, that nothing authored is ticked on their
 * behalf, and that a refusal is presented as the answer rather than as silence.
 */
class TestDlgProjectCleanup: public QObject  // NOLINT
{
    Q_OBJECT

private:
    fs::path folder;
    std::vector<std::string> opened;

    static QTreeWidget* materialList(const Gui::Dialog::DlgProjectCleanup& dlg)
    {
        return dlg.findChild<QTreeWidget*>(QStringLiteral("material"));
    }

    static QString rebuildSummary(const Gui::Dialog::DlgProjectCleanup& dlg)
    {
        return dlg.findChild<QLabel*>(QStringLiteral("rebuildSummary"))->text();
    }

    static QString materialSummary(const Gui::Dialog::DlgProjectCleanup& dlg)
    {
        return dlg.findChild<QLabel*>(QStringLiteral("materialSummary"))->text();
    }

    static QPushButton* removeMaterial(const Gui::Dialog::DlgProjectCleanup& dlg)
    {
        return dlg.findChild<QPushButton*>(QStringLiteral("removeMaterial"));
    }

    static void writeFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    /// A part in the project folder holding one handed-in file, saved.
    App::Document* aPartIncluding(const std::string& name, const std::string& contents)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName(name.c_str()).c_str());
        opened.push_back(doc->getName());

        const fs::path handedIn = folder.parent_path() / (name + "-source.txt");
        writeFile(handedIn, contents);

        auto* holder = doc->addObject<App::DocumentObjectFileIncluded>("Included");
        holder->File.setValue(handedIn.string().c_str());
        doc->recompute();
        doc->saveAs((folder / (name + ".cpart")).string().c_str());
        return doc;
    }

    /// The same part saved again over a different handed-in file, which is what leaves a body
    /// behind that nothing names.
    void replaceWhatItWasHanded(App::Document* doc, const std::string& contents)
    {
        auto* holder = dynamic_cast<App::DocumentObjectFileIncluded*>(doc->getObject("Included"));
        QVERIFY(holder != nullptr);
        const fs::path second = folder.parent_path() / "second-source.txt";
        writeFile(second, contents);
        holder->File.setValue(second.string().c_str());
        doc->recompute();
        QVERIFY(doc->save());
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initApplication();
    }

    void init()  // NOLINT
    {
        folder = fs::path(Base::FileInfo::getTempFileName()) / "project";
        fs::create_directories(folder);
    }

    void cleanup()  // NOLINT
    {
        for (const std::string& name : opened) {
            if (App::GetApplication().getDocument(name.c_str()) != nullptr) {
                App::GetApplication().closeDocument(name.c_str());
            }
        }
        opened.clear();
        std::error_code ignored;
        fs::remove_all(folder.parent_path(), ignored);
    }

    // The one thing only the interface can get wrong: ticking authored content on a person's
    // behalf. Every row is a file that cannot be produced again, and none of them arrives ticked.
    void test_nothingAuthoredIsTickedOnAPersonsBehalf()  // NOLINT
    {
        App::Document* doc = aPartIncluding("one", "the first body");
        replaceWhatItWasHanded(doc, "the second body, which is a different length");

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        QTreeWidget* rows = materialList(dlg);
        QCOMPARE(rows->topLevelItemCount(), 1);
        QCOMPARE(rows->topLevelItem(0)->checkState(0), Qt::Unchecked);
        QVERIFY2(
            !removeMaterial(dlg)->isEnabled(),
            "the removal was offered before a person had chosen anything"
        );
    }

    // And once a person does choose, the way out is open. A list nobody can act on is a report.
    void test_tickingAFileOffersTheRemoval()  // NOLINT
    {
        App::Document* doc = aPartIncluding("one", "the first body");
        replaceWhatItWasHanded(doc, "the second body, which is a different length");

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        materialList(dlg)->topLevelItem(0)->setCheckState(0, Qt::Checked);
        QVERIFY(removeMaterial(dlg)->isEnabled());

        materialList(dlg)->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QVERIFY2(
            !removeMaterial(dlg)->isEnabled(),
            "the removal stayed offered after a person changed their mind"
        );
    }

    // A person has to be able to find out that the answer is nothing, said in words rather than
    // by an empty box that reads the same as a question that was never asked.
    void test_aProjectThatNamesEverythingSaysSo()  // NOLINT
    {
        aPartIncluding("one", "a body the part still uses");

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        QCOMPARE(materialList(dlg)->topLevelItemCount(), 0);
        QVERIFY2(
            materialSummary(dlg).contains(QStringLiteral("still used")),
            qPrintable(materialSummary(dlg))
        );
        QVERIFY(!removeMaterial(dlg)->isEnabled());
    }

    // The halves ask different things of a person, and the one that cannot answer says why it
    // cannot rather than showing nothing. Unsaved changes are the case a person will hit.
    void test_theKeptResultsHalfSaysWhyItCannotAnswerYet()  // NOLINT
    {
        App::Document* doc = aPartIncluding("one", "a handed-in body");
        // Changed and not saved, so what it holds is no longer what its file states.
        doc->Comment.setValue("edited and not written out");
        QVERIFY(!doc->statesWhatItsFileStates());

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        const QString said = rebuildSummary(dlg);
        QVERIFY2(said.contains(QStringLiteral("cannot be answered yet")), qPrintable(said));
        QVERIFY2(said.contains(QStringLiteral("one.cpart")), qPrintable(said));
        QVERIFY2(
            !dlg.findChild<QPushButton*>(QStringLiteral("removeRebuild"))->isEnabled(),
            "a removal was offered for a question that was never answered"
        );

        // The other half needs nothing closed, and answers while that part is still open.
        QVERIFY2(
            !materialSummary(dlg).contains(QStringLiteral("cannot be answered")),
            qPrintable(materialSummary(dlg))
        );
    }

    // Asking again is what a person does after closing the part that was in the way -- and the
    // ticks a person made before are not carried over a fresh answer, because the list under them
    // is a fresh list.
    void test_askingAgainStartsFromWhatIsThereNow()  // NOLINT
    {
        App::Document* doc = aPartIncluding("one", "the first body");
        replaceWhatItWasHanded(doc, "the second body, which is a different length");

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        materialList(dlg)->topLevelItem(0)->setCheckState(0, Qt::Checked);
        QVERIFY(removeMaterial(dlg)->isEnabled());

        dlg.scan();
        QCOMPARE(materialList(dlg)->topLevelItemCount(), 1);
        QCOMPARE(materialList(dlg)->topLevelItem(0)->checkState(0), Qt::Unchecked);
        QVERIFY2(
            !removeMaterial(dlg)->isEnabled(),
            "a tick made against the old answer still stood against the new one"
        );
    }

    // And a part simply being open is not in the way. A person cleaning up a project should not
    // have to close what they are working on to be given an answer about it.
    void test_aPartOpenAndUnchangedIsNotInTheWay()  // NOLINT
    {
        App::Document* doc = aPartIncluding("one", "a handed-in body");  // left open, unchanged
        QVERIFY(doc->statesWhatItsFileStates());

        Gui::Dialog::DlgProjectCleanup dlg(QString::fromStdString(folder.string()));
        QVERIFY2(
            !rebuildSummary(dlg).contains(QStringLiteral("cannot be answered")),
            qPrintable(rebuildSummary(dlg))
        );
    }

    // With no folder there is no question, and the dialog says that rather than reporting an
    // empty project.
    void test_withNoFolderItAsksForOne()  // NOLINT
    {
        Gui::Dialog::DlgProjectCleanup dlg(QString {});
        QVERIFY2(
            rebuildSummary(dlg).contains(QStringLiteral("Choose a project folder")),
            qPrintable(rebuildSummary(dlg))
        );
        QVERIFY(!removeMaterial(dlg)->isEnabled());
        QVERIFY(!dlg.findChild<QPushButton*>(QStringLiteral("removeRebuild"))->isEnabled());
    }
};

QTEST_MAIN(TestDlgProjectCleanup)

#include "DlgProjectCleanup.moc"
