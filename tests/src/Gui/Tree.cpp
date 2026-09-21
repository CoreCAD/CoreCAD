// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>
#include <QTreeWidgetItem>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/DocumentObjectGroup.h>

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

    /// The tree as a person would read it down the page: one name per line, indented by depth.
    ///
    /// An item a person asked not to see is still an item -- it is marked hidden rather than
    /// taken out -- so reading the list the way a person reads it means passing those by.
    QStringList outline()
    {
        settle();
        QStringList lines;
        std::function<void(QTreeWidgetItem*, int)> walk = [&](QTreeWidgetItem* item, int depth) {
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* child = item->child(i);
                if (child->isHidden()) {
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
};

QTEST_MAIN(TestTree)

#include "Tree.moc"
