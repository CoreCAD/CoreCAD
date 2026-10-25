// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <Mod/Part/App/FeaturePartBox.h>
#include <Mod/Part/App/PrimitiveFeature.h>

#include <set>


class AttachExtensionTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    App::Document* getDocument() const
    {
        return _doc;
    }

private:
    std::string _docName;
    App::Document* _doc = nullptr;
};

TEST_F(AttachExtensionTest, testPlanePlane)
{
    auto plane1 = getDocument()->addObject<Part::Plane>("Plane1");
    auto plane2 = getDocument()->addObject<Part::Plane>("Plane2");

    ASSERT_TRUE(plane1);
    ASSERT_TRUE(plane2);

    getDocument()->recompute();

    plane2->MapReversed.setValue(false);
    plane2->AttachmentSupport.setValue(plane1);
    plane2->MapPathParameter.setValue(0.0);
    plane2->MapMode.setValue("FlatFace");

    getDocument()->recompute();
    EXPECT_TRUE(true);
}

TEST_F(AttachExtensionTest, testAttacherEngineType)
{
    auto plane = getDocument()->addObject<Part::Plane>("Plane");
    EXPECT_STREQ(plane->AttacherType.getValue(), "Attacher::AttachEngine3D");
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine 3D");

    plane->AttacherEngine.setValue(1L);
    EXPECT_STREQ(plane->AttacherType.getValue(), "Attacher::AttachEnginePlane");
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Plane");

    plane->AttacherEngine.setValue(2L);
    EXPECT_STREQ(plane->AttacherType.getValue(), "Attacher::AttachEngineLine");
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Line");

    plane->AttacherEngine.setValue(3L);
    EXPECT_STREQ(plane->AttacherType.getValue(), "Attacher::AttachEnginePoint");
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Point");
}

TEST_F(AttachExtensionTest, testAttacherTypeEngine)
{
    auto plane = getDocument()->addObject<Part::Plane>("Plane");
    EXPECT_STREQ(plane->AttacherType.getValue(), "Attacher::AttachEngine3D");
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine 3D");

    plane->AttacherType.setValue("Attacher::AttachEnginePlane");
    plane->Part::AttachExtension::onExtendedDocumentRestored();
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Plane");

    plane->AttacherType.setValue("Attacher::AttachEngineLine");
    plane->Part::AttachExtension::onExtendedDocumentRestored();
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Line");

    plane->AttacherType.setValue("Attacher::AttachEnginePoint");
    plane->Part::AttachExtension::onExtendedDocumentRestored();
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine Point");

    plane->AttacherType.setValue("Attacher::AttachEngine3D");
    plane->Part::AttachExtension::onExtendedDocumentRestored();
    EXPECT_STREQ(plane->AttacherEngine.getValueAsString(), "Engine 3D");
}

// A plane on a cone's top, and the top shrinks to a point. Without names the bottom face took the
// top's number and the plane moved there without a word.
TEST_F(AttachExtensionTest, aShapeOnAFaceThatIsGoneStopsInsteadOfMoving)
{
    auto cone = getDocument()->addObject<Part::Cone>("Cone");
    cone->Radius1.setValue(2.0);
    cone->Radius2.setValue(1.0);
    cone->Height.setValue(10.0);
    auto plane = getDocument()->addObject<Part::Plane>("Plane");
    plane->AttachmentSupport.setValue(cone, std::vector<std::string> {"Face2"});
    plane->MapMode.setValue("FlatFace");
    getDocument()->recompute();
    ASSERT_DOUBLE_EQ(plane->Placement.getValue().getPosition().z, 10.0);

    cone->Radius2.setValue(0.0);
    getDocument()->recompute();

    EXPECT_TRUE(plane->isError());
    EXPECT_DOUBLE_EQ(plane->Placement.getValue().getPosition().z, 10.0);
}

// Each face of a box is named for the side it faces, and keeps that name wherever the box is put.
TEST_F(AttachExtensionTest, aBoxNamesEachFaceForTheSideItFaces)
{
    auto box = getDocument()->addObject<Part::Box>("Box");
    getDocument()->recompute();
    std::set<std::string> names;
    for (int i = 1; i <= 6; ++i) {
        const std::string face = "Face" + std::to_string(i);
        names.insert(box->getElementName(face.c_str()).newName);
    }
    EXPECT_EQ(names.size(), 6U);
    EXPECT_EQ(names.count(""), 0U);

    box->Placement.setValue(
        Base::Placement(Base::Vector3d(5, 0, 0), Base::Rotation(Base::Vector3d(0, 0, 1), 1.0))
    );
    getDocument()->recompute();
    std::set<std::string> moved;
    for (int i = 1; i <= 6; ++i) {
        const std::string face = "Face" + std::to_string(i);
        moved.insert(box->getElementName(face.c_str()).newName);
    }
    EXPECT_EQ(moved, names);
}

// A square prism's sides sit at 45 degrees to the axes, so none faces one axis more than another.
// Each face keeps one name however the prism is turned, so turning it never moves what is on it.
TEST_F(AttachExtensionTest, aFaceKeepsItsNameHoweverThePartIsTurned)
{
    auto prism = getDocument()->addObject<Part::Prism>("Prism");
    prism->Polygon.setValue(4);
    auto namesAt = [&](double degrees) {
        prism->Placement.setValue(
            Base::Placement(
                Base::Vector3d(3, 0, 0),
                Base::Rotation(Base::Vector3d(0, 0, 1), Base::toRadians(degrees))
            )
        );
        getDocument()->recompute();
        std::vector<std::string> names;
        for (int i = 1; i <= 6; ++i) {
            const std::string face = "Face" + std::to_string(i);
            names.push_back(prism->getElementName(face.c_str()).newName);
        }
        return names;
    };

    const std::vector<std::string> unturned = namesAt(0.0);
    EXPECT_EQ(namesAt(10.0), unturned);
    EXPECT_EQ(namesAt(33.0), unturned);
    EXPECT_EQ(namesAt(77.0), unturned);
}

// A plane on an edge of a wedge's top, and the top narrows to a line. Without names another edge
// took the gone edge's number and the plane moved there without a word.
TEST_F(AttachExtensionTest, aShapeOnAnEdgeThatIsGoneStopsInsteadOfMoving)
{
    auto wedge = getDocument()->addObject<Part::Wedge>("Wedge");
    getDocument()->recompute();
    auto plane = getDocument()->addObject<Part::Plane>("Plane");
    plane->AttachmentSupport.setValue(wedge, std::vector<std::string> {"Edge7"});
    plane->MapMode.setValue("NormalToEdge");
    getDocument()->recompute();
    const Base::Vector3d onTop = plane->Placement.getValue().getPosition();
    ASSERT_DOUBLE_EQ(onTop.y, 10.0);

    wedge->X2min.setValue(5.0);
    wedge->X2max.setValue(5.0);
    getDocument()->recompute();

    EXPECT_TRUE(plane->isError());
    EXPECT_EQ(plane->Placement.getValue().getPosition(), onTop);
}

// Each edge of a box is named for the two sides it joins, and keeps that name wherever the box is
// put.
TEST_F(AttachExtensionTest, aBoxNamesEachEdgeForTheSidesItJoins)
{
    auto box = getDocument()->addObject<Part::Box>("Box");
    auto names = [&]() {
        getDocument()->recompute();
        std::set<std::string> found;
        for (int i = 1; i <= 12; ++i) {
            const std::string edge = "Edge" + std::to_string(i);
            found.insert(box->getElementName(edge.c_str()).newName);
        }
        return found;
    };
    const std::set<std::string> unmoved = names();
    EXPECT_EQ(unmoved.size(), 12U);
    EXPECT_EQ(unmoved.count(""), 0U);
    for (const std::string& name : unmoved) {
        const std::string mapped = name.substr(0, name.rfind('.'));
        EXPECT_EQ(mapped.find("Edge"), std::string::npos) << name;
        EXPECT_EQ(mapped.find('|'), std::string::npos) << "'|' separates names in a list: " << name;
    }

    box->Placement.setValue(
        Base::Placement(Base::Vector3d(5, 0, 0), Base::Rotation(Base::Vector3d(0, 0, 1), 1.0))
    );
    EXPECT_EQ(names(), unmoved);
}
