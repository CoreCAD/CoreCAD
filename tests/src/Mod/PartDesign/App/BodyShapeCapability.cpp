// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/TopoShapePy.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureBoolean.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeaturePrimitive.h>
#include <Mod/Sketcher/App/SketchObject.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

// A Body stores no shape: it carries the shape capability backed by its shown step (Amendment 17.2).
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

    // Oracle: the capability read, which getSubObject does not go through.
    auto oracleMap = Part::getShape(_body).getSubTopoShape(ref).getElementMap();

    // Actual: the live getSubObject, now dispatched to the composed Part::ShapeExtension.
    PyObject* pyActual = nullptr;
    auto* actualOwner = _body->getSubObject(ref, &pyActual, nullptr, false, 10);
    ASSERT_NE(actualOwner, nullptr);
    ASSERT_NE(pyActual, nullptr);
    ASSERT_EQ(actualOwner, static_cast<App::DocumentObject*>(_body));

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
    _body->getSubObject("Face2", &pyWrong, nullptr, false, 10);
    ASSERT_NE(pyWrong, nullptr);
    auto wrongMap = static_cast<Part::TopoShapePy*>(pyWrong)->getTopoShapePtr()->getElementMap();
    std::sort(wrongMap.begin(), wrongMap.end());
    EXPECT_FALSE(wrongMap == actualMap);

    Py_XDECREF(pyActual);
    Py_XDECREF(pyWrong);
}

TEST_F(BodyShapeCapabilityTest, storesNoShape)
{
    EXPECT_EQ(_body->getPropertyByName("Shape"), nullptr);
    const auto box = Part::getShape(_body).getBoundBox();
    EXPECT_NEAR(box.MaxZ - box.MinZ, 10.0, 1e-7);
}

TEST_F(BodyShapeCapabilityTest, followsItsTipBeforeItIsRecomputed)
{
    _pad->Length.setValue(20.0);
    _pad->recomputeFeature();

    const auto box = Part::getShape(_body).getBoundBox();
    EXPECT_NEAR(box.MaxZ - box.MinZ, 20.0, 1e-7);
}

TEST_F(BodyShapeCapabilityTest, namesTheElementsOfItsShape)
{
    // A mapped name comes back only when the Body answers for the elements of its shape.
    const auto names = _body->getElementName("Face1");
    EXPECT_FALSE(names.newName.empty());
    EXPECT_EQ(names.oldName, "Face1");
    EXPECT_FALSE(_body->getElementTypes().empty());
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

TEST_F(BodyShapeCapabilityTest, fusesBodiesWithNoBase)
{
    auto* other = _doc->addObject<PartDesign::Body>();
    auto* box = _doc->addObject<PartDesign::AdditiveBox>("Box");
    other->addFeature(box);
    box->Placement.setValue(Base::Placement(Base::Vector3d(5, 0, 0), Base::Rotation()));

    auto* host = _doc->addObject<PartDesign::Body>();
    auto* fuse = static_cast<PartDesign::Boolean*>(_doc->addObject("PartDesign::Boolean", "Fuse"));
    fuse->Tools.setValues({_body, other});
    host->addFeature(fuse);
    _doc->recompute();

    ASSERT_TRUE(fuse->isValid()) << fuse->getStatusString();
    // A radius-10 cylinder at the origin and a 10 mm box from x = 5 span x = -10 to 15.
    EXPECT_NEAR(fuse->Shape.getShape().getBoundBox().LengthX(), 25.0, 1e-6);
}

TEST_F(BodyShapeCapabilityTest, padsUpToAFaceOfABody)
{
    auto* other = _doc->addObject<PartDesign::Body>();
    auto* sketch = _doc->addObject<Sketcher::SketchObject>("Sketch2");
    sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
    sketch->MapMode.setValue("FlatFace");
    sketch->AttachmentOffset.setValue(Base::Placement(Base::Vector3d(0, 0, -10), Base::Rotation()));
    Part::GeomCircle circle;
    circle.setRadius(2.0);
    sketch->addGeometry(&circle, false);

    auto* pad = _doc->addObject<PartDesign::Pad>("UpToPad");
    other->addFeature(pad);
    pad->Profile.setValue(sketch, {""});
    pad->Type.setValue("UpToFace");
    pad->UpToFace.setValue(_body, {"Face3"});
    _doc->recompute();

    ASSERT_TRUE(pad->isValid()) << pad->getStatusString();
    const auto box = pad->Shape.getShape().getBoundBox();
    EXPECT_GT(box.MaxZ - box.MinZ, 1.0);
}

TEST_F(BodyShapeCapabilityTest, sketchKeepsAnEdgeOfABody)
{
    auto* sketch = _doc->addObject<Sketcher::SketchObject>("Sketch2");
    sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
    sketch->MapMode.setValue("FlatFace");
    _doc->recompute();
    ASSERT_GE(sketch->addExternal(_body, "Edge1"), 0);

    sketch->validateExternalLinks();

    EXPECT_EQ(sketch->ExternalGeometry.getValues().size(), 1u);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
