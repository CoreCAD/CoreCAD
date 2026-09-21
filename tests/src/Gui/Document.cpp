// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/ViewProviderDocumentObject.h>

#include <src/TempDirectory.h>

#include "InitGuiApplication.h"

/** The half of a document a person sees.
 *
 * Every object in a document has a counterpart here holding what it looks like and whether it is
 * shown at all. That state is written into the saved document beside the object's own, and it is
 * read back by a separate pass -- so it can be lost on its own, without the object being lost,
 * and nothing about the object would look wrong afterwards.
 */
class TestGuiDocument: public QObject  // NOLINT
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
        doc = tests::newViewlessDocument("guidoc");
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

    // An object arriving in the document brings its counterpart with it, and takes it away again.
    void test_everyObjectHasACounterpart()  // NOLINT
    {
        QVERIFY(guiDoc != nullptr);
        auto* obj = doc->addObject("App::DocumentObjectGroup", "Group");
        QVERIFY(viewOf(guiDoc, obj) != nullptr);
        QCOMPARE(guiDoc->getViewProviderByName("Group"), viewOf(guiDoc, obj));

        doc->removeObject("Group");
        QVERIFY(guiDoc->getViewProviderByName("Group") == nullptr);
    }

    // A decision that exists only on this side of the document.
    //
    // Whether an object is shown in the 3D view is also recorded by the object itself, so it
    // survives a reopen whether or not this half of the document was ever written -- which makes
    // it useless as proof that this half was written. Whether an object is listed in the tree is
    // recorded here and nowhere else, so it is lost the moment this half is lost.
    void test_aDecisionHeldOnlyOnThisSideSurvivesAReopen()  // NOLINT
    {
        tests::TempDirectory tmp("guidoc");
        const std::string file = (tmp.path() / "hidden.FCStd").string();

        auto* listed = doc->addObject("App::DocumentObjectGroup", "Listed");
        auto* unlisted = doc->addObject("App::DocumentObjectGroup", "Unlisted");
        viewOf(guiDoc, listed)->ShowInTree.setValue(true);
        viewOf(guiDoc, unlisted)->ShowInTree.setValue(false);
        viewOf(guiDoc, unlisted)->Visibility.setValue(false);

        doc->saveAs(file.c_str());
        App::GetApplication().closeDocument(doc->getName());
        doc = nullptr;

        doc = tests::openViewlessDocument(file.c_str());
        QVERIFY(doc != nullptr);
        Gui::Document* reopened = Gui::Application::Instance->getDocument(doc);
        QVERIFY(reopened != nullptr);

        QVERIFY(viewOf(reopened, doc->getObject("Listed"))->ShowInTree.getValue());
        QVERIFY(!viewOf(reopened, doc->getObject("Unlisted"))->ShowInTree.getValue());
        QVERIFY(!viewOf(reopened, doc->getObject("Unlisted"))->Visibility.getValue());
    }

    // The modified flag is what stands between a person and losing work on close.
    void test_aChangedDocumentSaysItIsChanged()  // NOLINT
    {
        tests::TempDirectory tmp("guidoc");
        const std::string file = (tmp.path() / "modified.FCStd").string();

        auto* obj = doc->addObject("App::DocumentObjectGroup", "Group");
        doc->saveAs(file.c_str());
        QVERIFY(!guiDoc->isModified());

        viewOf(guiDoc, obj)->Visibility.setValue(false);
        QVERIFY(guiDoc->isModified());
    }

    // The same handler read from the other end: a copy is written to a different file and the
    // document a person is working in is not the file that was written, so it is still changed.
    void test_savingACopyLeavesTheDocumentChanged()  // NOLINT
    {
        tests::TempDirectory tmp("guidoc");
        const std::string file = (tmp.path() / "original.FCStd").string();
        const std::string copy = (tmp.path() / "copy.FCStd").string();

        auto* obj = doc->addObject("App::DocumentObjectGroup", "Group");
        doc->saveAs(file.c_str());
        viewOf(guiDoc, obj)->Visibility.setValue(false);
        QVERIFY(guiDoc->isModified());

        doc->saveCopy(copy.c_str());
        QVERIFY(guiDoc->isModified());
    }
};

QTEST_MAIN(TestGuiDocument)

#include "Document.moc"
