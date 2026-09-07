// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (c) 2026 Sean Barton (Cruth)

/* A spreadsheet's contribution to the readable document recipe. Every cell a sheet holds lives
 * inside one link-shaped property, so the generic emitter took the sheet's references and drew
 * no content out of it: a sheet of formulas wrote out as its name and nothing else, and said
 * nothing about the omission either. These prove the content is there, that it is the authored
 * text rather than the computed result, and that a change to one cell shows up as a change to
 * one node -- each with a control, so a check cannot pass by always reporting the same thing.
 */

#include <gtest/gtest.h>

#include "src/App/InitApplication.h"

#include <algorithm>
#include <set>
#include <string>

#include <App/Document.h>
#include <App/Application.h>
#include <App/Range.h>
#include <App/RecipeDetail.h>
#include <Base/Color.h>
#include <Mod/Spreadsheet/App/Cell.h>
#include <Mod/Spreadsheet/App/Sheet.h>
#include <Mod/Spreadsheet/App/SheetRecipe.h>

namespace
{

class SheetRecipeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("sheetRecipe");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _sheet = static_cast<Spreadsheet::Sheet*>(_doc->addObject("Spreadsheet::Sheet", "Sheet"));
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    /// The node the provider emits for one cell, or nullptr if it emitted none for that address.
    static const App::RecipeNode* cellNode(const App::RecipeDetail& detail, const std::string& at)
    {
        for (const App::RecipeDetailSection& section : detail.sections) {
            for (const App::RecipeNode& node : section.nodes) {
                if (node.id == at) {
                    return &node;
                }
            }
        }
        return nullptr;
    }

    static std::string field(const App::RecipeNode& node, const std::string& name)
    {
        const auto found = node.fields.find(name);
        return found != node.fields.end() ? found->second : std::string();
    }

    App::RecipeDetail detail() const
    {
        return Spreadsheet::sheetRecipeDetail(*_sheet);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _docName {};
    App::Document* _doc {};
    Spreadsheet::Sheet* _sheet {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// A literal, a formula and an alias are all authored facts about the design, and all three now
// reach the recipe. The control is the count: a sheet with three filled cells emits three nodes,
// so a provider that emitted everything (or nothing) would fail here too.
TEST_F(SheetRecipeTest, cellContentsAndAliasesReachTheRecipe)
{
    // Arrange
    _sheet->setCell("A1", "'Width");
    _sheet->setCell("B1", "40");
    _sheet->setAlias(App::CellAddress("B1"), "width");
    _sheet->setCell("B2", "=B1 * 2");
    _doc->recompute();

    // Act
    const App::RecipeDetail emitted = detail();

    // Assert
    ASSERT_EQ(emitted.sections.size(), 1u);
    EXPECT_EQ(emitted.sections.front().name, "cells");
    EXPECT_EQ(emitted.sections.front().nodes.size(), 3u);

    const App::RecipeNode* b1 = cellNode(emitted, "B1");
    ASSERT_NE(b1, nullptr);
    EXPECT_EQ(b1->type, "cell");
    EXPECT_EQ(field(*b1, "content"), "40");
    EXPECT_EQ(field(*b1, "alias"), "width");

    const App::RecipeNode* b2 = cellNode(emitted, "B2");
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(field(*b2, "content"), "=B1 * 2");
}

// The formula is the authored fact; the number it produces is an outcome the sheet recomputes.
// Recording the result would make a diff over a parametric sheet lie, exactly as recording a
// resolved expression would elsewhere in the recipe.
TEST_F(SheetRecipeTest, theFormulaIsRecordedNotTheNumberItProduces)
{
    // Arrange: a formula whose answer (80) is nowhere in its authored text.
    _sheet->setCell("B1", "40");
    _sheet->setCell("B2", "=B1 * 2");
    _doc->recompute();
    ASSERT_EQ(_sheet->getCell(App::CellAddress("B2")) != nullptr, true);

    // Act
    const App::RecipeNode* b2 = cellNode(detail(), "B2");

    // Assert
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(field(*b2, "content"), "=B1 * 2");
    EXPECT_EQ(field(*b2, "content").find("80"), std::string::npos);
}

// The measurement that decides whether the view is any use to a person: one edited cell must
// move one node and leave every other alone.
TEST_F(SheetRecipeTest, oneEditedCellChangesOneNode)
{
    // Arrange
    _sheet->setCell("A1", "'Width");
    _sheet->setCell("B1", "40");
    _sheet->setCell("B2", "=B1 * 2");
    _doc->recompute();
    const App::RecipeDetail before = detail();

    // Act
    _sheet->setCell("B1", "55");
    _doc->recompute();
    const App::RecipeDetail after = detail();

    // Assert: same cells, one differing node.
    ASSERT_EQ(before.sections.size(), 1u);
    ASSERT_EQ(after.sections.size(), 1u);
    ASSERT_EQ(before.sections.front().nodes.size(), after.sections.front().nodes.size());

    int differing = 0;
    for (std::size_t i = 0; i < after.sections.front().nodes.size(); ++i) {
        if (before.sections.front().nodes[i] != after.sections.front().nodes[i]) {
            ++differing;
        }
    }
    EXPECT_EQ(differing, 1);

    // Control: the change is the one that was made, not some incidental churn.
    const App::RecipeNode* edited = cellNode(after, "B1");
    ASSERT_NE(edited, nullptr);
    EXPECT_EQ(field(*edited, "content"), "55");
}

// Presentation is not recipe content. How a cell is shown -- its unit, colour, style, alignment
// or span -- is a display choice, filed by the architecture alongside colour and visibility, and
// the generic emitter already leaves Label and Visibility out on the same grounds. A sheet
// formatted to the hilt and a plain one must describe the same design.
TEST_F(SheetRecipeTest, presentationIsNotRecipeContent)
{
    // Arrange: two cells with identical content, one of them heavily dressed up.
    _sheet->setCell("A1", "'Heading");
    _sheet->setCell("A2", "'Heading");
    _sheet->setStyle(App::CellAddress("A1"), std::set<std::string> {"bold"});
    _sheet->setForeground(App::CellAddress("A1"), Base::Color(1.0F, 0.0F, 0.0F, 1.0F));
    _sheet->setBackground(App::CellAddress("A1"), Base::Color(0.0F, 0.0F, 1.0F, 1.0F));
    _sheet->setAlignment(App::CellAddress("A1"), Spreadsheet::Cell::decodeAlignment("center", 0));
    _sheet->setDisplayUnit(App::CellAddress("A1"), "in");
    _doc->recompute();

    // Act
    const App::RecipeDetail emitted = detail();
    const App::RecipeNode* dressed = cellNode(emitted, "A1");
    const App::RecipeNode* plain = cellNode(emitted, "A2");

    // Assert: the two are indistinguishable, because they are the same design.
    ASSERT_NE(dressed, nullptr);
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(dressed->fields, plain->fields);

    for (const char* shown :
         {"displayUnit", "style", "foreground", "background", "alignment", "spans"}) {
        EXPECT_EQ(dressed->fields.count(shown), 0u) << shown << " is a display choice, not design";
    }
}

// A provider must claim what it accounts for, or the view goes on reporting the property missing
// while printing its contents just above.
TEST_F(SheetRecipeTest, theCellsPropertyIsClaimedAsAccountedFor)
{
    _sheet->setCell("A1", "1");
    _doc->recompute();

    const App::RecipeDetail emitted = detail();
    EXPECT_NE(
        std::find(emitted.coveredProperties.begin(), emitted.coveredProperties.end(), "cells"),
        emitted.coveredProperties.end()
    );
}

// An empty sheet is not a sheet full of empty cells: it emits no section at all rather than a
// heading with nothing under it.
TEST_F(SheetRecipeTest, anEmptySheetEmitsNoSection)
{
    _doc->recompute();
    EXPECT_TRUE(detail().sections.empty());
}

}  // namespace
