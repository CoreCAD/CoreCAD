// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/ViewProviderDocumentObject.h>

#include "InitGuiApplication.h"

/** The safety net under everything a person does.
 *
 * A command is a bracket: whatever happens between its opening and its close is one thing to
 * undo. Both halves of a document take part -- the object and the view provider that shows it --
 * and the second half is the one that can go missing without anything looking wrong, because an
 * object with no view provider is simply an object nobody can see or pick.
 */
class TestGuiTransaction: public QObject  // NOLINT
{
    Q_OBJECT

private:
    App::Document* doc {};
    Gui::Document* guiDoc {};

    static Gui::ViewProviderDocumentObject* viewOf(Gui::Document* gdoc, App::DocumentObject* obj)
    {
        return freecad_cast<Gui::ViewProviderDocumentObject*>(gdoc->getViewProvider(obj));
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
    }

    void init()  // NOLINT
    {
        doc = tests::newViewlessDocument("undo");
        doc->setUndoMode(1);
        guiDoc = Gui::Application::Instance->getDocument(doc);
    }

    void cleanup()  // NOLINT
    {
        if (doc) {
            App::GetApplication().closeDocument(doc->getName());
        }
        doc = nullptr;
        guiDoc = nullptr;
    }

    // Opening a command books a name; the step itself begins at the first change. So a command
    // that opens and closes with nothing between them is not an empty entry in the undo list --
    // it is no entry at all, which is why a person never sees one.
    void test_aCommandBecomesAStepOnlyOnceSomethingChanges()  // NOLINT
    {
        QVERIFY(!guiDoc->hasPendingCommand());
        guiDoc->openCommand("Change nothing");
        QVERIFY(!guiDoc->hasPendingCommand());
        guiDoc->commitCommand();
        QVERIFY(guiDoc->getUndoVector().empty());

        guiDoc->openCommand("Add a group");
        doc->addObject("App::DocumentObjectGroup", "Group");
        QVERIFY(guiDoc->hasPendingCommand());
        guiDoc->commitCommand();

        QVERIFY(!guiDoc->hasPendingCommand());
        QCOMPARE(guiDoc->getUndoVector().size(), std::size_t(1));
        QCOMPARE(guiDoc->getUndoVector().front(), std::string("Add a group"));
    }

    // A command a person abandoned leaves nothing behind -- on either side of the document.
    void test_anAbandonedCommandLeavesNothingBehind()  // NOLINT
    {
        guiDoc->openCommand("Add a group");
        doc->addObject("App::DocumentObjectGroup", "Group");
        QVERIFY(guiDoc->getViewProviderByName("Group") != nullptr);

        guiDoc->abortCommand();

        QVERIFY(doc->getObject("Group") == nullptr);
        QVERIFY(guiDoc->getViewProviderByName("Group") == nullptr);
        QVERIFY(guiDoc->getUndoVector().empty());
    }

    // Undoing a deletion has to bring back the half a person can see, not only the object. An
    // object restored without its view provider is in the document and invisible: it cannot be
    // shown, picked or selected, and nothing reports that anything is wrong.
    void test_undoingADeletionBringsBackWhatShowsTheObject()  // NOLINT
    {
        guiDoc->openCommand("Add a group");
        auto* obj = doc->addObject("App::DocumentObjectGroup", "Group");
        guiDoc->commitCommand();
        QVERIFY(viewOf(guiDoc, obj) != nullptr);
        // A decision recorded on this side only, so that a view provider built fresh from the
        // restored object rather than put back by the transaction is told apart from one that
        // was kept.
        viewOf(guiDoc, obj)->ShowInTree.setValue(false);

        guiDoc->openCommand("Delete the group");
        doc->removeObject("Group");
        guiDoc->commitCommand();
        QVERIFY(guiDoc->getViewProviderByName("Group") == nullptr);

        guiDoc->undo(1);

        auto* restored = doc->getObject("Group");
        QVERIFY(restored != nullptr);
        QVERIFY2(viewOf(guiDoc, restored) != nullptr, "the object came back with nothing to show it");
        QVERIFY2(
            !viewOf(guiDoc, restored)->ShowInTree.getValue(),
            "the object came back shown differently from how it was"
        );
    }

    // Redo puts back what undo took away, on both sides.
    void test_redoRestoresWhatUndoRemoved()  // NOLINT
    {
        guiDoc->openCommand("Add a group");
        auto* added = doc->addObject("App::DocumentObjectGroup", "Group");
        guiDoc->commitCommand();
        viewOf(guiDoc, added)->ShowInTree.setValue(false);

        guiDoc->undo(1);
        QVERIFY(doc->getObject("Group") == nullptr);
        QCOMPARE(guiDoc->getRedoVector().size(), std::size_t(1));

        guiDoc->redo(1);
        auto* obj = doc->getObject("Group");
        QVERIFY(obj != nullptr);
        QVERIFY2(viewOf(guiDoc, obj) != nullptr, "the object came back with nothing to show it");
        QVERIFY2(
            !viewOf(guiDoc, obj)->ShowInTree.getValue(),
            "the object came back shown differently from how it was"
        );
    }

    // A change to how an object is shown is a change a person made, and undo owes them the same
    // answer for it as for a change to the object itself.
    void test_undoRestoresAChangeToHowAnObjectIsShown()  // NOLINT
    {
        auto* obj = doc->addObject("App::DocumentObjectGroup", "Group");
        auto* view = viewOf(guiDoc, obj);
        view->ShowInTree.setValue(true);

        guiDoc->openCommand("Hide it from the tree");
        view->ShowInTree.setValue(false);
        guiDoc->commitCommand();

        guiDoc->undo(1);
        QVERIFY(viewOf(guiDoc, obj)->ShowInTree.getValue());
    }
};

QTEST_MAIN(TestGuiTransaction)

#include "Transaction.moc"
