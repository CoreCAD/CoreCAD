// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <algorithm>

#include <boost/core/ignore_unused.hpp>
#include "Mod/Part/App/FeaturePartCommon.h"
#include "Mod/Part/App/ShapeExtension.h"
#include "Mod/Part/App/TopoShapePy.h"
#include <src/App/InitApplication.h>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include "PartTestHelpers.h"
#include "App/MappedElement.h"
#include <App/PropertyGeo.h>
#include <Base/Matrix.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>

using namespace Part;
using namespace PartTestHelpers;

class FeaturePartTest: public ::testing::Test, public PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }


    void SetUp() override
    {
        createTestDoc();
        _common = _doc->addObject<Common>();
    }

    void TearDown() override
    {}

    Common* _common = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(FeaturePartTest, testGetElementName)
{
    // Arrange
    _common->Base.setValue(_boxes[0]);
    _common->Tool.setValue(_boxes[1]);

    // Act
    _common->execute();
    const TopoShape& ts = _common->Shape.getShape();

    auto namePair = _common->getElementName("test");
    auto namePairExport = _common->getElementName("test", App::GeoFeature::Export);
    auto namePairSelf = _common->getElementName(nullptr);
    // Assert
    EXPECT_STREQ(namePair.newName.c_str(), "");
    EXPECT_STREQ(namePair.oldName.c_str(), "test");
    EXPECT_STREQ(namePairExport.newName.c_str(), "");
    EXPECT_STREQ(namePairExport.oldName.c_str(), "test");
    EXPECT_STREQ(namePairSelf.newName.c_str(), "");
    EXPECT_STREQ(namePairSelf.oldName.c_str(), "");
    EXPECT_EQ(ts.getElementMap().size(), 26);
    // TBD
}

TEST_F(FeaturePartTest, create)
{
    // Arrange

    // A shape that will be passed to the various calls of Feature::create
    auto shape {TopoShape(BRepBuilderAPI_MakeVertex(gp_Pnt(1.0, 1.0, 1.0)).Vertex(), 1)};

    auto otherDocName {App::GetApplication().getUniqueDocumentName("otherDoc")};
    // Another document where it will be created a shape
    auto otherDoc {App::GetApplication().newDocument(otherDocName.c_str(), "otherDocUser")};

    // _doc is populated by PartTestHelperClass::createTestDoc. Making it an empty document
    _doc->clearDocument();

    // Setting the active document back to _doc otherwise the first 3 calls to Feature::create will
    // act on otherDoc
    App::GetApplication().setActiveDocument(_doc);

    // Act

    // A feature with an empty TopoShape
    auto featureNoShape {ShapeFeature::create(TopoShape())};

    // A feature with a TopoShape
    auto featureNoName {ShapeFeature::create(shape)};

    // A feature with a TopoShape and a name
    auto featureNoDoc {ShapeFeature::create(shape, "Vertex")};

    // A feature with a TopoShape and a name in the document otherDoc
    auto feature {ShapeFeature::create(shape, "Vertex", otherDoc)};

    // Assert

    // Check that the shapes have been added. Only featureNoShape should return an empty shape, the
    // others should have it as TopoShape shape is passed as argument
    EXPECT_TRUE(featureNoShape->Shape.getValue().IsNull());
    EXPECT_FALSE(featureNoName->Shape.getValue().IsNull());
    EXPECT_FALSE(featureNoDoc->Shape.getValue().IsNull());
    EXPECT_FALSE(feature->Shape.getValue().IsNull());

    // Check the features names

    // Without a name the feature's name will be set to "Shape"
    EXPECT_STREQ(_doc->getObjectName(featureNoShape), "Shape");

    // In _doc there's already a shape with name "Shape" and, as there can't be duplicated names in
    // the same document, the other feature will get an unique name that will still contain "Shape"
    EXPECT_STREQ(_doc->getObjectName(featureNoName), "Shape001");

    // There aren't other features with name "Vertex" in _doc, therefore that name will be assigned
    // without modifications
    EXPECT_STREQ(_doc->getObjectName(featureNoDoc), "Vertex");

    // The feature is created in otherDoc, which doesn't have other features and thertherefore the
    // feature's name will be assigned without modifications
    EXPECT_STREQ(otherDoc->getObjectName(feature), "Vertex");

    // Check that the features have been created in the correct document

    // The first 3 calls to Feature::create acts on _doc, which is empty, and therefore the number
    // of features in that document is the same of the features created with Feature::create
    EXPECT_EQ(_doc->getObjects().size(), 3);

    // The last call to Feature::create acts on otherDoc, which is empty, and therefore that
    // document will have only 1 feature
    EXPECT_EQ(otherDoc->getObjects().size(), 1);
}

TEST_F(FeaturePartTest, getElementHistory)
{
    // Arrange
    const char* name2 = "Edge2";  // Edge, Vertex, or Face. will work here.
    // Act
    auto result = Feature::getElementHistory(_boxes[0], name2, true, false);
    Data::HistoryItem histItem = result.front();
    // Assert
    EXPECT_EQ(result.size(), 1);
    EXPECT_NE(histItem.tag, 0);  // Make sure we have one.  It will vary.
    EXPECT_EQ(histItem.index.getIndex(), 2);
    EXPECT_STREQ(histItem.index.getType(), "Edge");
    EXPECT_STREQ(histItem.element.toString().c_str(), name2);
    EXPECT_EQ(histItem.obj, _boxes[0]);
}

TEST_F(FeaturePartTest, getRelatedElements)
{
    // Arrange
    _common->Base.setValue(_boxes[0]);
    _common->Tool.setValue(_boxes[1]);
    // Act
    _common->execute();
    auto label1 = _common->Label.getValue();
    auto label2 = _boxes[1]->Label.getValue();
    const TopoShape& ts = _common->Shape.getShape();
    boost::ignore_unused(ts);
    auto result = Feature::getRelatedElements(
        _doc->getObject(label1),
        "Edge2",
        HistoryTraceType::followTypeChange,
        true
    );
    auto result2 = Feature::getRelatedElements(
        _doc->getObject(label2),
        "Edge1",
        HistoryTraceType::followTypeChange,
        true
    );
    // Assert
    EXPECT_EQ(result.size(), 1);   // Found the one.
    EXPECT_EQ(result2.size(), 0);  // No element map, so no related elements
    // The results are always going to vary, so we can't test for specific values:
    // EXPECT_STREQ(result.front().name.toString().c_str(),"Edge3;:M;CMN;:H38d:7,E");
}

// Note that this test is pretty trivial and useless .. but the method in question is never
// called in the codebase.
TEST_F(FeaturePartTest, getElementFromSource)
{
    // Arrange
    _common->Base.setValue(_boxes[0]);
    _common->Tool.setValue(_boxes[1]);
    App::DocumentObject sourceObject;
    //    const char *sourceSubElement;
    // Act
    _common->execute();
    auto label1 = _common->Label.getValue();
    auto label2 = _boxes[1]->Label.getValue();
    const TopoShape& ts = _common->Shape.getShape();
    boost::ignore_unused(label1);
    boost::ignore_unused(label2);
    boost::ignore_unused(ts);
    auto element = Feature::getElementFromSource(
        _common,
        "Part__Box001",  // "Edge1",
        _boxes[0],
        "Face1",  // "Edge1",
        false
    );
    // Assert
    EXPECT_EQ(element.size(), 0);
}

TEST_F(FeaturePartTest, getSubObject)
{
    // Arrange
    _common->Base.setValue(_boxes[0]);
    _common->Tool.setValue(_boxes[1]);
    App::DocumentObject sourceObject;
    PyObject* pyObj;
    // Act
    _common->execute();
    auto result = _boxes[1]->getSubObject("Face5", &pyObj, nullptr, false, 10);
    // Assert
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result->getNameInDocument(), "Part__Box001");
}

// #79 step 2 (Amendment 17): Part::Box is the first concrete feature to carry its
// shape as a *composed capability* — it mixes in Part::ShapeExtension and routes
// getSubObject through it instead of the inherited ShapeFeature override. This
// test proves the routed live path hands back the *identical* element map as the
// inheritance path it replaces. The element map is the silent-failure surface the
// refactor fears — a subtly wrong map does not crash, it mis-binds a TechDraw
// dimension or a ShapeBinder weeks later. The inheritance path stays reachable as
// an oracle via an explicitly-qualified ShapeFeature::getSubObject call, which
// bypasses Box's override and does not touch the extension.
TEST_F(FeaturePartTest, shapeExtensionCarriesElementMap)
{
    // Arrange: a real box with a computed, element-mapped shape.
    auto* box = _boxes[0];
    box->execute();
    const char* ref = "Face5";

    // Oracle: the inheritance-based path, invoked explicitly past Box's override.
    PyObject* pyOracle = nullptr;
    auto* oracleOwner = box->ShapeFeature::getSubObject(ref, &pyOracle, nullptr, false, 10);
    ASSERT_NE(oracleOwner, nullptr);
    ASSERT_NE(pyOracle, nullptr);

    // Actual: the live getSubObject, now dispatched to the composed ShapeExtension.
    PyObject* pyActual = nullptr;
    auto* actualOwner = box->getSubObject(ref, &pyActual, nullptr, false, 10);
    ASSERT_NE(actualOwner, nullptr);
    ASSERT_NE(pyActual, nullptr);
    ASSERT_EQ(actualOwner, oracleOwner);

    // The element maps of the two returned sub-shapes must be identical.
    auto oracleMap = static_cast<Part::TopoShapePy*>(pyOracle)->getTopoShapePtr()->getElementMap();
    auto actualMap = static_cast<Part::TopoShapePy*>(pyActual)->getTopoShapePtr()->getElementMap();
    std::sort(oracleMap.begin(), oracleMap.end());
    std::sort(actualMap.begin(), actualMap.end());

    EXPECT_FALSE(oracleMap.empty());  // a bare face carries its face/edge/vertex names
    ASSERT_EQ(oracleMap.size(), actualMap.size());
    EXPECT_TRUE(oracleMap == actualMap);

    // Negative control: the comparison must be able to tell a wrong element map from
    // a right one. Pull a *different* face's map and confirm it does NOT match the
    // routed path's Face5 map — otherwise the equality above would be blind and
    // would pass even if the extension returned garbage.
    PyObject* pyWrong = nullptr;
    auto* wrongOwner = box->ShapeFeature::getSubObject("Face3", &pyWrong, nullptr, false, 10);
    ASSERT_NE(wrongOwner, nullptr);
    ASSERT_NE(pyWrong, nullptr);
    auto wrongMap = static_cast<Part::TopoShapePy*>(pyWrong)->getTopoShapePtr()->getElementMap();
    std::sort(wrongMap.begin(), wrongMap.end());
    EXPECT_FALSE(wrongMap == actualMap);

    Py_XDECREF(pyOracle);
    Py_XDECREF(pyActual);
    Py_XDECREF(pyWrong);
}

// #79 step 2, transform path: the proposal flagged that the extension reads
// placement via getPropertyByName("Placement") where the ported original composed
// getPlacement().toMatrix() — so transform=true parity must be verified, not
// assumed. Under a genuinely non-identity placement the routed path must (a)
// accumulate the same transform matrix and (b) return the same placed geometry as
// the inheritance oracle.
TEST_F(FeaturePartTest, shapeExtensionTransformParity)
{
    auto* box = _boxes[0];
    box->execute();
    const char* ref = "Face5";

    // A genuinely non-identity placement: translate + rotate about Z.
    auto* pla = freecad_cast<App::PropertyPlacement*>(box->getPropertyByName("Placement"));
    ASSERT_NE(pla, nullptr);
    pla->setValue(
        Base::Placement(Base::Vector3d(3, 5, 7), Base::Rotation(Base::Vector3d(0, 0, 1), 0.7))
    );

    // Oracle vs actual, both transform=true, each into its own accumulator matrix.
    Base::Matrix4D matOracle;
    PyObject* pyOracle = nullptr;
    box->ShapeFeature::getSubObject(ref, &pyOracle, &matOracle, true, 10);
    ASSERT_NE(pyOracle, nullptr);

    Base::Matrix4D matActual;
    PyObject* pyActual = nullptr;
    box->getSubObject(ref, &pyActual, &matActual, true, 10);
    ASSERT_NE(pyActual, nullptr);

    // (a) Same placement, same math → the accumulated transform must match exactly.
    EXPECT_TRUE(matOracle == matActual);

    // (b) The placed geometry must coincide: compare bounding boxes.
    auto bbOracle = static_cast<Part::TopoShapePy*>(pyOracle)->getTopoShapePtr()->getBoundBox();
    auto bbActual = static_cast<Part::TopoShapePy*>(pyActual)->getTopoShapePtr()->getBoundBox();
    EXPECT_NEAR(bbOracle.MinX, bbActual.MinX, 1e-7);
    EXPECT_NEAR(bbOracle.MinY, bbActual.MinY, 1e-7);
    EXPECT_NEAR(bbOracle.MinZ, bbActual.MinZ, 1e-7);
    EXPECT_NEAR(bbOracle.MaxX, bbActual.MaxX, 1e-7);
    EXPECT_NEAR(bbOracle.MaxY, bbActual.MaxY, 1e-7);
    EXPECT_NEAR(bbOracle.MaxZ, bbActual.MaxZ, 1e-7);

    // Negative control: pulling the same face with transform=false must NOT produce
    // the placed transform — otherwise the checks above would pass even if the
    // routed path silently ignored the placement.
    Base::Matrix4D matNoXform;
    PyObject* pyNoXform = nullptr;
    box->getSubObject(ref, &pyNoXform, &matNoXform, false, 10);
    ASSERT_NE(pyNoXform, nullptr);
    EXPECT_FALSE(matNoXform == matActual);

    Py_XDECREF(pyOracle);
    Py_XDECREF(pyActual);
    Py_XDECREF(pyNoXform);
}

TEST_F(FeaturePartTest, getElementTypes)
{
    Part::Feature pf;
    std::vector<const char*> types = pf.getElementTypes();

    EXPECT_EQ(types.size(), 3);
    EXPECT_STREQ(types[0], "Face");
    EXPECT_STREQ(types[1], "Edge");
    EXPECT_STREQ(types[2], "Vertex");
}

TEST_F(FeaturePartTest, getComplexElementTypes)
{
    Part::TopoShape shape;
    std::vector<const char*> types = shape.getElementTypes();

    EXPECT_EQ(types.size(), 3);
    EXPECT_STREQ(types[0], "Face");
    EXPECT_STREQ(types[1], "Edge");
    EXPECT_STREQ(types[2], "Vertex");
}
