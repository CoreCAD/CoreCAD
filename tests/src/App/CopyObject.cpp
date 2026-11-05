// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/StoredRecipe.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/// A copy becomes objects in a document, so it is made by the writer that makes the record and
/// read by that reader (Amendment 19 Clause 19.5). These are the claims that separates from a copy
/// written by a second writer of its own.
class CopyObjectTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _sourceName = App::GetApplication().getUniqueDocumentName("copysource");
        _source = App::GetApplication().newDocument(_sourceName.c_str(), "testUser");
        _targetName = App::GetApplication().getUniqueDocumentName("copytarget");
        _target = App::GetApplication().newDocument(_targetName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_targetName.c_str());
        App::GetApplication().closeDocument(_sourceName.c_str());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _sourceName {};
    std::string _targetName {};
    App::Document* _source {};
    App::Document* _target {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The ticket (#97). A copy went out through a second writer -- the legacy archive and each
// property's own Save -- which knows nothing of a statement the record is keeping on a property's
// behalf. So an object holding a value this build could not honour was copied without it, and the
// copy asserted as authored what the original only failed to honour.
TEST_F(CopyObjectTest, aStatementThisBuildCouldNotHonourSurvivesBeingCopied)
{
    // Arrange -- a file that states a property this build has no place for. Read into the source
    // document, the statement is kept rather than dropped.
    auto* original = _source->addObject("Part::Box", "Block");
    ASSERT_NE(original, nullptr);
    _source->recompute();

    std::string written = formatStoredRecipe(*_source);
    const std::string anchor = "<Properties>";
    const std::string::size_type at = written.find(anchor, written.find("<Objects>"));
    ASSERT_NE(at, std::string::npos);
    written.insert(
        at + anchor.size(),
        "\n<Property name=\"ChamferStyle\" type=\"App::PropertyEnumeration\">\n"
        "<Integer value=\"7\"/>\n"
        "</Property>"
    );

    App::Document* holding = App::GetApplication().newDocument(
        App::GetApplication().getUniqueDocumentName("copyholding").c_str(),
        "testUser"
    );
    const std::string holdingName = holding->getName();
    std::istringstream text(written);
    restoreStoredRecipe(*holding, text);

    DocumentObject* kept = holding->getObject("Block");
    ASSERT_NE(kept, nullptr);
    ASSERT_FALSE(kept->unhonouredStatements().empty())
        << "the arrangement is wrong: the source object is not holding a kept statement";

    // Act
    const std::vector<DocumentObject*> copied = _target->copyObject({kept}, false);

    // Assert -- on the copy, and in what the copy's own file would state.
    ASSERT_EQ(copied.size(), 1U);
    EXPECT_FALSE(copied.front()->unhonouredStatements().empty())
        << "the copy dropped the statement the original was keeping";
    EXPECT_NE(formatStoredRecipe(*_target).find("ChamferStyle"), std::string::npos)
        << "the copy's own file no longer states what the original's file states";

    App::GetApplication().closeDocument(holdingName.c_str());
}

// A reference binds to the copy that arrived with it, not to the original it was copied from --
// which is a real question only while the two still wear the same durable id, in the moment
// between reading the copy and minting it an identity of its own.
TEST_F(CopyObjectTest, aReferenceAmongCopiedObjectsBindsToTheCopy)
{
    // Arrange -- pasted back into the very document they came from, which is where an original
    // wearing the same id is there to be bound to by mistake.
    auto* base = _source->addObject("Part::Box", "Base");
    auto* holder = _source->addObject("App::VarSet", "Holder");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(holder, nullptr);
    auto* link = static_cast<PropertyLink*>(holder->addDynamicProperty("App::PropertyLink", "BuiltOn"));
    ASSERT_NE(link, nullptr);
    link->setValue(base);
    _source->recompute();

    // Act
    const std::vector<DocumentObject*> copied = _source->copyObject({holder, base}, false);

    // Assert
    ASSERT_EQ(copied.size(), 2U);
    DocumentObject* copiedHolder = copied.front();
    DocumentObject* copiedBase = copied.back();
    ASSERT_NE(copiedHolder, holder);
    ASSERT_NE(copiedBase, base);

    auto* copiedLink = static_cast<PropertyLink*>(copiedHolder->getPropertyByName("BuiltOn"));
    ASSERT_NE(copiedLink, nullptr);
    EXPECT_EQ(copiedLink->getValue(), copiedBase)
        << "the copy references the object it was copied from instead of the copy beside it";
    EXPECT_NE(copiedBase->Uid.getValueStr(), base->Uid.getValueStr())
        << "the copy inherited the identity of the object it was copied from";
}

// The caller is told which copy came from which original. The record states objects in durable-id
// order and not the order they were asked for, so position alone cannot answer it.
TEST_F(CopyObjectTest, eachCopyIsReturnedForTheObjectItWasCopiedFrom)
{
    auto* first = _source->addObject("Part::Box", "Zulu");
    auto* second = _source->addObject("Part::Box", "Alpha");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    // Durable ids chosen so the record states these two in the opposite order to the one they are
    // asked for. Left to the ids they are born with, the two orders agree about half the time and
    // a copy matched up by position would pass whenever they happened to agree.
    first->Uid.setValue(std::string("ffffffff-0000-4000-8000-000000000001"));
    second->Uid.setValue(std::string("00000000-0000-4000-8000-000000000002"));
    static_cast<PropertyLength*>(first->getPropertyByName("Length"))->setValue(11.0);
    static_cast<PropertyLength*>(second->getPropertyByName("Length"))->setValue(22.0);
    _source->recompute();

    const std::vector<DocumentObject*> copied = _target->copyObject({first, second}, false);

    ASSERT_EQ(copied.size(), 2U);
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(copied[0]->getPropertyByName("Length"))->getValue(),
        11.0
    ) << "the copies came back in the record's order rather than the order they were asked for";
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(copied[1]->getPropertyByName("Length"))->getValue(),
        22.0
    );
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
