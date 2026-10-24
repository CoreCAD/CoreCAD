// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionObject.h>

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

    // A person picking two faces of one part has picked two things, and a command is owed both.
    // They arrive gathered under the object they belong to rather than as two separate picks,
    // which is what lets a command ask "which part, and which bits of it".
    void test_twoPartsOfOneObjectArriveTogether()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face1");
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face2");

        const auto picked = Gui::Selection().getSelectionEx(doc->getName());
        QCOMPARE(picked.size(), std::size_t(1));
        QCOMPARE(picked.front().getObject(), obj);

        const auto& subs = picked.front().getSubNames();
        QCOMPARE(subs.size(), std::size_t(2));
        QCOMPARE(subs[0], std::string("Face1"));
        QCOMPARE(subs[1], std::string("Face2"));
    }

    // And letting go of one of them is letting go of one of them.
    void test_droppingOnePartKeepsTheOther()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face1");
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face2");

        Gui::Selection().rmvSelection(doc->getName(), obj->getNameInDocument(), "Face1");

        QVERIFY(!Gui::Selection().isSelected(obj, "Face1"));
        QVERIFY(Gui::Selection().isSelected(obj, "Face2"));
    }

    // Several parts of one object can be handed over in one go, which is how a command that
    // works out its own set of faces says so.
    void test_severalPartsCanBeGivenAtOnce()  // NOLINT
    {
        Gui::Selection()
            .addSelections(doc->getName(), obj->getNameInDocument(), {"Edge1", "Edge2", "Edge3"});

        const auto picked = Gui::Selection().getSelectionEx(doc->getName());
        QCOMPARE(picked.size(), std::size_t(1));
        QCOMPARE(picked.front().getSubNames().size(), std::size_t(3));
    }

    // Clearing what was picked in one document leaves another document's picks alone. A command
    // acting on one document must not quietly change what is picked in another.
    void test_clearingOneDocumentLeavesAnother()  // NOLINT
    {
        App::Document* other = tests::newViewlessDocument("sel_other");
        App::DocumentObject* otherObj = other->addObject("App::DocumentObjectGroup", "Group");

        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument());
        Gui::Selection().addSelection(other->getName(), otherObj->getNameInDocument());

        Gui::Selection().clearSelection(doc->getName());

        QVERIFY(!Gui::Selection().isSelected(obj));
        QVERIFY(Gui::Selection().isSelected(otherObj));

        Gui::Selection().clearCompleteSelection();
        App::GetApplication().closeDocument(other->getName());
    }

    // Asking about an object and asking about a part of it are two different questions, and
    // the answers are deliberately not symmetrical.
    //
    // "Is this object picked" means "is any part of it picked" -- true when a person picked one
    // of its faces, because a command offered the object has something to work on. "Is this
    // part picked" means that exact part -- false when a person picked the whole object, because
    // a command offered a face must not be handed one nobody named. A command author who assumes
    // one answer implies the other gets it wrong in one direction or the other.
    void test_askingAboutAnObjectAndAboutAPartOfItAreDifferentQuestions()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face1");
        QVERIFY(Gui::Selection().isSelected(obj));
        QVERIFY(Gui::Selection().isSelected(obj, "Face1"));
        QVERIFY(!Gui::Selection().isSelected(obj, "Face2"));

        Gui::Selection().clearCompleteSelection();

        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument());
        QVERIFY(Gui::Selection().isSelected(obj));
        QVERIFY(!Gui::Selection().isSelected(obj, "Face1"));
    }

    // Picking the whole object when one of its parts is already picked does not make a second
    // entry, and does not widen the first one: the part a person named is still the part a
    // command is given.
    void test_pickingTheWholeObjectOnTopOfAPartChangesNothing()  // NOLINT
    {
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument(), "Face1");
        Gui::Selection().addSelection(doc->getName(), obj->getNameInDocument());

        const auto picked = Gui::Selection().getSelectionEx(doc->getName());
        QCOMPARE(picked.size(), std::size_t(1));
        QCOMPARE(picked.front().getSubNames().size(), std::size_t(1));
        QCOMPARE(picked.front().getSubNames().front(), std::string("Face1"));
    }
};

QTEST_MAIN(TestSelection)

#include "Selection.moc"
