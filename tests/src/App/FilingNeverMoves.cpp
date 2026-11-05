// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>
#include <src/App/PlacedGroup.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObjectGroup.h>
#include <App/GeoFeature.h>
#include <App/GeoFeatureGroupExtension.h>

// Cruth #130: a folder organises; it never moves an object into or out of the placed group
// whose frame positions it. Filing used to pull a placed member out of its group (so it jumped
// in space) and push an object into the placed group a folder sat in.

class FilingNeverMovesTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("filing");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _placed = tests::addPlacedGroup(_doc, "Placed");
        _folder = _doc->addObject<App::DocumentObjectGroup>("Folder");
        _item = _doc->addObject("App::GeoFeature", "Item");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    App::GeoFeatureGroupExtension* placed() const
    {
        return _placed->getExtensionByType<App::GeoFeatureGroupExtension>();
    }

    std::string _docName;
    App::Document* _doc = nullptr;
    App::DocumentObject* _placed = nullptr;
    App::DocumentObjectGroup* _folder = nullptr;
    App::DocumentObject* _item = nullptr;
};

TEST_F(FilingNeverMovesTest, FilingAPlacedMemberKeepsItInItsPlacedGroup)
{
    placed()->addObject(_item);
    _folder->addObject(_item);

    EXPECT_TRUE(placed()->hasObject(_item));
    EXPECT_TRUE(_folder->hasObject(_item));
}

TEST_F(FilingNeverMovesTest, AFolderInsideAPlacedGroupDoesNotPullItsFilingIn)
{
    placed()->addObject(_folder);
    _folder->addObject(_item);

    EXPECT_TRUE(_folder->hasObject(_item));
    EXPECT_FALSE(placed()->hasObject(_item));
    EXPECT_EQ(App::GeoFeatureGroupExtension::getGroupOfObject(_item), nullptr);
}
