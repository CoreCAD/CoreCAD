// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QMimeData>
#include <QTest>
#include <Inventor/nodes/SoSwitch.h>
#include <QTreeWidgetItem>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>
#include <App/FeatureTest.h>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Tree.h>
#include <Gui/ViewProviderDocumentObject.h>

#include "InitGuiApplication.h"

/** The list a person reads to find out what is in a document.
 *
 * Nothing here decides anything -- which is the point. What an object belongs to is worked out
 * from what the objects say about each other, and this list is one reading of that answer. When
 * it disagrees with the document, the document is right and a person is misinformed.
 */
/// Reaches the tree's own drag handlers; a synthetic drag event is not delivered without a real drag.
struct DragAccess: Gui::TreeWidget  // NOLINT
{
    using Gui::TreeWidget::dragMoveEvent;
    using Gui::TreeWidget::dropEvent;
};

class TestTree: public QObject  // NOLINT
{
    Q_OBJECT

private:
    App::Document* doc {};
    Gui::Document* guiDoc {};
    Gui::TreeWidget* tree {};

    /// Force the pending work through rather than waiting for the timer that normally does it.
    void settle()
    {
        QMetaObject::invokeMethod(tree, "onUpdateStatus", Qt::DirectConnection);
    }

    /// The tree as a person would read it down the page: one name per line, indented by depth,
    /// leaving out the End of Model row, which stands last in every document.
    ///
    /// An item a person asked not to see is still an item -- it is marked hidden rather than
    /// taken out -- so reading the list the way a person reads it means passing those by.
    QStringList outline(bool withEndOfModel = false)
    {
        settle();
        QStringList lines;
        std::function<void(QTreeWidgetItem*, int)> walk = [&](QTreeWidgetItem* item, int depth) {
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                if (child->isHidden()) {
                    continue;
                }
                if (child->type() == Gui::TreeWidget::StopRowType && !withEndOfModel) {
                    continue;
                }
                lines << QString(depth * 2, QLatin1Char(' ')) + child->text(0);
                walk(child, depth + 1);
            }
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            walk(tree->topLevelItem(i), 0);
        }
        return lines;
    }

    /// Drag the row named @p dragged and drop it just above the row named @p target, as a
    /// person does with the mouse. False when the tree refuses the drop while it is dragged.
    bool dropAbove(const QString& dragged, const QString& target)
    {
        settle();
        tree->resize(400, 400);
        tree->show();
        tree->expandAll();
        auto* from = tree->findItems(dragged, Qt::MatchExactly | Qt::MatchRecursive).value(0);
        auto* onto = tree->findItems(target, Qt::MatchExactly | Qt::MatchRecursive).value(0);
        if (!from || !onto) {
            return false;
        }
        tree->clearSelection();
        from->setSelected(true);
        const QRect rect = tree->visualItemRect(onto);
        const QPointF above(rect.center().x(), rect.top() + 1);
        QMimeData mime;
        QDragMoveEvent move(above.toPoint(), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        (tree->*(&DragAccess::dragMoveEvent))(&move);
        if (!move.isAccepted()) {
            return false;
        }
        QDropEvent drop(above, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        (tree->*(&DragAccess::dropEvent))(&drop);
        return true;
    }

    QStringList documentOrder() const
    {
        QStringList names;
        for (auto* obj : doc->getObjects()) {
            names << QString::fromLatin1(obj->getNameInDocument());
        }
        return names;
    }

private Q_SLOTS:
    void initTestCase()  // NOLINT
    {
        tests::initGuiApplication();
    }

    void init()  // NOLINT
    {
        // The tree is built before the document so that it hears the document arrive, which is
        // the order the running program uses.
        tree = new Gui::TreeWidget("test");
        doc = tests::newViewlessDocument("tree");
        guiDoc = Gui::Application::Instance->getDocument(doc);
    }

    void cleanup()  // NOLINT
    {
        if (doc) {
            App::GetApplication().closeDocument(doc->getName());
        }
        delete tree;
        tree = nullptr;
        doc = nullptr;
        guiDoc = nullptr;
    }

    // An object that belongs to nothing is read at the top of the list.
    void test_anObjectWithNoOwnerIsReadAtTheTop()  // NOLINT
    {
        doc->addObject("App::DocumentObjectGroup", "Alpha");
        QCOMPARE(outline(), QStringList {QStringLiteral("Alpha")});
    }

    // What an object belongs to is worked out from what the group says it holds. Nothing is
    // stored on the object pointing back, and nothing in this list is told where to put things.
    void test_anObjectIsReadUnderWhatClaimsIt()  // NOLINT
    {
        auto* group = doc->addObject("App::DocumentObjectGroup", "Holder");
        auto* child = doc->addObject("App::DocumentObjectGroup", "Held");
        freecad_cast<App::DocumentObjectGroup*>(group)->addObject(child);

        const QStringList expected {QStringLiteral("Holder"), QStringLiteral("  Held")};
        QCOMPARE(outline(), expected);
    }

    // And the answer follows the document when it changes: an object let go of is read at the
    // top again, not left where it used to be.
    void test_anObjectLetGoOfIsReadAtTheTopAgain()  // NOLINT
    {
        auto* group = doc->addObject("App::DocumentObjectGroup", "Holder");
        auto* child = doc->addObject("App::DocumentObjectGroup", "Held");
        auto* asGroup = freecad_cast<App::DocumentObjectGroup*>(group);
        asGroup->addObject(child);
        const QStringList nested {QStringLiteral("Holder"), QStringLiteral("  Held")};
        QCOMPARE(outline(), nested);

        asGroup->removeObject(child);

        const QStringList expected {QStringLiteral("Held"), QStringLiteral("Holder")};
        QStringList actual = outline();
        actual.sort();
        QCOMPARE(actual, expected);
    }

    // An object a person asked not to see is not in the list, and is still in the document.
    void test_anObjectAPersonHidFromTheListIsNotInIt()  // NOLINT
    {
        auto* shown = doc->addObject("App::DocumentObjectGroup", "Shown");
        auto* hidden = doc->addObject("App::DocumentObjectGroup", "Hidden");
        Q_UNUSED(shown);
        auto* view = freecad_cast<Gui::ViewProviderDocumentObject*>(guiDoc->getViewProvider(hidden));
        view->ShowInTree.setValue(false);

        QCOMPARE(outline(), QStringList {QStringLiteral("Shown")});
        QVERIFY(doc->getObject("Hidden") != nullptr);
    }

    // End of Model stands last while the model is built to the end, and just after the row it
    // stops at when rolled back.
    void test_endOfModelStandsLastOrAfterTheRowTheModelStopsAt()  // NOLINT
    {
        auto* first = doc->addObject("App::DocumentObjectGroup", "First");
        doc->addObject("App::DocumentObjectGroup", "Second");
        doc->addObject("App::DocumentObjectGroup", "Third");

        const QStringList built {
            QStringLiteral("First"),
            QStringLiteral("Second"),
            QStringLiteral("Third"),
            QStringLiteral("End of Model")
        };
        QCOMPARE(outline(true), built);

        doc->setRollbackMarker(first);
        const QStringList rolledBack {
            QStringLiteral("First"),
            QStringLiteral("End of Model"),
            QStringLiteral("Second"),
            QStringLiteral("Third")
        };
        QCOMPARE(outline(true), rolledBack);

        doc->setRollbackMarker(nullptr);
        QCOMPARE(outline(true), built);
    }

    // A step past the stop row is not drawn, and keeps its own visibility so that rolling
    // forward brings it back as it was.
    void test_aStepPastTheStopRowIsNotDrawnUntilRolledForward()  // NOLINT
    {
        auto* first = doc->addObject("App::DocumentObjectGroup", "First");
        auto* second = doc->addObject("App::Plane", "Second");
        auto* view = guiDoc->getViewProvider(second);
        view->show();
        auto drawn = [view] {
            return view->getModeSwitch()->whichChild.getValue() >= 0;
        };
        QVERIFY(drawn());

        doc->setRollbackMarker(first);
        settle();
        QVERIFY(!drawn());
        QVERIFY(second->Visibility.getValue());

        doc->setRollbackMarker(nullptr);
        settle();
        QVERIFY(drawn());
        // Its row reads as shown again too, not left greyed from the roll-back.
        auto* row = tree->findItems(QStringLiteral("Second"), Qt::MatchExactly | Qt::MatchRecursive)
                        .value(0);
        QVERIFY(row);
        QVERIFY(!row->data(0, Qt::ForegroundRole).isValid());
    }

    // The list is the timeline: dropping a row moves the step in the document's history.
    void test_droppingARowMovesTheStepInTheHistory()  // NOLINT
    {
        doc->addObject("App::FeatureTest", "First");
        doc->addObject("App::FeatureTest", "Second");
        QVERIFY(dropAbove(QStringLiteral("Second"), QStringLiteral("First")));
        QCOMPARE(documentOrder(), (QStringList {QStringLiteral("Second"), QStringLiteral("First")}));
    }

    // A step cannot be dropped above a step it is built from.
    void test_aStepCannotBeDroppedAboveWhatItIsBuiltFrom()  // NOLINT
    {
        auto* source = doc->addObject("App::FeatureTest", "Source");
        auto* user = doc->addObject("App::FeatureTest", "User");
        static_cast<App::FeatureTest*>(user)->Link.setValue(source);
        QVERIFY(!dropAbove(QStringLiteral("User"), QStringLiteral("Source")));
        QCOMPARE(documentOrder(), (QStringList {QStringLiteral("Source"), QStringLiteral("User")}));
    }

    // The world frame stays above every step, as in most CAD tools; no drop moves it below one.
    void test_theOriginStaysAboveEveryStep()  // NOLINT
    {
        doc->addObject("App::Origin", "Origin");
        doc->addObject("App::FeatureTest", "First");
        doc->addObject("App::FeatureTest", "Second");
        const bool originDown = dropAbove(QStringLiteral("Origin"), QStringLiteral("Second"));
        const bool stepUp = dropAbove(QStringLiteral("Second"), QStringLiteral("Origin"));
        QVERIFY(!originDown);
        QVERIFY(!stepUp);
        QCOMPARE(documentOrder().indexOf(QStringLiteral("Origin")), 0);
    }

    // Standing on the world frame, the model builds no step at all.
    void test_endOfModelCanStandRightBelowTheOrigin()  // NOLINT
    {
        auto* origin = doc->addObject("App::Origin", "Origin");
        auto* first = doc->addObject("App::FeatureTest", "First");
        doc->setRollbackMarker(origin);
        settle();
        QVERIFY(doc->isRolledBackPast(first));
        QStringList rows;
        for (const QString& line : outline(true)) {
            if (!line.startsWith(QLatin1Char(' '))) {
                rows << line;
            }
        }
        QCOMPARE(
            rows.indexOf(QStringLiteral("End of Model")),
            rows.indexOf(QStringLiteral("Origin")) + 1
        );
    }

    // A step made while rolled back goes in just after the row the model stopped at, and the
    // stop row moves on to stand after it -- in the list as in the history.
    void test_aStepMadeWhileRolledBackIsListedAtTheStopRow()  // NOLINT
    {
        auto* first = doc->addObject("App::FeatureTest", "First");
        doc->addObject("App::FeatureTest", "Second");
        doc->setRollbackMarker(first);
        settle();

        doc->addObject("App::FeatureTest", "Third");
        const QStringList expected {
            QStringLiteral("First"),
            QStringLiteral("Third"),
            QStringLiteral("End of Model"),
            QStringLiteral("Second")
        };
        QCOMPARE(outline(true), expected);
    }
};

QTEST_MAIN(TestTree)

#include "Tree.moc"
