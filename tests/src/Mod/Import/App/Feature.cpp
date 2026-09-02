// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include <fstream>

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Mod/Import/App/Feature.h>

class ImportFeature: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("ImportFeature");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
        for (const auto& path : _written) {
            Base::FileInfo(path).deleteFile();
        }
    }

    /// Writes \a contents to a throwaway file and returns its path.
    std::string writeFile(const char* name, const char* contents)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        std::ofstream out(file.filePath(), std::ios::binary | std::ios::trunc);
        out << contents;
        out.close();
        _written.push_back(file.filePath());
        return file.filePath();
    }

    Import::Feature* addImport()
    {
        auto* feature = _doc->addObject<Import::Feature>("Import");
        _doc->recompute();
        return feature;
    }

    App::Document* _doc {nullptr};
    std::vector<std::string> _written;
};

TEST_F(ImportFeature, anImportRemembersTheFileItCameFrom)
{
    auto* feature = addImport();

    // The point of the type: the source is a declared input, not something the
    // importer knows only while it is running.
    ASSERT_NE(feature->getPropertyByName("SourceFile"), nullptr);
    ASSERT_NE(feature->getPropertyByName("SourceHash"), nullptr);
    ASSERT_NE(feature->getPropertyByName("TranslatorSettings"), nullptr);

    const std::string path = writeFile("cc_import_source.txt", "bracket v1");
    feature->SourceFile.setValue(path.c_str());

    EXPECT_EQ(std::string(feature->SourceFile.getValue()), path);
}

TEST_F(ImportFeature, anImportIsAnAnchorSoItHoldsItsOwnPlacement)
{
    auto* feature = addImport();

    // An import builds from raw input rather than from objects it references, so
    // unlike a boolean or a dress-up it authors a placement of its own.
    EXPECT_TRUE(feature->holdsAuthoredPlacement());
}

TEST_F(ImportFeature, theFingerprintFollowsTheFileContents)
{
    auto* feature = addImport();

    const std::string path = writeFile("cc_import_change.txt", "bracket v1");
    feature->SourceFile.setValue(path.c_str());

    EXPECT_TRUE(feature->refreshSourceHash());
    const std::string first = feature->SourceHash.getStrValue();
    EXPECT_FALSE(first.empty());

    // Re-reading an unchanged file is not a change -- otherwise every recompute
    // would look like a re-import.
    EXPECT_FALSE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), first);

    // The supplier sends a new revision under the same name.
    writeFile("cc_import_change.txt", "bracket v2");

    EXPECT_TRUE(feature->refreshSourceHash());
    EXPECT_NE(feature->SourceHash.getStrValue(), first);
}

TEST_F(ImportFeature, anUnreadableSourceFingerprintsAsEmpty)
{
    auto* feature = addImport();

    // Distinguishable from a file that is genuinely empty, which hashes to the
    // SHA-1 of no bytes rather than to nothing at all.
    feature->SourceFile.setValue("/no/such/file/anywhere.step");
    EXPECT_FALSE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), "");

    const std::string path = writeFile("cc_import_empty.txt", "");
    feature->SourceFile.setValue(path.c_str());
    EXPECT_TRUE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}
