// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentChanges.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <App/PropertyUnits.h>
#include <App/StoredRecipe.h>
#include <Base/Interpreter.h>

#include <sstream>
#include <string>
#include <vector>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class DocumentChangesTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _beforeName = App::GetApplication().getUniqueDocumentName("before");
        _before = App::GetApplication().newDocument(_beforeName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_beforeName.c_str());
        if (!_afterName.empty()) {
            App::GetApplication().closeDocument(_afterName.c_str());
        }
    }

    /// The next version of the model: a copy through the saved text, so every object keeps its
    /// Uid, as a version read back from history would.
    App::Document* nextVersion()
    {
        _afterName = App::GetApplication().getUniqueDocumentName("after");
        _after = App::GetApplication().newDocument(_afterName.c_str(), "testUser");
        std::istringstream text(formatStoredRecipe(*_before));
        restoreStoredRecipe(*_after, text);
        return _after;
    }

    App::DocumentObject* box(App::Document* doc, const char* name, double length)
    {
        auto* obj = doc->addObject("Part::Box", name);
        static_cast<PropertyLength*>(obj->getPropertyByName("Length"))->setValue(length);
        return obj;
    }

    static void setLength(App::DocumentObject* obj, double length)
    {
        static_cast<PropertyLength*>(obj->getPropertyByName("Length"))->setValue(length);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _beforeName {};
    std::string _afterName {};
    App::Document* _before {};
    App::Document* _after {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(DocumentChangesTest, anUnchangedCopyHasNoChanges)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    EXPECT_TRUE(changes.empty()) << formatDocumentChanges(changes);
    EXPECT_EQ(formatDocumentChanges(changes), "No changes.\n");
}

TEST_F(DocumentChangesTest, aValueEditIsOneLineWithBothValues)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();
    setLength(after->getObject("Box"), 40.0);

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].change.kind, RecipeChange::Kind::FieldChanged);
    EXPECT_EQ(changes[0].change.field, "Length");
    EXPECT_EQ(changes[0].text, "Box: Length 10 mm → 40 mm");
}

TEST_F(DocumentChangesTest, aFormulaIsShownAsWrittenNotAsItsResult)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();
    Base::Interpreter().runString(
        ("App.getDocument('" + _afterName + "').Box.setExpression('Length', 'Width * 2')").c_str()
    );

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    ASSERT_TRUE(changes[0].change.after.has_value());
    EXPECT_NE(changes[0].change.after->find("Width"), std::string::npos) << changes[0].text;
}

TEST_F(DocumentChangesTest, addedObjectsFollowTheModelAndRemovedOnesComeLast)
{
    box(_before, "Box", 10.0);
    box(_before, "Doomed", 10.0);
    auto* after = nextVersion();
    after->removeObject("Doomed");
    box(after, "Fresh", 10.0);

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 2u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].text, "Fresh added");
    EXPECT_EQ(changes[1].text, "Doomed removed");
}

TEST_F(DocumentChangesTest, aRenameIsReportedAsARenameAndNothingElse)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();
    after->getObject("Box")->Label.setValue("Housing");

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].text, "Box renamed to Housing");
    EXPECT_EQ(changes[0].label, "Housing");
}

// The case that separates matching by identity from matching by name: the newer version holds
// a different object under the old one's name.
TEST_F(DocumentChangesTest, aReplacementUnderTheSameNameIsARemovalAndAnAddition)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();
    after->removeObject("Box");
    box(after, "Box", 10.0);

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 2u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].change.kind, RecipeChange::Kind::Added);
    EXPECT_EQ(changes[1].change.kind, RecipeChange::Kind::Removed);
    EXPECT_NE(changes[0].objectId, changes[1].objectId);
}

TEST_F(DocumentChangesTest, aRetargetedLinkNamesBothTargetsByLabel)
{
    box(_before, "Base", 10.0);
    box(_before, "Small", 5.0);
    box(_before, "Large", 20.0);
    auto* cut = _before->addObject("Part::Cut", "Cut");
    static_cast<PropertyLink*>(cut->getPropertyByName("Base"))->setValue(_before->getObject("Base"));
    static_cast<PropertyLink*>(cut->getPropertyByName("Tool"))->setValue(_before->getObject("Small"));
    auto* after = nextVersion();
    static_cast<PropertyLink*>(after->getObject("Cut")->getPropertyByName("Tool"))
        ->setValue(after->getObject("Large"));

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].change.kind, RecipeChange::Kind::LinksChanged);
    EXPECT_NE(changes[0].text.find("Small"), std::string::npos) << changes[0].text;
    EXPECT_NE(changes[0].text.find("Large"), std::string::npos) << changes[0].text;
}

// A version read back from its file holds its objects in id order, which is no order a person
// knows. A chain of steps makes a chance pass unlikely.
TEST_F(DocumentChangesTest, changesFollowTheOrderTheModelIsBuilt)
{
    box(_before, "Stock", 10.0);
    box(_before, "Tool", 5.0);
    box(_before, "OtherTool", 6.0);
    App::DocumentObject* previous = _before->getObject("Stock");
    for (const char* name : {"CutA", "CutB", "CutC", "CutD"}) {
        auto* cut = _before->addObject("Part::Cut", name);
        static_cast<PropertyLink*>(cut->getPropertyByName("Base"))->setValue(previous);
        static_cast<PropertyLink*>(cut->getPropertyByName("Tool"))->setValue(_before->getObject("Tool"));
        previous = cut;
    }
    auto* after = nextVersion();
    setLength(after->getObject("Stock"), 40.0);
    for (const char* name : {"CutD", "CutB", "CutC", "CutA"}) {
        static_cast<PropertyLink*>(after->getObject(name)->getPropertyByName("Tool"))
            ->setValue(after->getObject("OtherTool"));
    }

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    std::vector<std::string> labels;
    for (const DocumentChange& change : changes) {
        labels.push_back(change.label);
    }
    EXPECT_EQ(labels, (std::vector<std::string> {"Stock", "CutA", "CutB", "CutC", "CutD"}))
        << formatDocumentChanges(changes);
}

// A part's first version, compared with nothing: the world frame came with the document, so only
// what the person made is listed.
TEST_F(DocumentChangesTest, theWorldFrameIsNotListedAsAdded)
{
    App::DocumentInitFlags typed;
    typed.documentType = App::Document::DocTypePart;
    _afterName = App::GetApplication().getUniqueDocumentName("after");
    auto* after = App::GetApplication().newDocument(_afterName.c_str(), "testUser", typed);
    ASSERT_GT(after->getObjects().size(), 0u)
        << "a part document should start with its world frame";
    box(after, "Box", 10.0);

    const std::vector<DocumentChange> changes = diffDocuments(*_before, *after);

    ASSERT_EQ(changes.size(), 1u) << formatDocumentChanges(changes);
    EXPECT_EQ(changes[0].text, "Box added");
}

TEST_F(DocumentChangesTest, scriptsGetOneRecordPerChange)
{
    box(_before, "Box", 10.0);
    auto* after = nextVersion();
    setLength(after->getObject("Box"), 40.0);

    Base::Interpreter().runString(("_changes = App.diffDocuments(App.getDocument('" + _beforeName
                                   + "'), App.getDocument('" + _afterName + "'))")
                                      .c_str());
    const std::string summary = Base::Interpreter().runStringWithKey(
        "_summary = '%d|%s|%s|%s|%s' % (len(_changes), _changes[0]['kind'], "
        "_changes[0]['field'], _changes[0]['before'], _changes[0]['after'])",
        "_summary"
    );

    EXPECT_EQ(summary, "1|field|Length|10 mm|40 mm");
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
