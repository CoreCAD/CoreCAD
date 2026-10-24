// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <gtest/gtest.h>
#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Extension.h>
#include <Mod/Part/App/MaterialExtension.h>

/** #121, second pass: being made of something is declared by the classes that can be a solid.
 *
 *  The first pass put the capability on `Part::Feature`, the placed rung of the shape lineage --
 *  and a sketch is a `Part::Feature`, so a sketch was made of steel. That is the same defect the
 *  ticket was filed on, one rung lower: a capability riding on something inherited rather than
 *  declared by the classes that mean it.
 *
 *  Nothing inherits it now. A box, a cut, an imported solid and a Body each say so themselves; a
 *  line, a plane, a surface, a compound and a sketch say nothing, and answer with the material of
 *  the part they belong to, if they belong to one.
 */
class MadeOfSomethingIsForSolidsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("MadeOfSomething_test", "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    bool madeOfSomething(const char* type)
    {
        App::DocumentObject* obj = _doc->addObject(type, "o");
        EXPECT_NE(obj, nullptr) << type << " could not be created";
        return obj != nullptr && Part::hasMaterial(obj);
    }

    App::Document* _doc = nullptr;
};

// A thing that can be a solid can be made of something.
TEST_F(MadeOfSomethingIsForSolidsTest, aSolidIsMadeOfSomething)
{
    EXPECT_TRUE(madeOfSomething("Part::Box"));
    EXPECT_TRUE(madeOfSomething("Part::Cylinder"));
    EXPECT_TRUE(madeOfSomething("Part::Sphere"));
    EXPECT_TRUE(madeOfSomething("Part::Cut"));
    EXPECT_TRUE(madeOfSomething("Part::Fuse"));
    EXPECT_TRUE(madeOfSomething("Part::Extrusion"));
    EXPECT_TRUE(madeOfSomething("Part::Fillet"));
    EXPECT_TRUE(madeOfSomething("Part::ImportStep"));
}

// Nothing else is, and the placed rung they all share is what used to hand it out.
TEST_F(MadeOfSomethingIsForSolidsTest, aProfileOrASurfaceIsMadeOfNothing)
{
    EXPECT_FALSE(madeOfSomething("Part::Part2DObject")) << "a profile is made of something";
    EXPECT_FALSE(madeOfSomething("Part::Line"));
    EXPECT_FALSE(madeOfSomething("Part::Plane"));
    EXPECT_FALSE(madeOfSomething("Part::Vertex"));
    EXPECT_FALSE(madeOfSomething("Part::Circle"));
    EXPECT_FALSE(madeOfSomething("Part::Helix"));
    EXPECT_FALSE(madeOfSomething("Part::RuledSurface"));
    EXPECT_FALSE(madeOfSomething("Part::Face"));
    EXPECT_FALSE(madeOfSomething("Part::Compound")) << "a bag of parts is not itself a part";
    EXPECT_FALSE(madeOfSomething("Part::Feature"))
        << "the placed rung still hands the capability out";
}

// A scripted object inherits nothing, so it asks by name -- the way CAM's stock does. What it
// asked for has to still be there when the document is opened again.
TEST_F(MadeOfSomethingIsForSolidsTest, aScriptedObjectCanAskForItByName)
{
    App::DocumentObject* stock = _doc->addObject("Part::FeaturePython", "Stock");
    ASSERT_NE(stock, nullptr);
    EXPECT_FALSE(Part::hasMaterial(stock))
        << "a scripted object was given a material it never asked for";

    // The same two steps the Python addExtension() takes.
    auto* asked = static_cast<App::Extension*>(
        Part::MaterialExtensionPython::getExtensionClassTypeId().createInstance()
    );
    ASSERT_NE(asked, nullptr)
        << "the capability is not registered under a name a script can ask for";
    EXPECT_TRUE(asked->isPythonExtension()) << "a script cannot be granted this capability";
    asked->initExtension(stock);
    EXPECT_TRUE(Part::hasMaterial(stock)) << "asking for the capability by name did not grant it";
    EXPECT_NE(stock->getPropertyByName("Material"), nullptr)
        << "the capability was granted without the property it carries";
}
