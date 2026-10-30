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
#include <QPlainTextEdit>
#include <QTest>
#include <QTreeWidget>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include "Gui/Dialogs/DlgHeldStatements.h"

#include <src/App/InitApplication.h>

/** A person can read what this document is holding before deciding anything about it.
 *
 * Cruth (Amendment 19 Clause 19.4): the act of discarding is a person's and only a person's, and
 * the act itself is proved elsewhere. What is proved here is the half that was missing -- that
 * the interface shows what is held, attributed and in the file's own words, so the decision is
 * an informed one rather than a leap. A view that reports a durable id and nothing else is not a
 * view a person can decide from.
 */
class TestDlgHeldStatements: public QObject  // NOLINT
{
    Q_OBJECT

private:
    App::Document* doc {};

    static QStringList rowsOf(const Gui::Dialog::DlgHeldStatements& dlg)
    {
        auto* tree = dlg.findChild<QTreeWidget*>(QStringLiteral("statements"));
        QStringList said;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            const QTreeWidgetItem* item = tree->topLevelItem(i);
            said << QStringLiteral("%1 | %2 | %3").arg(item->text(0), item->text(1), item->text(2));
        }
        return said;
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initApplication();
    }

    void init()  // NOLINT
    {
        auto& app = App::GetApplication();
        doc = app.newDocument(app.getUniqueDocumentName("held").c_str(), "testUser");
    }

    void cleanup()  // NOLINT
    {
        if (doc != nullptr) {
            App::GetApplication().closeDocument(doc->getName());
            doc = nullptr;
        }
    }

    // A person has to be able to find out that the answer is nothing. A view that says nothing
    // at all, or that a person can only reach once a document is stuck, is not an answer.
    void test_aDocumentHoldingNothingSaysSo()  // NOLINT
    {
        Gui::Dialog::DlgHeldStatements dlg(doc);
        QVERIFY(rowsOf(dlg).isEmpty());
        QVERIFY(!dlg.findChild<QTreeWidget*>(QStringLiteral("statements"))->isVisibleTo(&dlg));
        QVERIFY(dlg.findChild<QLabel*>(QStringLiteral("explanation"))
                    ->text()
                    .contains(QStringLiteral("nothing it cannot honour")));
    }

    // Who holds it, under what name, and why -- the three things the act needs and a person
    // needs, attributed to the object rather than to the document at large.
    void test_aStatementIsAttributedToTheObjectHoldingIt()  // NOLINT
    {
        App::DocumentObject* alpha = doc->addObject("App::VarSet", "Alpha");
        alpha->Label.setValue("First");
        alpha->rememberStatedProperty(
            "Sparkle",
            "<Property name=\"Sparkle\"/>",
            "this build has no property of that name and type"
        );

        Gui::Dialog::DlgHeldStatements dlg(doc);
        QCOMPARE(
            rowsOf(dlg),
            QStringList {QStringLiteral(
                "First (Alpha) | Sparkle | this build has no property of that name and type"
            )}
        );
    }

    // The file's own words, not this session's reading of them: the point of keeping a statement
    // is that nobody here understood it, so nobody here may paraphrase it either.
    void test_theViewShowsTheWordsTheFileStates()  // NOLINT
    {
        App::DocumentObject* alpha = doc->addObject("App::VarSet", "Alpha");
        alpha->rememberStatedProperty(
            "Sparkle",
            "<Glitter value=\"lots\"/>",
            "this build has no property of that name and type"
        );

        Gui::Dialog::DlgHeldStatements dlg(doc);
        auto* tree = dlg.findChild<QTreeWidget*>(QStringLiteral("statements"));
        tree->setCurrentItem(tree->topLevelItem(0));
        QCOMPARE(
            dlg.findChild<QPlainTextEdit*>(QStringLiteral("words"))->toPlainText(),
            QStringLiteral("<Glitter value=\"lots\"/>")
        );
    }

    // One name held for two reasons is one act, so it is one row. A row per reason would offer a
    // person a choice between them, and discarding the name drops both.
    void test_oneNameHeldForTwoReasonsIsOneRow()  // NOLINT
    {
        App::DocumentObject* alpha = doc->addObject("App::VarSet", "Alpha");
        alpha->Label.setValue("Alpha");
        alpha->rememberMissingSource("Skin", "brushed-steel");
        alpha->rememberUnresolvedReference(
            "Skin",
            {{"00000000-0000-4000-8000-000000000000", std::string {}}}
        );

        Gui::Dialog::DlgHeldStatements dlg(doc);
        const QStringList rows = rowsOf(dlg);
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.front().contains(QStringLiteral("brushed-steel")));
        QVERIFY(rows.front().contains(QStringLiteral("does not hold")));
    }

    // A durable id identifies an object to the program and to nobody else. A person about to drop
    // a whole object is entitled to know which object it is.
    void test_aKeptObjectIsNamedNotOnlyIdentified()  // NOLINT
    {
        doc->keepUnreadObject(
            "11111111-2222-4333-8444-555555555555",
            "Addon::Widget",
            "<Object uuid=\"11111111-2222-4333-8444-555555555555\" type=\"Addon::Widget\" "
            "name=\"Beta\"><Properties/></Object>"
        );

        Gui::Dialog::DlgHeldStatements dlg(doc);
        const QStringList rows = rowsOf(dlg);
        QCOMPARE(rows.size(), 1);
        QVERIFY2(
            rows.front().contains(QStringLiteral("Beta (11111111-2222-4333-8444-555555555555)")),
            qPrintable(rows.front())
        );
        QVERIFY(rows.front().contains(QStringLiteral("Addon::Widget")));
    }
};

QTEST_MAIN(TestDlgHeldStatements)

#include "DlgHeldStatements.moc"
