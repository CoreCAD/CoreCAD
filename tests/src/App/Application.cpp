// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#define FC_OS_MACOSX 1
#include "App/ProgramOptionsUtilities.h"

#include <App/Document.h>
#include <Base/Uuid.h>

#include <src/App/InitApplication.h>


using namespace App::Util;

using Spr = std::pair<std::string, std::string>;


class ApplicationTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

TEST_F(ApplicationTest, fCustomSyntaxLookup)
{
    Spr res {customSyntax("-display")};
    Spr exp {"display", "null"};
    EXPECT_EQ(res, exp);
};
TEST_F(ApplicationTest, fCustomSyntaxMac)
{
    Spr res {customSyntax("-psn_stuff")};
    Spr exp {"psn", "stuff"};
    EXPECT_EQ(res, exp);
};
TEST_F(ApplicationTest, fCustomSyntaxWidgetCount)
{
    Spr res {customSyntax("-widgetcount")};
    Spr exp {"widgetcount", ""};
    EXPECT_EQ(res, exp);
}
TEST_F(ApplicationTest, fCustomSyntaxNotFound)
{
    Spr res {customSyntax("-displayx")};
    Spr exp {"", ""};
    EXPECT_EQ(res, exp);
};
TEST_F(ApplicationTest, fCustomSyntaxAmpersand)
{
    Spr res {customSyntax("@freddie")};
    Spr exp {"response-file", "freddie"};
    EXPECT_EQ(res, exp);
};
TEST_F(ApplicationTest, fCustomSyntaxEmptyIn)
{
    Spr res {customSyntax("")};
    Spr exp {"", ""};
    EXPECT_EQ(res, exp);
};

// getDocumentByUuid resolves an open document by its durable UUID — the namespace
// half of a cross-document (document UUID, object UUID) reference (Amendment 3,
// Clause 3.7; ARCHITECTURE §7.2).
TEST_F(ApplicationTest, getDocumentByUuidResolvesOpenDocument)
{
    auto& app = App::GetApplication();
    const std::string nameA = app.getUniqueDocumentName("uuidLookupA");
    const std::string nameB = app.getUniqueDocumentName("uuidLookupB");
    App::Document* docA = app.newDocument(nameA.c_str(), "testUser");
    App::Document* docB = app.newDocument(nameB.c_str(), "testUser");

    const Base::Uuid uidA = docA->Uid.getValue();
    const Base::Uuid uidB = docB->Uid.getValue();

    // Each document is found by its own UUID, and never confused for the other.
    EXPECT_EQ(app.getDocumentByUuid(uidA), docA);
    EXPECT_EQ(app.getDocumentByUuid(uidB), docB);
    EXPECT_NE(uidA.getValue(), uidB.getValue());

    // A UUID no document carries resolves to nothing (a fresh random UUID).
    const Base::Uuid unknown;
    EXPECT_NE(unknown.getValue(), uidA.getValue());
    EXPECT_NE(unknown.getValue(), uidB.getValue());
    EXPECT_EQ(app.getDocumentByUuid(unknown), nullptr);

    // A closed document is no longer resolvable.
    app.closeDocument(nameB.c_str());
    EXPECT_EQ(app.getDocumentByUuid(uidB), nullptr);
    EXPECT_EQ(app.getDocumentByUuid(uidA), docA);

    app.closeDocument(nameA.c_str());
};
