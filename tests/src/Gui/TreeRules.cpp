// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>

#include <QGuiApplication>

#include <App/Application.h>
#include <App/Document.h>
#include <App/FeatureTest.h>

#include <Gui/TreeHistory.h>
#include <Gui/TreeLineage.h>
#include <Gui/TreeSwatches.h>

#include <src/App/InitApplication.h>

/** The tree's rules for ordering the history and tinting a selection's lineage, asked directly,
 * with no tree on screen. Each body's colour is handed in as its row's Bodies column would report
 * it, so a test states the bodies it means rather than building them.
 */
class TreeRulesTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        doc = App::GetApplication().newDocument("TreeRules", "TreeRules");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }

    App::FeatureTest* step(const char* name, std::vector<App::DocumentObject*> inputs = {})
    {
        auto* obj = static_cast<App::FeatureTest*>(doc->addObject("App::FeatureTest", name));
        obj->LinkList.setValues(inputs);
        return obj;
    }

    /// @p row builds @p body, drawn in @p color.
    void builds(App::DocumentObject* row, App::DocumentObject* body, const QColor& color)
    {
        auto& column = columns[row];
        column.partOf.push_back({{color}, {body}});
        column.builds.push_back({{color}, {body}});
        columns[body].swatchRow = row;
    }

    Gui::TreeLineage trace(std::set<App::DocumentObject*> selected)
    {
        return Gui::TreeLineage::trace(
            std::move(selected),
            [this](App::DocumentObject* obj) {
                auto found = columns.find(obj);
                return found == columns.end() ? Gui::ViewProvider::TreeBodyColumn {} : found->second;
            },
            highlight
        );
    }

    App::Document* doc {};
    std::map<App::DocumentObject*, Gui::ViewProvider::TreeBodyColumn> columns;
    const QColor red {Qt::red};
    const QColor blue {Qt::blue};
    const QColor highlight {Qt::yellow};
};

// --- history ----------------------------------------------------------------------------------

TEST_F(TreeRulesTest, aDroppedRowLandsAboveOrBelowTheRowUnderTheMouse)
{
    auto* a = step("A");
    auto* b = step("B");
    auto* c = step("C");
    using Gui::TreeHistory::placeDropped;
    EXPECT_EQ(placeDropped({a, b, c}, {c}, a, false), (std::vector<App::DocumentObject*> {c, a, b}));
    EXPECT_EQ(placeDropped({a, b, c}, {a}, b, true), (std::vector<App::DocumentObject*> {b, a, c}));
}

TEST_F(TreeRulesTest, aStepMustComeAfterWhatItIsBuiltFrom)
{
    auto* source = step("Source");
    auto* user = step("User", {source});
    EXPECT_TRUE(Gui::TreeHistory::keepsInputsFirst({source, user}));
    EXPECT_FALSE(Gui::TreeHistory::keepsInputsFirst({user, source}));
}

TEST_F(TreeRulesTest, theOriginMustComeBeforeEveryStep)
{
    auto* origin = doc->addObject("App::Origin", "Origin");
    auto* first = step("First");
    EXPECT_TRUE(Gui::TreeHistory::keepsInputsFirst({origin, first}));
    EXPECT_FALSE(Gui::TreeHistory::keepsInputsFirst({first, origin}));
}

TEST_F(TreeRulesTest, anObjectLeavingTheHistorySortsLast)
{
    auto* first = step("First");
    auto position = Gui::TreeHistory::historyPositions(doc);
    EXPECT_EQ(Gui::TreeHistory::positionIn(position, first), position.at(first));
    EXPECT_EQ(Gui::TreeHistory::positionIn(position, nullptr), std::numeric_limits<int>::max());
}

// --- lineage ----------------------------------------------------------------------------------

TEST_F(TreeRulesTest, aSelectedStepTintsWhatItCameFromNeverWhatCameAfter)
{
    auto* sketch = step("Sketch");
    auto* pad = step("Pad", {sketch});
    auto* pocket = step("Pocket", {pad});
    auto* body = step("Body");
    builds(pad, body, red);
    builds(pocket, body, red);

    auto lineage = trace({pad});
    EXPECT_EQ(lineage.tints.at(sketch), red);
    EXPECT_EQ(lineage.tints.at(pad), red);
    EXPECT_FALSE(lineage.tints.count(pocket));
    EXPECT_EQ(lineage.selected, (std::set<App::DocumentObject*> {pad}));
}

TEST_F(TreeRulesTest, theOriginIsLeftOutOfEveryLineage)
{
    auto* origin = doc->addObject("App::Origin", "Origin");
    auto* sketch = step("Sketch", {origin});
    auto* pad = step("Pad", {sketch});

    auto lineage = trace({pad});
    EXPECT_TRUE(lineage.tints.count(sketch));
    EXPECT_FALSE(lineage.tints.count(origin));
}

TEST_F(TreeRulesTest, aRowWithNoBodyToGoByTakesTheHighlight)
{
    auto* sketch = step("Sketch");
    auto* user = step("User", {sketch});

    auto lineage = trace({user});
    EXPECT_EQ(lineage.tints.at(sketch), highlight);
    EXPECT_EQ(lineage.tints.at(user), highlight);
}

// A step of another body that this one was built on is tinted in this body's colour; what went
// into that step belongs to the other body's lineage and stays out.
TEST_F(TreeRulesTest, anotherBodysStepIsBorrowedButNotItsInputs)
{
    auto* sketchA = step("SketchA");
    auto* padA = step("PadA", {sketchA});
    auto* sketchB = step("SketchB");
    auto* padB = step("PadB", {padA, sketchB});
    builds(padA, step("BodyA"), blue);
    builds(padB, step("BodyB"), red);

    auto lineage = trace({padB});
    EXPECT_EQ(lineage.tints.at(padA), red);
    EXPECT_EQ(lineage.tints.at(sketchB), red);
    EXPECT_FALSE(lineage.tints.count(sketchA));
}

// A pattern's copy names the step it copied, not that step's sketch, even when the selection also
// uses the sketch directly.
TEST_F(TreeRulesTest, anInputOfABorrowedStepStaysOutEvenWhenUsedDirectly)
{
    auto* sketch = step("Sketch");
    auto* padA = step("PadA", {sketch});
    auto* copy = step("Copy", {padA, sketch});
    builds(padA, step("BodyA"), blue);
    builds(copy, step("BodyB"), red);

    auto lineage = trace({copy});
    EXPECT_TRUE(lineage.tints.count(padA));
    EXPECT_FALSE(lineage.tints.count(sketch));
}

// Clicking a body's swatch starts from the row its swatch stands on and tints that row's lineage
// in the body's colour.
TEST_F(TreeRulesTest, aSelectedBodyStartsFromItsSwatchRow)
{
    auto* sketch = step("Sketch");
    auto* pad = step("Pad", {sketch});
    auto* pocket = step("Pocket", {pad});
    auto* later = step("Later", {pocket});
    auto* body = step("Body");
    builds(pad, body, red);
    builds(pocket, body, red);

    auto lineage = trace({body});
    EXPECT_EQ(lineage.tints.at(pocket), red);
    EXPECT_EQ(lineage.tints.at(pad), red);
    EXPECT_EQ(lineage.tints.at(sketch), red);
    EXPECT_FALSE(lineage.tints.count(later));
}

// --- swatches ---------------------------------------------------------------------------------

/// A click on the Bodies column means the body whose square it lands on; past three bodies from
/// one step, the ellipsis and count stand for all of them.
class TreeSwatchesTest: public TreeRulesTest
{
protected:
    static void SetUpTestSuite()
    {
        TreeRulesTest::SetUpTestSuite();
        if (!qApp) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char name[] = "TreeRules";
            static char* argv[] = {name, nullptr};
            new QGuiApplication(argc, argv);  // NOLINT: lives for the whole run
        }
    }

    std::vector<App::DocumentObject*> bodiesAt(
        const Gui::ViewProvider::TreeBodyColumn& column,
        const QPoint& point
    )
    {
        std::vector<Gui::TreeSwatches::SwatchHit> hits;
        Gui::TreeSwatches::paintBodySwatches(column, &hits);
        for (const auto& hit : hits) {
            if (hit.rect.contains(point)) {
                return hit.bodies;
            }
        }
        return {};
    }
};

TEST_F(TreeSwatchesTest, aRowThatBuildsNoBodyDrawsNothing)
{
    EXPECT_TRUE(Gui::TreeSwatches::paintBodySwatches({}).isNull());
}

TEST_F(TreeSwatchesTest, eachSquareMeansItsOwnBody)
{
    auto* first = step("First");
    auto* second = step("Second");
    Gui::ViewProvider::TreeBodyColumn column;
    column.builds.push_back({{red, blue}, {first, second}});

    EXPECT_EQ(bodiesAt(column, {2, 5}), (std::vector<App::DocumentObject*> {first}));
    EXPECT_EQ(bodiesAt(column, {14, 5}), (std::vector<App::DocumentObject*> {second}));
}

TEST_F(TreeSwatchesTest, theCountPastThreeMeansEveryBody)
{
    std::vector<App::DocumentObject*> bodies;
    std::vector<QColor> colors;
    for (const char* name : {"A", "B", "C", "D", "E"}) {
        bodies.push_back(step(name));
        colors.push_back(red);
    }
    Gui::ViewProvider::TreeBodyColumn column;
    column.builds.push_back({colors, bodies});

    QPixmap pixmap = Gui::TreeSwatches::paintBodySwatches(column);
    EXPECT_EQ(bodiesAt(column, {pixmap.width() - 2, 5}), bodies);
    EXPECT_EQ(bodiesAt(column, {2, 5}), (std::vector<App::DocumentObject*> {bodies.front()}));
}
