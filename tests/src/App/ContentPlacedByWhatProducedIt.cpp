// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeometryCache.h>
#include <App/PropertyFile.h>
#include <Base/FileInfo.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <src/App/InitApplication.h>

namespace fs = std::filesystem;

namespace
{
void writeFile(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

namespace Tests
{
/// An object that was HANDED its content: a file a person chose, which nothing in the document
/// produces. It says nothing, because saying nothing is saying that.
class HandedAFile: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Tests::HandedAFile);

public:
    HandedAFile()
    {
        ADD_PROPERTY_TYPE(Content, (nullptr), "Base", App::Prop_None, "A handed file");
    }

    App::PropertyFileIncluded Content;
};

/// An object that PRODUCES its content, in a property of exactly the same kind. This is the pair
/// the clause is about: the property cannot tell these two apart, and the object can.
class ProducedAFile: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(Tests::ProducedAFile);

public:
    ProducedAFile()
    {
        ADD_PROPERTY_TYPE(Content, (nullptr), "Base", App::Prop_None, "A produced file");
    }

    App::PropertyFileIncluded Content;

    bool producesContentOf(const App::Property& prop) const override
    {
        return &prop == &Content;
    }
};

}  // namespace Tests

PROPERTY_SOURCE(Tests::HandedAFile, App::DocumentObject)    // NOLINT
PROPERTY_SOURCE(Tests::ProducedAFile, App::DocumentObject)  // NOLINT

/** Where content lives is decided by what produced it, and the OBJECT answers.
 *
 *  Cruth (Amendment 18 Clause 18.2): authored content too bulky for a file meant to be read goes
 *  to the source store, which is visible, versioned and kept; content the document's own recipe
 *  produces goes to the rebuild store, which is hidden and may be deleted at any moment. The
 *  question that separates them is *what produced it*, and it is asked of the object that holds
 *  the content, never of the property that carries it.
 *
 *  It used to be asked of the property, by a type test: only a `PropertyGeometry` could be
 *  output, so every other kind of bulk was authored content whatever produced it. Measured: a
 *  machine toolpath -- produced by the job above it and reproducible from it -- was written into
 *  the source store, kept for ever beside the imported bodies a person chose.
 *
 *  The two objects below carry properties of exactly the same kind and mean different things by
 *  them. That is the whole case: no test of the property's class can tell them apart, because the
 *  difference is not in the property.
 */
class ContentPlacedByWhatProducedItTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        Tests::HandedAFile::init();
        Tests::ProducedAFile::init();
    }

    void SetUp() override
    {
        _folder = fs::path(Base::FileInfo::getTempFileName()) / "project";
        fs::create_directories(_folder);
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        std::error_code ignored;
        fs::remove_all(_folder.parent_path(), ignored);
    }

    /// One document holding both objects, each given the same kind of content, saved.
    void aPartHoldingBoth(const std::string& handed, const std::string& produced)
    {
        auto& app = App::GetApplication();
        _doc = app.newDocument(app.getUniqueDocumentName("placed").c_str(), "testUser");

        const fs::path handedIn = _folder.parent_path() / "handed-source.txt";
        writeFile(handedIn, handed);
        auto* source = _doc->addObject<Tests::HandedAFile>("Source");
        source->Content.setValue(handedIn.string().c_str());

        const fs::path built = _folder.parent_path() / "produced-source.txt";
        writeFile(built, produced);
        auto* output = _doc->addObject<Tests::ProducedAFile>("Output");
        output->Content.setValue(built.string().c_str());

        _doc->recompute();
        ASSERT_TRUE(_doc->saveAs((_folder / "placed.cpart").string().c_str()))
            << "the part did not save, so there is nothing placed anywhere";
    }

    /// Everything the source store holds, read as text so a test can say which is which.
    std::vector<std::string> whatTheSourceStoreHolds() const
    {
        std::vector<std::string> held;
        std::error_code failed;
        for (const auto& entry : fs::recursive_directory_iterator(_folder / "assets", failed)) {
            if (entry.is_regular_file() && entry.path().extension() != ".xml") {
                held.push_back(readAll(entry.path()));
            }
        }
        return held;
    }

    /// Everything the rebuild store holds, the same way.
    std::vector<std::string> whatTheRebuildStoreHolds() const
    {
        std::vector<std::string> held;
        std::error_code failed;
        const fs::path built = App::builtGeometryFolder(_doc->cacheDirectory());
        for (const auto& entry : fs::recursive_directory_iterator(built, failed)) {
            if (entry.is_regular_file()) {
                held.push_back(readAll(entry.path()));
            }
        }
        return held;
    }

    std::string theRecipe() const
    {
        return readAll(_folder / "placed.cpart");
    }

    /// One object's block, from its opening element to its close. Found by name rather than by
    /// position, because the file states objects in durable-id order and a uuid is random.
    std::string theBlockFor(const std::string& name) const
    {
        const std::string recipe = theRecipe();
        const std::string opening = "name=\"" + name + "\">";
        const std::string::size_type at = recipe.find(opening);
        if (at == std::string::npos) {
            return {};
        }
        const std::string::size_type from = recipe.rfind("<Object ", at);
        const std::string::size_type to = recipe.find("</Object>", at);
        if (from == std::string::npos || to == std::string::npos) {
            return {};
        }
        return recipe.substr(from, to - from);
    }

    static bool somethingMentions(const std::vector<std::string>& held, const std::string& text)
    {
        for (const std::string& one : held) {
            if (one.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path _folder;
    App::Document* _doc {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The control, and the direction that must never break: content nobody produced stays in the
// source store, which is versioned and kept. Putting it anywhere disposable loses a design.
TEST_F(ContentPlacedByWhatProducedItTest, contentTheDocumentWasHandedGoesToTheSourceStore)
{
    ASSERT_NO_FATAL_FAILURE(aPartHoldingBoth("a file a person chose", "a file a step produced"));

    EXPECT_TRUE(somethingMentions(whatTheSourceStoreHolds(), "a file a person chose"))
        << "authored content is not in the store that keeps it";
    EXPECT_FALSE(somethingMentions(whatTheRebuildStoreHolds(), "a file a person chose"))
        << "authored content was put somewhere it can be deleted without loss";
}

// The fix: the same kind of property, on an object that says it produced the content, is placed
// as output. Before this the property's class decided, so both went to the source store.
TEST_F(ContentPlacedByWhatProducedItTest, contentTheObjectProducedGoesToTheRebuildStore)
{
    ASSERT_NO_FATAL_FAILURE(aPartHoldingBoth("a file a person chose", "a file a step produced"));

    EXPECT_TRUE(somethingMentions(whatTheRebuildStoreHolds(), "a file a step produced"))
        << "produced content was not kept where produced content is kept";
    EXPECT_FALSE(somethingMentions(whatTheSourceStoreHolds(), "a file a step produced"))
        << "produced content is being kept for ever as though a person had authored it";
}

// And the recipe says the same thing the stores do. A file of record that named produced content
// as its source material would send a later reader looking for an author who never existed.
TEST_F(ContentPlacedByWhatProducedItTest, theRecipeNamesTheSourceMaterialAndNotTheOutput)
{
    ASSERT_NO_FATAL_FAILURE(aPartHoldingBoth("a file a person chose", "a file a step produced"));

    const std::string source = theBlockFor("Source");
    const std::string output = theBlockFor("Output");
    ASSERT_FALSE(source.empty()) << "the recipe does not state the object at all";
    ASSERT_FALSE(output.empty()) << "the recipe does not state the object at all";

    EXPECT_NE(source.find("asset=\""), std::string::npos)
        << "the recipe does not name the source material it was saved with";
    EXPECT_EQ(output.find("asset=\""), std::string::npos)
        << "the recipe names produced content as source material";
}
