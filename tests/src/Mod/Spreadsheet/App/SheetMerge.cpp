// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Sean Barton (Cruth)

/* A formula names a sheet cell by its address. When one side of a merge inserts a row, the other
 * side's formulas still count rows the old way; the merge replays the insertion on that side first,
 * so each formula reads the cell its author meant. Where the replay cannot tell, it stops. */

#include <gtest/gtest.h>

#include "src/App/InitApplication.h"

#include <sstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ObjectRecipe.h>
#include <App/Range.h>
#include <App/StoredRecipe.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Spreadsheet/App/Sheet.h>

namespace
{

class SheetMergeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _ancestor = newDocument("ancestor");
        auto* sheet = addSheet(_ancestor, "Sheet");
        sheet->setCell("A1", "1");
        sheet->setCell("A2", "2");
        sheet->setCell("A3", "=A2*10");
        _ancestor->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeAllDocuments();
    }

    static App::Document* newDocument(const char* name)
    {
        auto& app = App::GetApplication();
        return app.newDocument(app.getUniqueDocumentName(name).c_str(), "testUser");
    }

    static Spreadsheet::Sheet* addSheet(App::Document* doc, const char* name)
    {
        return static_cast<Spreadsheet::Sheet*>(doc->addObject("Spreadsheet::Sheet", name));
    }

    App::Document* branch(const char* name)
    {
        App::Document* doc = newDocument(name);
        std::istringstream recipe(App::formatStoredRecipe(*_ancestor));
        App::restoreStoredRecipe(*doc, recipe);
        return doc;
    }

    /// The ancestor's sheet of that name, as `doc` holds it.
    Spreadsheet::Sheet* sheetOf(App::Document* doc, const char* name = "Sheet") const
    {
        const std::string uid = _ancestor->getObject(name)->Uid.getValueStr();
        for (App::DocumentObject* obj : doc->getObjects()) {
            if (obj->Uid.getValueStr() == uid) {
                return static_cast<Spreadsheet::Sheet*>(obj);
            }
        }
        ADD_FAILURE() << doc->getName() << " has no " << name;
        return nullptr;
    }

    std::string contents(App::Document* doc, const char* address, const char* name = "Sheet")
    {
        std::string content;
        const Spreadsheet::Sheet* sheet = sheetOf(doc, name);
        if (const auto* cell = sheet ? sheet->getCell(App::CellAddress(address)) : nullptr) {
            cell->getStringContent(content);
        }
        return content;
    }

    /// The sheet as a saved and reopened file holds it: a row left empty keeps no id.
    void reopen(App::Document* doc) const
    {
        Spreadsheet::PropertySheet* cells = sheetOf(doc)->getCells();
        Base::StringWriter writer;
        cells->Save(writer);
        std::istringstream stored("<root>" + writer.getString() + "</root>");
        Base::XMLReader reader("Cells", stored);
        cells->Restore(reader);
    }

    App::MergedDocument merge(App::Document* branchA, App::Document* branchB)
    {
        branchA->recompute();
        branchB->recompute();
        return App::buildMergedDocument(*_ancestor, *branchA, *branchB);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _ancestor {};
};

TEST_F(SheetMergeTest, editsToDifferentCellsBothLand)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->setCell("A1", "100");
    sheetOf(b)->setCell("A2", "200");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "A1"), "100");
    EXPECT_EQ(contents(merged.document, "A2"), "200");
}

TEST_F(SheetMergeTest, aCellEditedTwoWaysIsAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->setCell("A1", "100");
    sheetOf(b)->setCell("A1", "5");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("A1"), std::string::npos);
}

TEST_F(SheetMergeTest, anEditLandsBesideARowTheOtherSideInserted)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->setCell("A1", "100");
    sheetOf(b)->insertRows(0, 1);
    sheetOf(b)->setCell("A1", "0");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "A1"), "0");
    EXPECT_EQ(contents(merged.document, "A2"), "100");
    EXPECT_EQ(contents(merged.document, "A3"), "2");
    EXPECT_EQ(contents(merged.document, "A4"), "=A3 * 10");
}

TEST_F(SheetMergeTest, aFormulaWrittenBesideAnInsertedRowReadsTheCellItsAuthorMeant)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->insertRows(0, 1);
    sheetOf(b)->setCell("B1", "=A2");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "B2"), "=A3");
}

TEST_F(SheetMergeTest, aFormulaInAnotherObjectFollowsTheRowsTheOtherSideMoved)
{
    addSheet(_ancestor, "Other")->setCell("A1", "0");
    for (const bool movingSideIsA : {true, false}) {
        App::Document* a = branch("a");
        App::Document* b = branch("b");
        sheetOf(movingSideIsA ? a : b)->insertRows(0, 1);
        sheetOf(movingSideIsA ? b : a, "Other")->setCell("A1", "=Sheet.A2");

        const App::MergedDocument merged = merge(a, b);

        ASSERT_NE(merged.document, nullptr) << merged.refusal;
        EXPECT_EQ(contents(merged.document, "A1", "Other"), "=Sheet.A3") << movingSideIsA;
    }
}

TEST_F(SheetMergeTest, aFormulaNamingARowTheOtherSideRemovedIsAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->removeRows(1, 1);
    sheetOf(b)->setCell("B1", "=A2");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("names A2"), std::string::npos);
}

TEST_F(SheetMergeTest, aFormulaNamingAnEmptyStretchThatGrewIsAConflict)
{
    sheetOf(_ancestor)->setCell("A7", "7");
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    // Rows 4 to 6 never held a cell, so where among them A inserted its row is not known.
    sheetOf(a)->insertRows(4, 1);
    sheetOf(b)->setCell("B1", "=A5");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("names A5"), std::string::npos);
}

TEST_F(SheetMergeTest, aRowInsertedBetweenTwoFilledRowsMovesByAKnownAmount)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->insertRows(1, 1);
    sheetOf(b)->setCell("B1", "=A2 + A1");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "B1"), "=A3 + A1");
}

TEST_F(SheetMergeTest, bothSidesMovingRowsIsAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->insertRows(0, 1);
    sheetOf(b)->insertRows(1, 1);

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("both sides"), std::string::npos);
}

TEST_F(SheetMergeTest, anEditLandsBesideARowTheOtherSideRemoved)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->removeRows(0, 1);
    sheetOf(b)->setCell("A3", "5");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "A1"), "2");
    EXPECT_EQ(contents(merged.document, "A2"), "5");
    EXPECT_EQ(contents(merged.document, "A3"), "");
}

TEST_F(SheetMergeTest, aFormulaFollowsAnInsertedColumn)
{
    sheetOf(_ancestor)->setCell("C1", "3");
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->insertColumns(0, 1);
    sheetOf(b)->setCell("A5", "=C1");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "B5"), "=D1");
}

TEST_F(SheetMergeTest, anEmptiedRowIsNotARemovedOne)
{
    sheetOf(_ancestor)->setCell("A5", "5");
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->clear(App::CellAddress("A2"));
    reopen(a);
    sheetOf(a)->insertRows(0, 1);
    sheetOf(b)->setCell("A3", "7");
    sheetOf(b)->setCell("B5", "=A5");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_EQ(contents(merged.document, "A3"), "");
    EXPECT_EQ(contents(merged.document, "A4"), "7");
    EXPECT_EQ(contents(merged.document, "B6"), "=A6");
}

TEST_F(SheetMergeTest, aFormulaBelowARowThatLostItsIdIsAConflict)
{
    addSheet(_ancestor, "Other")->setCell("A1", "0");
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    // A2 and A3 lose their ids: emptied or removed, nothing below them can tell which.
    sheetOf(b)->removeRows(1, 2);
    reopen(b);
    sheetOf(a, "Other")->setCell("A1", "=Sheet.A5");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("names A5"), std::string::npos);
}

TEST_F(SheetMergeTest, oneAliasGivenToTwoCellsIsAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->setAlias(App::CellAddress("A1"), "len");
    sheetOf(b)->setAlias(App::CellAddress("A2"), "len");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("alias len"), std::string::npos);
}

TEST_F(SheetMergeTest, mergedCellsThatOverlapAreAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->mergeCells(App::Range("E1:F2"));
    sheetOf(b)->mergeCells(App::Range("F2:G3"));

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("E1"), std::string::npos);
}

TEST_F(SheetMergeTest, aCellWrittenUnderTheOtherSidesMergedCellsIsAConflict)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->mergeCells(App::Range("E1:F2"));
    sheetOf(b)->setCell("F2", "hidden");

    const App::MergedDocument merged = merge(a, b);

    EXPECT_EQ(merged.document, nullptr);
    ASSERT_EQ(merged.report.conflicts.size(), 1U);
    EXPECT_NE(merged.report.conflicts.front().detail.find("F2 lies under"), std::string::npos);
}

TEST_F(SheetMergeTest, cellsMergedBesideTheOtherSidesEditsBothLand)
{
    App::Document* a = branch("a");
    App::Document* b = branch("b");
    sheetOf(a)->mergeCells(App::Range("A1:B2"));
    sheetOf(b)->setCell("C1", "beside");

    const App::MergedDocument merged = merge(a, b);

    ASSERT_NE(merged.document, nullptr) << merged.refusal;
    EXPECT_TRUE(sheetOf(merged.document)->isMergedCell(App::CellAddress("B2")));
    EXPECT_EQ(contents(merged.document, "C1"), "'beside");
}

}  // namespace
