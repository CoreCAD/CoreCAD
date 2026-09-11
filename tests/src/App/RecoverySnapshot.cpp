// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/RecoverySnapshot.h>
#include <App/StoredRecipe.h>
#include <Base/FileInfo.h>

#include <cstring>
#include <sstream>
#include <string>

#include <src/App/InitApplication.h>

/** Anything that can later be opened AS this document is written AS this document.
 *
 *  An automatic snapshot can become the record: recovery binds what it reads to the original
 *  document's path, and an ordinary save then writes it there. A snapshot written in some other
 *  form is a second record, and every duty this law places on the record reaches only the first
 *  (Amendment 19 Clause 19.5).
 */
class RecoverySnapshotTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        for (App::Document* doc : {_doc, _recovered}) {
            if (doc != nullptr) {
                App::GetApplication().closeDocument(doc->getName());
            }
        }
        _doc = nullptr;
        _recovered = nullptr;
    }

    /// A document holding a statement this build cannot honour: a property whose declaration the
    /// file no longer carries, so this build has no place for what it states.
    App::Document* openHoldingAKeptStatement()
    {
        auto& app = App::GetApplication();
        App::Document* source
            = app.newDocument(app.getUniqueDocumentName("snapshot").c_str(), "testUser");
        auto* holder = source->addObject("App::VarSet", "Holder");
        holder->addDynamicProperty("App::PropertyLength", "Clearance");
        source->recompute();
        std::string written = App::formatStoredRecipe(*source);
        app.closeDocument(source->getName());

        const std::string::size_type declares = written.find(" dynamic=\"1\"");
        EXPECT_NE(declares, std::string::npos);
        written.erase(declares, std::strlen(" dynamic=\"1\""));

        _doc = app.newDocument(app.getUniqueDocumentName("snapshot").c_str(), "testUser");
        std::istringstream text(written);
        App::restoreStoredRecipe(*_doc, text);
        return _doc;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
    App::Document* _recovered {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The measured loss: a statement kept by the record's own writer does not survive a snapshot and a
// recovery, and what comes back is bound to the original file and saved over it.
TEST_F(RecoverySnapshotTest, aSnapshotCarriesWhatTheRecordCouldNotHonour)
{
    // Arrange
    App::Document* doc = openHoldingAKeptStatement();
    ASSERT_NE(doc, nullptr);
    ASSERT_TRUE(doc->holdsUnreadContent()) << "the document under test holds nothing to lose";

    // Act
    ASSERT_TRUE(App::writeRecoverySnapshotToTransientDir(*doc));

    // Assert -- read back exactly as recovery reads it.
    const std::string snapshot = App::recoverySnapshotPath(*doc);
    ASSERT_FALSE(snapshot.empty());
    ASSERT_TRUE(Base::FileInfo(snapshot).exists()) << "no snapshot was written";

    auto& app = App::GetApplication();
    _recovered = app.openDocument(snapshot.c_str());
    ASSERT_NE(_recovered, nullptr);

    App::DocumentObject* holder = _recovered->getObject("Holder");
    ASSERT_NE(holder, nullptr) << "the snapshot did not carry the object";
    EXPECT_TRUE(holder->holdsUnhonouredStatement())
        << "a statement the record keeps was erased by the writer that runs unattended";
    EXPECT_NE(App::formatStoredRecipe(*_recovered).find("name=\"Clearance\""), std::string::npos)
        << "a save of the recovered document would publish the loss over the record";
}
