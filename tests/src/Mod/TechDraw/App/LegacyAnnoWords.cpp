// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Writer.h>
#include <Mod/TechDraw/App/DrawRichAnno.h>

#include <string>

#include <src/App/InitApplication.h>

/** What a file written before the annotation rework does not say still means something.
 *
 *  Those annotations sat on their origin, and the property that says so was only added later. The
 *  feature used to answer that question by reading the whole property block itself and watching
 *  for the name to go by -- a second reader, and a poorer one: no check that a value is stated in
 *  the type it is declared as, no dynamic properties, no extensions, and nothing said when a value
 *  would not read back. The question it was asked is answered here instead, and the reading is
 *  left to the reader.
 */
class LegacyAnnoWordsTest: public ::testing::Test
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

    TechDraw::DrawRichAnno* openAnno(const std::string& properties, int count)
    {
        const std::string path = Base::FileInfo::getTempFileName() + ".FCStd";
        {
            Base::ZipWriter writer(path.c_str());
            writer.putNextEntry("Document.xml");
            writer.Stream()
                << "<?xml version='1.0' encoding='utf-8'?>\n"
                << "<Document SchemaVersion=\"4\" ProgramVersion=\"1.0\" FileVersion=\"1\">\n"
                << "  <Properties Count=\"0\">\n"
                << "  </Properties>\n"
                << "  <Objects Count=\"1\">\n"
                << "    <Object type=\"TechDraw::DrawRichAnno\" name=\"Anno\" />\n"
                << "  </Objects>\n"
                << "  <ObjectData Count=\"1\">\n"
                << "    <Object name=\"Anno\">\n"
                << "      <Properties Count=\"" << count << "\">\n"
                << properties << "      </Properties>\n"
                << "    </Object>\n"
                << "  </ObjectData>\n"
                << "</Document>\n";
            writer.writeFiles();
        }
        _doc = App::GetApplication().openDocument(path.c_str());
        EXPECT_NE(_doc, nullptr);
        return _doc == nullptr ? nullptr
                               : dynamic_cast<TechDraw::DrawRichAnno*>(_doc->getObject("Anno"));
    }

    static std::string text(const char* value)
    {
        return std::string("        <Property name=\"AnnoText\" type=\"App::PropertyString\">\n")
            + "          <String value=\"" + value + "\"/>\n        </Property>\n";
    }

    static std::string centred(const char* value)
    {
        return std::string(
                   "        <Property name=\"OriginCentered\" "
                   "type=\"App::PropertyBool\">\n"
               )
            + "          <Bool value=\"" + value + "\"/>\n        </Property>\n";
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The silence of an older file means centred.
TEST_F(LegacyAnnoWordsTest, aFileThatSaysNothingLeavesTheAnnotationOnItsOrigin)
{
    TechDraw::DrawRichAnno* anno = openAnno(text("hello"), 1);
    ASSERT_NE(anno, nullptr);

    EXPECT_TRUE(anno->OriginCentered.getValue())
        << "an annotation from before the rework moved off its origin";
    EXPECT_EQ(anno->AnnoText.getStrValue(), "hello") << "the rest of the block was not read";
}

// A file that states it says so itself, either way, and is not overruled by the older meaning.
TEST_F(LegacyAnnoWordsTest, aFileThatStatesItIsTakenAtItsWord)
{
    TechDraw::DrawRichAnno* anno = openAnno(text("hello") + centred("false"), 2);
    ASSERT_NE(anno, nullptr);
    EXPECT_FALSE(anno->OriginCentered.getValue()) << "a file stating 'false' was overruled";
    App::GetApplication().closeDocument(_doc->getName());
    _doc = nullptr;

    anno = openAnno(text("hello") + centred("true"), 2);
    ASSERT_NE(anno, nullptr);
    EXPECT_TRUE(anno->OriginCentered.getValue());
}

// A new annotation is not an old one: the older meaning belongs to reading a file, and nowhere
// else.
TEST_F(LegacyAnnoWordsTest, aNewAnnotationIsNotCentred)
{
    auto& app = App::GetApplication();
    _doc = app.newDocument(app.getUniqueDocumentName("anno").c_str(), "testUser");
    auto* anno = dynamic_cast<TechDraw::DrawRichAnno*>(
        _doc->addObject("TechDraw::DrawRichAnno", "Anno")
    );
    ASSERT_NE(anno, nullptr);
    EXPECT_FALSE(anno->OriginCentered.getValue());
}

// What the second reader could not do: a value stated in a type this build does not hold it in is
// not forced into the property, and the loss is named rather than passed over.
TEST_F(LegacyAnnoWordsTest, aValueStatedInTheWrongTypeIsNotForcedIntoTheProperty)
{
    TechDraw::DrawRichAnno* anno = openAnno(
        text("hello")
            + "        <Property name=\"MaxWidth\" type=\"App::PropertyString\">\n"
              "          <String value=\"not a width\"/>\n        </Property>\n",
        2
    );
    ASSERT_NE(anno, nullptr);
    EXPECT_DOUBLE_EQ(anno->MaxWidth.getValue(), -1.0)
        << "a string was read into a width because nobody checked the type";
}
