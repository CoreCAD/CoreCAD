// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Sean Barton (Cruth)

/* A cell's address is a position: inserting a row renumbers every cell below it. Merging two
 * versions of a sheet cell by cell needs something that stays put while the address moves. Rows and
 * columns carry the ids, since whole rows and columns are what people insert and remove; a cell is
 * its row's id and its column's id. */

#include <gtest/gtest.h>

#include "src/App/InitApplication.h"

#include <memory>
#include <regex>
#include <sstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Range.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Spreadsheet/App/PropertySheet.h>
#include <Mod/Spreadsheet/App/Sheet.h>

namespace
{

class CellIdentityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("cellIdentity");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _sheet = addSheet("Sheet");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    Spreadsheet::Sheet* addSheet(const char* name)
    {
        return static_cast<Spreadsheet::Sheet*>(_doc->addObject("Spreadsheet::Sheet", name));
    }

    static std::string rowId(Spreadsheet::Sheet* sheet, const char* address)
    {
        return sheet->getCells()->rowId(App::CellAddress(address).row());
    }

    static std::string columnId(Spreadsheet::Sheet* sheet, const char* address)
    {
        return sheet->getCells()->columnId(App::CellAddress(address).col());
    }

    static std::string saved(Spreadsheet::Sheet* sheet)
    {
        Base::StringWriter writer;
        sheet->getCells()->Save(writer);
        return writer.getString();
    }

    static void restore(Spreadsheet::Sheet* sheet, const std::string& file)
    {
        std::istringstream stored("<root>" + file + "</root>");
        Base::XMLReader reader("Cells", stored);
        sheet->getCells()->Restore(reader);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _docName {};
    App::Document* _doc {};
    Spreadsheet::Sheet* _sheet {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(CellIdentityTest, everyRowAndColumnWithACellHasAnId)
{
    _sheet->setCell("A1", "1");
    _sheet->setCell("B2", "2");

    EXPECT_FALSE(rowId(_sheet, "A1").empty());
    EXPECT_FALSE(columnId(_sheet, "B2").empty());
    EXPECT_NE(rowId(_sheet, "A1"), rowId(_sheet, "A2"));
    EXPECT_NE(columnId(_sheet, "A1"), columnId(_sheet, "B1"));
    EXPECT_TRUE(rowId(_sheet, "A9").empty()) << "a row that never held a cell needs no id";
}

TEST_F(CellIdentityTest, aRowKeepsItsIdWhenARowIsInsertedAboveIt)
{
    _sheet->setCell("A2", "2");
    const std::string id = rowId(_sheet, "A2");

    _sheet->insertRows(0, 1);

    EXPECT_EQ(rowId(_sheet, "A3"), id) << "the row moved down and became a different row";
    EXPECT_TRUE(rowId(_sheet, "A2").empty());
}

TEST_F(CellIdentityTest, aRemovedRowTakesItsIdWithIt)
{
    _sheet->setCell("A1", "1");
    _sheet->setCell("A2", "2");
    const std::string removed = rowId(_sheet, "A1");
    const std::string kept = rowId(_sheet, "A2");

    _sheet->removeRows(0, 1);

    EXPECT_EQ(rowId(_sheet, "A1"), kept);
    EXPECT_TRUE(rowId(_sheet, "A2").empty());
    EXPECT_NE(rowId(_sheet, "A1"), removed);
}

TEST_F(CellIdentityTest, removingSeveralRowsShiftsTheRestByThatMany)
{
    for (const char* address : {"A1", "A2", "A3", "A4"}) {
        _sheet->setCell(address, "1");
    }
    const std::string first = rowId(_sheet, "A1");
    const std::string last = rowId(_sheet, "A4");

    _sheet->removeRows(1, 2);

    EXPECT_EQ(rowId(_sheet, "A1"), first);
    EXPECT_EQ(rowId(_sheet, "A2"), last);
    EXPECT_TRUE(rowId(_sheet, "A3").empty());
}

TEST_F(CellIdentityTest, aColumnKeepsItsIdWhenAColumnIsInsertedBeforeIt)
{
    _sheet->setCell("B1", "1");
    const std::string id = columnId(_sheet, "B1");

    _sheet->insertColumns(0, 2);

    EXPECT_EQ(columnId(_sheet, "D1"), id);
    EXPECT_TRUE(columnId(_sheet, "B1").empty());
}

TEST_F(CellIdentityTest, aColumnKeepsItsIdWhenAColumnBeforeItIsRemoved)
{
    _sheet->setCell("A1", "1");
    _sheet->setCell("C1", "3");
    const std::string id = columnId(_sheet, "C1");

    _sheet->removeColumns(0, 1);

    EXPECT_EQ(columnId(_sheet, "B1"), id);
}

TEST_F(CellIdentityTest, theIdsSurviveASaveAndReopen)
{
    _sheet->setCell("B2", "=1+1");

    Spreadsheet::Sheet* reopened = addSheet("Reopened");
    restore(reopened, saved(_sheet));

    EXPECT_EQ(rowId(reopened, "B2"), rowId(_sheet, "B2"));
    EXPECT_EQ(columnId(reopened, "B2"), columnId(_sheet, "B2"));
}

TEST_F(CellIdentityTest, aFileWithoutIdsStillOpensWithIds)
{
    _sheet->setCell("A1", "5");
    std::string file = saved(_sheet);
    file = std::regex_replace(file, std::regex(R"( rows="\d+" columns="\d+")"), "");
    file = std::regex_replace(file, std::regex(R"(<(Row|Column) [^>]*/>)"), "");

    Spreadsheet::Sheet* reopened = addSheet("Reopened");
    restore(reopened, file);

    EXPECT_FALSE(rowId(reopened, "A1").empty());
    EXPECT_FALSE(columnId(reopened, "A1").empty());
}

TEST_F(CellIdentityTest, twoRowsInOneFileNeverShareAnId)
{
    _sheet->setCell("A1", "1");
    _sheet->setCell("A2", "2");
    const std::string shared = rowId(_sheet, "A1");
    std::string file = saved(_sheet);
    file.replace(file.find(rowId(_sheet, "A2")), shared.size(), shared);

    Spreadsheet::Sheet* reopened = addSheet("Reopened");
    restore(reopened, file);

    EXPECT_EQ(rowId(reopened, "A1"), shared);
    EXPECT_FALSE(rowId(reopened, "A2").empty());
    EXPECT_NE(rowId(reopened, "A2"), shared);
}

TEST_F(CellIdentityTest, undoingAnInsertGivesBackTheSameRows)
{
    _sheet->setCell("A1", "1");
    const std::string id = rowId(_sheet, "A1");

    std::unique_ptr<App::Property> before(_sheet->getCells()->Copy());
    _sheet->insertRows(0, 1);
    _sheet->getCells()->Paste(*before);

    EXPECT_EQ(rowId(_sheet, "A1"), id);
    EXPECT_TRUE(rowId(_sheet, "A2").empty());
}

TEST_F(CellIdentityTest, aDuplicatedSheetHasRowsAndColumnsOfItsOwn)
{
    _sheet->setCell("A1", "1");

    Spreadsheet::Sheet* copy = addSheet("Copy");
    std::unique_ptr<App::Property> contents(_sheet->getCells()->Copy());
    copy->getCells()->Paste(*contents);
    copy->getCells()->mintDurableIdentity();

    EXPECT_FALSE(rowId(copy, "A1").empty());
    EXPECT_NE(rowId(copy, "A1"), rowId(_sheet, "A1"));
    EXPECT_NE(columnId(copy, "A1"), columnId(_sheet, "A1"));
}

}  // namespace
