// SPDX-License-Identifier: LGPL-2.1-or-later
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <src/App/InitApplication.h>

/** A saved document must be a function of what it contains, not of when it was written.
 *
 *  A save-time clock inside the file breaks that: two saves of an unchanged document differ,
 *  so version control reports a change where nothing was designed, and a file's bytes stop
 *  meaning "this content". These tests hold the property in place.
 */
class DocumentByteStabilityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    /// A document with a little geometry-free content, saved at `path`.
    static App::Document* makeSavedDocument(const std::string& name, const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* doc
            = app.newDocument(app.getUniqueDocumentName(name.c_str()).c_str(), "testUser");
        doc->addObject("App::VarSet", "Thing");
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        return doc;
    }
};

TEST_F(DocumentByteStabilityTest, resavingAnUnchangedDocumentProducesIdenticalBytes)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    App::Document* doc = makeSavedDocument("byteStable", path);
    const std::string first = readAll(path);

    // Cross a clock second: the stored date this test guards had one-second resolution, so
    // two saves inside the same second would agree even with the defect present.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    doc->save();
    const std::string second = readAll(path);

    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second) << "a save that changed nothing rewrote the file";

    const std::string docName = doc->getName();
    App::GetApplication().closeDocument(docName.c_str());
    Base::FileInfo(path).deleteFile();
}

TEST_F(DocumentByteStabilityTest, lastModifiedDateIsDerivedFromTheFileNotStoredInIt)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    App::Document* doc = makeSavedDocument("derivedDate", path);
    const std::string docName = doc->getName();
    App::GetApplication().closeDocument(docName.c_str());

    // Backdate the file well past anything a save-time clock could have written. A stored
    // date would still report the moment of the save; a derived one follows the file.
    const auto backdated = std::filesystem::last_write_time(path) - std::chrono::hours(24 * 365);
    std::filesystem::last_write_time(path, backdated);

    App::Document* reopened = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(reopened, nullptr);

    Base::FileInfo info(path);
    auto modified = info.lastModified();
    const std::string expected = Base::Tools::dateTimeString(modified.getTime_t());

    EXPECT_EQ(std::string(reopened->LastModifiedDate.getValue()), expected)
        << "the date came from somewhere other than the file it was read from";

    const std::string reopenedName = reopened->getName();
    App::GetApplication().closeDocument(reopenedName.c_str());
    Base::FileInfo(path).deleteFile();
}
