// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/StoredRecipe.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

/** A file names the format it was written to, and a reader consults it before interpreting.
 *
 *  Amendment 19 Clause 19.7. The writer has always stated the version; nothing ever read it.
 *  Measured before this: a file stating version 99 opened exactly as though it had stated version
 *  1, and every value in it was interpreted by rules it was never written to. A stamp that is
 *  written and never read leaves every other rule to catch the damage one case at a time.
 *
 *  The refusal is total for the same reason Clause 19.2's is -- a document read by rules it was
 *  not written to looks like the part and is not it -- and it says which version was asked for and
 *  which this build reads, because "this file will not open" without either number leaves a person
 *  with nothing to do about it.
 *
 *  The clause's other half is here too: no version of the PROGRAM appears in the file. Which
 *  release last saved a document does not determine whether it can be read, and a stamp rewritten
 *  on every save by a different person is churn in a file that exists to be diffed and merged.
 */
class FormatVersionTest: public ::testing::Test
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

    /// A saved two-object document, written to `path` and closed again.
    static std::string writeWholeDocument(const std::string& path)
    {
        auto& app = App::GetApplication();
        auto* whole = app.newDocument(app.getUniqueDocumentName("format").c_str(), "testUser");
        whole->addObject("App::VarSet", "Alpha");
        whole->addObject("App::VarSet", "Beta");
        EXPECT_TRUE(whole->saveAs(path.c_str()));
        const std::string text = readAll(path);
        app.closeDocument(whole->getName());
        return text;
    }

    /// The same file, saying it was written to some other format.
    static void restate(const std::string& path, const std::string& text, const std::string& as)
    {
        std::string said = text;
        const std::string stamp = "<Recipe Version=\"1\">";
        const std::string::size_type at = said.find(stamp);
        EXPECT_NE(at, std::string::npos) << "the writer stated no version to restate";
        said.replace(at, stamp.size(), as);
        writeAll(path, said);
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

// The case the clause exists for: a file from a later format, read by a build that has no idea
// what its statements mean. Measured before this: it opened, with two objects in it.
TEST_F(FormatVersionTest, aFileWrittenToAFormatThisBuildDoesNotReadDoesNotOpen)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    restate(path, whole, "<Recipe Version=\"99\">");

    const std::string refused = refusalFor(path);
    ASSERT_FALSE(refused.empty()) << "a format this build does not read was interpreted anyway";
    EXPECT_NE(refused.find("99"), std::string::npos)
        << "the refusal does not say which version the file asked for";
    EXPECT_NE(refused.find('1'), std::string::npos)
        << "the refusal does not say which version this build reads";
    EXPECT_FALSE(anyDocumentFrom(path)) << "a refusal that leaves a document open is not total";
    Base::FileInfo(path).deleteFile();
}

// Its own type, so the open door and a script can tell "this build does not read that format"
// from "this file is broken". They are different facts and only one of them is the file's fault.
TEST_F(FormatVersionTest, theRefusalIsItsOwnKindOfRefusal)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    restate(path, whole, "<Recipe Version=\"99\">");

    EXPECT_THROW(App::GetApplication().openDocument(path.c_str()), App::DocumentFormatUnknownError);
    Base::FileInfo(path).deleteFile();
}

// Read whole or not at all. A parse that took the leading digits and went on would be
// interpreting a file by a rule it made up, and "1.0" is not the name of this format.
TEST_F(FormatVersionTest, aVersionThatIsNotAWholeNumberIsNotThisFormat)
{
    for (const char* stated :
         {"<Recipe Version=\"1.0\">", "<Recipe Version=\"1x\">", "<Recipe Version=\" 1\">"}) {
        const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
        const std::string whole = writeWholeDocument(path);
        restate(path, whole, stated);

        EXPECT_FALSE(refusalFor(path).empty()) << stated << " was read as version 1";
        TearDown();
        Base::FileInfo(path).deleteFile();
    }
}

// A file that states no version cannot be asked the question, and is not assumed to answer the
// way this build would like. Every file this program has written states one, so the assumption
// would only ever be made about a file this program did not write.
TEST_F(FormatVersionTest, aFileStatingNoFormatVersionDoesNotOpen)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);
    restate(path, whole, "<Recipe>");

    const std::string refused = refusalFor(path);
    EXPECT_FALSE(refused.empty()) << "a file stating no format was interpreted as this one";
    EXPECT_FALSE(anyDocumentFrom(path));
    Base::FileInfo(path).deleteFile();
}

// The control, and the thing that would make all of the above worthless if it broke: the version
// this build writes is the version it reads, and an ordinary file opens.
TEST_F(FormatVersionTest, theVersionThisBuildWritesIsTheOneItReads)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);

    EXPECT_NE(
        whole.find("<Recipe Version=\"" + std::to_string(App::storedRecipeFormat) + "\">"),
        std::string::npos
    ) << "the file does not state the format this build writes";
    EXPECT_TRUE(refusalFor(path).empty()) << "a file this build wrote would not open";
    ASSERT_NE(_doc, nullptr);
    EXPECT_EQ(_doc->getObjects().size(), 2U);
    Base::FileInfo(path).deleteFile();
}

// The clause's other half. A version of the PROGRAM in the file would be rewritten on every save
// by a different person, which is churn in a file whose whole purpose is to be diffed and merged
// -- and it answers a question nobody asked: which release last saved a document does not
// determine whether it can be read.
TEST_F(FormatVersionTest, noVersionOfTheProgramAppearsInTheFile)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    const std::string whole = writeWholeDocument(path);

    EXPECT_EQ(whole.find("ProgramVersion"), std::string::npos)
        << "the file names the program that wrote it";
    EXPECT_EQ(whole.find("BuildRevision"), std::string::npos)
        << "the file names the build that wrote it";
    Base::FileInfo(path).deleteFile();
}
