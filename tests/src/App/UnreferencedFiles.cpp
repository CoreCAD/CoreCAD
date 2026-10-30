// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectFileIncluded.h>
#include <App/UnreferencedFiles.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <src/App/InitApplication.h>

namespace fs = std::filesystem;

namespace
{
void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}
}  // namespace

/** What a project folder holds that nothing in it names any more.
 *
 *  Cruth: source material is stored under a digest of its own bytes, so replacing an import
 *  leaves the old body in `assets/` with nothing naming it -- and `assets/` is visible and
 *  versioned, so the dead weight is handed over with the project. Measured on a 75-entry part:
 *  changing one dimension and saving left 77 entries, two of them unreachable.
 *
 *  A save cannot decide this. A save sees one document, and a file that document does not name
 *  may still be named by a sibling part it never opened -- an imported body shared by five parts
 *  is stored once on purpose. So the question is asked of a project, and it is asked before
 *  anything is removed.
 */
class UnreferencedFilesTest: public ::testing::Test
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

    /// A document in the project folder holding one handed-in file, saved.
    ///
    /// `sourceName` is the file itself -- what it is called, which is part of what was handed in.
    /// Two parts including the SAME file name the same material; two parts including two files
    /// that happen to hold the same bytes do not, and the store keeps both.
    std::string aPartIncluding(
        const std::string& name,
        const std::string& contents,
        const std::string& sourceName = {}
    )
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName(name.c_str()).c_str());
        _open.push_back(doc->getName());

        const fs::path handedIn = _folder.parent_path()
            / (sourceName.empty() ? name + "-source.txt" : sourceName);
        writeFile(handedIn, contents);

        auto* holder = doc->addObject<App::DocumentObjectFileIncluded>("Included");
        holder->File.setValue(handedIn.string().c_str());
        doc->recompute();

        const std::string path = (_folder / (name + ".cpart")).string();
        const bool saved = doc->saveAs(path.c_str());
        EXPECT_TRUE(saved) << "the part did not save, so there is no recipe to read";
        return path;
    }

    fs::path sourceFolder() const
    {
        return _folder / "assets";
    }

    std::set<std::string> whatIsStored() const
    {
        std::set<std::string> ids;
        std::error_code failed;
        for (const auto& entry : fs::directory_iterator(sourceFolder(), failed)) {
            ids.insert(entry.path().filename().string());
        }
        return ids;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path _folder;
    std::vector<std::string> _open;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// A recipe names the source material it uses, and the survey reads it from the recipe itself.
TEST_F(UnreferencedFilesTest, aRecipeNamesTheSourceMaterialItUses)
{
    const std::string recipe = aPartIncluding("one", "a handed-in body");

    const std::set<std::string> named = App::sourceMaterialNamedBy(recipe);
    ASSERT_EQ(named.size(), 1U) << "the recipe does not name the material it was saved with";
    EXPECT_EQ(whatIsStored(), named) << "what the recipe names is not what the project stores";
}

// The narrowing, and the dangerous direction: while every file is named, nothing is collectable.
TEST_F(UnreferencedFilesTest, nothingIsUnreferencedWhileEveryFileIsNamed)
{
    aPartIncluding("one", "a handed-in body");
    // Or the answer below is about an empty folder, and would read the same however wrong the
    // survey was.
    ASSERT_EQ(whatIsStored().size(), 1U) << "nothing was stored, so nothing could be collected";

    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    EXPECT_EQ(found.recipesRead.size(), 1U);
    EXPECT_TRUE(found.unreferenced.empty())
        << "material a recipe still names was reported as collectable: "
        << found.unreferenced.front().path;
    EXPECT_EQ(found.bytes, 0U);
}

// The measured case: replacing an import leaves the old body named by nothing.
TEST_F(UnreferencedFilesTest, replacingAnImportLeavesTheOldBodyNamedByNothing)
{
    const std::string recipe = aPartIncluding("one", "the first body");
    const std::set<std::string> first = whatIsStored();
    ASSERT_EQ(first.size(), 1U);

    // The same part, saved again over a different handed-in file.
    App::Document* doc = App::GetApplication().getDocument(_open.front().c_str());
    ASSERT_NE(doc, nullptr);
    auto* holder = dynamic_cast<App::DocumentObjectFileIncluded*>(doc->getObject("Included"));
    ASSERT_NE(holder, nullptr);
    const fs::path second = _folder.parent_path() / "second-source.txt";
    writeFile(second, "the second body, which is a different length");
    holder->File.setValue(second.string().c_str());
    doc->recompute();
    ASSERT_TRUE(doc->save());

    ASSERT_EQ(whatIsStored().size(), 2U) << "the store did not keep the old body beside the new";

    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    ASSERT_EQ(found.unreferenced.size(), 1U) << "the body nothing names was not found";
    EXPECT_EQ(fs::path(found.unreferenced.front().path).filename().string(), *first.begin());
    EXPECT_GT(found.unreferenced.front().bytes, 0U) << "what it holds was reported as nothing";
    EXPECT_EQ(found.bytes, found.unreferenced.front().bytes);

    const std::set<std::string> named = App::sourceMaterialNamedBy(recipe);
    EXPECT_EQ(named.count(*first.begin()), 0U);
}

// Why a save cannot decide this: a file this document does not name may be named by a sibling it
// never opened. One imported body shared by two parts is stored once, on purpose.
TEST_F(UnreferencedFilesTest, aFileNamedByASiblingRecipeIsNotUnreferenced)
{
    // The same file, included by both -- which is what sharing a body IS. Two separate files
    // holding equal bytes are two pieces of material, because what a file is called travels with
    // it, and the store keeps each.
    aPartIncluding("one", "a body two parts share", "shared-body.txt");
    aPartIncluding("two", "a body two parts share", "shared-body.txt");
    ASSERT_EQ(whatIsStored().size(), 1U) << "the same body was stored twice";

    // One of the two stops naming it. The other still does.
    App::Document* doc = App::GetApplication().getDocument(_open.front().c_str());
    ASSERT_NE(doc, nullptr);
    doc->removeObject("Included");
    ASSERT_TRUE(doc->save());

    ASSERT_EQ(whatIsStored().size(), 1U) << "the shared body is no longer stored to be found";

    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    EXPECT_EQ(found.recipesRead.size(), 2U) << "the survey did not read both recipes";
    EXPECT_TRUE(found.unreferenced.empty())
        << "a body another part still names was reported as collectable";
}

// The other side of the same rule: equal bytes under two names are two pieces of material.
//
// The store names an entry by what it holds, and what a file is CALLED is part of what was handed
// in -- so it is held too. Before this, the name was dropped on the way in and both parts came
// back pointing at a file named after nothing, which read as one shared body and was two files
// with one name lost.
TEST_F(UnreferencedFilesTest, twoFilesWithEqualBytesAndDifferentNamesAreBothKept)
{
    aPartIncluding("one", "the very same bytes", "bracket.txt");
    aPartIncluding("two", "the very same bytes", "plate.txt");

    EXPECT_EQ(whatIsStored().size(), 2U) << "one of the two files was dropped for the other";

    const std::set<std::string> byTheFirst = App::sourceMaterialNamedBy(
        (_folder / "one.cpart").string()
    );
    const std::set<std::string> bySecond = App::sourceMaterialNamedBy((_folder / "two.cpart").string());
    ASSERT_EQ(byTheFirst.size(), 1U) << "the first part does not name what it was saved with";
    ASSERT_EQ(bySecond.size(), 1U) << "the second part does not name what it was saved with";
    EXPECT_NE(*byTheFirst.begin(), *bySecond.begin())
        << "both parts were pointed at one entry, so one of the two names is gone";

    // And neither is collectable: each is named by the part it belongs to.
    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    EXPECT_TRUE(found.unreferenced.empty()) << "material a part still names was reported as spare";
}

// A recipe that cannot be read stops the survey, rather than having its references counted as
// absent -- which would collect exactly the files it names.
TEST_F(UnreferencedFilesTest, aRecipeThatCannotBeReadStopsTheSurvey)
{
    const std::string recipe = aPartIncluding("one", "a handed-in body");
    App::GetApplication().closeDocument(_open.front().c_str());
    _open.clear();

    // Truncated halfway, which is what an interrupted write or a bad merge leaves behind.
    std::string text;
    {
        std::ifstream in(recipe, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ASSERT_GT(text.size(), 200U);
    writeFile(fs::path(recipe), text.substr(0, text.size() / 2));

    EXPECT_THROW(App::sourceMaterialNamedBy(recipe), Base::Exception);
    EXPECT_THROW(App::surveyProjectSourceMaterial(_folder.string()), Base::Exception);

    // And it says where it stopped, or a person has nothing to act on.
    try {
        App::surveyProjectSourceMaterial(_folder.string());
        FAIL() << "the survey read a truncated recipe without complaint";
    }
    catch (const Base::Exception& refused) {
        const std::string said = refused.what();
        EXPECT_NE(said.find("one.cpart"), std::string::npos) << said;
        EXPECT_NE(said.find("line"), std::string::npos) << said;
    }
}

// A statement kept verbatim because this build could not honour it still names its source
// material (Clause 19.1). Read by opening the document, that material would look unnamed -- and
// the one build that cannot read a value is the one that must not delete what it points at.
TEST_F(UnreferencedFilesTest, aStatementKeptVerbatimStillNamesItsSourceMaterial)
{
    const std::string kept = "0000000000000000000000000000000000000001";
    writeFile(
        _folder / "held.cpart",
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<Document>\n"
        "  <Objects>\n"
        "    <Object name=\"Thing\" type=\"Some::TypeThisBuildHasNot\">\n"
        "      <Properties>\n"
        "        <Property name=\"Body\" type=\"Some::PropertyThisBuildHasNot\" asset=\""
            + kept
            + "\"/>\n"
              "      </Properties>\n"
              "    </Object>\n"
              "  </Objects>\n"
              "</Document>\n"
    );
    writeFile(sourceFolder() / kept / "content.xml", "<Value/>");

    const std::set<std::string> named = App::sourceMaterialNamedBy((_folder / "held.cpart").string());
    EXPECT_EQ(named.count(kept), 1U) << "a kept statement's source material was not counted";

    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    EXPECT_TRUE(found.unreferenced.empty())
        << "material named only by a statement this build cannot honour was reported collectable";
}

// A folder with no source material at all is not an error: a project need not have any.
TEST_F(UnreferencedFilesTest, aProjectWithNoSourceMaterialIsNotAnError)
{
    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    EXPECT_TRUE(found.recipesRead.empty());
    EXPECT_TRUE(found.unreferenced.empty());
}

/** Discarding source material: a person's act, and only the named files.
 *
 *  What is in `assets/` is authored content -- an imported body, a scanned mesh, something a
 *  person was handed and cannot produce again. A part that quietly loses the body it was built
 *  from looks exactly like a part that never had one, so there is no sweep here: each file is
 *  named, and each is removed only if the survey run inside the call still says nothing names it.
 */
class DiscardSourceMaterialTest: public UnreferencedFilesTest
{
protected:
    /// The part saved over a different handed-in file, which is what leaves the old body behind.
    std::string theOldBody()
    {
        aPartIncluding("one", "the first body");
        const std::set<std::string> first = whatIsStored();
        EXPECT_EQ(first.size(), 1U);

        App::Document* doc = App::GetApplication().getDocument(_open.front().c_str());
        auto* holder = dynamic_cast<App::DocumentObjectFileIncluded*>(doc->getObject("Included"));
        EXPECT_NE(holder, nullptr);
        const fs::path second = _folder.parent_path() / "second-source.txt";
        writeFile(second, "the second body, which is a different length");
        holder->File.setValue(second.string().c_str());
        doc->recompute();
        EXPECT_TRUE(doc->save());

        return (sourceFolder() / *first.begin()).string();
    }
};

// The plain case: a body nothing names any more is named by a person and goes.
TEST_F(DiscardSourceMaterialTest, aBodyNothingNamesIsRemovedWhenAPersonNamesIt)
{
    const std::string old = theOldBody();
    ASSERT_EQ(whatIsStored().size(), 2U);

    const App::Discarded done = App::discardSourceMaterial(_folder.string(), {old});
    ASSERT_EQ(done.removed.size(), 1U) << "the body a person named was not removed";
    EXPECT_EQ(done.removed.front().path, old);
    EXPECT_GT(done.bytes, 0U) << "what it held was reported as nothing";
    EXPECT_TRUE(done.kept.empty());

    EXPECT_FALSE(fs::exists(old)) << "it was reported removed and is still there";
    EXPECT_EQ(whatIsStored().size(), 1U) << "the body the part still names went too";
}

// The one that matters: asked for a body the project still names, it refuses and says why.
// Authored content is not deleted on the strength of having been asked for.
TEST_F(DiscardSourceMaterialTest, aBodyTheProjectStillNamesIsLeftAloneWithItsReason)
{
    theOldBody();
    std::set<std::string> stored = whatIsStored();
    ASSERT_EQ(stored.size(), 2U);

    const App::ProjectSurvey found = App::surveyProjectSourceMaterial(_folder.string());
    ASSERT_EQ(found.unreferenced.size(), 1U);
    const std::string unreferenced = found.unreferenced.front().path;

    // Whichever of the two the survey did NOT name is the one the part is built from.
    std::string named;
    for (const std::string& id : stored) {
        if ((sourceFolder() / id).string() != unreferenced) {
            named = (sourceFolder() / id).string();
        }
    }
    ASSERT_FALSE(named.empty());

    const App::Discarded done = App::discardSourceMaterial(_folder.string(), {named});
    EXPECT_TRUE(done.removed.empty()) << "the body the part is built from was removed";
    ASSERT_EQ(done.kept.size(), 1U) << "it was neither removed nor explained";
    EXPECT_EQ(done.kept.front().first, named);
    EXPECT_NE(done.kept.front().second.find("names it"), std::string::npos)
        << done.kept.front().second;
    EXPECT_TRUE(fs::exists(named)) << "the body the part is built from is gone";
}

// A path naming something elsewhere on the machine is answered, not obeyed.
TEST_F(DiscardSourceMaterialTest, aPathOutsideTheProjectIsAnsweredNotObeyed)
{
    theOldBody();
    const fs::path elsewhere = _folder.parent_path() / "not-ours.txt";
    writeFile(elsewhere, "something that has nothing to do with this project");

    const App::Discarded done = App::discardSourceMaterial(_folder.string(), {elsewhere.string()});
    EXPECT_TRUE(done.removed.empty());
    ASSERT_EQ(done.kept.size(), 1U);
    EXPECT_NE(done.kept.front().second.find("not source material"), std::string::npos)
        << done.kept.front().second;
    EXPECT_TRUE(fs::exists(elsewhere)) << "a file outside the project was removed";
}

// Only what was named. A second file nothing names is left where it is.
TEST_F(DiscardSourceMaterialTest, onlyWhatWasNamedIsRemoved)
{
    const std::string old = theOldBody();

    // A second body nothing names: stored the same way, by a part that then stops naming it.
    App::Document* doc = App::GetApplication().getDocument(_open.front().c_str());
    auto* holder = dynamic_cast<App::DocumentObjectFileIncluded*>(doc->getObject("Included"));
    ASSERT_NE(holder, nullptr);
    const fs::path third = _folder.parent_path() / "third-source.txt";
    writeFile(third, "a third body, longer than either of the other two bodies");
    holder->File.setValue(third.string().c_str());
    doc->recompute();
    ASSERT_TRUE(doc->save());
    ASSERT_EQ(whatIsStored().size(), 3U);

    const App::Discarded done = App::discardSourceMaterial(_folder.string(), {old});
    EXPECT_EQ(done.removed.size(), 1U);
    EXPECT_EQ(whatIsStored().size(), 2U)
        << "a body that was not named was removed along with the one that was";
}

// Asked for something that is not there, it says so rather than reporting work it did not do.
TEST_F(DiscardSourceMaterialTest, somethingThatIsNotThereIsSaidSoRatherThanCounted)
{
    theOldBody();
    const App::Discarded done
        = App::discardSourceMaterial(_folder.string(), {(sourceFolder() / "nosuchid").string()});
    EXPECT_TRUE(done.removed.empty());
    EXPECT_EQ(done.bytes, 0U);
    ASSERT_EQ(done.kept.size(), 1U);
    EXPECT_NE(done.kept.front().second.find("nothing there"), std::string::npos)
        << done.kept.front().second;
}
