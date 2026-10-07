// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectFileIncluded.h>
#include <App/Project.h>
#include <App/UnreferencedFiles.h>
#include <App/VersionHistory.h>
#include <Base/Exception.h>
#include <Base/Uuid.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace App;
namespace fs = std::filesystem;

namespace
{

void write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string resolved(const fs::path& path)
{
    return fs::weakly_canonical(path).string();
}

}  // namespace

class ProjectTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _folder = fs::temp_directory_path() / ("cruth_project_" + Base::Uuid::createUuid());
        fs::create_directories(_folder);
    }

    void TearDown() override
    {
        for (const std::string& name : _open) {
            if (GetApplication().getDocument(name.c_str()) != nullptr) {
                GetApplication().closeDocument(name.c_str());
            }
        }
        std::error_code ignored;
        fs::remove_all(_folder, ignored);
    }

    /// A part holding one handed-in file, saved at `path`.
    Document* aSavedPart(const fs::path& path)
    {
        auto& app = GetApplication();
        Document* doc = app.newDocument(app.getUniqueDocumentName("Part").c_str());
        _open.push_back(doc->getName());
        const fs::path handedIn = _folder / ("source-" + Base::Uuid::createUuid() + ".txt");
        write(handedIn, "a handed-in body");
        auto* holder = doc->addObject<DocumentObjectFileIncluded>("Included");
        holder->File.setValue(handedIn.string().c_str());
        doc->recompute();
        fs::create_directories(path.parent_path());
        EXPECT_TRUE(doc->saveAs(path.string().c_str()));
        return doc;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path _folder;
    std::vector<std::string> _open;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// A folder kept in version control is not yet a project; the project file makes it one.
TEST_F(ProjectTest, aDocumentIsInAProjectOnlyWhereAProjectFileIs)
{
    const fs::path file = _folder / "Loose" / "Bracket.cpart";
    write(file, "text");
    EXPECT_EQ(projectFileOf(file.string()), "") << "a file outside version control";

    startHistory((_folder / "Loose").string());
    EXPECT_EQ(projectFileOf(file.string()), "") << "history alone made a project";

    const std::string project = createProject((_folder / "Loose").string(), "Gearbox");
    EXPECT_EQ(projectFileOf(file.string()), project);
    EXPECT_EQ(projectName(project), "Gearbox");
}

TEST_F(ProjectTest, aNewProjectStartsItsOwnHistory)
{
    const fs::path where = _folder / "Gearbox";
    const std::string project = createProject(where.string(), "Gearbox");

    EXPECT_EQ(project, resolved(where / "Gearbox.cproject"));
    EXPECT_TRUE(fs::exists(project));
    EXPECT_EQ(historyFolderOf(project), resolved(where));
    EXPECT_TRUE(fs::exists(where / ".gitignore")) << "the rebuild store would be versioned";
}

// A project made inside a repository joins it rather than starting a second one inside it.
TEST_F(ProjectTest, aProjectInsideExistingHistoryJoinsIt)
{
    startHistory(_folder.string());
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");

    EXPECT_EQ(historyFolderOf(project), resolved(_folder));
    EXPECT_FALSE(fs::exists(_folder / "Gearbox" / ".git"));
}

TEST_F(ProjectTest, aProjectNeedsANameAndAFolderWithoutOne)
{
    EXPECT_THROW(createProject((_folder / "A").string(), ""), Base::Exception);
    EXPECT_THROW(createProject((_folder / "A").string(), "a/b"), Base::Exception);
    createProject((_folder / "A").string(), "First");
    EXPECT_THROW(createProject((_folder / "A").string(), "Second"), Base::Exception);
    EXPECT_FALSE(fs::exists(_folder / "A" / "Second.cproject"));
}

// The nearest project file above a document is its project, and the search stays inside the
// document's repository: a project file beyond it cannot give the document history.
TEST_F(ProjectTest, theNearestProjectFileInsideTheRepositoryIsTheProject)
{
    const std::string outer = createProject(_folder.string(), "Outer");
    const fs::path deep = _folder / "parts" / "brackets" / "Bracket.cpart";
    write(deep, "text");
    EXPECT_EQ(projectFileOf(deep.string()), outer);

    const std::string inner = createProject((_folder / "parts").string(), "Inner");
    EXPECT_EQ(projectFileOf(deep.string()), inner);

    const fs::path nested = _folder / "parts" / "vendor";
    startHistory(nested.string());
    write(nested / "Motor.cpart", "text");
    EXPECT_EQ(projectFileOf((nested / "Motor.cpart").string()), "")
        << "a project file outside the document's own repository was taken as its project";
}

TEST_F(ProjectTest, aMovedPartLeavesNoCopyAndBringsItsSourceMaterial)
{
    const fs::path loose = _folder / "Desktop" / "Bracket.cpart";
    Document* doc = aSavedPart(loose);
    const std::set<std::string> named = sourceMaterialNamedBy(loose.string());
    ASSERT_EQ(named.size(), 1U);
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");

    const std::string moved = moveIntoProject(*doc, project);

    EXPECT_EQ(moved, resolved(_folder / "Gearbox" / "Bracket.cpart"));
    EXPECT_EQ(doc->FileName.getStrValue(), moved);
    EXPECT_FALSE(fs::exists(loose)) << "two files now carry one document identity";
    EXPECT_EQ(projectFileOf(moved), project);
    EXPECT_TRUE(fs::is_directory(_folder / "Gearbox" / "assets" / *named.begin()))
        << "the part arrived without the body it was built from";
    EXPECT_TRUE(fs::is_directory(_folder / "Desktop" / "assets" / *named.begin()))
        << "material another part may still name was removed";
}

TEST_F(ProjectTest, aMoveOntoAnExistingFileIsRefusedAndChangesNothing)
{
    const fs::path loose = _folder / "Desktop" / "Bracket.cpart";
    Document* doc = aSavedPart(loose);
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    write(_folder / "Gearbox" / "Bracket.cpart", "another bracket");

    EXPECT_THROW(moveIntoProject(*doc, project), Base::Exception);

    EXPECT_TRUE(fs::exists(loose));
    EXPECT_EQ(doc->FileName.getStrValue(), loose.string());
    std::ifstream kept(_folder / "Gearbox" / "Bracket.cpart");
    EXPECT_EQ(
        std::string(std::istreambuf_iterator<char>(kept), std::istreambuf_iterator<char>()),
        "another bracket"
    );
}

TEST_F(ProjectTest, aPartAlreadyInTheFolderStaysWhereItIs)
{
    const fs::path file = _folder / "Gearbox" / "Bracket.cpart";
    Document* doc = aSavedPart(file);
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");

    EXPECT_EQ(moveIntoProject(*doc, project), resolved(file));
    EXPECT_TRUE(fs::exists(file));
}

TEST_F(ProjectTest, aDocumentNeverSavedCannotBeMoved)
{
    auto& app = GetApplication();
    Document* doc = app.newDocument(app.getUniqueDocumentName("Unsaved").c_str());
    _open.push_back(doc->getName());
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");

    EXPECT_THROW(moveIntoProject(*doc, project), Base::Exception);
}

namespace
{

std::string textOf(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// The folder decides membership: a folder holding its own project or its own repository is
// someone else's, and source material and hidden folders hold no documents.
TEST_F(ProjectTest, aProjectsDocumentsAreTheOnesInItsFolder)
{
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    const fs::path top = _folder / "Gearbox";
    write(top / "Housing.cpart", "text");
    write(top / "shafts" / "Shaft.cpart", "text");
    write(top / "notes.txt", "not a document");
    write(top / "assets" / "abc" / "Handed.FCStd", "source material");
    write(top / ".cruth" / "Cached.cpart", "rebuild store");
    createProject((top / "motor").string(), "Motor");
    write(top / "motor" / "Rotor.cpart", "text");
    startHistory((top / "vendor").string());
    write(top / "vendor" / "Bearing.cpart", "text");

    const std::vector<std::string> expected {
        resolved(top / "Housing.cpart"),
        resolved(top / "shafts" / "Shaft.cpart")
    };
    EXPECT_EQ(projectDocuments(project), expected);
}

TEST_F(ProjectTest, anOutsideDocumentIsListedWhereItIsAndHeldAtItsNewestVersion)
{
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    const fs::path bolt = _folder / "Library" / "Bolt.cpart";
    Document* doc = aSavedPart(bolt);
    startHistory((_folder / "Library").string());
    const std::string committed = commitFile(bolt.string(), "Bolt");

    const OutsideDocument added = addOutsideDocument(project, bolt.string());

    EXPECT_TRUE(fs::exists(bolt)) << "listing a document moved it";
    EXPECT_EQ(added.version, committed);
    EXPECT_EQ(added.uuid, doc->Uid.getValueStr());
    const std::vector<OutsideDocument> listed = outsideDocuments(project);
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed[0].path, resolved(bolt));
    EXPECT_EQ(listed[0].uuid, added.uuid);
    EXPECT_EQ(listed[0].version, committed);
    EXPECT_NE(textOf(project).find("path=\"../Library/Bolt.cpart\""), std::string::npos)
        << "the project file names the document by where this machine keeps it:\n"
        << textOf(project);
    EXPECT_EQ(projectName(project), "Gearbox");
}

TEST_F(ProjectTest, aDocumentNotVersionedIsHeldAtNoVersion)
{
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    const fs::path loose = _folder / "Desktop" / "Washer.cpart";
    aSavedPart(loose);

    const OutsideDocument added = addOutsideDocument(project, loose.string());

    EXPECT_EQ(added.version, "");
    EXPECT_EQ(newerVersionOf(added), "");
    EXPECT_EQ(textOf(project).find("\" version=\""), std::string::npos) << textOf(project);
}

// A newer version is reported and never taken on its own; Update takes it.
TEST_F(ProjectTest, aNewerVersionWaitsForUpdate)
{
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    const fs::path bolt = _folder / "Library" / "Bolt.cpart";
    Document* doc = aSavedPart(bolt);
    startHistory((_folder / "Library").string());
    const std::string first = commitFile(bolt.string(), "Bolt");
    addOutsideDocument(project, bolt.string());

    doc->addObject("App::DocumentObjectGroup", "Thread");
    ASSERT_TRUE(doc->save());
    const std::string second = commitFile(bolt.string(), "Thread");

    std::vector<OutsideDocument> listed = outsideDocuments(project);
    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed[0].version, first) << "the newer version was taken without Update";
    EXPECT_EQ(newerVersionOf(listed[0]), second);

    EXPECT_EQ(updateOutsideDocument(project, bolt.string()), second);
    listed = outsideDocuments(project);
    EXPECT_EQ(listed[0].version, second);
    EXPECT_EQ(newerVersionOf(listed[0]), "");
}

TEST_F(ProjectTest, onlyADocumentFromOutsideCanBeListedOnce)
{
    const std::string project = createProject((_folder / "Gearbox").string(), "Gearbox");
    const fs::path inside = _folder / "Gearbox" / "Housing.cpart";
    write(inside, "text");
    const fs::path outside = _folder / "Desktop" / "Washer.cpart";
    write(outside, "text");
    write(_folder / "Desktop" / "notes.txt", "text");
    addOutsideDocument(project, outside.string());
    const std::string before = textOf(project);

    EXPECT_THROW(addOutsideDocument(project, inside.string()), Base::Exception);
    EXPECT_THROW(addOutsideDocument(project, outside.string()), Base::Exception);
    EXPECT_THROW(
        addOutsideDocument(project, (_folder / "Desktop" / "notes.txt").string()),
        Base::Exception
    );
    EXPECT_THROW(addOutsideDocument(project, (_folder / "Missing.cpart").string()), Base::Exception);
    EXPECT_THROW(updateOutsideDocument(project, inside.string()), Base::Exception);
    EXPECT_EQ(textOf(project), before);
}
