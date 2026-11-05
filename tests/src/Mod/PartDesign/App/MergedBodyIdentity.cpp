// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <cstdio>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ObjectRecipe.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

/** A merge whose two versions each keep a body whole, and whose merged part does not.
 *
 *  A block with a slot pocketed partway down and partway across it. One version takes the pocket
 *  through the block; the other takes the slot across it. Each leaves the block in one piece, so
 *  each keeps its one body, and the recipes merge with nothing to settle. Together they cut the
 *  block in two, and the rebuild retires the body and makes one per piece (§4.7): a change of
 *  identity that neither version made, which the merge has to say (Clause 21.3, item 2 of the
 *  merge proposal).
 */
class MergedBodyIdentityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void TearDown() override
    {
        App::GetApplication().closeAllDocuments();
        for (const char* leaf : {"ancestor", "a", "b"}) {
            std::remove(pathOf(leaf).c_str());
        }
    }

    std::string pathOf(const char* leaf) const
    {
        return _base + leaf + ".cpart";
    }

    /// Make the ancestor, save it, and open two copies of it as the same part.
    void makeVersions()
    {
        std::string cmd = R"(
import FreeCAD as App, Part, PartDesign, shutil
V = App.Vector
def rect(doc, name, x0, y0, x1, y1, z):
    sk = doc.addObject('Sketcher::SketchObject', name)
    xy = [o for o in doc.Objects if o.TypeId == 'App::Plane' and o.Role == 'XY_Plane'][0]
    sk.AttachmentSupport = [(xy, '')]
    sk.MapMode = 'FlatFace'
    sk.AttachmentOffset = App.Placement(V(0, 0, z), App.Rotation())
    corners = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
    return sk
d = App.newDocument('ancestor', type='Part')
block = rect(d, 'Block', 0, 0, 30, 10, 0)
d.recompute()
PartDesign.makeFeature(block, 'Pad').Length = 10
d.recompute()
body = [o for o in d.Objects if o.TypeId == 'PartDesign::Body'][0]
PartDesign.makeFeature(rect(d, 'Slot', 14, -1, 16, 6, 10), 'Pocket', body=body).Length = 6
d.recompute()
)";
        cmd += "d.saveAs('" + pathOf("ancestor") + "')\n";
        cmd += "shutil.copy('" + pathOf("ancestor") + "', '" + pathOf("a") + "')\n";
        cmd += "shutil.copy('" + pathOf("ancestor") + "', '" + pathOf("b") + "')\n";
        Base::Interpreter().runString(cmd.c_str());
        _ancestor = App::GetApplication().getDocument("ancestor");
        App::DocumentInitFlags same;
        same.duplicate = App::DuplicateAnswer::SamePart;
        _a = App::GetApplication().openDocument(pathOf("a").c_str(), same);
        _b = App::GetApplication().openDocument(pathOf("b").c_str(), same);
    }

    void run(App::Document* doc, const std::string& lines)
    {
        Base::Interpreter().runString(("d = App.getDocument('" + std::string(doc->getName())
                                       + "')\n" + lines + "d.recompute()\n")
                                          .c_str());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _base = Base::FileInfo::getTempFileName();
    App::Document* _ancestor {};
    App::Document* _a {};
    App::Document* _b {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(MergedBodyIdentityTest, aBodyTheMergedPartSplitsIsReported)
{
    makeVersions();
    ASSERT_NE(_ancestor, nullptr);
    ASSERT_NE(_a, nullptr);
    ASSERT_NE(_b, nullptr);
    run(_a, "d.getObject('Pocket').Length = 12\n");
    run(_b,
        "s = d.getObject('Slot')\n"
        "for geo, end in ((1, 2), (2, 1), (2, 2), (3, 1)):\n"
        "    x = 16 if (geo, end) in ((1, 2), (2, 1)) else 14\n"
        "    s.moveGeometry(geo, end, App.Vector(x, 11, 0))\n");

    const App::MergedDocument made = App::buildMergedDocument(*_ancestor, *_a, *_b);

    ASSERT_NE(made.document, nullptr) << made.refusal;
    EXPECT_TRUE(made.report.conflicts.empty());
    EXPECT_EQ(made.identityChanges.size(), 3U) << "one body retired, one made per piece";
}

// Control: the pocket taken through the block and renamed, the slot left as it was. The merged
// part keeps one piece, and its body.
TEST_F(MergedBodyIdentityTest, aBodyTheMergedPartKeepsWholeIsNotReported)
{
    makeVersions();
    ASSERT_NE(_ancestor, nullptr);
    run(_a, "d.getObject('Pocket').Length = 12\n");
    run(_b, "d.getObject('Pocket').Label = 'Groove'\n");

    const App::MergedDocument made = App::buildMergedDocument(*_ancestor, *_a, *_b);

    ASSERT_NE(made.document, nullptr) << made.refusal;
    EXPECT_TRUE(made.identityChanges.empty());
}
