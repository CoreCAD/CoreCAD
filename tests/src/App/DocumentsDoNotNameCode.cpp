// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

#include <cstdlib>
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

/** A document names what it holds. It does not decide what code this session runs.
 *
 *  Cruth: every type name in a document -- the object's own, and the type of each property it
 *  states -- used to be handed to the import machinery before it was looked up, on the reasoning
 *  that a name this build does not know yet is a name whose module has not been loaded yet. That
 *  made the file the thing that chose what ran: opening a document imported whatever module its
 *  text named, in a fresh session, with nothing for the person to decline. Measured before this:
 *  a document arriving with Part, Sketcher and PartDesign in it pulled all three modules in for
 *  no reason other than that it said their names.
 *
 *  The name is now looked up among what is already registered, and every module the program
 *  installed is imported at startup instead (FreeCADInit.py for the App side, FreeCADGuiInit.py
 *  for the view providers). So the program decides what it runs, once, before any file is read.
 *
 *  A name this build genuinely has no type for is then what it always was -- a statement the
 *  build cannot honour -- and it is kept as written rather than dropped (Amendment 19).
 */
class DocumentsDoNotNameCodeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _dir = Base::FileInfo::getTempFileName();
        Base::FileInfo(_dir).createDirectory();
        writeAll(_dir + "/" + namedByADocument + ".py", aModuleThatLeavesAMark(ranMarker));
        writeAll(_dir + "/" + importedDirectly + ".py", aModuleThatLeavesAMark(proofMarker));

        Base::PyGILStateLocker lock;
        Base::Interpreter().runString(("import sys; sys.path.insert(0, r'" + _dir + "')").c_str());
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        unsetenv(ranMarker);
        unsetenv(proofMarker);
    }

    /// A module whose only effect is to say, durably, that it was imported.
    static std::string aModuleThatLeavesAMark(const char* mark)
    {
        return std::string("import os\nos.environ['") + mark + "'] = 'yes'\n";
    }

    static bool wasImported(const char* mark)
    {
        return getenv(mark) != nullptr;
    }

    /// A saved document, reopened with one of the type names in its text replaced by a name only
    /// this test's module could supply.
    App::Document* reopenedWithTheFileSaying(const std::string& was, const std::string& now)
    {
        auto& app = App::GetApplication();
        const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
        App::Document* doc = app.newDocument(app.getUniqueDocumentName("names").c_str(), "testUser");
        App::DocumentObject* obj = doc->addObject("App::VarSet", "Alpha");
        obj->addDynamicProperty("App::PropertyFloat", "Span", "Base");
        EXPECT_TRUE(doc->saveAs(path.c_str()));
        app.closeDocument(doc->getName());

        std::string text = readAll(path);
        const std::string::size_type at = text.find(was);
        EXPECT_NE(at, std::string::npos) << "the file does not state " << was << " to replace";
        if (at == std::string::npos) {
            return nullptr;
        }
        text.replace(at, was.size(), now);
        writeAll(path, text);

        _doc = app.openDocument(path.c_str());
        return _doc;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    static constexpr const char* namedByADocument = "cruthnamedbyadocument";
    static constexpr const char* importedDirectly = "cruthimporteddirectly";
    static constexpr const char* ranMarker = "CRUTH_TEST_A_DOCUMENT_CHOSE_A_MODULE";
    static constexpr const char* proofMarker = "CRUTH_TEST_THE_MARKER_MODULE_WORKS";
    App::Document* _doc {};
    std::string _dir;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The control on the control: a module of this shape really does leave a mark when it is
// imported, so an absent mark below means nothing was imported rather than nothing marks.
TEST_F(DocumentsDoNotNameCodeTest, theMarkerModuleMarks)
{
    ASSERT_FALSE(wasImported(proofMarker));
    {
        Base::PyGILStateLocker lock;
        Base::Interpreter().loadModule(importedDirectly);
    }
    EXPECT_TRUE(wasImported(proofMarker))
        << "a module of this shape marks nothing when imported, so these tests cannot tell";
}

// The object's own type. The document is read, the name is not found, and no module was fetched
// on the strength of it.
TEST_F(DocumentsDoNotNameCodeTest, anObjectTypeDoesNotImportItsModule)
{
    App::Document* doc = reopenedWithTheFileSaying(
        "type=\"App::VarSet\"",
        std::string("type=\"") + namedByADocument + "::Thing\""
    );
    ASSERT_NE(doc, nullptr);

    EXPECT_FALSE(wasImported(ranMarker))
        << "a document's text chose which module this session imported";
}

// The same name, kept rather than dropped: the point is that the read carries on and says what
// it could not honour, not that an unknown name is quietly thrown away.
TEST_F(DocumentsDoNotNameCodeTest, anObjectOfAnUnknownTypeIsKeptAsWritten)
{
    App::Document* doc = reopenedWithTheFileSaying(
        "type=\"App::VarSet\"",
        std::string("type=\"") + namedByADocument + "::Thing\""
    );
    ASSERT_NE(doc, nullptr);

    EXPECT_EQ(doc->getObject("Alpha"), nullptr) << "a type this build has no place for was built";
    EXPECT_TRUE(doc->holdsUnreadContent()) << "the object was dropped instead of kept";

    bool named = false;
    for (const auto& held : doc->heldStatements()) {
        named = named || held[1].find(namedByADocument) != std::string::npos
            || held[2].find(namedByADocument) != std::string::npos;
    }
    EXPECT_TRUE(named) << "what is held does not say which type could not be built";
}

// A property's type is a name in the file too, and it took the same path.
TEST_F(DocumentsDoNotNameCodeTest, aPropertyTypeDoesNotImportItsModule)
{
    // Named down to the one property: the document states properties of its own too, and a
    // plain search for the type would have spoiled one of those instead -- a property that
    // already exists, so nothing would ever have been declared and the test would have passed
    // either way. It did, until the control was run.
    App::Document* doc = reopenedWithTheFileSaying(
        "<Property name=\"Span\" type=\"App::PropertyFloat\"",
        std::string("<Property name=\"Span\" type=\"") + namedByADocument + "::PropertyThing\""
    );
    ASSERT_NE(doc, nullptr);

    EXPECT_FALSE(wasImported(ranMarker))
        << "a property type stated in a document chose which module this session imported";
}
