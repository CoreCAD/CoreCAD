// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Placement.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <array>
#include <cstdio>
#include <string>

class UnattachedSketchPlacementTest: public ::testing::Test
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
    }

    void TearDown() override
    {
        App::GetApplication().closeAllDocuments();
        std::remove(_path.c_str());
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _path = Base::FileInfo::getTempFileName() + ".cpart";
};

// A sketch attached to nothing is where a person put it. The file states that position, and
// opening the file gives it back.
TEST_F(UnattachedSketchPlacementTest, aSketchAttachedToNothingStaysWhereItWasPut)
{
    App::Document* doc = App::GetApplication().newDocument("unattached");
    auto* sketch = static_cast<Sketcher::SketchObject*>(doc->addObject("Sketcher::SketchObject", "S"));
    sketch->Placement.setValue(Base::Placement(Base::Vector3d(0, 0, 10), Base::Rotation()));
    Part::GeomLineSegment line;
    line.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(5, 0, 0));
    sketch->addGeometry(&line);
    doc->recompute();
    ASSERT_TRUE(doc->saveAs(_path.c_str()));
    App::GetApplication().closeDocument(doc->getName());

    App::Document* reopened = App::GetApplication().openDocument(_path.c_str());
    ASSERT_NE(reopened, nullptr);
    auto* back = static_cast<Sketcher::SketchObject*>(reopened->getObject("S"));
    ASSERT_NE(back, nullptr);
    EXPECT_DOUBLE_EQ(back->Placement.getValue().getPosition().z, 10.0);
    EXPECT_DOUBLE_EQ(back->Shape.getShape().getBoundBox().MinZ, 10.0);
}
