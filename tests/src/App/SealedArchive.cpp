// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyStandard.h>
#include <App/SealedArchive.h>

#include <zipios++/zipfile.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <src/App/InitApplication.h>

namespace fs = std::filesystem;

/** A document is handed over as one sealed archive, and read back from it.
 *
 *  Cruth (Amendment 18 Clause 18.1): a document's record is its recipe together with the source
 *  material the recipe names. Handing that over meant handing over a file plus the right entries
 *  out of a folder shared with sibling parts -- and being right about which ones. An export
 *  answers that question once, in one file, and is a RENDERING of the document: it carries what
 *  the recipe states and nothing the recipe produces.
 */
class SealedArchiveTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _project = fs::temp_directory_path()
            / ("cruth-archive-test-" + std::to_string(::getpid()) + "-" + std::to_string(++_serial));
        fs::create_directories(_project);
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        std::error_code ignored;
        fs::remove_all(_project, ignored);
    }

    /// A saved part, with one authored value and one that follows from it by formula.
    App::Document* aSavedPart()
    {
        auto& app = App::GetApplication();
        _doc = app.newDocument(app.getUniqueDocumentName("archived").c_str(), "testUser");
        App::DocumentObject* obj = _doc->addObject("App::VarSet", "Block");
        auto* span = freecad_cast<App::PropertyFloat*>(
            obj->addDynamicProperty("App::PropertyFloat", "Span", "Base")
        );
        span->setValue(30.0);
        App::DocumentObject* second = _doc->addObject("App::VarSet", "Pin");
        second->addDynamicProperty("App::PropertyFloat", "Reach", "Base");
        second->setExpression(
            App::ObjectIdentifier::parse(second, "Reach"),
            std::shared_ptr<App::Expression>(App::Expression::parse(second, "Block.Span / 3"))
        );
        // A solid, so that saving fills the rebuild store: a document with nothing to build gives
        // the archive nothing to wrongly pick up, and a check over one proves nothing.
        _doc->addObject("Part::Box", "Solid");
        _doc->recompute();
        EXPECT_TRUE(_doc->saveAs(partPath().c_str()));
        return _doc;
    }

    std::string partPath() const
    {
        return (_project / "part.FCStd").string();
    }

    std::string archivePath() const
    {
        return (_project / "part.sealed").string();
    }

    static std::set<std::string> membersOf(const std::string& archive)
    {
        std::set<std::string> names;
        zipios::ZipFile zip(archive);
        for (const zipios::ConstEntryPointer& entry : zip.entries()) {
            if (entry && entry->isValid()) {
                names.insert(entry->getName());
            }
        }
        return names;
    }

    fs::path _project;
    App::Document* _doc {nullptr};
    static int _serial;
};

int SealedArchiveTest::_serial = 0;

// The claim, end to end: what the writer produces is read back by the reader that opens documents,
// and the document that comes out is the document that went in. Verified through the reader rather
// than by inspecting the archive, because an export that only looks right is the failure this is
// for.
TEST_F(SealedArchiveTest, anExportIsReadBackAsTheDocumentItRendered)
{
    App::Document* source = aSavedPart();
    ASSERT_NE(source, nullptr);
    const std::set<std::string> unhonoured = App::writeSealedArchive(*source, archivePath());
    EXPECT_TRUE(unhonoured.empty()) << "an ordinary document was reported as carrying a statement "
                                       "this build could not honour";
    ASSERT_TRUE(fs::exists(fs::path(archivePath())));

    auto& app = App::GetApplication();
    app.closeDocument(_doc->getName());
    _doc = nullptr;

    App::Document* back = app.openDocument(archivePath().c_str());
    ASSERT_NE(back, nullptr);
    _doc = back;

    App::DocumentObject* block = back->getObject("Block");
    ASSERT_NE(block, nullptr) << "the archive did not give back the objects it was written from";
    auto* span = freecad_cast<App::PropertyFloat*>(block->getPropertyByName("Span"));
    ASSERT_NE(span, nullptr);
    EXPECT_DOUBLE_EQ(span->getValue(), 30.0);

    App::DocumentObject* pin = back->getObject("Pin");
    ASSERT_NE(pin, nullptr);
    back->recompute();
    auto* reach = freecad_cast<App::PropertyFloat*>(pin->getPropertyByName("Reach"));
    ASSERT_NE(reach, nullptr);
    EXPECT_DOUBLE_EQ(reach->getValue(), 10.0)
        << "the formula the archive carries did not rebuild the value it drives";
}

// An export is a rendering and never the record, so it carries nothing the recipe PRODUCES: the
// rebuild store is disposable by definition (Clause 18.5) and shipping it would hand the receiver
// results they must not trust. The archive holds the recipe and source material, and no third
// thing.
TEST_F(SealedArchiveTest, anArchiveCarriesNothingTheRecipeProduces)
{
    App::Document* source = aSavedPart();
    ASSERT_NE(source, nullptr);
    // Saved twice on purpose: the first save is what fills the rebuild store, so by here the
    // project folder holds built content for the archive to wrongly pick up.
    source->recompute();
    ASSERT_TRUE(source->saveAs(partPath().c_str()));
    ASSERT_TRUE(fs::is_directory(fs::path(source->cacheDirectory())))
        << "nothing was built, so this proves nothing about an archive that carries no build";

    App::writeSealedArchive(*source, archivePath());

    for (const std::string& member : membersOf(archivePath())) {
        const bool recipe = member == App::sealedArchiveRecipeEntry;
        const bool material = member.rfind(std::string(App::sealedArchiveAssetFolder) + "/", 0) == 0;
        EXPECT_TRUE(recipe || material)
            << "the archive carries something that is neither the recipe nor the source material "
               "it names: "
            << member;
    }
}

// Refused whole. A short archive that looks complete is worse than no archive at all: the receiver
// cannot tell that what is missing was ever there. So an export that cannot carry everything the
// recipe states writes no file, not even a partial one.
TEST_F(SealedArchiveTest, anExportThatCannotCarryWhatItNamesLeavesNoFile)
{
    auto& app = App::GetApplication();
    _doc = app.newDocument(app.getUniqueDocumentName("archived").c_str(), "testUser");
    App::DocumentObject* obj = _doc->addObject("App::VarSet", "Imported");
    obj->addDynamicProperty("App::PropertyFloat", "Span", "Base");
    ASSERT_TRUE(_doc->saveAs(partPath().c_str()));
    app.closeDocument(_doc->getName());
    _doc = nullptr;

    // The file states a piece of source material that this project does not hold -- what a part
    // separated from the folder its import was stored in looks like. The statement is kept as the
    // file words it (Amendment 19 Clause 19.1), so the recipe still names the material.
    std::string text;
    {
        std::ifstream in(partPath(), std::ios::in | std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string::size_type at = text.find("<Property name=\"Span\"");
    ASSERT_NE(at, std::string::npos);
    const std::string::size_type close = text.find('>', at);
    ASSERT_NE(close, std::string::npos);
    text.insert(close, " asset=\"0000000000000000000000000000000000000000\"");
    {
        std::ofstream out(partPath(), std::ios::out | std::ios::binary | std::ios::trunc);
        out << text;
    }

    _doc = app.openDocument(partPath().c_str());
    ASSERT_NE(_doc, nullptr);

    EXPECT_THROW(App::writeSealedArchive(*_doc, archivePath()), Base::Exception)
        << "an export that cannot carry what the recipe names was allowed through";
    EXPECT_FALSE(fs::exists(fs::path(archivePath()))) << "a refused export left an archive behind";
    EXPECT_FALSE(fs::exists(fs::path(archivePath() + ".writing")))
        << "a refused export left a partial file behind";
}
