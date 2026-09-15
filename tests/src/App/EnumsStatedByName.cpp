// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <src/App/InitApplication.h>

namespace
{
std::string readAll(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    out << text;
}

/// What one property says about itself, in the words the file would carry.
std::string statedForm(const App::Property& prop)
{
    Base::StringWriter writer;
    prop.Save(writer);
    return writer.getString();
}

/// Read one property back from words a file could have stated.
void restoreFrom(App::Property& prop, const std::string& words)
{
    std::string text = "<?xml version='1.0' encoding='utf-8'?>\n";
    text.append("<Property name='Choice' type='App::PropertyEnumeration'>\n");
    text.append(words);
    text.append("</Property>\n");
    std::stringstream data(text);
    Base::XMLReader reader("Document.xml", data);
    // Positioned the way the document reader leaves it: on the property's own element, with the
    // value it states still to be read.
    reader.readElement("Property");
    prop.Restore(reader);
}
}  // namespace

/** An enumeration states the value that was chosen, by name.
 *
 *  Cruth: the values an enumeration offers live in C++ source, so a stored position is a reference
 *  into a list that is free to move. Insert one value near the front and every file written before
 *  that day goes on stating the same number while meaning a different value -- silently, in every
 *  document ever saved, with nothing for a person or a merge to notice it by. A name cannot drift
 *  that way. It also says what was chosen to anyone reading the file, which is the whole reason
 *  the document is a text a person can read.
 */
class EnumsStatedByNameTest: public ::testing::Test
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

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The name, in the file, where a person reading it can see what was chosen.
TEST_F(EnumsStatedByNameTest, theChosenValueIsStatedByName)
{
    static const char* colours[] = {"Red", "Green", "Blue", nullptr};
    App::PropertyEnumeration choice;
    choice.setEnums(colours);
    choice.setValue("Green");

    const std::string words = statedForm(choice);
    EXPECT_NE(words.find("value=\"Green\""), std::string::npos)
        << "the file does not name the value that was chosen: " << words;
    EXPECT_EQ(words.find("<Integer"), std::string::npos)
        << "the file still states a position in the list: " << words;
}

// The defect this form exists to remove: a list that gained a value is a list whose positions have
// all moved, and a file read positionally would come back meaning something nobody authored.
TEST_F(EnumsStatedByNameTest, aValueIsReadFromItsNameNotItsPositionInTheList)
{
    static const char* before[] = {"Red", "Green", "Blue", nullptr};
    App::PropertyEnumeration written;
    written.setEnums(before);
    written.setValue("Blue");
    const std::string words = statedForm(written);

    // The same build, one release later: a value was inserted, so "Blue" is no longer third.
    static const char* after[] = {"Red", "Amber", "Green", "Blue", nullptr};
    App::PropertyEnumeration read;
    read.setEnums(after);
    restoreFrom(read, words);

    EXPECT_STREQ(read.getValueAsString(), "Blue")
        << "the value moved when the list did, which is the failure the name prevents";
}

// A document written before the form changed states a position. Reading it positionally is the
// only way an index can be read, and the next save states it by name.
TEST_F(EnumsStatedByNameTest, aPositionStatedByAnOlderFileIsStillRead)
{
    static const char* colours[] = {"Red", "Green", "Blue", nullptr};
    App::PropertyEnumeration read;
    read.setEnums(colours);
    restoreFrom(read, "<Integer value=\"1\"/>\n");

    EXPECT_STREQ(read.getValueAsString(), "Green") << "an upstream document no longer opens";
    EXPECT_NE(statedForm(read).find("value=\"Green\""), std::string::npos)
        << "what was read positionally was not stated by name on the way back out";
}

// Some lists are not fixed: a hole's thread class is built from its thread type, so the values a
// property offers can depend on another property of the same object -- one the file may state
// further down. A name is therefore measured against the list the property ends up offering.
TEST_F(EnumsStatedByNameTest, aNameNoListYetOffersWaitsForOneThatDoes)
{
    static const char* provisional[] = {"None", nullptr};
    App::PropertyEnumeration choice;
    choice.setEnums(provisional);
    restoreFrom(choice, "<Enum value=\"Fine\"/>\n");

    EXPECT_FALSE(choice.nameAwaitingItsList().empty())
        << "a name nothing yet offers was not held for the list that would";

    static const char* settled[] = {"Coarse", "Fine", nullptr};
    choice.setEnums(settled);

    EXPECT_STREQ(choice.getValueAsString(), "Fine")
        << "the name was not honoured once a list arrived that offers it";
    EXPECT_TRUE(choice.nameAwaitingItsList().empty()) << "the name was still being held";
}

// A custom enumeration carries the values it offers, so its choice is read against those.
TEST_F(EnumsStatedByNameTest, aCustomListAndItsChoiceBothSurviveTheRoundTrip)
{
    App::PropertyEnumeration choice;
    choice.setEnums(std::vector<std::string> {"Alpha", "Beta", "Gamma"});
    choice.setValue("Gamma");

    App::PropertyEnumeration read;
    restoreFrom(read, statedForm(choice));

    EXPECT_STREQ(read.getValueAsString(), "Gamma");
    EXPECT_EQ(read.getEnumVector(), (std::vector<std::string> {"Alpha", "Beta", "Gamma"}));
}

// A value nothing here offers is a statement this build cannot honour. Stepping over it is how a
// document comes back quietly meaning something its author never chose, so the file's own words
// are kept, the document says it is not whole, and the next save states them again unchanged.
TEST_F(EnumsStatedByNameTest, aValueThisBuildDoesNotOfferIsKeptAndTheDocumentSaysSo)
{
    auto& app = App::GetApplication();
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";

    App::Document* doc = app.newDocument(app.getUniqueDocumentName("enum").c_str(), "testUser");
    doc->addObject("App::FeatureTest", "Thing");
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    app.closeDocument(doc->getName());

    std::string text = readAll(path);
    const std::string::size_type at = text.find("<Enum value=\"Four\"/>");
    ASSERT_NE(at, std::string::npos) << "the file does not state the value by name";
    text.replace(at, std::string("<Enum value=\"Four\"/>").size(), "<Enum value=\"Eleven\"/>");
    writeAll(path, text);

    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr) << "a value this build cannot honour stopped the document opening";
    EXPECT_FALSE(_doc->isWhole()) << "the document reported itself whole while holding a value "
                                     "it could not honour";

    const std::vector<std::array<std::string, 3>> held = _doc->heldStatements();
    ASSERT_EQ(held.size(), 1U);
    EXPECT_EQ(held.front()[0], "Thing");
    EXPECT_EQ(held.front()[1], "Enum");

    const std::string resaved = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(resaved.c_str()));
    EXPECT_NE(readAll(resaved).find("<Enum value=\"Eleven\"/>"), std::string::npos)
        << "the re-save wrote this build's reading over what the file stated";
}
