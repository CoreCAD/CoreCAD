// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/* A sketch's lines and dimensions live inside two properties the object-level comparison cannot
 * read, so a moved line has to reach a history list through the sketch's own detail. */

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentChanges.h>
#include <App/StoredRecipe.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/Sketcher/App/SketchRecipe.h>

#include <array>
#include <sstream>
#include <string>
#include <vector>

namespace
{

class SketchChangesTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (App::Application::GetARGC() == 0) {
            constexpr int argc = 1;
            std::array<char*, argc> argv {const_cast<char*>("FreeCAD")};
            App::Application::Config()["ExeName"] = "FreeCAD";
            App::Application::init(argc, argv.data());
        }
        Sketcher::registerSketchRecipeDetail();
    }

    void SetUp() override
    {
        _before = App::GetApplication().newDocument("sketchBefore");
        _sketch = static_cast<Sketcher::SketchObject*>(
            _before->addObject("Sketcher::SketchObject", "Sketch")
        );
    }

    void TearDown() override
    {
        App::GetApplication().closeAllDocuments();
    }

    /// The next version, copied through the saved text so every line keeps its durable tag.
    Sketcher::SketchObject* nextVersion()
    {
        auto* after = App::GetApplication().newDocument("sketchAfter");
        std::istringstream text(App::formatStoredRecipe(*_before));
        App::restoreStoredRecipe(*after, text);
        _after = after;
        return static_cast<Sketcher::SketchObject*>(after->getObject("Sketch"));
    }

    static int addLine(Sketcher::SketchObject* sketch, double x1, double y1, double x2, double y2)
    {
        Part::GeomLineSegment line;
        line.setPoints(Base::Vector3d(x1, y1, 0), Base::Vector3d(x2, y2, 0));
        return sketch->addGeometry(&line);
    }

    static std::string listed(const std::vector<App::DocumentChange>& changes)
    {
        return App::formatDocumentChanges(changes);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _before {};
    App::Document* _after {};
    Sketcher::SketchObject* _sketch {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(SketchChangesTest, aMovedLineIsReportedInsideTheSketch)
{
    addLine(_sketch, 0, 0, 10, 0);
    addLine(_sketch, 0, 5, 10, 5);
    auto* after = nextVersion();
    after->moveGeometry(0, Sketcher::PointPos::end, Base::Vector3d(20, 0, 0));

    const std::vector<App::DocumentChange> changes = App::diffDocuments(*_before, *_after);

    ASSERT_EQ(changes.size(), 1u) << listed(changes);
    EXPECT_EQ(changes[0].objectId, _sketch->Uid.getValueStr());
    EXPECT_EQ(changes[0].section, "geometry");
    EXPECT_EQ(changes[0].change.field, "to");
    EXPECT_EQ(changes[0].text.rfind("Sketch, ", 0), 0u) << changes[0].text;
}

// Deleting the first line renumbers the second. Matched by tag, the second line is unchanged.
TEST_F(SketchChangesTest, deletingAnEarlyLineDoesNotReportTheRenumberedOnes)
{
    addLine(_sketch, 0, 0, 10, 0);
    addLine(_sketch, 0, 5, 10, 5);
    addLine(_sketch, 0, 9, 10, 9);
    auto* after = nextVersion();
    after->delGeometry(0);

    const std::vector<App::DocumentChange> changes = App::diffDocuments(*_before, *_after);

    ASSERT_EQ(changes.size(), 1u) << listed(changes);
    EXPECT_EQ(changes[0].section, "geometry");
    EXPECT_EQ(changes[0].change.kind, App::RecipeChange::Kind::Removed);
}

TEST_F(SketchChangesTest, anAddedDimensionIsReportedInTheConstraints)
{
    addLine(_sketch, 0, 0, 10, 0);
    auto* after = nextVersion();
    Sketcher::Constraint distance;
    distance.Type = Sketcher::Distance;
    distance.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::none));
    distance.setValue(10.0);
    after->addConstraint(&distance);

    const std::vector<App::DocumentChange> changes = App::diffDocuments(*_before, *_after);

    ASSERT_EQ(changes.size(), 1u) << listed(changes);
    EXPECT_EQ(changes[0].section, "constraints");
    EXPECT_EQ(changes[0].change.kind, App::RecipeChange::Kind::Added);
}

// The solver moves the line to meet the new length; only the length was changed by anyone.
TEST_F(SketchChangesTest, aChangedDimensionDoesNotReportTheLinesTheSolverMoved)
{
    addLine(_sketch, 0, 0, 10, 0);
    Sketcher::Constraint length;
    length.Type = Sketcher::DistanceX;
    length.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::start));
    length.setElement(1, Sketcher::GeoElementId(0, Sketcher::PointPos::end));
    length.setValue(10.0);
    const int lengthId = _sketch->addConstraint(&length);
    auto* after = nextVersion();
    after->setDatum(lengthId, 20.0);

    const std::vector<App::DocumentChange> changes = App::diffDocuments(*_before, *_after);

    ASSERT_EQ(changes.size(), 1u) << listed(changes);
    EXPECT_EQ(changes[0].section, "constraints");
    EXPECT_EQ(changes[0].change.field, "value");
}

TEST_F(SketchChangesTest, aLineDraggedAlongsideADimensionChangeIsStillReported)
{
    addLine(_sketch, 0, 0, 10, 0);
    addLine(_sketch, 0, 5, 10, 5);
    Sketcher::Constraint length;
    length.Type = Sketcher::DistanceX;
    length.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::start));
    length.setElement(1, Sketcher::GeoElementId(0, Sketcher::PointPos::end));
    length.setValue(10.0);
    const int lengthId = _sketch->addConstraint(&length);
    auto* after = nextVersion();
    after->setDatum(lengthId, 20.0);
    after->moveGeometry(1, Sketcher::PointPos::end, Base::Vector3d(30, 5, 0));

    const std::vector<App::DocumentChange> changes = App::diffDocuments(*_before, *_after);

    ASSERT_EQ(changes.size(), 2u) << listed(changes);
    EXPECT_EQ(changes[0].section, "geometry");
    EXPECT_EQ(changes[0].change.field, "to");
    EXPECT_EQ(changes[1].section, "constraints");
}

}  // namespace
