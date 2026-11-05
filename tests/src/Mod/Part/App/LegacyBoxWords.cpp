// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Writer.h>
#include <Mod/Part/App/FeaturePartBox.h>

#include <string>

#include <src/App/InitApplication.h>

/** A box stated in the words of a release that is twenty years old still opens as that box.
 *
 *  Those words -- sizes called `l`, `w` and `h`, a position called `x`, `y` and `z`, a direction
 *  called `Axis` and `Location` -- are this feature's own history, and only this feature can say
 *  what they mean. Reading the block they sit in is not: that is the reader's work, done once for
 *  every type, and the hand-written second reader this feature used to carry is gone.
 *
 *  These pin the meaning of the older words so the move could be made without changing it.
 */
class LegacyBoxWordsTest: public ::testing::Test
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

    static void writeArchive(const std::string& path, const std::string& properties, int count)
    {
        Base::ZipWriter writer(path.c_str());
        writer.putNextEntry("Document.xml");
        writer.Stream()
            << "<?xml version='1.0' encoding='utf-8'?>\n"
            << "<Document SchemaVersion=\"4\" ProgramVersion=\"0.7\" FileVersion=\"1\">\n"
            << "  <Properties Count=\"0\">\n"
            << "  </Properties>\n"
            << "  <Objects Count=\"1\">\n"
            << "    <Object type=\"Part::Box\" name=\"Box\" />\n"
            << "  </Objects>\n"
            << "  <ObjectData Count=\"1\">\n"
            << "    <Object name=\"Box\">\n"
            << "      <Properties Count=\"" << count << "\">\n"
            << properties << "      </Properties>\n"
            << "    </Object>\n"
            << "  </ObjectData>\n"
            << "</Document>\n";
        writer.writeFiles();
    }

    static std::string distance(const char* name, const char* value)
    {
        return std::string("        <Property name=\"") + name + "\" type=\"PropertyDistance\">\n"
            + "          <Float value=\"" + value + "\"/>\n" + "        </Property>\n";
    }

    static std::string vector(const char* name, const char* x, const char* y, const char* z)
    {
        return std::string("        <Property name=\"") + name
            + "\" type=\"App::PropertyVector\">\n" + "          <PropertyVector valueX=\"" + x
            + "\" valueY=\"" + y + "\" valueZ=\"" + z + "\"/>\n        </Property>\n";
    }

    Part::Box* openBox(const std::string& properties, int count)
    {
        const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
        writeArchive(path, properties, count);
        _doc = App::GetApplication().openDocument(path.c_str());
        EXPECT_NE(_doc, nullptr);
        return _doc == nullptr ? nullptr : dynamic_cast<Part::Box*>(_doc->getObject("Box"));
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The sizes of the oldest files, and the mix-up they were written with: what the file calls width
// is the height, and what it calls height is the width. A box that came back 3 x 5 instead of
// 5 x 3 would be a different part.
TEST_F(LegacyBoxWordsTest, theSizesOfTheOldestFilesKeepTheirMeaning)
{
    Part::Box* box = openBox(distance("l", "7.0") + distance("w", "3.0") + distance("h", "5.0"), 3);
    ASSERT_NE(box, nullptr);

    EXPECT_DOUBLE_EQ(box->Length.getValue(), 7.0);
    EXPECT_DOUBLE_EQ(box->Height.getValue(), 3.0) << "'w' is the height in these files";
    EXPECT_DOUBLE_EQ(box->Width.getValue(), 5.0) << "'h' is the width in these files";
}

// The names stayed; the type they were said in changed. A file that says "PropertyDistance"
// without its module is still saying a distance.
TEST_F(LegacyBoxWordsTest, theSizesSaidInTheOlderTypeKeepTheirMeaning)
{
    Part::Box* box = openBox(
        distance("Length", "7.0") + distance("Width", "3.0") + distance("Height", "5.0"),
        3
    );
    ASSERT_NE(box, nullptr);

    EXPECT_DOUBLE_EQ(box->Length.getValue(), 7.0);
    EXPECT_DOUBLE_EQ(box->Width.getValue(), 3.0)
        << "under their own names the sizes are not swapped";
    EXPECT_DOUBLE_EQ(box->Height.getValue(), 5.0);
}

// Where a 0.7 file put the box, before a placement said it.
TEST_F(LegacyBoxWordsTest, thePositionOfA07FileBecomesAPlacement)
{
    Part::Box* box = openBox(
        distance("l", "1.0") + distance("x", "2.0") + distance("y", "3.0") + distance("z", "4.0"),
        4
    );
    ASSERT_NE(box, nullptr);

    const Base::Vector3d at = box->Placement.getValue().getPosition();
    EXPECT_DOUBLE_EQ(at.x, 2.0);
    EXPECT_DOUBLE_EQ(at.y, 3.0);
    EXPECT_DOUBLE_EQ(at.z, 4.0);
    EXPECT_TRUE(box->Shape.testStatus(App::Property::User1))
        << "the shape was not marked to take its place from the placement";
}

// The direction a 0.8 file built the box along becomes the rotation of its placement.
TEST_F(LegacyBoxWordsTest, theDirectionOfA08FileBecomesARotation)
{
    Part::Box* box = openBox(
        distance("l", "1.0") + vector("Axis", "1.0", "0.0", "0.0")
            + vector("Location", "5.0", "0.0", "0.0"),
        3
    );
    ASSERT_NE(box, nullptr);

    const Base::Placement plm = box->Placement.getValue();
    EXPECT_DOUBLE_EQ(plm.getPosition().x, 5.0);
    // Built along Z, laid down onto X.
    Base::Vector3d up(0.0, 0.0, 1.0);
    plm.getRotation().multVec(up, up);
    EXPECT_NEAR(up.x, 1.0, 1e-9);
    EXPECT_NEAR(up.z, 0.0, 1e-9);
}

// An ordinary file is not touched by any of this.
TEST_F(LegacyBoxWordsTest, aBoxSaidInTodaysWordsIsReadByTheReaderAlone)
{
    Part::Box* box = openBox(
        std::string(
            "        <Property name=\"Length\" type=\"App::PropertyLength\">\n"
            "          <Float value=\"7.0\"/>\n"
            "        </Property>\n"
            "        <Property name=\"Width\" type=\"App::PropertyLength\">\n"
            "          <Float value=\"3.0\"/>\n"
            "        </Property>\n"
        ),
        2
    );
    ASSERT_NE(box, nullptr);

    EXPECT_DOUBLE_EQ(box->Length.getValue(), 7.0);
    EXPECT_DOUBLE_EQ(box->Width.getValue(), 3.0);
    EXPECT_FALSE(box->Shape.testStatus(App::Property::User1));
    EXPECT_TRUE(_doc->whatASaveWouldLose().empty()) << "an ordinary file was charged for saving";
}
