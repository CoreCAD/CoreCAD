// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentChanges.h>
#include <App/DocumentObject.h>
#include <App/PropertyUnits.h>
#include <App/VersionHistory.h>
#include <Base/Exception.h>
#include <Base/Uuid.h>

#include <git2.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace App;
namespace fs = std::filesystem;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

/// A repository the test writes commits into directly, so the machine's git settings (a signing
/// requirement, a hook) cannot change what gets committed.
class ScratchRepository
{
public:
    explicit ScratchRepository(const fs::path& folder)
    {
        git_libgit2_init();
        git_repository_init(&_repository, folder.string().c_str(), 0);
    }
    ~ScratchRepository()
    {
        git_repository_free(_repository);
        git_libgit2_shutdown();
    }
    ScratchRepository(const ScratchRepository&) = delete;
    ScratchRepository& operator=(const ScratchRepository&) = delete;

    /// Commit `files` (paths inside the repository) on top of HEAD; returns the commit id.
    std::string commit(const std::vector<std::string>& files, const std::string& message)
    {
        return commitOn(headOrNothing(), files, message, "HEAD");
    }

    /// Commit with the given parents, moving `ref` to it, or no ref at all when empty.
    std::string commitOn(
        const std::vector<std::string>& parentIds,
        const std::vector<std::string>& files,
        const std::string& message,
        const std::string& ref
    )
    {
        git_index* index = nullptr;
        git_repository_index(&index, _repository);
        for (const std::string& file : files) {
            EXPECT_EQ(git_index_add_bypath(index, file.c_str()), 0) << file;
        }
        git_index_write(index);
        git_oid treeId;
        git_index_write_tree(&treeId, index);
        git_index_free(index);

        git_tree* tree = nullptr;
        git_tree_lookup(&tree, _repository, &treeId);
        git_signature* signature = nullptr;
        git_signature_new(&signature, "Tester", "tester@example.com", _clock++, 0);

        std::vector<git_commit*> parents;
        for (const std::string& parentId : parentIds) {
            git_oid id;
            git_oid_fromstr(&id, parentId.c_str());
            git_commit* parent = nullptr;
            git_commit_lookup(&parent, _repository, &id);
            parents.push_back(parent);
        }
        std::vector<const git_commit*> parentList(parents.begin(), parents.end());
        git_oid id;
        git_commit_create(
            &id,
            _repository,
            ref.empty() ? nullptr : ref.c_str(),
            signature,
            signature,
            nullptr,
            message.c_str(),
            tree,
            parentList.size(),
            parentList.data()
        );

        for (git_commit* parent : parents) {
            git_commit_free(parent);
        }
        git_signature_free(signature);
        git_tree_free(tree);

        char text[GIT_OID_MAX_HEXSIZE + 1] {};
        git_oid_tostr(text, sizeof(text), &id);
        return text;
    }

private:
    std::vector<std::string> headOrNothing() const
    {
        git_oid id;
        if (git_reference_name_to_id(&id, _repository, "HEAD") != 0) {
            return {};
        }
        char text[GIT_OID_MAX_HEXSIZE + 1] {};
        git_oid_tostr(text, sizeof(text), &id);
        return {text};
    }

    git_repository* _repository {};
    git_time_t _clock {1700000000};
};

void write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

}  // namespace

class VersionHistoryTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _folder = fs::temp_directory_path() / ("cruth_history_" + Base::Uuid::createUuid());
        fs::create_directories(_folder);
    }

    void TearDown() override
    {
        for (App::Document* doc : _opened) {
            App::GetApplication().closeDocument(doc->getName());
        }
        std::error_code ignored;
        fs::remove_all(_folder, ignored);
    }

    App::Document* track(App::Document* doc)
    {
        _opened.push_back(doc);
        return doc;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path _folder;
    std::vector<App::Document*> _opened;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(VersionHistoryTest, aFileOutsideAnyRepositoryHasNoVersions)
{
    const fs::path file = _folder / "Loose.cpart";
    write(file, "text");

    EXPECT_TRUE(fileVersions(file.string()).empty());
    EXPECT_THROW(fileTextAt(file.string(), "HEAD"), Base::Exception);
}

TEST_F(VersionHistoryTest, onlyCommitsThatChangedTheFileAreVersionsNewestFirst)
{
    ScratchRepository repository(_folder);
    const fs::path file = _folder / "parts" / "Widget.cpart";
    write(file, "first");
    const std::string first = repository.commit({"parts/Widget.cpart"}, "Start the widget");
    write(_folder / "notes.txt", "unrelated");
    repository.commit({"notes.txt"}, "Notes only");
    write(file, "second");
    const std::string second = repository.commit({"parts/Widget.cpart"}, "Lengthen it\n\nbody");

    const std::vector<FileVersion> versions = fileVersions(file.string());

    ASSERT_EQ(versions.size(), 2u);
    EXPECT_EQ(versions[0].commit, second);
    EXPECT_EQ(versions[0].summary, "Lengthen it");
    EXPECT_EQ(versions[0].author, "Tester");
    EXPECT_EQ(versions[1].commit, first);
    EXPECT_GT(versions[0].time, versions[1].time);
}

TEST_F(VersionHistoryTest, aVersionIsTheCommittedTextNotTheFileOnDisk)
{
    ScratchRepository repository(_folder);
    const fs::path file = _folder / "Widget.cpart";
    write(file, "as committed");
    const std::string first = repository.commit({"Widget.cpart"}, "One");
    write(file, "edited since");

    EXPECT_EQ(fileTextAt(file.string(), first), "as committed");
    EXPECT_EQ(fileTextAt(file.string(), "HEAD"), "as committed");
}

TEST_F(VersionHistoryTest, aRevisionWithoutTheFileIsRefused)
{
    ScratchRepository repository(_folder);
    write(_folder / "notes.txt", "x");
    const std::string before = repository.commit({"notes.txt"}, "Before the part");
    const fs::path file = _folder / "Widget.cpart";
    write(file, "part");
    repository.commit({"Widget.cpart"}, "The part");

    EXPECT_THROW(fileTextAt(file.string(), before), Base::Exception);
    EXPECT_THROW(fileTextAt(file.string(), "no-such-revision"), Base::Exception);
}

// A merged-in branch's own commits are not this line's versions; the merge that brought the
// change in is.
TEST_F(VersionHistoryTest, historyFollowsTheBranchThroughAMerge)
{
    ScratchRepository repository(_folder);
    const fs::path file = _folder / "Widget.cpart";
    write(file, "one");
    const std::string start = repository.commit({"Widget.cpart"}, "Start");
    write(file, "two");
    const std::string side = repository.commitOn({start}, {"Widget.cpart"}, "Side edit", "");
    write(file, "one");
    write(_folder / "notes.txt", "x");
    const std::string main = repository.commit({"Widget.cpart", "notes.txt"}, "Notes");
    write(file, "two");
    const std::string merge = repository.commitOn({main, side}, {"Widget.cpart"}, "Merge side", "HEAD");

    const std::vector<FileVersion> versions = fileVersions(file.string());

    ASSERT_EQ(versions.size(), 2u);
    EXPECT_EQ(versions[0].commit, merge);
    EXPECT_EQ(versions[1].commit, start);
}

// The whole path piece 2 exists for: a saved part, changed and committed, compared with what it
// was.
TEST_F(VersionHistoryTest, aPastVersionOpensAndComparesWithTheOpenDocument)
{
    ScratchRepository repository(_folder);
    const fs::path file = _folder / "Widget.cpart";
    auto* live = track(
        App::GetApplication()
            .newDocument(App::GetApplication().getUniqueDocumentName("live").c_str(), "Widget")
    );
    auto* box = live->addObject("Part::Box", "Box");
    auto* length = static_cast<PropertyLength*>(box->getPropertyByName("Length"));
    length->setValue(10.0);
    live->saveAs(file.string().c_str());
    const std::string first = repository.commit({"Widget.cpart"}, "Ten");
    length->setValue(40.0);
    live->save();
    repository.commit({"Widget.cpart"}, "Forty");

    auto* past = track(openFileVersion(file.string(), first));
    const std::vector<DocumentChange> changes = diffDocuments(*past, *live);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].text, "Box: Length 10 mm → 40 mm");
    EXPECT_TRUE(past->testStatus(App::Document::TempDoc));
    EXPECT_EQ(std::string(past->Label.getValue()), "Widget at " + first.substr(0, 8));
    EXPECT_NE(past->Uid.getValueStr(), live->Uid.getValueStr());
    EXPECT_EQ(App::GetApplication().getDocumentByUuid(live->Uid.getValue()), live);
}

// Each version needs a document of its own; two read into one would be compared with itself.
TEST_F(VersionHistoryTest, twoVersionsOpenSideBySide)
{
    ScratchRepository repository(_folder);
    const fs::path file = _folder / "Widget.cpart";
    auto* live = track(
        App::GetApplication()
            .newDocument(App::GetApplication().getUniqueDocumentName("live").c_str(), "Widget")
    );
    auto* length = static_cast<PropertyLength*>(
        live->addObject("Part::Box", "Box")->getPropertyByName("Length")
    );
    length->setValue(10.0);
    live->saveAs(file.string().c_str());
    const std::string first = repository.commit({"Widget.cpart"}, "Ten");
    length->setValue(40.0);
    live->save();
    const std::string second = repository.commit({"Widget.cpart"}, "Forty");

    auto* older = track(openFileVersion(file.string(), first));
    auto* newer = track(openFileVersion(file.string(), second));
    const std::vector<DocumentChange> changes = diffDocuments(*older, *newer);

    EXPECT_NE(older, newer);
    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].text, "Box: Length 10 mm → 40 mm");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
