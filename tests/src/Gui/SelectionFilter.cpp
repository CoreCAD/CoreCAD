// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Base/Exception.h>

#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

#include "InitGuiApplication.h"

/** What a command will accept.
 *
 * A command says what it can work on in a small language -- a kind of object, optionally a kind
 * of part of it, optionally how many. Everything that decides whether a menu entry is available,
 * and whether a command run from a script is given input it can use, goes through this. The
 * failure that matters is a filter that accepts too much: the command then runs on input it was
 * never written for, and the first sign of it is whatever the command does next.
 */
class TestSelectionFilter: public QObject  // NOLINT
{
    Q_OBJECT

private:
    /// A gate that says no to everything, so that what is being read is the gate and not the
    /// rule inside it.
    ///
    /// Gates are handed over, not lent: the selection takes ownership and deletes the gate when
    /// it is removed or replaced, so one of these is always built on the heap and never owned
    /// by the test.
    struct RefuseEverything: Gui::SelectionGate
    {
        bool allow(App::Document*, App::DocumentObject*, const char*) override
        {
            notAllowedReason = "nothing is allowed here";
            return false;
        }
    };

    App::Document* doc {};
    App::DocumentObject* group {};

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
    }

    void init()  // NOLINT
    {
        doc = tests::newViewlessDocument("filter");
        group = doc->addObject("App::DocumentObjectGroup", "Group");
    }

    void cleanup()  // NOLINT
    {
        Gui::Selection().clearCompleteSelection();
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;
        group = nullptr;
    }

    // A filter naming a kind of object accepts that kind and refuses another.
    void test_aFilterNamingAKindAcceptsThatKind()  // NOLINT
    {
        Gui::SelectionFilter accepts("SELECT App::DocumentObjectGroup");
        QVERIFY(accepts.test(group, nullptr));

        Gui::SelectionFilter refuses("SELECT App::MaterialObject");
        QVERIFY(!refuses.test(group, nullptr));
    }

    // A kind includes the kinds derived from it, which is what lets a command say "any object"
    // once rather than listing everything that has ever been written.
    void test_aKindIncludesTheKindsDerivedFromIt()  // NOLINT
    {
        Gui::SelectionFilter anyObject("SELECT App::DocumentObject");
        QVERIFY(anyObject.test(group, nullptr));
    }

    // A filter naming a kind of part accepts that part and refuses another part of the same
    // object. This is the whole point of the sub-element half: a command that works on edges
    // must not be handed a face.
    void test_aFilterNamingAPartRefusesADifferentPart()  // NOLINT
    {
        Gui::SelectionFilter edges("SELECT App::DocumentObject SUBELEMENT Edge");
        QVERIFY(edges.test(group, "Edge1"));
        QVERIFY(!edges.test(group, "Face1"));
    }

    // ★ The one that matters: a filter nobody can read refuses to exist.
    //
    // A filter is written by hand, in a string, often in a workbench nobody is currently looking
    // at. A typo that made it accept everything instead of nothing would quietly widen every
    // command carrying it to input it was never written for, and nothing would report it -- the
    // command would simply be offered, and then misbehave. So an unreadable filter is not a
    // filter that accepts nothing; it is refused outright, at the point of writing, where whoever
    // wrote it is still looking.
    void test_aFilterNobodyCanReadRefusesToExist()  // NOLINT
    {
        QVERIFY_THROWS_EXCEPTION(
            Base::ParserError,
            Gui::SelectionFilter nonsense("this is not a filter")
        );
        QVERIFY_THROWS_EXCEPTION(Base::ParserError, Gui::SelectionFilter halfWritten("SELECT"));
    }

    // A filter that says nothing accepts nothing, which is the quiet half of the same rule: a
    // command that forgot to say what it takes is offered nothing rather than everything.
    void test_aFilterThatSaysNothingAcceptsNothing()  // NOLINT
    {
        Gui::SelectionFilter empty("");
        QVERIFY(!empty.test(group, nullptr));
        QVERIFY(!empty.match());

        Gui::Selection().addSelection(doc->getName(), group->getNameInDocument());
        QVERIFY(!empty.match());
    }

    // A filter reads what a person has picked, not only what it is handed.
    void test_aFilterReadsWhatAPersonHasPicked()  // NOLINT
    {
        Gui::SelectionFilter oneObject("SELECT App::DocumentObjectGroup");
        QVERIFY(!oneObject.match());

        Gui::Selection().addSelection(doc->getName(), group->getNameInDocument());
        QVERIFY(oneObject.match());
    }

    // A gate is the same decision made earlier: instead of judging what was picked, it refuses
    // the pick itself, so a person is stopped at the click rather than told afterwards.
    void test_aGateRefusesThePickItself()  // NOLINT
    {
        Gui::Selection().addSelectionGate(
            new RefuseEverything,
            Gui::ResolveMode::OldStyleElement,
            doc->getName()
        );

        QVERIFY(!Gui::Selection().addSelection(doc->getName(), group->getNameInDocument()));
        QVERIFY(!Gui::Selection().hasSelection());

        Gui::Selection().rmvSelectionGate(doc);
        QVERIFY(Gui::Selection().addSelection(doc->getName(), group->getNameInDocument()));
    }

    // A gate belongs to the document a command is working in. A command that narrowed what can
    // be picked in one document must not narrow another, or a person would find a second
    // document inexplicably refusing them.
    void test_aGateBelongsToOneDocument()  // NOLINT
    {
        App::Document* other = tests::newViewlessDocument("filter_other");
        App::DocumentObject* otherGroup = other->addObject("App::DocumentObjectGroup", "Group");

        Gui::Selection().addSelectionGate(
            new RefuseEverything,
            Gui::ResolveMode::OldStyleElement,
            doc->getName()
        );

        QVERIFY(!Gui::Selection().addSelection(doc->getName(), group->getNameInDocument()));
        QVERIFY(Gui::Selection().addSelection(other->getName(), otherGroup->getNameInDocument()));

        Gui::Selection().rmvSelectionGate(doc);
        Gui::Selection().clearCompleteSelection();
        App::GetApplication().closeDocument(other->getName());
    }

    // And how many were picked is part of what it accepts, so a command written for one thing is
    // not offered two.
    void test_howManyWerePickedIsPartOfIt()  // NOLINT
    {
        auto* second = doc->addObject("App::DocumentObjectGroup", "Other");

        Gui::SelectionFilter exactlyOne("SELECT App::DocumentObjectGroup COUNT 1");
        Gui::Selection().addSelection(doc->getName(), group->getNameInDocument());
        QVERIFY(exactlyOne.match());

        Gui::Selection().addSelection(doc->getName(), second->getNameInDocument());
        QVERIFY(!exactlyOne.match());
    }
};

QTEST_MAIN(TestSelectionFilter)

#include "SelectionFilter.moc"
