// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/FileInfo.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

/** A document that did not come back whole is a fragment, and the file it came from is the only
 *  remaining copy of what is missing from it.
 *
 *  A truncated file, or one still carrying the markers of an unfinished merge, opens as its own
 *  beginning. An ordinary save then publishes that beginning over the whole record. These tests
 *  hold the guard, and the way past it, in place (Amendment 19 Clause 19.3).
 */
class FragmentWriteGuardTest: public ::testing::Test
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

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static void writeAll(const std::string& path, const std::string& text)
    {
        std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
        out << text;
    }

    /// A saved three-object document, cut off part way through -- what a truncated write or an
    /// interrupted transfer leaves behind.
    App::Document* openTruncated(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* whole
            = app.newDocument(app.getUniqueDocumentName("fragment").c_str(), "testUser");
        for (const char* name : {"Alpha", "Beta", "Gamma"}) {
            whole->addObject("App::VarSet", name);
        }
        EXPECT_TRUE(whole->saveAs(path.c_str()));
        const std::string full = readAll(path);
        app.closeDocument(whole->getName());

        const std::string::size_type at = full.find("name=\"Gamma\"");
        EXPECT_NE(at, std::string::npos);
        writeAll(path, full.substr(0, at));

        _doc = app.openDocument(path.c_str());
        return _doc;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The measured loss: the document opens as the prefix that was read, and an ordinary save writes
// that prefix over the file that still holds everything.
TEST_F(FragmentWriteGuardTest, savingAFragmentOverItsOwnFileIsRefusedAndChangesNothing)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);
    ASSERT_TRUE(doc->testStatus(App::Document::RestoreError))
        << "a truncated file opened as a whole document";

    const std::string before = readAll(path);
    EXPECT_FALSE(doc->save()) << "a fragment was published over the record";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file on disk";
}

// The guard travels with the document's state, never with the destination: a rule attached to the
// path is walked around by writing elsewhere and writing back.
TEST_F(FragmentWriteGuardTest, savingAFragmentToAnyOtherPathIsRefusedToo)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    EXPECT_FALSE(doc->saveCopy(elsewhere.c_str()))
        << "a fragment was written to a new path, from where it can be copied back";
    EXPECT_TRUE(readAll(elsewhere).empty()) << "a refused copy left a fragment on disk";
}

// A refused save changes nothing -- not the document, not the name or the path it is saved under.
// A refusal that has already renamed the document performed half of the write it declined.
TEST_F(FragmentWriteGuardTest, aRefusedSaveAsLeavesTheDocumentWhereItWas)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::string name = doc->FileName.getStrValue();
    const std::string label = doc->Label.getStrValue();

    const std::string elsewhere = Base::FileInfo::getTempFileName() + ".cpart";
    EXPECT_FALSE(doc->saveAs(elsewhere.c_str()));
    EXPECT_EQ(doc->FileName.getStrValue(), name)
        << "a refused Save As left the document pointing at the file it declined to write";
    EXPECT_EQ(doc->Label.getStrValue(), label) << "a refused Save As renamed the document";
}

// The way past the guard, which the clause requires exist: a caller may accept the loss, by name,
// per write. What is forbidden is a write that proceeds on an assumed answer.
TEST_F(FragmentWriteGuardTest, aWriteThatNamesWhatItLosesIsAllowedThrough)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = openTruncated(path);
    ASSERT_NE(doc, nullptr);

    const std::vector<std::string> losing = doc->whatASaveWouldLose();
    ASSERT_FALSE(losing.empty()) << "a fragment would not say what a save would cost";

    EXPECT_FALSE(doc->saveAcceptingLoss({"something else"}, ""))
        << "a write was let through on an answer to a question nobody asked";

    const std::string before = readAll(path);
    EXPECT_TRUE(doc->saveAcceptingLoss(losing, ""))
        << "a caller that accepted the loss was refused";
    EXPECT_NE(readAll(path), before) << "an accepted write did not happen";

    // Per write: the acceptance does not carry to the next one.
    EXPECT_FALSE(doc->save()) << "accepting one write turned the guard off";
}
