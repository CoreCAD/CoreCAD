// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#define FC_OS_MACOSX 1
#include "App/ProgramOptionsUtilities.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyLinks.h>
#include <Base/FileInfo.h>
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

// A cross-document PropertyXLink persists and resolves the durable
// (document UUID, object UUID) pair across a save/close/reopen cycle. The stored
// file path and object name are locator hints; the UUID pair is the real binding
// (Amendment 3, Clause 3.7; ARCHITECTURE §7.2).
TEST_F(ApplicationTest, xLinkPersistsAndResolvesUuidPair)
{
    auto& app = App::GetApplication();
    const std::string targetPath = Base::FileInfo::getTempFileName() + ".FCStd";
    const std::string hostPath = Base::FileInfo::getTempFileName() + ".FCStd";

    // Target document with an object to link to, saved to disk.
    App::Document* tdoc = app.newDocument(app.getUniqueDocumentName("xlinkTarget").c_str(), "testUser");
    App::DocumentObject* box = tdoc->addObject("App::VarSet", "TheBox");
    const std::string objUuid = box->Uid.getValueStr();
    ASSERT_TRUE(tdoc->saveAs(targetPath.c_str()));
    const std::string docUuid = tdoc->Uid.getValueStr();

    // Host document with a cross-document XLink into the target.
    App::Document* hdoc = app.newDocument(app.getUniqueDocumentName("xlinkHost").c_str(), "testUser");
    App::DocumentObject* ref = hdoc->addObject("App::VarSet", "Referrer");
    auto* link = dynamic_cast<App::PropertyXLink*>(
        ref->addDynamicProperty("App::PropertyXLink", "Link")
    );
    ASSERT_NE(link, nullptr);
    ASSERT_TRUE(hdoc->saveAs(hostPath.c_str()));  // save first: the XLink needs a base path
    link->setValue(box);
    ASSERT_EQ(link->getValue(), box);
    hdoc->save();

    const std::string hostName = hdoc->getName();
    const std::string targetName = tdoc->getName();
    app.closeDocument(hostName.c_str());
    app.closeDocument(targetName.c_str());

    // Reopen both: the link resolves to the object carrying the durable UUID.
    App::Document* tdoc2 = app.openDocument(targetPath.c_str());
    App::Document* hdoc2 = app.openDocument(hostPath.c_str());
    ASSERT_NE(tdoc2, nullptr);
    ASSERT_NE(hdoc2, nullptr);
    App::DocumentObject* box2 = tdoc2->getObject("TheBox");
    App::DocumentObject* ref2 = hdoc2->getObject("Referrer");
    ASSERT_NE(box2, nullptr);
    ASSERT_NE(ref2, nullptr);
    EXPECT_EQ(box2->Uid.getValueStr(), objUuid);
    EXPECT_EQ(tdoc2->Uid.getValueStr(), docUuid);
    auto* link2 = dynamic_cast<App::PropertyXLink*>(ref2->getPropertyByName("Link"));
    ASSERT_NE(link2, nullptr);
    EXPECT_EQ(link2->getValue(), box2);

    const std::string hostName2 = hdoc2->getName();
    const std::string targetName2 = tdoc2->getName();
    app.closeDocument(hostName2.c_str());
    app.closeDocument(targetName2.c_str());
    Base::FileInfo(hostPath).deleteFile();
    Base::FileInfo(targetPath).deleteFile();
};

// Resolution prefers the durable object UUID over the stored name: when the name
// locator is wrong but the UUID pair is intact, the link still binds correctly.
// This drives the restore path directly, exactly as PropertyXLink::Restore feeds it.
TEST_F(ApplicationTest, xLinkResolvesByUuidWhenNameIsWrong)
{
    auto& app = App::GetApplication();
    const std::string targetPath = Base::FileInfo::getTempFileName() + ".FCStd";
    const std::string hostPath = Base::FileInfo::getTempFileName() + ".FCStd";

    App::Document* tdoc = app.newDocument(app.getUniqueDocumentName("xlinkTarget").c_str(), "testUser");
    App::DocumentObject* box = tdoc->addObject("App::VarSet", "TheBox");
    const std::string objUuid = box->Uid.getValueStr();
    ASSERT_TRUE(tdoc->saveAs(targetPath.c_str()));
    const std::string docUuid = tdoc->Uid.getValueStr();

    App::Document* hdoc = app.newDocument(app.getUniqueDocumentName("xlinkHost").c_str(), "testUser");
    App::DocumentObject* ref = hdoc->addObject("App::VarSet", "Referrer");
    auto* link = dynamic_cast<App::PropertyXLink*>(
        ref->addDynamicProperty("App::PropertyXLink", "Link")
    );
    ASSERT_NE(link, nullptr);
    ASSERT_TRUE(hdoc->saveAs(hostPath.c_str()));

    // Wrong name, correct UUID pair — the deferred restore must bind by UUID.
    link->setValue(
        std::string(targetPath),
        std::string("NoSuchObject"),
        std::string(docUuid),
        std::string(objUuid),
        {},
        {}
    );
    EXPECT_EQ(link->getValue(), box);

    const std::string hostName = hdoc->getName();
    const std::string targetName = tdoc->getName();
    app.closeDocument(hostName.c_str());
    app.closeDocument(targetName.c_str());
    Base::FileInfo(hostPath).deleteFile();
    Base::FileInfo(targetPath).deleteFile();
};

// A file that has been replaced at the stored path by a DIFFERENT document must
// not bind the reference: the stored document UUID is the real identity, so a
// path resolving to the wrong UUID is treated as unresolved (Amendment 3, Clause
// 3.7 step 3). The old name/path-based code would have bound the same-named
// object in the impostor document.
TEST_F(ApplicationTest, xLinkRejectsWrongUuidAtPath)
{
    auto& app = App::GetApplication();
    const std::string targetPath = Base::FileInfo::getTempFileName() + ".FCStd";
    const std::string hostPath = Base::FileInfo::getTempFileName() + ".FCStd";

    // Target document with an object to link to, saved to disk.
    App::Document* tdoc = app.newDocument(app.getUniqueDocumentName("xlinkTarget").c_str(), "testUser");
    App::DocumentObject* box = tdoc->addObject("App::VarSet", "TheBox");
    const std::string objUuid = box->Uid.getValueStr();
    ASSERT_TRUE(tdoc->saveAs(targetPath.c_str()));
    const std::string docUuid = tdoc->Uid.getValueStr();

    // Host document with a cross-document XLink into the target, saved.
    App::Document* hdoc = app.newDocument(app.getUniqueDocumentName("xlinkHost").c_str(), "testUser");
    App::DocumentObject* ref = hdoc->addObject("App::VarSet", "Referrer");
    auto* link = dynamic_cast<App::PropertyXLink*>(
        ref->addDynamicProperty("App::PropertyXLink", "Link")
    );
    ASSERT_NE(link, nullptr);
    ASSERT_TRUE(hdoc->saveAs(hostPath.c_str()));
    link->setValue(box);
    ASSERT_EQ(link->getValue(), box);
    hdoc->save();

    const std::string hostName = hdoc->getName();
    const std::string targetName = tdoc->getName();
    app.closeDocument(hostName.c_str());
    app.closeDocument(targetName.c_str());

    // Overwrite the target path with a DIFFERENT document (fresh UUID) that
    // happens to carry a same-named object — the classic file-manager copy or
    // an unrelated file dropped in place.
    App::Document* impostor
        = app.newDocument(app.getUniqueDocumentName("xlinkImpostor").c_str(), "testUser");
    impostor->addObject("App::VarSet", "TheBox");
    ASSERT_NE(impostor->Uid.getValueStr(), docUuid);
    ASSERT_TRUE(impostor->saveAs(targetPath.c_str()));
    app.closeDocument(impostor->getName());

    // Reopen the host: the path now yields the wrong document UUID, so the link
    // must NOT bind to the impostor's same-named object — it stays unresolved.
    App::Document* hdoc2 = app.openDocument(hostPath.c_str());
    ASSERT_NE(hdoc2, nullptr);
    App::DocumentObject* ref2 = hdoc2->getObject("Referrer");
    ASSERT_NE(ref2, nullptr);
    auto* link2 = dynamic_cast<App::PropertyXLink*>(ref2->getPropertyByName("Link"));
    ASSERT_NE(link2, nullptr);
    EXPECT_EQ(link2->getValue(), nullptr);

    app.closeDocument(hdoc2->getName());
    // The impostor may have been opened as a pending dependency; close if so.
    for (auto* d : app.getDocuments()) {
        app.closeDocument(d->getName());
    }
    Base::FileInfo(hostPath).deleteFile();
    Base::FileInfo(targetPath).deleteFile();
};
