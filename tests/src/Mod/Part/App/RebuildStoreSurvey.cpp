// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/GeometryCache.h>
#include <App/UnreferencedFiles.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <src/App/InitApplication.h>

namespace fs = std::filesystem;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/** Which kept rebuild results nothing in a project names any more.
 *
 *  Cruth: an entry is named by a digest of the recipe text that produced it, so editing one
 *  dimension does not overwrite the old entry -- it writes a new one beside it, and the old one
 *  is dead weight from that moment on. Measured on a 75-entry part: one changed dimension and a
 *  save left 77 entries, two of them unreachable.
 *
 *  That name is DERIVED, which is what separates this from the source-material half: an asset id
 *  is stated in the recipe and can be read from the text, while an entry's name has to be
 *  computed from the recipe and from every recipe it stands on. So the survey opens the
 *  documents -- and the tests below are mostly about what it refuses to answer, because an entry
 *  whose name was not computed is an entry reported as named by nothing.
 */
class RebuildStoreSurveyTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _folder = fs::path(Base::FileInfo::getTempFileName()) / "project";
        fs::create_directories(_folder);
    }

    void TearDown() override
    {
        for (const std::string& name : _open) {
            if (App::GetApplication().getDocument(name.c_str()) != nullptr) {
                App::GetApplication().closeDocument(name.c_str());
            }
        }
        _open.clear();
        std::error_code ignored;
        fs::remove_all(_folder.parent_path(), ignored);
    }

    /// A part in the project folder holding one built solid, saved and then closed -- which is
    /// the state the survey insists on finding.
    std::string aSavedPart(const std::string& name, double length = 10.0)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName(name.c_str()).c_str());
        const std::string opened = doc->getName();

        auto* box = doc->addObject<Part::Box>("Block");
        box->Length.setValue(length);
        box->Width.setValue(20.0);
        box->Height.setValue(30.0);
        doc->recompute();

        const std::string path = (_folder / (name + ".cpart")).string();
        EXPECT_TRUE(doc->saveAs(path.c_str())) << "the part did not save, so nothing was kept";
        app.closeDocument(opened.c_str());
        return path;
    }

    /// Where one saved part's results are kept, found the way the survey finds them.
    fs::path resultsKeptFor(const std::string& recipePath)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.openDocument(recipePath.c_str());
        const std::string uuid = doc->Uid.getValueStr();
        app.closeDocument(doc->getName());
        return fs::path(App::builtGeometryFolder((_folder / ".cruth" / uuid).string()));
    }

    static std::set<std::string> entriesIn(const fs::path& folder)
    {
        std::set<std::string> keys;
        std::error_code failed;
        for (const auto& entry : fs::directory_iterator(folder, failed)) {
            keys.insert(entry.path().filename().string());
        }
        return keys;
    }

    static std::size_t howManyDocumentsAreOpen()
    {
        return App::GetApplication().getDocuments().size();
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path _folder;
    std::vector<std::string> _open;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The narrowing, and the dangerous direction: a part opened again names every entry its save
// kept. If the name computed on opening did not match the name written on saving, everything the
// project holds would be reported as collectable -- and the cache would never have worked either.
TEST_F(RebuildStoreSurveyTest, aPartNamesEveryResultItsSaveKept)
{
    const std::string recipe = aSavedPart("one");
    // Or the answer below is about an empty store, and would read the same however wrong the
    // survey was.
    ASSERT_FALSE(entriesIn(resultsKeptFor(recipe)).empty()) << "the save kept no result at all";

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_EQ(found.recipesRead.size(), 1U);
    EXPECT_TRUE(found.unreferenced.empty())
        << "a result the part still names was reported as collectable: "
        << found.unreferenced.front().path;
    EXPECT_EQ(found.bytes, 0U);
}

// The measured case: one changed dimension writes a new entry beside the old one, and nothing
// names the old one from that moment on.
TEST_F(RebuildStoreSurveyTest, aChangedDimensionLeavesTheOldResultNamedByNothing)
{
    const std::string recipe = aSavedPart("one");
    const fs::path kept = resultsKeptFor(recipe);
    const std::set<std::string> first = entriesIn(kept);
    ASSERT_EQ(first.size(), 1U);

    auto& app = App::GetApplication();
    App::Document* doc = app.openDocument(recipe.c_str());
    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Length.setValue(11.0);
    doc->recompute();
    ASSERT_TRUE(doc->save());
    app.closeDocument(doc->getName());

    const std::set<std::string> both = entriesIn(kept);
    ASSERT_EQ(both.size(), 2U) << "the store did not keep the new result beside the old";

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    ASSERT_EQ(found.unreferenced.size(), 1U) << "the result nothing names was not found";
    EXPECT_EQ(fs::path(found.unreferenced.front().path).filename().string(), *first.begin());
    EXPECT_GT(found.unreferenced.front().bytes, 0U) << "what it holds was reported as nothing";
    EXPECT_EQ(found.bytes, found.unreferenced.front().bytes);
}

// Each part is asked about its own results: a folder of parts is surveyed once, and a part that
// has not been touched keeps everything it names.
TEST_F(RebuildStoreSurveyTest, aPartThatWasNotTouchedKeepsWhatItNames)
{
    const std::string first = aSavedPart("one");
    aSavedPart("two", 40.0);
    const fs::path keptForFirst = resultsKeptFor(first);

    auto& app = App::GetApplication();
    App::Document* doc = app.openDocument(first.c_str());
    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Height.setValue(31.0);
    doc->recompute();
    ASSERT_TRUE(doc->save());
    app.closeDocument(doc->getName());

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_EQ(found.recipesRead.size(), 2U) << "the survey did not read both parts";
    ASSERT_EQ(found.unreferenced.size(), 1U) << "the other part's result was reported too";
    EXPECT_EQ(fs::path(found.unreferenced.front().path).parent_path(), keptForFirst)
        << "what was reported is not a result of the part that changed";
}

// A part that was deleted or moved away leaves everything kept for it behind, and nothing in the
// folder is that document any more -- so all of it is reported, the display state included.
TEST_F(RebuildStoreSurveyTest, whatIsKeptForAPartThatIsGoneIsAllUnreferenced)
{
    const std::string recipe = aSavedPart("one");
    const fs::path kept = resultsKeptFor(recipe).parent_path();
    ASSERT_TRUE(fs::is_directory(kept));

    // Display state is kept beside the results, and it goes with them when the part is gone.
    std::ofstream(kept / "GuiDocument.xml", std::ios::binary) << "<View/>";
    fs::remove(recipe);

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_TRUE(found.recipesRead.empty());
    ASSERT_EQ(found.unreferenced.size(), 1U) << "what is kept for a part that is gone was kept";
    EXPECT_EQ(found.unreferenced.front().path, kept.string())
        << "the folder was reported piece by piece rather than as the one dead thing it is";
    EXPECT_GT(found.unreferenced.front().bytes, 0U);
}

// The colours and the camera beside the results are rebuildable too, but a person chose them and
// the document names them directly. A survey of what nothing names leaves them alone.
TEST_F(RebuildStoreSurveyTest, theDisplayStateBesideTheResultsIsLeftAlone)
{
    const std::string recipe = aSavedPart("one");
    const fs::path kept = resultsKeptFor(recipe).parent_path();
    std::ofstream(kept / "GuiDocument.xml", std::ios::binary) << "<View/>";

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    for (const App::UnreferencedFile& file : found.unreferenced) {
        EXPECT_NE(fs::path(file.path).filename().string(), "GuiDocument.xml")
            << "the display state of a part that is still here was reported as collectable";
        EXPECT_NE(file.path, kept.string())
            << "everything kept for a part that is still here was reported as collectable";
    }
}

// What is kept was named when the part was last saved. A part that has moved on from its file
// names what no save has written yet, so the survey refuses rather than answering about that.
TEST_F(RebuildStoreSurveyTest, aPartWithUnsavedChangesStopsTheSurvey)
{
    const std::string recipe = aSavedPart("one");
    App::Document* doc = App::GetApplication().openDocument(recipe.c_str());
    _open.push_back(doc->getName());

    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Length.setValue(11.0);
    doc->recompute();
    ASSERT_FALSE(doc->statesWhatItsFileStates()) << "an edited part called itself unchanged";

    try {
        App::surveyProjectRebuildStore(_folder.string());
        FAIL() << "the survey answered about a part that had moved on from its file";
    }
    catch (const Base::Exception& refused) {
        const std::string said = refused.what();
        EXPECT_NE(said.find("one.cpart"), std::string::npos) << said;
        EXPECT_NE(said.find("unsaved changes"), std::string::npos) << said;
    }

    EXPECT_NE(App::GetApplication().getDocument(_open.front().c_str()), nullptr)
        << "the survey closed a part the person had open";
}

// And a part simply being open is not a reason to refuse. What it holds is what its file states,
// so the names it gives are the file's own names -- which is how a person gets an answer without
// first closing the thing they are working on.
TEST_F(RebuildStoreSurveyTest, aPartOpenAndUnchangedIsAnsweredAbout)
{
    const std::string recipe = aSavedPart("one");
    App::Document* doc = App::GetApplication().openDocument(recipe.c_str());
    _open.push_back(doc->getName());
    ASSERT_TRUE(doc->statesWhatItsFileStates())
        << "a part that was only read called itself changed";

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_EQ(found.recipesRead.size(), 1U);
    EXPECT_TRUE(found.unreferenced.empty())
        << "a result the open part still names was reported as collectable";
    EXPECT_NE(App::GetApplication().getDocument(_open.front().c_str()), nullptr)
        << "the survey closed a part the person had open";
}

// Saving is what makes the two agree again, and the survey follows that rather than holding a
// grudge about an edit that has since been written out.
TEST_F(RebuildStoreSurveyTest, savingWhatWasChangedLetsTheSurveyAnswerAgain)
{
    const std::string recipe = aSavedPart("one");
    App::Document* doc = App::GetApplication().openDocument(recipe.c_str());
    _open.push_back(doc->getName());

    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Length.setValue(11.0);
    doc->recompute();
    EXPECT_THROW(App::surveyProjectRebuildStore(_folder.string()), Base::Exception);

    ASSERT_TRUE(doc->save());
    ASSERT_TRUE(doc->statesWhatItsFileStates()) << "a saved part still called itself changed";

    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    ASSERT_EQ(found.unreferenced.size(), 1U)
        << "the result the edit left behind was not found once the edit was saved";
}

// A rebuild produces solids the file never held, so producing one is not a disagreement with it.
// A part that called itself changed every time it recomputed could never say when it had changed.
TEST_F(RebuildStoreSurveyTest, rebuildingDoesNotMakeAPartDisagreeWithItsFile)
{
    const std::string recipe = aSavedPart("one");
    App::Document* doc = App::GetApplication().openDocument(recipe.c_str());
    _open.push_back(doc->getName());

    for (App::DocumentObject* obj : doc->getObjects()) {
        obj->enforceRecompute();
    }
    doc->recompute();
    EXPECT_TRUE(doc->statesWhatItsFileStates())
        << "rebuilding a part made it call itself changed, which nothing could then distinguish";
}

// A statement this build could not honour may name anything, so an absence of references is no
// longer evidence of anything (Clause 19.3). The survey stops and says which part froze it.
TEST_F(RebuildStoreSurveyTest, aPartHoldingWhatItCouldNotReadStopsTheSurvey)
{
    const std::string recipe = aSavedPart("one");

    // A property of a type this build does not have, stated on an object it otherwise reads
    // perfectly well -- which is what a file written by a session with an addon looks like here.
    std::string text;
    {
        std::ifstream in(recipe, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string::size_type at = text.find("<Properties>");
    ASSERT_NE(at, std::string::npos) << "the part states no properties to add one to";
    text.insert(
        at + std::string("<Properties>").size(),
        "\n<Property name=\"Sparkle\" type=\"Addon::PropertyGlitter\" dynamic=\"1\">\n"
        "<Glitter value=\"lots\"/>\n</Property>"
    );
    std::ofstream(recipe, std::ios::binary | std::ios::trunc) << text;

    try {
        App::surveyProjectRebuildStore(_folder.string());
        FAIL() << "the survey answered from a part holding a statement it could not read";
    }
    catch (const Base::Exception& refused) {
        const std::string said = refused.what();
        EXPECT_NE(said.find("one.cpart"), std::string::npos) << said;
        EXPECT_NE(said.find("Sparkle"), std::string::npos)
            << "the refusal does not say what is being held: " << said;
    }
}

// Whatever it had to open for an answer, and whatever it had to open before refusing one, the
// survey puts the session back as it found it.
TEST_F(RebuildStoreSurveyTest, theSurveyLeavesTheSessionAsItFoundIt)
{
    aSavedPart("one");
    aSavedPart("two", 40.0);
    const std::size_t before = howManyDocumentsAreOpen();

    App::surveyProjectRebuildStore(_folder.string());
    EXPECT_EQ(howManyDocumentsAreOpen(), before) << "the survey left a part it opened open";

    std::ofstream(_folder / "broken.cpart", std::ios::binary) << "<Document><Objects";
    EXPECT_THROW(App::surveyProjectRebuildStore(_folder.string()), Base::Exception);
    EXPECT_EQ(howManyDocumentsAreOpen(), before)
        << "the survey left a part open on its way out of a refusal";
}

// A project that has kept nothing is not an error: nothing has been saved in it yet.
TEST_F(RebuildStoreSurveyTest, aProjectThatHasKeptNothingIsNotAnError)
{
    const App::ProjectSurvey found = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_TRUE(found.recipesRead.empty());
    EXPECT_TRUE(found.unreferenced.empty());
    EXPECT_EQ(found.bytes, 0U);
}

/** Discarding kept rebuild results: the safe half, and safe for one reason.
 *
 *  An entry here costs a rebuild and nothing in it was designed, so this one takes no list --
 *  there is nothing to choose between when every answer costs the same. What it does inherit is
 *  every refusal the survey makes, which is what stops it emptying the store of a part somebody
 *  is in the middle of editing.
 */
class DiscardRebuildResultsTest: public RebuildStoreSurveyTest
{
};

// What nothing names goes; what the part still names stays; and the part still opens.
TEST_F(DiscardRebuildResultsTest, whatNothingNamesGoesAndWhatThePartNamesStays)
{
    const std::string recipe = aSavedPart("one");
    const fs::path kept = resultsKeptFor(recipe);

    auto& app = App::GetApplication();
    App::Document* doc = app.openDocument(recipe.c_str());
    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Length.setValue(11.0);
    doc->recompute();
    ASSERT_TRUE(doc->save());
    app.closeDocument(doc->getName());
    ASSERT_EQ(entriesIn(kept).size(), 2U);

    const App::Discarded done = App::discardUnreferencedRebuildResults(_folder.string());
    ASSERT_EQ(done.removed.size(), 1U) << "the result nothing names was not removed";
    EXPECT_GT(done.bytes, 0U);
    EXPECT_TRUE(done.kept.empty());

    const std::set<std::string> left = entriesIn(kept);
    ASSERT_EQ(left.size(), 1U) << "the result the part still names went too";

    // And the part is still a part: it opens, and what is left is what it names.
    const App::ProjectSurvey after = App::surveyProjectRebuildStore(_folder.string());
    EXPECT_TRUE(after.unreferenced.empty());
    App::Document* reopened = app.openDocument(recipe.c_str());
    ASSERT_NE(reopened, nullptr);
    EXPECT_NE(reopened->getObject("Block"), nullptr) << "the part lost what it was made of";
    app.closeDocument(reopened->getName());
}

// It refuses wherever the survey refuses, and a part being edited is the one that matters: nobody
// should be able to empty the store of a part with changes not yet written.
TEST_F(DiscardRebuildResultsTest, aPartWithUnsavedChangesStopsTheDiscardAndNothingIsRemoved)
{
    const std::string recipe = aSavedPart("one");
    const fs::path kept = resultsKeptFor(recipe);
    const std::set<std::string> before = entriesIn(kept);
    ASSERT_FALSE(before.empty());

    App::Document* doc = App::GetApplication().openDocument(recipe.c_str());
    _open.push_back(doc->getName());
    auto* box = dynamic_cast<Part::Box*>(doc->getObject("Block"));
    ASSERT_NE(box, nullptr);
    box->Length.setValue(11.0);
    doc->recompute();

    EXPECT_THROW(App::discardUnreferencedRebuildResults(_folder.string()), Base::Exception);
    EXPECT_EQ(entriesIn(kept), before) << "a result was removed on the way out of a refusal";
}

// Nothing to do is not an error, and it is not reported as work either.
TEST_F(DiscardRebuildResultsTest, aStoreWhereEverythingIsNamedIsLeftUntouched)
{
    const std::string recipe = aSavedPart("one");
    const std::set<std::string> before = entriesIn(resultsKeptFor(recipe));
    ASSERT_FALSE(before.empty());

    const App::Discarded done = App::discardUnreferencedRebuildResults(_folder.string());
    EXPECT_TRUE(done.removed.empty());
    EXPECT_EQ(done.bytes, 0U);
    EXPECT_EQ(entriesIn(resultsKeptFor(recipe)), before);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
