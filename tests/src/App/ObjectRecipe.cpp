// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ObjectRecipe.h>
#include <Base/Interpreter.h>

#include <set>

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

// An object's links to other objects are emitted as refs addressing each target by its durable
// Uid (never by name), so a downstream reference survives a rename of what it points at.
TEST_F(ObjectRecipeTest, linksBecomeRefsByTargetUid)
{
    // Arrange: a Part::Cut whose Base and Tool link two boxes.
    auto* base = _doc->addObject("Part::Box");
    auto* tool = _doc->addObject("Part::Box");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(tool, nullptr);

    std::string cmd = "import FreeCAD as App\n";
    cmd += "cut = App.ActiveDocument.addObject('Part::Cut', 'TheCut')\n";
    cmd += "cut.Base = App.ActiveDocument.getObject('" + std::string(base->getNameInDocument())
        + "')\n";
    cmd += "cut.Tool = App.ActiveDocument.getObject('" + std::string(tool->getNameInDocument())
        + "')\n";
    Base::Interpreter().runString(cmd.c_str());

    auto* cut = _doc->getObject("TheCut");
    ASSERT_NE(cut, nullptr);

    // Act
    const RecipeNode node = emitObjectRecipe(*cut);

    // Assert: both operands appear as refs, addressed by their durable Uid, whole-object (pos 0).
    std::set<std::string> refTargets;
    for (const auto& ref : node.refs) {
        refTargets.insert(ref.target);
        EXPECT_EQ(ref.pos, 0);
    }
    EXPECT_EQ(refTargets.count(base->Uid.getValueStr()), 1u);
    EXPECT_EQ(refTargets.count(tool->Uid.getValueStr()), 1u);
}

// When a property is driven by an expression, the authored value is the expression itself, not
// its resolved number — a diff over a parametric model must not lie by comparing resolved values.
TEST_F(ObjectRecipeTest, boundExpressionIsTheAuthoredValueNotTheResolvedNumber)
{
    // Arrange: a Box whose Length is driven by an expression resolving to 10 mm.
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);

    std::string cmd = "import FreeCAD as App\n";
    cmd += "App.ActiveDocument.getObject('" + std::string(box->getNameInDocument())
        + "').setExpression('Length', u'7 mm + 3 mm')\n";
    Base::Interpreter().runString(cmd.c_str());
    _doc->recompute();

    // Act
    const RecipeNode node = emitObjectRecipe(*box);

    // Assert: the expression is recorded, not the resolved literal "10 mm".
    ASSERT_EQ(node.fields.count("Length"), 1u);
    EXPECT_NE(node.fields.at("Length").find('+'), std::string::npos);
    EXPECT_NE(node.fields.at("Length"), "10 mm");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
