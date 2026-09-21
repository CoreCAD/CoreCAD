// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Selection/Selection.h>

#include "InitGuiApplication.h"

/** What the program believes a person has picked.
 *
 * Selection is the layer every command reads before it acts, and until now nothing tested it.
 * A defect here is silent by construction: the command runs, it just runs on the wrong thing.
 */
class TestSelection: public QObject  // NOLINT
{
    Q_OBJECT

private:
    App::Document* doc {};
    App::DocumentObject* obj {};

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
    }

    void init()  // NOLINT
    {
        doc = tests::newViewlessDocument("sel");
        obj = doc->addObject("App::DocumentObjectGroup", "Group");
    }

    void cleanup()  // NOLINT
    {
        Gui::Selection().clearCompleteSelection();
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
        obj = nullptr;
    }

    void test_theHarnessStandsUpTheProgram()  // NOLINT
    {
        QVERIFY(Gui::Application::Instance != nullptr);
        QVERIFY(Gui::getMainWindow() != nullptr);
        QVERIFY(Gui::Application::Instance->getDocument(doc) != nullptr);
    }

    void test_anObjectAddedIsAnObjectSelected()  // NOLINT
    {
        QVERIFY(!Gui::Selection().hasSelection());
        QVERIFY(Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument()));
        QVERIFY(Gui::Selection().isSelected(obj));
        QCOMPARE(Gui::Selection().getSelection(doc->getName()).size(), std::size_t(1));
    }

    void test_anObjectRemovedIsNoLongerSelected()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument());
        Gui::Selection().rmvSelection(doc->getName(), obj->getNameInDocument());
        QVERIFY(!Gui::Selection().isSelected(obj));
        QVERIFY(!Gui::Selection().hasSelection());
    }

    void test_aDeletedObjectLeavesTheSelection()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument());
        doc->removeObject(obj->getNameInDocument());
        obj = nullptr;
        QVERIFY(!Gui::Selection().hasSelection());
    }
};

QTEST_MAIN(TestSelection)

#include "Selection.moc"
