// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ObjectRecipe.h>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class ObjectRecipeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _docName {};
    App::Document* _doc {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// A single object emits a node keyed by its durable Uid (never its positional name), carrying
// its type and its authored fields, while the kernel-derived Shape (Prop_Output) is excluded.
TEST_F(ObjectRecipeTest, emitsIdentityTypeAndAuthoredFieldsButNotOutput)
{
    // Arrange: a Part::Box carries authored dimensions and a computed Shape (Prop_Output).
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);

    // Act
    const RecipeNode node = emitObjectRecipe(*box);

    // Assert: identity is the durable Uid, not the in-document name.
    EXPECT_EQ(node.id, box->Uid.getValueStr());
    EXPECT_EQ(node.type, "Part::Box");

    // An authored dimension is emitted as a field...
    EXPECT_EQ(node.fields.count("Length"), 1u);
    EXPECT_FALSE(node.fields.at("Length").empty());

    // ...but the kernel-derived output is not.
    EXPECT_EQ(node.fields.count("Shape"), 0u);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
