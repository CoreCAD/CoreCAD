// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/TopoShapePy.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// #79 step 3a: a Body carries its shape as a composed Part::ShapeExtension and routes the own-shape
// half of getSubObject through it (App::DocumentObject::getSubObject dispatches to the extension),
// instead of resolving derivedTipShape() in line. These tests assert the routed capability path
// returns the same element-mapped geometry as the derived Tip shape it is backed by — the
// derived-backed counterpart of the stored-backed Part::Box parity proof.
class BodyShapeCapabilityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication()
                   .newDocument("BodyShapeCapability_test", "testUser", {.documentType = "Part"});
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addFeature(_sketch);

        _sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        _sketch->MapMode.setValue("FlatFace");
        Part::GeomCircle circle;
        circle.setRadius(10.0);
        _sketch->addGeometry(&circle, false);
        _doc->recompute();

        _pad = _doc->addObject<PartDesign::Pad>("Pad");
        _body->addFeature(_pad);
        _pad->Profile.setValue(_sketch, {""});
        _pad->Length.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    PartDesign::Pad* _pad = nullptr;
};

TEST_F(BodyShapeCapabilityTest, carriesElementMap)
{
    // A padded circle is a cylinder — Face1 is the lateral face; Face2/Face3 are the caps.
    const char* ref = "Face1";

    // Oracle: the inheritance-based path, invoked explicitly past Body's own getSubObject override
    // (which the extension now backs). ShapeFeature::getSubObject resolves the sub-element against
    // the Body's Shape mirror and never touches the extension, so it is an independent reference
    // for what the routed path must reproduce — the same style of oracle the Part::Box parity uses.
    PyObject* pyOracle = nullptr;
    auto* oracleOwner = _body->ShapeFeature::getSubObject(ref, &pyOracle, nullptr, false, 10);
    ASSERT_NE(oracleOwner, nullptr);
    ASSERT_NE(pyOracle, nullptr);
    auto oracleMap = static_cast<Part::TopoShapePy*>(pyOracle)->getTopoShapePtr()->getElementMap();

    // Actual: the live getSubObject, now dispatched to the composed Part::ShapeExtension.
    PyObject* pyActual = nullptr;
    auto* actualOwner = _body->getSubObject(ref, &pyActual, nullptr, false, 10);
    ASSERT_NE(actualOwner, nullptr);
    ASSERT_NE(pyActual, nullptr);
    ASSERT_EQ(actualOwner, oracleOwner);

    auto actualMap = static_cast<Part::TopoShapePy*>(pyActual)->getTopoShapePtr()->getElementMap();

    std::sort(oracleMap.begin(), oracleMap.end());
    std::sort(actualMap.begin(), actualMap.end());

    EXPECT_FALSE(oracleMap.empty());  // a face carries its own face/edge/vertex names
    ASSERT_EQ(oracleMap.size(), actualMap.size());
    EXPECT_TRUE(oracleMap == actualMap);

    // The routed path must carry the real topological-naming map, not fall back to plain positional
    // names: at least one element's mapped name must differ from its index (i.e. bear a ;: postfix).
    // This locks in that the capability preserves the element map rather than dropping it.
    bool anyMapped = std::any_of(actualMap.begin(), actualMap.end(), [](const auto& e) {
        return e.name.toString() != e.index.toString();
    });
    EXPECT_TRUE(anyMapped);

    // Negative control: a *different* face's map must NOT match the routed Face1 map, or the
    // equality above would be blind and pass even if the extension returned the wrong sub-shape.
    PyObject* pyWrong = nullptr;
    _body->ShapeFeature::getSubObject("Face2", &pyWrong, nullptr, false, 10);
    ASSERT_NE(pyWrong, nullptr);
    auto wrongMap = static_cast<Part::TopoShapePy*>(pyWrong)->getTopoShapePtr()->getElementMap();
    std::sort(wrongMap.begin(), wrongMap.end());
    EXPECT_FALSE(wrongMap == actualMap);

    Py_XDECREF(pyOracle);
    Py_XDECREF(pyActual);
    Py_XDECREF(pyWrong);
}

TEST_F(BodyShapeCapabilityTest, wholeShapeParity)
{
    // The whole-shape query (empty subname) through the capability must return the Body itself and
    // geometry coinciding with the derived Tip shape — the Body is unplaced, so transform=true adds
    // no placement of its own.
    Part::TopoShape derived = _body->derivedTipShape();
    ASSERT_FALSE(derived.isNull());
    Base::BoundBox3d bbDerived = derived.getBoundBox();

    Base::Matrix4D mat;
    PyObject* pyActual = nullptr;
    auto* owner = _body->getSubObject("", &pyActual, &mat, true, 10);
    ASSERT_EQ(owner, static_cast<App::DocumentObject*>(_body));
    ASSERT_NE(pyActual, nullptr);

    // A Body composes no frame of its own (§4): the accumulated transform stays identity.
    EXPECT_TRUE(mat == Base::Matrix4D());

    auto bbActual = static_cast<Part::TopoShapePy*>(pyActual)->getTopoShapePtr()->getBoundBox();
    EXPECT_NEAR(bbDerived.MinX, bbActual.MinX, 1e-7);
    EXPECT_NEAR(bbDerived.MinY, bbActual.MinY, 1e-7);
    EXPECT_NEAR(bbDerived.MinZ, bbActual.MinZ, 1e-7);
    EXPECT_NEAR(bbDerived.MaxX, bbActual.MaxX, 1e-7);
    EXPECT_NEAR(bbDerived.MaxY, bbActual.MaxY, 1e-7);
    EXPECT_NEAR(bbDerived.MaxZ, bbActual.MaxZ, 1e-7);

    Py_XDECREF(pyActual);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
