// SPDX-License-Identifier: LGPL-2.1-or-later
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Datums.h>
#include <App/Document.h>
#include <App/PlacementExtension.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>
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

TEST_F(DocumentByteStabilityTest, reopeningADocumentDoesNotChangeIt)
{
    // A document must also be a function of its content across a round trip: opening a file and
    // saving it, having designed nothing, must reproduce it. A rotation is where this failed --
    // the placement was stored as a quaternion AND as an axis and angle, and restoring rebuilt
    // it from the angle, through sine and cosine, landing a step off the value that was saved.
    // The next save then wrote the drifted value, and version control saw an edit nobody made.
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";

    // Named for the file it will live in: a reopened document takes its label from the file
    // name, so any other name would differ on the second save for a reason that is not drift.
    auto& app = App::GetApplication();
    App::Document* doc = app.newDocument(Base::FileInfo(path).fileNamePure().c_str(), "testUser");
    auto* plane = doc->addObject<App::Plane>("Datum");
    ASSERT_NE(plane, nullptr);

    // A quarter turn about X, stated as a quaternion -- the form the world frame's own planes
    // use. Stating it as an axis and an angle would not test anything: rebuilding it from the
    // angle it was built from returns the same value. The defect appears when the angle has to
    // be RECOVERED from a quaternion and the rotation rebuilt from that.
    plane->Placement.setValue(
        Base::Placement(Base::Vector3d(0, 0, 0), Base::Rotation(1.0, 0.0, 0.0, 1.0))
    );
    // Settle the document before saving: a property still marked touched is recorded as such,
    // and a reopened document is not touched, which is a difference of its own and not the one
    // under test here.
    doc->recompute();
    ASSERT_TRUE(doc->saveAs(path.c_str()));

    const std::string saved = readAll(path);
    const std::string docName = doc->getName();
    app.closeDocument(docName.c_str());

    App::Document* reopened = app.openDocument(path.c_str());
    ASSERT_NE(reopened, nullptr);
    const std::string reopenedName = reopened->getName();
    ASSERT_TRUE(reopened->save());

    EXPECT_FALSE(saved.empty());
    EXPECT_EQ(saved, readAll(path)) << "opening a document and saving it rewrote the file";

    app.closeDocument(reopenedName.c_str());
    Base::FileInfo(path).deleteFile();
}
