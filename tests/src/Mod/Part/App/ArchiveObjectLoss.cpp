// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/FileInfo.h>
#include <Base/Writer.h>

#include <fstream>
#include <string>

#include <src/App/InitApplication.h>

/** An object whose own read stops part way through loses everything its file says after that.
 *
 *  Several feature types read their own properties with no handling of their own -- Part::Box is
 *  one, carrying a hand-written reader for the shapes its properties used to have. A value it
 *  cannot read ends the object's read where it stands, and the properties stated after it come
 *  back at their defaults. Nothing throws out of the load: the document opens, looks ordinary, and
 *  an ordinary save writes those defaults over what the file says (Amendment 19 Clause 19.3).
 *
 *  Found by opening such a file in the real program, not by a test: a document of App-layer
 *  objects alone cannot reach this path, because the shared property reader catches what they
 *  raise.
 */
class ArchiveObjectLossTest: public ::testing::Test
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

    static void writeArchive(const std::string& path, const std::string& documentXml)
    {
        Base::ZipWriter writer(path.c_str());
        writer.putNextEntry("Document.xml");
        writer.Stream() << documentXml;
        writer.writeFiles();
    }

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

TEST_F(ArchiveObjectLossTest, anObjectWhoseReadStoppedIsNamedAsACostOfSaving)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
    // The first size will not read back; the second is stated after it, and is reached only if
    // the object's read survives the first.
    writeArchive(
        path,
        "<?xml version='1.0' encoding='utf-8'?>\n"
        "<Document SchemaVersion=\"4\" ProgramVersion=\"0.21\" FileVersion=\"1\">\n"
        "  <Properties Count=\"0\">\n"
        "  </Properties>\n"
        "  <Objects Count=\"1\">\n"
        "    <Object type=\"Part::Box\" name=\"Box\" />\n"
        "  </Objects>\n"
        "  <ObjectData Count=\"1\">\n"
        "    <Object name=\"Box\">\n"
        "      <Properties Count=\"2\">\n"
        "        <Property name=\"Length\" type=\"App::PropertyLength\">\n"
        "          <Float/>\n"
        "        </Property>\n"
        "        <Property name=\"Width\" type=\"App::PropertyLength\">\n"
        "          <Float value=\"7.0\"/>\n"
        "        </Property>\n"
        "      </Properties>\n"
        "    </Object>\n"
        "  </ObjectData>\n"
        "</Document>\n"
    );

    _doc = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);
    App::DocumentObject* box = _doc->getObject("Box");
    ASSERT_NE(box, nullptr) << "the whole read stopped instead of only this object's";
    ASSERT_FALSE(_doc->testStatus(App::Document::RestoreError))
        << "this loss is supposed to be the one that does NOT throw out of the load";

    const std::vector<std::string> losing = _doc->whatASaveWouldLose();
    ASSERT_FALSE(losing.empty()) << "a save that would drop stated sizes said it would cost "
                                    "nothing";
    EXPECT_NE(losing.front().find("Box"), std::string::npos)
        << "the cost did not name the object it belongs to: " << losing.front();
    EXPECT_FALSE(_doc->holdsUnreadContent() == false) << "the document called itself whole";

    const std::string before = readAll(path);
    EXPECT_FALSE(_doc->save()) << "sizes stated in the file were written away without a word";
    EXPECT_EQ(readAll(path), before) << "a refused save changed the file on disk";
}
