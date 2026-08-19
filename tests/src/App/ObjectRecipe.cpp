// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ObjectRecipe.h>
#include <App/PropertyGeo.h>
#include <App/PropertyStandard.h>
#include <App/Recipe.h>
#include <Base/Interpreter.h>
#include <Base/Matrix.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Uuid.h>
#include <Base/Vector3D.h>

#include <set>
#include <string>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class ObjectRecipeTest: public ::testing::Test
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

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _docName {};
    App::Document* _doc {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// A single object emits a node keyed by its durable Uid (never its positional name), carrying
// its type and its authored fields, while the kernel-derived Shape (Prop_Output) is excluded.
TEST_F(ObjectRecipeTest, emitsIdentityTypeAndAuthoredFieldsButNotOutput)
{
    // Arrange: a Part::Box carries authored dimensions and a computed Shape (Prop_Output).
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);

    // Act
    const RecipeNode node = emitObjectRecipe(*box);

    // Assert: identity is the durable Uid, not the in-document name.
    EXPECT_EQ(node.id, box->Uid.getValueStr());
    EXPECT_EQ(node.type, "Part::Box");

    // An authored dimension is emitted as a field...
    EXPECT_EQ(node.fields.count("Length"), 1u);
    EXPECT_FALSE(node.fields.at("Length").empty());

    // ...but the kernel-derived output is not.
    EXPECT_EQ(node.fields.count("Shape"), 0u);
}

// An object's links to other objects are emitted as refs addressing each target by its durable
// Uid (never by name), so a downstream reference survives a rename of what it points at.
TEST_F(ObjectRecipeTest, linksBecomeRefsByTargetUid)
{
    // Arrange: a Part::Cut whose Base and Tool link two boxes.
    auto* base = _doc->addObject("Part::Box");
    auto* tool = _doc->addObject("Part::Box");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(tool, nullptr);

    std::string cmd = "import FreeCAD as App\n";
    cmd += "cut = App.ActiveDocument.addObject('Part::Cut', 'TheCut')\n";
    cmd += "cut.Base = App.ActiveDocument.getObject('" + std::string(base->getNameInDocument())
        + "')\n";
    cmd += "cut.Tool = App.ActiveDocument.getObject('" + std::string(tool->getNameInDocument())
        + "')\n";
    Base::Interpreter().runString(cmd.c_str());

    auto* cut = _doc->getObject("TheCut");
    ASSERT_NE(cut, nullptr);

    // Act
    const RecipeNode node = emitObjectRecipe(*cut);

    // Assert: both operands appear as refs, addressed by their durable Uid, whole-object (pos 0).
    std::set<std::string> refTargets;
    for (const auto& ref : node.refs) {
        refTargets.insert(ref.target);
        EXPECT_EQ(ref.pos, 0);
    }
    EXPECT_EQ(refTargets.count(base->Uid.getValueStr()), 1u);
    EXPECT_EQ(refTargets.count(tool->Uid.getValueStr()), 1u);
}

// When a property is driven by an expression, the authored value is the expression itself, not
// its resolved number — a diff over a parametric model must not lie by comparing resolved values.
TEST_F(ObjectRecipeTest, boundExpressionIsTheAuthoredValueNotTheResolvedNumber)
{
    // Arrange: a Box whose Length is driven by an expression resolving to 10 mm.
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);

    std::string cmd = "import FreeCAD as App\n";
    cmd += "App.ActiveDocument.getObject('" + std::string(box->getNameInDocument())
        + "').setExpression('Length', u'7 mm + 3 mm')\n";
    Base::Interpreter().runString(cmd.c_str());
    _doc->recompute();

    // Act
    const RecipeNode node = emitObjectRecipe(*box);

    // Assert: the expression is recorded, not the resolved literal "10 mm".
    ASSERT_EQ(node.fields.count("Length"), 1u);
    EXPECT_NE(node.fields.at("Length").find('+'), std::string::npos);
    EXPECT_NE(node.fields.at("Length"), "10 mm");
}

// The document recipe holds one node per object, keyed by durable Uid — the section the merge
// engine reconciles for model-wide, object-granular three-way merge.
TEST_F(ObjectRecipeTest, documentRecipeKeysEveryObjectByUid)
{
    auto* first = _doc->addObject("Part::Box");
    auto* second = _doc->addObject("Part::Cylinder");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const RecipeSection section = emitDocumentRecipe(*_doc);

    ASSERT_EQ(section.count(first->Uid.getValueStr()), 1u);
    ASSERT_EQ(section.count(second->Uid.getValueStr()), 1u);
    EXPECT_EQ(section.at(first->Uid.getValueStr()).type, "Part::Box");
    EXPECT_EQ(section.at(second->Uid.getValueStr()).type, "Part::Cylinder");
}

// Three versions of a model sharing durable object identity (as branches copied from one origin
// would): a lone edit on one branch merges through and the report reads clean.
TEST_F(ObjectRecipeTest, mergeDocumentsTakesTheLoneEditAndReadsClean)
{
    // Ancestor: a box at its default length.
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    const std::string uid = ancestorBox->Uid.getValueStr();

    auto& app = App::GetApplication();

    // Branch A: the same object (shared Uid), lengthened.
    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    auto* boxA = docA->addObject("Part::Box");
    Base::Uuid sharedId;
    sharedId.setValue(uid);
    boxA->Uid.setValue(sharedId);
    dynamic_cast<App::PropertyFloat*>(boxA->getPropertyByName("Length"))->setValue(25.0);

    // Branch B: the same object, untouched.
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxB = docB->addObject("Part::Box");
    boxB->Uid.setValue(sharedId);

    // Act
    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    // Assert: A's edit is taken, no conflict, and the summary is human-readable.
    EXPECT_TRUE(report.conflicts.empty());
    ASSERT_EQ(report.merged.count(uid), 1u);
    EXPECT_EQ(report.merged.at(uid).fields.at("Length"), "25 mm");
    EXPECT_NE(App::formatDocumentReport(report).find("Merged cleanly"), std::string::npos);

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// Two branches edit DIFFERENT fields of the same object (A its length, B its width). Through the
// full emit -> object-granular merge -> field-granular refinement path, the disjoint edits are
// auto-merged and the whole-document report reads clean — no conflict is put to the user.
TEST_F(ObjectRecipeTest, mergeDocumentsAutoMergesDisjointFieldEdits)
{
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    const std::string uid = ancestorBox->Uid.getValueStr();

    auto& app = App::GetApplication();
    Base::Uuid sharedId;
    sharedId.setValue(uid);

    // Branch A: same object, lengthened.
    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    auto* boxA = docA->addObject("Part::Box");
    boxA->Uid.setValue(sharedId);
    dynamic_cast<App::PropertyFloat*>(boxA->getPropertyByName("Length"))->setValue(25.0);

    // Branch B: same object, widened (a different field).
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxB = docB->addObject("Part::Box");
    boxB->Uid.setValue(sharedId);
    dynamic_cast<App::PropertyFloat*>(boxB->getPropertyByName("Width"))->setValue(30.0);

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    EXPECT_TRUE(report.conflicts.empty());  // disjoint fields dissolved
    ASSERT_EQ(report.merged.count(uid), 1u);
    EXPECT_EQ(report.merged.at(uid).fields.at("Length"), "25 mm");  // A's edit
    EXPECT_EQ(report.merged.at(uid).fields.at("Width"), "30 mm");   // and B's, on one object
    EXPECT_NE(App::formatDocumentReport(report).find("Merged cleanly"), std::string::npos);

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// Both branches change the SAME field of the same object differently. The report keeps one
// conflict, now naming the field — the case the refinement cannot and must not dissolve.
TEST_F(ObjectRecipeTest, mergeDocumentsKeepsSameFieldEditAsAFieldConflict)
{
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    const std::string uid = ancestorBox->Uid.getValueStr();

    auto& app = App::GetApplication();
    Base::Uuid sharedId;
    sharedId.setValue(uid);

    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    auto* boxA = docA->addObject("Part::Box");
    boxA->Uid.setValue(sharedId);
    dynamic_cast<App::PropertyFloat*>(boxA->getPropertyByName("Length"))->setValue(25.0);

    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxB = docB->addObject("Part::Box");
    boxB->Uid.setValue(sharedId);
    dynamic_cast<App::PropertyFloat*>(boxB->getPropertyByName("Length"))->setValue(30.0);

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    ASSERT_EQ(report.conflicts.size(), 1u);
    EXPECT_EQ(report.conflicts.front().id, uid);
    EXPECT_NE(report.conflicts.front().detail.find("Length"), std::string::npos);

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// A placement is a non-scalar authored value; it is now canonicalized into a field, and the
// field changes when the placement changes (a metric that reported the same string either way
// would be blind to exactly the move this closes).
TEST_F(ObjectRecipeTest, placementIsEmittedAndDistinguishesAMove)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* placement = dynamic_cast<PropertyPlacement*>(box->getPropertyByName("Placement"));
    ASSERT_NE(placement, nullptr);

    placement->setValue(Base::Placement(Base::Vector3d(1.0, 2.0, 3.0), Base::Rotation()));
    const std::string atOrigin = emitObjectRecipe(*box).fields.at("Placement");

    placement->setValue(Base::Placement(Base::Vector3d(5.0, 2.0, 3.0), Base::Rotation()));
    const std::string moved = emitObjectRecipe(*box).fields.at("Placement");

    EXPECT_FALSE(atOrigin.empty());
    EXPECT_NE(atOrigin, moved);  // the x-move is visible in the field
}

// The headline of #84: a placement-only change on one branch — previously dropped from the
// recipe and so invisible — now merges through as the authored edit it is.
TEST_F(ObjectRecipeTest, mergeDocumentsSeesAPlacementOnlyEdit)
{
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    const std::string uid = ancestorBox->Uid.getValueStr();

    auto& app = App::GetApplication();
    Base::Uuid sharedId;
    sharedId.setValue(uid);

    // Branch A: same object, moved.
    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    auto* boxA = docA->addObject("Part::Box");
    boxA->Uid.setValue(sharedId);
    dynamic_cast<PropertyPlacement*>(boxA->getPropertyByName("Placement"))
        ->setValue(Base::Placement(Base::Vector3d(10.0, 0.0, 0.0), Base::Rotation()));

    // Branch B: same object, untouched.
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxB = docB->addObject("Part::Box");
    boxB->Uid.setValue(sharedId);

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    EXPECT_TRUE(report.conflicts.empty());  // lone edit — no conflict
    ASSERT_EQ(report.merged.count(uid), 1u);
    const std::string ancestorPlacement = emitObjectRecipe(*ancestorBox).fields.at("Placement");
    EXPECT_NE(report.merged.at(uid).fields.at("Placement"), ancestorPlacement);  // A's move landed

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// A bare vector value type is canonicalized too (covers the PropertyVector path directly, via a
// dynamic property so the test does not lean on any one object type carrying a vector).
TEST_F(ObjectRecipeTest, vectorValueIsEmittedAsAField)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* prop = dynamic_cast<PropertyVector*>(
        box->addDynamicProperty("App::PropertyVector", "Anchor")
    );
    ASSERT_NE(prop, nullptr);
    prop->setValue(Base::Vector3d(4.0, 5.0, 6.0));

    const RecipeNode node = emitObjectRecipe(*box);

    ASSERT_EQ(node.fields.count("Anchor"), 1u);
    EXPECT_NE(node.fields.at("Anchor").find('4'), std::string::npos);
    EXPECT_NE(node.fields.at("Anchor").find('6'), std::string::npos);
}

// A scalar list value type is canonicalized element-by-element, and the field changes when any
// element changes (a metric returning the same string either way would be blind to the edit).
TEST_F(ObjectRecipeTest, floatListIsEmittedAndDistinguishesAnElementEdit)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* prop = dynamic_cast<PropertyFloatList*>(
        box->addDynamicProperty("App::PropertyFloatList", "Samples")
    );
    ASSERT_NE(prop, nullptr);

    prop->setValues({1.5, 2.5, 3.5});
    const std::string before = emitObjectRecipe(*box).fields.at("Samples");

    prop->setValues({1.5, 9.5, 3.5});
    const std::string after = emitObjectRecipe(*box).fields.at("Samples");

    EXPECT_FALSE(before.empty());
    EXPECT_NE(before, after);  // the middle-element edit is visible in the field
}

// A list's element count is part of its canonical form: appending an element changes the field
// even when every pre-existing element is unchanged (order and length are authored state).
TEST_F(ObjectRecipeTest, listLengthChangeIsVisible)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* prop = dynamic_cast<PropertyIntegerList*>(
        box->addDynamicProperty("App::PropertyIntegerList", "Counts")
    );
    ASSERT_NE(prop, nullptr);

    prop->setValues({1, 2, 3});
    const std::string shorter = emitObjectRecipe(*box).fields.at("Counts");

    prop->setValues({1, 2, 3, 4});
    const std::string longer = emitObjectRecipe(*box).fields.at("Counts");

    EXPECT_NE(shorter, longer);
}

// A vector list carries non-scalar elements; a change to one component of one element changes
// the field (covers PropertyVectorList over the shared per-element canonicalizer).
TEST_F(ObjectRecipeTest, vectorListIsEmittedAndDistinguishesAComponentEdit)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* prop = dynamic_cast<PropertyVectorList*>(
        box->addDynamicProperty("App::PropertyVectorList", "Path")
    );
    ASSERT_NE(prop, nullptr);

    prop->setValues({Base::Vector3d(0, 0, 0), Base::Vector3d(1, 1, 1)});
    const std::string before = emitObjectRecipe(*box).fields.at("Path");

    prop->setValues({Base::Vector3d(0, 0, 0), Base::Vector3d(1, 7, 1)});
    const std::string after = emitObjectRecipe(*box).fields.at("Path");

    EXPECT_NE(before, after);
}

// A matrix is a non-scalar authored value; it is canonicalized as its 16 cells, and the field
// changes when any cell changes.
TEST_F(ObjectRecipeTest, matrixIsEmittedAndDistinguishesACellEdit)
{
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* prop = dynamic_cast<PropertyMatrix*>(
        box->addDynamicProperty("App::PropertyMatrix", "Transform")
    );
    ASSERT_NE(prop, nullptr);

    Base::Matrix4D mat;  // identity
    prop->setValue(mat);
    const std::string identity = emitObjectRecipe(*box).fields.at("Transform");

    mat[0][3] = 12.0;  // a translation cell
    prop->setValue(mat);
    const std::string translated = emitObjectRecipe(*box).fields.at("Transform");

    EXPECT_FALSE(identity.empty());
    EXPECT_NE(identity, translated);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
