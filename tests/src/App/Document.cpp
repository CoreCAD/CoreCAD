// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/StringHasher.h"
#include "Base/Uuid.h"
#include "Base/Writer.h"
#include <src/App/InitApplication.h>

using ::testing::Eq;
using ::testing::Ne;

// NOLINTBEGIN(readability-magic-numbers)

class FakeWriter: public Base::Writer
{
    void writeFiles() override
    {}
    std::ostream& Stream() override
    {
        return std::cout;
    }
};

class DocumentTest: public ::testing::Test
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

    App::Document* doc()
    {
        return _doc;
    }

private:
    std::string _docName;
    App::Document* _doc {};
};


TEST_F(DocumentTest, addStringHasherIndicatesUnwrittenWhenNew)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);

    // Act
    auto addResult = doc()->addStringHasher(hasher);

    // Assert
    EXPECT_TRUE(addResult.first);
    EXPECT_THAT(addResult.second, Ne(-1));
}

TEST_F(DocumentTest, addStringHasherIndicatesAlreadyWritten)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);
    doc()->addStringHasher(hasher);

    // Act
    auto addResult = doc()->addStringHasher(hasher);

    // Assert
    EXPECT_FALSE(addResult.first);
}

TEST_F(DocumentTest, getStringHasherGivesExpectedHasher)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);
    auto pair = doc()->addStringHasher(hasher);
    int index = pair.second;

    // Act
    auto foundHasher = doc()->getStringHasher(index);

    // Assert
    EXPECT_EQ(hasher, foundHasher);
}

// --- Durable-UUID resolution (Amendment 3, Clause 3.6 foundation) ---

TEST_F(DocumentTest, getObjectByUuidResolvesEachObject)
{
    // Arrange
    auto* a = doc()->addObject("App::DocumentObjectGroup");
    auto* b = doc()->addObject("App::DocumentObjectGroup");

    // Act / Assert — each object resolves through its own durable UUID
    EXPECT_EQ(doc()->getObjectByUuid(a->Uid.getValue()), a);
    EXPECT_EQ(doc()->getObjectByUuid(b->Uid.getValue()), b);
}

TEST_F(DocumentTest, getObjectByUuidReturnsNullForUnknownUuid)
{
    // Arrange
    doc()->addObject("App::DocumentObjectGroup");

    // Act / Assert — a UUID no object carries resolves to nothing
    EXPECT_EQ(doc()->getObjectByUuid(Base::Uuid()), nullptr);
}

TEST_F(DocumentTest, getObjectByUuidEvictsRemovedObject)
{
    // Arrange
    auto* a = doc()->addObject("App::DocumentObjectGroup");
    auto* b = doc()->addObject("App::DocumentObjectGroup");
    const Base::Uuid aUuid = a->Uid.getValue();

    // Act
    doc()->removeObject(a->getNameInDocument());

    // Assert — the removed object no longer resolves; the survivor still does
    EXPECT_EQ(doc()->getObjectByUuid(aUuid), nullptr);
    EXPECT_EQ(doc()->getObjectByUuid(b->Uid.getValue()), b);
}

TEST_F(DocumentTest, getObjectByUuidTracksUuidChange)
{
    // Arrange — mirrors restore, where the minted Uid is overwritten from file
    auto* a = doc()->addObject("App::DocumentObjectGroup");
    const Base::Uuid minted = a->Uid.getValue();

    // Act — assign a fresh UUID, as a restore would
    Base::Uuid replacement;
    replacement.setValue(Base::Uuid::createUuid());
    a->Uid.setValue(replacement);

    // Assert — the stale key is gone, the new one resolves
    EXPECT_EQ(doc()->getObjectByUuid(minted), nullptr);
    EXPECT_EQ(doc()->getObjectByUuid(replacement), a);
}

// NOLINTEND(readability-magic-numbers)
