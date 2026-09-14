// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

/** A refusal is total: a file whose record cannot be determined does not open at all.
 *
 *  Not as the part that was read before the failure, and not as an empty document. A
 *  partially-read document standing in for the record is the mechanism by which the record is
 *  destroyed -- it looks like a document, and a save publishes it over the only remaining copy of
 *  what is missing from it.
 *
 *  Measured before this: a truncated file opened with three objects in it and a file still
 *  carrying the markers of an unfinished merge opened with two, each with the values from the
 *  point of failure onwards silently missing. What stood in front of that was the write guard of
 *  Clause 19.3, which was always meant to be the second line of defence rather than the first.
 *
 *  The other half of the clause is here too, and matters just as much: unfamiliar is not
 *  malformed. A well-formed file stating content this build has no place for still opens, because
 *  refusing those would make every file a build does not fully understand unopenable (Amendment 19
 *  Clause 19.2).
 */
class BrokenFileRefusalTest: public ::testing::Test
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

    /// A saved three-object document, written to `path` and closed again.
    static std::string writeWholeDocument(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* whole = app.newDocument(app.getUniqueDocumentName("broken").c_str(), "testUser");
        for (const char* name : {"Alpha", "Beta", "Gamma"}) {
            whole->addObject("App::VarSet", name);
        }
        EXPECT_TRUE(whole->saveAs(path.c_str()));
        const std::string text = readAll(path);
        app.closeDocument(whole->getName());
        return text;
    }

    /// What the open door did with the file: the message it refused with, or nothing if it opened.
    std::string refusalFor(const std::string& path)
    {
        try {
            _doc = App::GetApplication().openDocument(path.c_str());
        }
        catch (const Base::Exception& e) {
            _doc = nullptr;
            return e.what();
        }
        return {};
    }

    /// Whether any document in the application was opened from `path`.
    static bool anyDocumentFrom(const std::string& path)
    {
        for (const App::Document* doc : App::GetApplication().getDocuments()) {
            if (doc->FileName.getStrValue() == path) {
                return true;
            }
        }
        return false;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// A write that was cut off, or a transfer that stopped part way. The reader gets as far as it
// gets; what it has at that point is the beginning of a file and not a document.
TEST_F(BrokenFileRefusalTest, aTruncatedFileDoesNotOpen)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    const std::string::size_type at = whole.find("name=\"Gamma\"");
    ASSERT_NE(at, std::string::npos);
    writeAll(path, whole.substr(0, at));
    const std::string before = readAll(path);

    const std::string refusal = refusalFor(path);

    EXPECT_FALSE(refusal.empty()) << "a truncated file opened as its own beginning";
    EXPECT_FALSE(anyDocumentFrom(path)) << "a refused open left a document standing in for the "
                                           "record";
    EXPECT_EQ(readAll(path), before) << "a refused open changed the file";
}

// The case git-for-CAD makes ordinary: two people edited the same file, the merge did not settle,
// and the markers are still in it. Well-formed to a person reading it, and not a record.
TEST_F(BrokenFileRefusalTest, aFileStillCarryingMergeMarkersDoesNotOpen)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    const std::string::size_type at = whole.find("<Objects>");
    ASSERT_NE(at, std::string::npos);
    writeAll(
        path,
        whole.substr(0, at) + "<<<<<<< HEAD\n" + whole.substr(at) + "\n=======\n>>>>>>> theirs\n"
    );

    const std::string refusal = refusalFor(path);

    EXPECT_FALSE(refusal.empty()) << "a file carrying merge markers opened as its own beginning";
    EXPECT_FALSE(anyDocumentFrom(path)) << "a refused open left a document behind";
}

// Nothing in the file could be read at all. This is the one failure the reader does not raise on
// -- it simply is not valid -- and returning quietly from it handed back an empty document
// wearing the file's name.
TEST_F(BrokenFileRefusalTest, aFileThatIsNotARecipeDoesNotOpenAsAnEmptyDocument)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    writeAll(path, "this is not a recipe, and not XML either\n");

    const std::string refusal = refusalFor(path);

    EXPECT_FALSE(refusal.empty()) << "a file that is not a recipe opened as an empty document";
    EXPECT_FALSE(anyDocumentFrom(path)) << "a refused open left an empty document behind";
}

// Refusing to open a file is not refusing to say anything about it. The file has to be repairable
// in a text editor, which takes a line number: "element name expected" on its own is not a
// diagnosis of anything.
TEST_F(BrokenFileRefusalTest, theRefusalSaysWhereTheTroubleIs)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    const std::string::size_type at = whole.find("<Objects>");
    ASSERT_NE(at, std::string::npos);
    // The marker is put on a line of its own, so the line it is reported at is a fact and not an
    // artefact of where the writer happened to break the text.
    const std::string broken = whole.substr(0, at) + "<<<<<<< HEAD\n" + whole.substr(at);
    writeAll(path, broken);
    const long line = 1 + std::count(broken.begin(), broken.begin() + static_cast<long>(at), '\n');

    const std::string refusal = refusalFor(path);

    ASSERT_FALSE(refusal.empty());
    EXPECT_NE(refusal.find(path), std::string::npos)
        << "the refusal did not name the file: " << refusal;
    EXPECT_NE(refusal.find("line " + std::to_string(line)), std::string::npos)
        << "the refusal did not say which line the trouble starts on (expected line " << line
        << "): " << refusal;
    EXPECT_NE(refusal.find("column"), std::string::npos)
        << "the refusal did not say where on the line: " << refusal;
}

// The other half of the clause, and the reason the refusal has to be narrow. A file this build
// does not fully understand is still that person's work, and every one of these statements is
// kept and given back. Refusing them would be refusing to open files that are not broken at all.
TEST_F(BrokenFileRefusalTest, aWellFormedFileStatingUnfamiliarContentStillOpens)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    std::string whole = writeWholeDocument(path);

    // A property of a type this build does not have, declared on an object that does exist...
    const std::string::size_type at = whole.find("<Properties>");
    ASSERT_NE(at, std::string::npos);
    whole.insert(
        at + std::string("<Properties>").size(),
        "\n<Property name=\"Sparkle\" type=\"Addon::PropertyGlitter\" dynamic=\"1\">\n"
        "<Glitter value=\"lots\"/>\n</Property>"
    );
    // ...and an object of a type it cannot construct at all.
    const std::string::size_type gamma = whole.find("\"App::VarSet\" name=\"Gamma\"");
    ASSERT_NE(gamma, std::string::npos);
    whole.replace(gamma, std::string("\"App::VarSet\"").size(), "\"Addon::Widget\"");
    writeAll(path, whole);

    const std::string refusal = refusalFor(path);

    EXPECT_TRUE(refusal.empty()) << "a well-formed file was refused for being unfamiliar: "
                                 << refusal;
    ASSERT_NE(_doc, nullptr);
    EXPECT_TRUE(_doc->holdsUnreadContent())
        << "a document holding statements it could not honour called itself whole";
    EXPECT_TRUE(_doc->whatASaveWouldLose().empty())
        << "content that was kept and will be given back was charged as a loss";
}

// A document already open, whose file changed under it -- a branch switched, a copy synchronised
// -- and was then read again. The open door is not involved: this document existed before the
// read and goes on existing after it. What must not survive is the fragment.
TEST_F(BrokenFileRefusalTest, aReReadThatStopsLeavesTheDocumentHoldingNothing)
{
    auto& app = App::GetApplication();
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);

    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    ASSERT_EQ(_doc->getObjects().size(), 3U);

    const std::string::size_type at = whole.find("name=\"Gamma\"");
    ASSERT_NE(at, std::string::npos);
    writeAll(path, whole.substr(0, at));
    const std::string before = readAll(path);

    std::string refusal;
    try {
        _doc->restore();
    }
    catch (const Base::Exception& e) {
        refusal = e.what();
    }

    EXPECT_FALSE(refusal.empty()) << "a re-read that stopped part way was reported as a read";
    EXPECT_TRUE(_doc->getObjects().empty())
        << "a stopped re-read left the beginning of the file sitting in the document";
    EXPECT_FALSE(_doc->save()) << "the fragment left behind could be published over the record";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file";
}
