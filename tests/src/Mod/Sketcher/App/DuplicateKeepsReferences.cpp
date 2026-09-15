// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/GeoEnum.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <array>
#include <set>
#include <string>
#include <vector>

/** A re-mint carries the references that resolve through what it re-mints.
 *
 *  Cruth: a duplicate takes an identity of its own, and everything it authors is reborn with it.
 *  A sketch's constraints name its geometry by that identity, and between being read and being
 *  bound to the geometry they hold the identities the file stated -- the binding happens once the
 *  whole document has been read, which is after the re-mint.
 *
 *  Measured before this: duplicating an ordinary three-constraint sketch produced a copy in which
 *  EVERY constraint referenced nothing.
 *
 *      BEFORE: [('Coincident', 0, 1), ('Horizontal', 0, -2000), ('DistanceX', 0, 0)]
 *      COPY:   [('Coincident', -2000, -2000), ('Horizontal', -2000, -2000), …]
 *
 *  The drop itself was correct -- a durable reference whose target is gone is marked lost rather
 *  than re-bound to whatever now sits at that index (§10.1), and it said so. The re-mint underneath
 *  it was what was wrong.
 */
class DuplicateKeepsReferencesTest: public ::testing::Test
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
    }

    /// Two joined lines, one of them pinned to the sketch's own X axis -- a reference with no
    /// durable identity of its own, which must survive the same operation untouched.
    Sketcher::SketchObject* buildSketch(App::Document* doc)
    {
        auto* sketch = static_cast<Sketcher::SketchObject*>(
            doc->addObject("Sketcher::SketchObject", "S")
        );

        Part::GeomLineSegment first;
        first.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
        sketch->addGeometry(&first);

        Part::GeomLineSegment second;
        second.setPoints(Base::Vector3d(10, 0, 0), Base::Vector3d(10, 10, 0));
        sketch->addGeometry(&second);

        Sketcher::Constraint joined;
        joined.Type = Sketcher::Coincident;
        joined.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::end));
        joined.setElement(1, Sketcher::GeoElementId(1, Sketcher::PointPos::start));
        sketch->addConstraint(&joined);

        Sketcher::Constraint onAxis;
        onAxis.Type = Sketcher::PointOnObject;
        onAxis.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::start));
        onAxis.setElement(1, Sketcher::GeoElementId(-1, Sketcher::PointPos::none));
        sketch->addConstraint(&onAxis);

        doc->recompute();
        return sketch;
    }

    static Sketcher::SketchObject* theCopy(App::Document* doc, const Sketcher::SketchObject* source)
    {
        for (App::DocumentObject* obj : doc->getObjects()) {
            if (obj != source && obj->isDerivedFrom<Sketcher::SketchObject>()) {
                return static_cast<Sketcher::SketchObject*>(obj);
            }
        }
        return nullptr;
    }
};

// The copy is the same sketch, held together the same way.
TEST_F(DuplicateKeepsReferencesTest, aDuplicateKeepsEveryConstraintReference)
{
    App::Document* doc = App::GetApplication().newDocument("duplicate");
    Sketcher::SketchObject* source = buildSketch(doc);
    ASSERT_EQ(source->Constraints.getValues().size(), 2U);

    doc->copyObject({source}, true);
    doc->recompute();

    Sketcher::SketchObject* copy = theCopy(doc, source);
    ASSERT_NE(copy, nullptr) << "nothing was duplicated";

    const std::vector<Sketcher::Constraint*>& kept = copy->Constraints.getValues();
    ASSERT_EQ(kept.size(), 2U);
    EXPECT_EQ(kept[0]->getElement(0), Sketcher::GeoElementId(0, Sketcher::PointPos::end));
    EXPECT_EQ(kept[0]->getElement(1), Sketcher::GeoElementId(1, Sketcher::PointPos::start));

    // The reference with no durable identity of its own has nothing to carry, and is untouched.
    EXPECT_EQ(kept[1]->getElement(1), Sketcher::GeoElementId(-1, Sketcher::PointPos::none));

    EXPECT_EQ(copy->solve(), 0) << "the duplicate does not solve";
}

// The control that makes the test above mean something: carrying the references must not be
// achieved by leaving the identities alone. A duplicate that kept its source's identity would
// pass every assertion above and be two objects claiming to be the same one.
TEST_F(DuplicateKeepsReferencesTest, aDuplicateStillTakesAnIdentityOfItsOwn)
{
    App::Document* doc = App::GetApplication().newDocument("identity");
    Sketcher::SketchObject* source = buildSketch(doc);

    doc->copyObject({source}, true);
    doc->recompute();

    Sketcher::SketchObject* copy = theCopy(doc, source);
    ASSERT_NE(copy, nullptr);

    std::set<std::string> sourceTags;
    for (const Part::Geometry* geo : source->getInternalGeometry()) {
        sourceTags.insert(boost::uuids::to_string(geo->getTag()));
    }
    ASSERT_FALSE(sourceTags.empty());

    for (const Part::Geometry* geo : copy->getInternalGeometry()) {
        EXPECT_EQ(sourceTags.count(boost::uuids::to_string(geo->getTag())), 0U)
            << "the duplicate is still carrying the identity it was copied from";
    }

    EXPECT_NE(copy->Uid.getValue(), source->Uid.getValue())
        << "the sketch itself was not reborn either";
}
