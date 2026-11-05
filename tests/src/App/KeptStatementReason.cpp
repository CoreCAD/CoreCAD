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
}  // namespace

/** A kept statement reports the failure that actually happened.
 *
 *  Cruth (Amendment 19 Clause 19.4): the discard is a person's act, and the reason is most of
 *  what they have to decide on. Two different failures keep a property block exactly as the file
 *  worded it -- a name this build has no property for, and a value this build could not read for
 *  a property it does have -- and they send a person to two different places: one to look for an
 *  add-on that is missing, the other to a value they can simply retype. One sentence for both
 *  makes the report a guess presented as a fact (§3.6).
 */
class KeptStatementReasonTest: public ::testing::Test
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

    /// A saved document whose one object states a value for a property this build DOES have, in
    /// words this build cannot turn into a value.
    App::Document* statingAValueItCannotRead(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("reason").c_str(), "testUser");
        App::DocumentObject* obj = doc->addObject("App::VarSet", "Alpha");
        obj->addDynamicProperty("App::PropertyFloat", "Span", "Base");
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        app.closeDocument(doc->getName());

        std::string text = readAll(path);
        const std::string::size_type at = text.find("name=\"Span\"");
        EXPECT_NE(at, std::string::npos) << "the file does not state the property to spoil";
        const std::string::size_type value = text.find("<Float value=\"", at);
        EXPECT_NE(value, std::string::npos);
        const std::string::size_type from = value + std::string("<Float value=\"").size();
        const std::string::size_type to = text.find('"', from);
        text.replace(from, to - from, "banana");
        writeAll(path, text);

        _doc = app.openDocument(path.c_str());
        _doc->setUndoMode(1);
        return _doc;
    }

    /// A saved document whose one object states a property of a type this build does not have.
    App::Document* statingAPropertyItHasNoPlaceFor(const std::string& path)
    {
        auto& app = App::GetApplication();
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("reason").c_str(), "testUser");
        doc->addObject("App::VarSet", "Alpha");
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        app.closeDocument(doc->getName());

        std::string text = readAll(path);
        const std::string::size_type at = text.find("<Properties>")
            + std::string("<Properties>").size();
        text.insert(
            at,
            "\n<Property name=\"Sparkle\" type=\"Addon::PropertyGlitter\" dynamic=\"1\">\n"
            "<Glitter value=\"lots\"/>\n</Property>"
        );
        writeAll(path, text);

        _doc = app.openDocument(path.c_str());
        _doc->setUndoMode(1);
        return _doc;
    }

    /// What the document says it is holding for one property.
    std::string reasonFor(const App::Document* doc, const std::string& name) const
    {
        for (const auto& held : doc->heldStatements()) {
            if (held[1] == name) {
                return held[2];
            }
        }
        return {};
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

// The failure that happened, not the other one. `Span` is a property this build has; what it
// could not honour was the value stated for it, and a person told otherwise goes looking for an
// add-on that was never missing.
TEST_F(KeptStatementReasonTest, aValueThatCouldNotBeReadSaysThatIsWhatFailed)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = statingAValueItCannotRead(path);
    ASSERT_NE(doc, nullptr);

    const std::string why = reasonFor(doc, "Span");
    ASSERT_FALSE(why.empty()) << "the document is not holding the statement at all";
    EXPECT_NE(why.find("could not read the value"), std::string::npos)
        << "the reason does not say the value was the trouble: " << why;
    EXPECT_EQ(why.find("no property of that name"), std::string::npos)
        << "the reason blames a property this build has: " << why;
}

// The other failure, unchanged: this one really is a name this build has no place for, and the
// sentence that was wrong above is right here. Without this the fix could have replaced one
// fixed sentence with another.
TEST_F(KeptStatementReasonTest, aPropertyThisBuildHasNoPlaceForStillSaysThat)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = statingAPropertyItHasNoPlaceFor(path);
    ASSERT_NE(doc, nullptr);

    const std::string why = reasonFor(doc, "Sparkle");
    ASSERT_FALSE(why.empty()) << "the document is not holding the statement at all";
    EXPECT_NE(why.find("no property of that name"), std::string::npos)
        << "the reason no longer names the failure it did name: " << why;
}

// The reason is part of what is kept, so undoing a discard brings back the statement as it was
// rather than a statement with the wrong reason attached (Clause 19.1, §10.6).
TEST_F(KeptStatementReasonTest, undoingADiscardBringsTheReasonBackWithTheWords)
{
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    App::Document* doc = statingAValueItCannotRead(path);
    ASSERT_NE(doc, nullptr);
    const std::string before = reasonFor(doc, "Span");
    ASSERT_FALSE(before.empty());

    ASSERT_TRUE(doc->discardStatement(doc->getObject("Alpha"), "Span"));
    ASSERT_TRUE(reasonFor(doc, "Span").empty()) << "the discard left the statement held";

    doc->undo();
    EXPECT_EQ(reasonFor(doc, "Span"), before)
        << "undo brought the words back under a different reason";
}
