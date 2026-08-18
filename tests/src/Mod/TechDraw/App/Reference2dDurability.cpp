// SPDX-License-Identifier: LGPL-2.1-or-later

// Regression test for the TechDraw 2D-picked-dimension "silent wrong-edge" leak.
//
// A DrawViewDimension picked on the projected 2D geometry (References2D) stores a
// positional sub-name ("EdgeN"). TechDraw tries to keep that reference durable by
// saving the referenced edge's shape (DrawViewDimension::SavedGeometry) and
// re-matching it after re-projection (DimensionAutoCorrect). That re-match is by
// coordinate equality (GeometryMatcher::compareLines -> endpoint compare) and its
// "find a *similar* edge" fallback is unimplemented, so it fails the moment the
// referenced edge moves or resizes: the stale positional sub then resolves to an
// unrelated surviving edge, with the dimension still reporting "Up-to-date".
//
// This test drives a real headless projection, references the left notch's bottom
// edge, then edits the model so (a) that edge moves (defeating the exact-geometry
// re-match) and (b) the projected edge array is renumbered. Today the reference
// silently jumps to an edge of a newly-added feature on the *right* side of the
// part; the durable behaviour is for it to stay on the (moved) left-side edge.
// The invariant asserted is scale-sign robust: the resolved edge must remain on the
// left (midpoint x < 0), where the referenced notch lives.

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Interpreter.h>

#include "Mod/TechDraw/App/DrawUtil.h"
#include "Mod/TechDraw/App/DrawViewDimension.h"
#include "Mod/TechDraw/App/DrawViewPart.h"
#include "Mod/TechDraw/App/Geometry.h"
#include "src/App/InitApplication.h"

namespace
{

// Run a block of Python in the embedded interpreter, one line at a time.
void runPy(const std::vector<std::string>& lines)
{
    for (const auto& line : lines) {
        Base::Interpreter().runInteractiveString(line.c_str());
    }
}

}  // namespace

class Reference2dDurabilityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // Importing the modules registers their document-object types with App.
        runPy({"import FreeCAD", "import Part", "import TechDraw"});
    }

    void SetUp() override
    {
        // Build the "before" state: a box with a single notch on the left, projected
        // into a DrawViewPart, with a length dimension picked on the notch's bottom
        // edge (a left-side, x < 0 edge in the projection).
        runPy({
            "_doc = FreeCAD.newDocument('Ref2dDurability')",
            "_box = _doc.addObject('Part::Box', 'Box')",
            "_box.Length = 40; _box.Width = 20; _box.Height = 20",
            "_n1 = _doc.addObject('Part::Box', 'N1')",
            "_n1.Length = 6; _n1.Width = 30; _n1.Height = 6",
            "_n1.Placement.Base = FreeCAD.Vector(4, -5, 14)",
            "_cut = _doc.addObject('Part::Cut', 'Cut')",
            "_cut.Base = _box; _cut.Tool = _n1",
            "_page = _doc.addObject('TechDraw::DrawPage', 'Page')",
            "_tmpl = _doc.addObject('TechDraw::DrawSVGTemplate', 'Template')",
            "_page.Template = _tmpl",
            "_view = _doc.addObject('TechDraw::DrawViewPart', 'View')",
            "_page.addView(_view)",
            "_view.Source = [_cut]",
            "_doc.recompute()",
            // Pick the notch bottom edge: the projected edge whose midpoint sits at
            // the notch-bottom height and on the left (x < 0). Choose it dynamically
            // so the test does not hard-code an HLR edge index.
            "def _find_target(v):\n"
            "    best = None\n"
            "    i = 0\n"
            "    while True:\n"
            "        try:\n"
            "            e = v.getEdgeByIndex(i)\n"
            "        except Exception:\n"
            "            break\n"
            "        c = e.CenterOfMass\n"
            "        if c.x < 0 and 3.0 < c.y < 5.0:\n"
            "            best = i\n"
            "        i += 1\n"
            "        if i > 300:\n"
            "            break\n"
            "    return best",
            "_tgt = _find_target(_view)",
            "assert _tgt is not None, 'could not locate notch-bottom edge'",
            "_dim = _doc.addObject('TechDraw::DrawViewDimension', 'Dim')",
            "_page.addView(_dim)",
            "_dim.Type = 'Distance'",
            "_dim.References2D = [(_view, 'Edge%d' % _tgt)]",
            "_doc.recompute()",
        });
        _doc = App::GetApplication().getDocument("Ref2dDurability");
        ASSERT_NE(_doc, nullptr);
        _view = dynamic_cast<TechDraw::DrawViewPart*>(_doc->getObject("View"));
        _dim = dynamic_cast<TechDraw::DrawViewDimension*>(_doc->getObject("Dim"));
        ASSERT_NE(_view, nullptr);
        ASSERT_NE(_dim, nullptr);
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
        }
        _doc = nullptr;
        _view = nullptr;
        _dim = nullptr;
    }

    // Resolve the dimension's current 2D reference the way the dimension code does
    // (positional sub-name -> projected geometry) and return the referenced edge's
    // projected midpoint. Returns false if the reference cannot be resolved.
    bool resolveRefMidpoint(Base::Vector3d& out) const
    {
        const std::vector<std::string>& subs = _dim->References2D.getSubValues();
        if (subs.empty()) {
            return false;
        }
        int idx = TechDraw::DrawUtil::getIndexFromName(subs.front());
        TechDraw::BaseGeomPtr geom = _view->getGeomByIndex(idx);
        if (!geom) {
            return false;
        }
        out = geom->getMidPoint();
        return true;
    }

    App::Document* _doc = nullptr;
    TechDraw::DrawViewPart* _view = nullptr;
    TechDraw::DrawViewDimension* _dim = nullptr;
};

// The reference is picked on a left-side (x < 0) edge; sanity-check the fixture.
TEST_F(Reference2dDurabilityTest, referenceStartsOnLeftSideEdge)
{
    Base::Vector3d mid;
    ASSERT_TRUE(resolveRefMidpoint(mid));
    EXPECT_LT(mid.x, 0.0) << "fixture picked the wrong edge; expected a left-side edge";
}

// After an edit that moves the referenced edge AND renumbers the projected edges,
// the 2D reference must still resolve to a left-side edge (the moved notch edge),
// not silently jump to a newly-added feature on the right. This FAILS today (the
// stale positional sub resolves to a right-side edge) and passes once References2D
// carries durable identity through the projection.
TEST_F(Reference2dDurabilityTest, referenceSurvivesEditThatMovesAndRenumbersEdge)
{
    Base::Vector3d before;
    ASSERT_TRUE(resolveRefMidpoint(before));
    ASSERT_LT(before.x, 0.0);

    // Edit: deepen the left notch (its bottom edge moves) and add a second notch on
    // the right (renumbers the projected edge array).
    runPy({
        "_n1.Height = 9",
        "_n1.Placement.Base = FreeCAD.Vector(4, -5, 11)",
        "_n2 = _doc.addObject('Part::Box', 'N2')",
        "_n2.Length = 6; _n2.Width = 30; _n2.Height = 6",
        "_n2.Placement.Base = FreeCAD.Vector(28, -5, 14)",
        "_cut2 = _doc.addObject('Part::Cut', 'Cut2')",
        "_cut2.Base = _cut; _cut2.Tool = _n2",
        "_view.Source = [_cut2]",
        "_doc.recompute()",
    });

    Base::Vector3d after;
    ASSERT_TRUE(resolveRefMidpoint(after));
    EXPECT_LT(after.x, 0.0)
        << "2D dimension reference silently jumped to a right-side edge after the "
           "referenced edge moved and the projection was renumbered";
}
