// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Link.h>
#include <App/PropertyStandard.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

namespace
{
std::string readAll(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    out << text;
}

/// Point a reference the file states at a durable id nothing in it holds, which is what a branch
/// that deleted the target leaves behind: the reference is kept as stated and resolves to nothing.
void pointTheReferenceAtNothing(const std::string& path)
{
    std::string text = readAll(path);
    const std::string::size_type at = text.find("<Target uuid=\"");
    ASSERT_NE(at, std::string::npos) << "the file states no reference to spoil";
    const std::string::size_type from = at + std::string("<Target uuid=\"").size();
    const std::string::size_type to = text.find('"', from);
    ASSERT_NE(to, std::string::npos);
    text.replace(from, to - from, "00000000-0000-4000-8000-000000000000");
    writeAll(path, text);
}

/// A property of a type this build does not have, written into one object's block.
void stateSomethingUnfamiliar(const std::string& path, const std::string& onObject)
{
    std::string text = readAll(path);
    const std::string::size_type object = text.find("name=\"" + onObject + "\"");
    ASSERT_NE(object, std::string::npos) << "the file does not state " << onObject;
    const std::string::size_type at = text.find("<Properties>", object)
        + std::string("<Properties>").size();
    text.insert(
        at,
        "\n<Property name=\"Sparkle\" type=\"Addon::PropertyGlitter\" dynamic=\"1\">\n"
        "<Glitter value=\"lots\"/>\n</Property>"
    );
    writeAll(path, text);
}
}  // namespace

/** A statement this build cannot honour is discarded by a person, and by nothing else.
 *
 *  Such a statement binds the document: while one is held, no cleanup or duplication may act on
 *  the assumption that it can see every reference. Clause 19.4 gives exactly one way out, and its
 *  terms are the point of it -- the act names what it drops, it is recorded in the document's
 *  history like any other edit, and no save, sweep, recompute or repair routine may perform it on
 *  a person's behalf. The system cannot tell a value it failed to honour from a value nobody
 *  authored, so a system that may delete on that judgement has been handed back the authority the
 *  whole amendment removes.
 */
class DiscardTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
    }

    /// A saved document of two objects, one of which states something this build cannot place.
    App::Document* holdingAStatement(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("held").c_str(), "testUser");
        doc->addObject("App::VarSet", "Alpha");
        doc->addObject("App::VarSet", "Beta");
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        app.closeDocument(doc->getName());

        stateSomethingUnfamiliar(path, "Alpha");
        _doc = app.openDocument(path.c_str());
        // Undo is off by default outside a session with a person in it, and an act that is
        // recorded in the document's history needs the history to be running.
        _doc->setUndoMode(1);
        return _doc;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// What is held has to be readable before anybody can decide about it -- by a person, or by a
// script asking the same question (P8).
TEST_F(DiscardTest, whatIsHeldIsNamedRatherThanCounted)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);

    const std::vector<std::array<std::string, 3>> held = doc->heldStatements();
    ASSERT_EQ(held.size(), 1U);
    EXPECT_EQ(held.front()[0], "Alpha") << "the statement was not attributed to its holder";
    EXPECT_EQ(held.front()[1], "Sparkle") << "the statement was not named";
    EXPECT_FALSE(held.front()[2].empty()) << "nothing said why it could not be honoured";
}

// The act names what it drops. A history entry that says only "discard" cannot be read back a
// week later, and being deliberate is the whole of what makes the act legitimate.
TEST_F(DiscardTest, theActNamesWhatItDroppedAndIsInTheDocumentsHistory)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);

    EXPECT_TRUE(doc->discardStatement(doc->getObject("Alpha"), "Sparkle"));
    EXPECT_TRUE(doc->heldStatements().empty()) << "the statement was still held after the discard";

    const std::vector<std::string> history = doc->getAvailableUndoNames();
    ASSERT_FALSE(history.empty()) << "the discard never reached the document's history";
    EXPECT_NE(history.front().find("Sparkle"), std::string::npos)
        << "the entry does not name what was dropped: " << history.front();
}

// Recorded in the history like any other edit means it can be taken back like any other edit.
TEST_F(DiscardTest, theDiscardCanBeUndoneAndDoneAgain)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);
    ASSERT_TRUE(doc->discardStatement(doc->getObject("Alpha"), "Sparkle"));

    doc->undo();
    ASSERT_EQ(doc->heldStatements().size(), 1U) << "undoing the discard did not bring it back";
    EXPECT_EQ(doc->heldStatements().front()[1], "Sparkle");

    doc->redo();
    EXPECT_TRUE(doc->heldStatements().empty()) << "the discard could not be performed again";
}

// The same act for a whole object block, which is the case that binds a document hardest: an
// object of a type this build cannot construct at all.
TEST_F(DiscardTest, aKeptObjectBlockIsDiscardedAndRestoredTheSameWay)
{
    auto& app = App::GetApplication();
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = app.newDocument(app.getUniqueDocumentName("kept").c_str(), "testUser");
    doc->addObject("App::VarSet", "Alpha");
    doc->addObject("App::VarSet", "Beta");
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    app.closeDocument(doc->getName());

    std::string text = readAll(path);
    const std::string::size_type at = text.find("\"App::VarSet\" name=\"Beta\"");
    ASSERT_NE(at, std::string::npos);
    text.replace(at, std::string("\"App::VarSet\"").size(), "\"Addon::Widget\"");
    writeAll(path, text);

    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    _doc->setUndoMode(1);
    ASSERT_EQ(_doc->unreadObjects().size(), 1U);
    const std::string uuid = _doc->unreadObjects().front()[0];

    EXPECT_TRUE(_doc->discardUnreadObject(uuid.c_str()));
    EXPECT_TRUE(_doc->unreadObjects().empty());
    EXPECT_TRUE(_doc->holdsUnreadContent() == false)
        << "a document holding nothing still said it was not whole";

    _doc->undo();
    ASSERT_EQ(_doc->unreadObjects().size(), 1U) << "undoing dropped the block for good";
    EXPECT_EQ(_doc->unreadObjects().front()[1], "Addon::Widget")
        << "what came back is not what was dropped";

    _doc->redo();
    EXPECT_TRUE(_doc->unreadObjects().empty()) << "the discard could not be performed again";
}

// A discard is an edit a person made, not a loss a read suffered, so the save that follows it is
// an ordinary save. The write guard of Clause 19.3 measures what the READ could not bring back
// and has nothing to say about content a person chose to drop.
TEST_F(DiscardTest, aSaveAfterTheDiscardIsOrdinaryAndWritesTheDocumentWithoutIt)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);
    ASSERT_NE(readAll(path).find("Sparkle"), std::string::npos);

    ASSERT_TRUE(doc->discardStatement(doc->getObject("Alpha"), "Sparkle"));
    EXPECT_TRUE(doc->whatASaveWouldLose().empty())
        << "a deliberate discard was charged as a loss the read suffered";
    ASSERT_TRUE(doc->save()) << "the save after a discard was refused";

    const std::string written = readAll(path);
    EXPECT_EQ(written.find("Sparkle"), std::string::npos) << "the discarded statement was written";
    EXPECT_NE(written.find("name=\"Alpha\""), std::string::npos) << "the object went with it";
    EXPECT_NE(written.find("name=\"Beta\""), std::string::npos) << "another object went with it";
}

// Only a person may. These are the callers that are not one, and each is refused rather than
// obeyed -- however confident it might be that nothing needs the statement.
TEST_F(DiscardTest, aReadARebuildAnImportAndAnUndoAreAllRefused)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);

    for (const auto& [status, what] : std::vector<std::pair<App::Document::Status, const char*>> {
             {App::Document::Restoring, "a read"},
             {App::Document::Recomputing, "a rebuild"},
             {App::Document::Importing, "an import"}
         }) {
        doc->setStatus(status, true);
        EXPECT_THROW(doc->discardStatement(doc->getObject("Alpha"), "Sparkle"), Base::Exception)
            << what << " was allowed to discard on a person's behalf";
        doc->setStatus(status, false);
    }
    EXPECT_EQ(doc->heldStatements().size(), 1U) << "a refused discard dropped it anyway";
    EXPECT_TRUE(doc->discardStatement(doc->getObject("Alpha"), "Sparkle"))
        << "a person was refused along with the machinery";
}

// The live path, which is the one that matters: a feature's own rebuild asking to drop a
// statement it does not understand. The status flags above are set by the machinery; this is the
// machinery actually running.
TEST_F(DiscardTest, aFeatureRebuildingItselfCannotDiscard)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);

    const std::string script = "import FreeCAD\n"
                               "doc = FreeCAD.getDocument('"
        + std::string(doc->getName())
        + "')\n"
          "class Greedy:\n"
          "    def execute(self, obj):\n"
          "        try:\n"
          "            obj.Document.discardStatement(obj.Document.getObject('Alpha'), 'Sparkle')\n"
          "            obj.Outcome = 'discarded'\n"
          "        except Exception as raised:\n"
          "            obj.Outcome = 'refused'\n"
          "o = doc.addObject('App::FeaturePython', 'Rebuilder')\n"
          "o.addProperty('App::PropertyString', 'Outcome')\n"
          "o.Proxy = Greedy()\n"
          "doc.recompute()\n";
    Base::Interpreter().runString(script.c_str());

    App::DocumentObject* rebuilder = doc->getObject("Rebuilder");
    ASSERT_NE(rebuilder, nullptr);
    const auto* outcome = dynamic_cast<const App::PropertyString*>(
        rebuilder->getPropertyByName("Outcome")
    );
    ASSERT_NE(outcome, nullptr);
    EXPECT_EQ(std::string(outcome->getValue()), "refused")
        << "a rebuild discarded a statement on a person's behalf";
    EXPECT_EQ(doc->heldStatements().size(), 1U) << "the statement is gone";
}

// Nothing held under that name is not an error, and it is not an edit either.
TEST_F(DiscardTest, discardingWhatIsNotHeldChangesNothing)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = holdingAStatement(path);
    ASSERT_NE(doc, nullptr);

    EXPECT_FALSE(doc->discardStatement(doc->getObject("Beta"), "Sparkle"));
    EXPECT_FALSE(doc->discardUnreadObject("not-a-durable-id"));
    EXPECT_TRUE(doc->getAvailableUndoNames().empty())
        << "a discard that dropped nothing still wrote an entry into the document's history";
}


/** While a document holds a statement it could not honour, no sweep may act on the assumption
 *  that it can see every reference (Amendment 19 Clause 19.3).
 *
 *  What could not be honoured may name anything, including the very object a sweep is about to
 *  conclude is unreferenced. So an absence of RESOLVABLE references stops being evidence that
 *  nothing references an object, and a duplication that would carry such a statement is refused:
 *  duplication mints fresh identities and rewires the references between the copies, and a
 *  reference that never resolved cannot be rewired -- the copy would go on naming the original.
 */
class ReferenceFreezeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        for (App::Document* doc : _docs) {
            if (doc != nullptr) {
                App::GetApplication().closeDocument(doc->getName());
            }
        }
        _docs.clear();
    }

    /// What the file is made to state, beyond the pair of objects themselves.
    enum class Spoil
    {
        Nothing,
        /// A property of a type this build has no place for, kept as written.
        AnUnfamiliarProperty,
        /// A reference to an object the document does not hold, kept as stated.
        AReferenceToNothing
    };

    /// A saved document whose `Linker` references `Target`, read back under the given spoiling.
    App::Document* linkedPair(const std::string& path, Spoil spoil)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("pair").c_str(), "testUser");
        App::DocumentObject* target = doc->addObject("App::VarSet", "Target");
        auto* linker = static_cast<App::Link*>(doc->addObject("App::Link", "Linker"));
        linker->LinkedObject.setValue(target);
        doc->recompute();
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        app.closeDocument(doc->getName());

        if (spoil == Spoil::AnUnfamiliarProperty) {
            stateSomethingUnfamiliar(path, "Target");
        }
        else if (spoil == Spoil::AReferenceToNothing) {
            pointTheReferenceAtNothing(path);
        }
        App::Document* opened = app.openDocument(path.c_str());
        opened->setUndoMode(1);
        _docs.push_back(opened);
        return opened;
    }

    App::Document* freshDocument()
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("into").c_str(), "testUser");
        _docs.push_back(doc);
        return doc;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<App::Document*> _docs;
};

// The sweep that §3.5 describes: a relocation takes an object away, and what it depended on is
// cleaned up when nothing is left referencing it. An empty list of referrers is what this session
// could READ, which is not the same as what exists.
TEST_F(ReferenceFreezeTest, aCleanupLeavesWhatItCannotProveIsUnreferenced)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* source = linkedPair(path, Spoil::AnUnfamiliarProperty);
    ASSERT_NE(source, nullptr);
    ASSERT_FALSE(source->seesEveryReference());

    freshDocument()->moveObject(source->getObject("Linker"), /*recursive=*/true);

    EXPECT_NE(source->getObject("Target"), nullptr)
        << "a sweep removed an object it could not prove was unreferenced";
}

// The control, and the reason the freeze is keyed to holding rather than to anything wider: an
// ordinary document sweeps exactly as it did before.
TEST_F(ReferenceFreezeTest, anOrdinaryCleanupStillRemovesWhatNothingReferences)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* source = linkedPair(path, Spoil::Nothing);
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(source->seesEveryReference());

    freshDocument()->moveObject(source->getObject("Linker"), /*recursive=*/true);

    EXPECT_EQ(source->getObject("Target"), nullptr) << "the ordinary cleanup stopped happening";
}

// A duplication that would carry a reference this session could not resolve is refused with its
// reason. Duplication answers, for every reference among the copied objects, whether it should
// now point at the copy or still at the original -- and for a name that never resolved there is
// no way to answer, so the copy would quietly go on naming the original.
TEST_F(ReferenceFreezeTest, aDuplicationCarryingAReferenceThatNeverResolvedIsRefused)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = linkedPair(path, Spoil::AReferenceToNothing);
    ASSERT_NE(doc, nullptr);
    App::DocumentObject* linker = doc->getObject("Linker");
    ASSERT_NE(linker, nullptr);
    ASSERT_FALSE(linker->unresolvedReferenceNames().empty())
        << "the arrangement is wrong: the reference resolved after all";

    std::string refusal;
    try {
        doc->copyObject({linker});
    }
    catch (const Base::Exception& e) {
        refusal = e.what();
    }
    EXPECT_FALSE(refusal.empty()) << "a copy that cannot be rewired was made anyway";
    EXPECT_NE(refusal.find(linker->unresolvedReferenceNames().front()), std::string::npos)
        << "the refusal did not say what it was refusing over: " << refusal;
}

// The narrowing, and it is not a detail: a value this build simply had no place for is carried
// across verbatim and states on the copy exactly what it states on the original, which is
// Clause 19.1's duty and has to keep working. Only a statement that NAMES something raises the
// rewiring question, so only that one is refused.
TEST_F(ReferenceFreezeTest, aKeptValueThatNamesNothingIsStillCopied)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = linkedPair(path, Spoil::AnUnfamiliarProperty);
    ASSERT_NE(doc, nullptr);
    App::DocumentObject* target = doc->getObject("Target");
    ASSERT_NE(target, nullptr);
    ASSERT_FALSE(target->unhonouredStatements().empty());

    EXPECT_NO_THROW(doc->copyObject({target}))
        << "a kept value that names nothing was refused as though it named something";
}

// An object that holds nothing at all, in a document that does, is untouched by any of this.
TEST_F(ReferenceFreezeTest, anObjectHoldingNothingIsStillDuplicated)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = linkedPair(path, Spoil::AnUnfamiliarProperty);
    ASSERT_NE(doc, nullptr);
    ASSERT_FALSE(doc->seesEveryReference());

    EXPECT_NO_THROW(doc->copyObject({doc->getObject("Linker")}))
        << "an object holding nothing was refused for the document's sake";
}

// And the way out, which is the whole reason Clause 19.4 exists: once a person has discarded the
// statement, the document is no longer bound by it.
TEST_F(ReferenceFreezeTest, aPersonsDiscardReleasesTheDuplication)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = linkedPair(path, Spoil::AReferenceToNothing);
    ASSERT_NE(doc, nullptr);
    App::DocumentObject* linker = doc->getObject("Linker");
    ASSERT_NE(linker, nullptr);
    const std::string held = linker->unresolvedReferenceNames().front();
    ASSERT_TRUE(doc->discardStatement(linker, held.c_str()));

    EXPECT_NO_THROW(doc->copyObject({linker}))
        << "the duplication was still refused after the statement was discarded";
    EXPECT_TRUE(doc->seesEveryReference());
}
