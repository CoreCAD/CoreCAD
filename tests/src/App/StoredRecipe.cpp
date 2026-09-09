// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/StoredRecipe.h>

#include <sstream>
#include <string>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class StoredRecipeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _sourceName = App::GetApplication().getUniqueDocumentName("stored");
        _source = App::GetApplication().newDocument(_sourceName.c_str(), "testUser");
        _rebuiltName = App::GetApplication().getUniqueDocumentName("rebuilt");
        _rebuilt = App::GetApplication().newDocument(_rebuiltName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_rebuiltName.c_str());
        if (_source != nullptr) {
            App::GetApplication().closeDocument(_sourceName.c_str());
        }
    }

    /// The objects half of a written recipe -- the half that must not depend on creation order.
    /// The document half carries identity, which two live documents may never share.
    static std::string objectsSection(const std::string& written)
    {
        const std::string::size_type start = written.find("<Objects");
        return start == std::string::npos ? written : written.substr(start);
    }

    /// Write the source document's stored recipe and read it back into the empty one, which is
    /// the whole claim being tested: the file returns the document, not a readable summary of it.
    void roundTrip()
    {
        std::istringstream text(formatStoredRecipe(*_source));
        restoreStoredRecipe(*_rebuilt, text);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _sourceName {};
    std::string _rebuiltName {};
    App::Document* _source {};
    App::Document* _rebuilt {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The spine: an object comes back with its type, its in-document name, its durable identity and
// its authored values -- and the geometry it computed does not come back at all, because a shape
// is rebuilt from the recipe rather than stored in it.
TEST_F(StoredRecipeTest, anObjectReturnsWithItsIdentityTypeAndAuthoredValues)
{
    // Arrange
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    box->Label.setValue("Bearing block");
    static_cast<PropertyLength*>(box->getPropertyByName("Length"))->setValue(40.0);
    static_cast<PropertyLength*>(box->getPropertyByName("Height"))->setValue(12.5);
    _source->recompute();

    // Act
    roundTrip();

    // Assert
    DocumentObject* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->getTypeId().getName(), std::string("Part::Box"));
    EXPECT_EQ(rebuilt->Uid.getValueStr(), box->Uid.getValueStr());
    EXPECT_EQ(std::string(rebuilt->Label.getValue()), std::string("Bearing block"));
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(rebuilt->getPropertyByName("Length"))->getValue(),
        40.0
    );
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(rebuilt->getPropertyByName("Height"))->getValue(),
        12.5
    );
}

// A value has to return EXACTLY, not to the four decimals a person reads. The readable view
// rounds an outcome on purpose; a file of record that did the same would quietly redesign the
// part every time it was read.
TEST_F(StoredRecipeTest, aValueReturnsExactlyNotRounded)
{
    // Arrange: a length no rounded rendering could reproduce.
    constexpr double awkward = 12.345678901234567;
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    static_cast<PropertyLength*>(box->getPropertyByName("Length"))->setValue(awkward);

    // Act
    roundTrip();

    // Assert: the same double, bit for bit.
    auto* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(static_cast<PropertyLength*>(rebuilt->getPropertyByName("Length"))->getValue(), awkward);
}

// A property added at runtime carries its declaration, or the value would have nowhere to land.
TEST_F(StoredRecipeTest, aPropertyAddedAtRuntimeIsDeclaredAndReturns)
{
    // Arrange
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    auto* note = static_cast<PropertyString*>(
        box->addDynamicProperty("App::PropertyString", "Supplier", "Sourcing", "who makes it")
    );
    ASSERT_NE(note, nullptr);
    note->setValue("Cruth Engineering");

    // Act
    roundTrip();

    // Assert
    auto* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    auto* returned = rebuilt->getPropertyByName("Supplier");
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(
        std::string(static_cast<PropertyString*>(returned)->getValue()),
        std::string("Cruth Engineering")
    );
    EXPECT_EQ(std::string(rebuilt->getPropertyGroup("Supplier")), std::string("Sourcing"));
}

// The document's own authored facts are part of the recipe. The readable view walks objects only,
// so a document's comment, its author and its identity had nowhere to go at all.
//
// The source document is closed before the file is read back, because that is the real case -- a
// file becomes a document when it is opened -- and because two documents open at once may not
// share one identity: the document model mints a new uuid for the second, on purpose.
TEST_F(StoredRecipeTest, theDocumentsOwnFactsAreCarried)
{
    // Arrange
    _source->Comment.setValue("first cut of the housing");
    _source->CreatedBy.setValue("S. Barton");
    const std::string written = formatStoredRecipe(*_source);
    const std::string identity = _source->Uid.getValueStr();
    App::GetApplication().closeDocument(_sourceName.c_str());
    _source = nullptr;

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert
    EXPECT_EQ(std::string(_rebuilt->Comment.getValue()), std::string("first cut of the housing"));
    EXPECT_EQ(std::string(_rebuilt->CreatedBy.getValue()), std::string("S. Barton"));
    EXPECT_EQ(_rebuilt->Uid.getValueStr(), identity);
}

// Objects are written in durable-id order, so the file does not depend on the order they were
// created in. Two documents holding the same design must produce the same file whichever object
// was made first -- otherwise a reordered session reads as a rewritten document.
TEST_F(StoredRecipeTest, theFileDoesNotDependOnCreationOrder)
{
    // Arrange: the same two objects, created in opposite orders, given matching identities.
    auto* first = _source->addObject("Part::Box", "First");
    auto* second = _source->addObject("Part::Box", "Second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    auto* laterSecond = _rebuilt->addObject("Part::Box", "Second");
    auto* laterFirst = _rebuilt->addObject("Part::Box", "First");
    ASSERT_NE(laterSecond, nullptr);
    ASSERT_NE(laterFirst, nullptr);
    laterFirst->Uid.setValue(first->Uid.getValueStr());
    laterSecond->Uid.setValue(second->Uid.getValueStr());

    // Act
    const std::string written = formatStoredRecipe(*_source);
    const std::string writtenInReverse = formatStoredRecipe(*_rebuilt);

    // Assert
    EXPECT_EQ(objectsSection(written), objectsSection(writtenInReverse));
}

// What the form cannot carry, it names. A reference between objects is not stored yet -- and the
// file has to say so, because a gap nobody can see is indistinguishable from a value that was
// never there.
TEST_F(StoredRecipeTest, contentItCannotCarryIsNamedInTheFile)
{
    // Arrange: a link, and a shape the recipe never stores because it is rebuilt.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->addDynamicProperty("App::PropertyLink", "Origin"), nullptr);

    // Act
    const std::string written = formatStoredRecipe(*_source);

    // Assert: the link is named as unrecorded, with the reason.
    EXPECT_NE(
        written.find(
            "<Property name=\"Origin\" type=\"App::PropertyLink\" "
            "reason=\"reference\"/>"
        ),
        std::string::npos
    );
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
