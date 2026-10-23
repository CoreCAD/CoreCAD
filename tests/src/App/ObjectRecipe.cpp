// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ObjectRecipe.h>
#include <App/PropertyGeo.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/Recipe.h>
#include <Base/Interpreter.h>
#include <Base/Matrix.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Uuid.h>
#include <Base/Vector3D.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

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

// Two boxes, the second attached to a face of the first, with the given durable ids so three
// documents can hold the same two objects.
void attachedBoxes(
    App::Document* doc,
    const std::string& baseUid,
    const std::string& attachedUid,
    const std::string& face
)
{
    std::string cmd = "import FreeCAD as App\n";
    cmd += "doc = App.getDocument('" + std::string(doc->getName()) + "')\n";
    cmd += "base = doc.addObject('Part::Box', 'Base')\n";
    cmd += "top = doc.addObject('Part::Box', 'Top')\n";
    cmd += "doc.recompute()\n";
    cmd += "top.AttachmentSupport = [(base, '" + face + "')]\n";
    cmd += "top.MapMode = 'FlatFace'\n";
    cmd += "doc.recompute()\n";
    Base::Interpreter().runString(cmd.c_str());
    for (const auto& [name, uid] : {std::pair {"Base", baseUid}, std::pair {"Top", attachedUid}}) {
        if (uid.empty()) {
            continue;
        }
        Base::Uuid shared;
        shared.setValue(uid);
        doc->getObject(name)->Uid.setValue(shared);
    }
}

// A moves the attachment to another face of the same box; B leaves it and lengthens the box, so
// the box's links are merged on their own. Before the picked face was part of the recipe, the
// links read the same on all three sides and A's choice was lost without a word. Control: both
// sides re-picking different faces is a conflict.
TEST_F(ObjectRecipeTest, mergeDocumentsTakesAReChosenFaceAndConflictsOnTwo)
{
    attachedBoxes(_doc, "", "", "Face6");
    const std::string baseUid = _doc->getObject("Base")->Uid.getValueStr();
    const std::string topUid = _doc->getObject("Top")->Uid.getValueStr();
    const auto pickedLabels = [&](const App::DocumentMergeReport& report) {
        std::vector<std::string> labels;
        for (const RecipeRef& ref : report.merged.at(topUid).refs) {
            if (ref.target == baseUid && !ref.elementLabel.empty()) {
                labels.push_back(ref.elementLabel);
            }
        }
        return labels;
    };

    auto& app = App::GetApplication();
    for (const std::string& otherFace : {std::string("Face6"), std::string("Face4")}) {
        App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
        App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
        attachedBoxes(docA, baseUid, topUid, "Face5");
        attachedBoxes(docB, baseUid, topUid, otherFace);
        dynamic_cast<App::PropertyFloat*>(docB->getObject("Top")->getPropertyByName("Length"))
            ->setValue(20.0);

        const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

        EXPECT_EQ(report.merged.at(topUid).fields.count("Placement"), 0U)
            << "the attachment produces the placement; it is not merged";
        if (otherFace == "Face6") {
            EXPECT_TRUE(report.conflicts.empty());
            EXPECT_EQ(pickedLabels(report), std::vector<std::string> {"Face5"})
                << "A's re-chosen face is the merged one";
            EXPECT_EQ(report.merged.at(topUid).fields.at("Length"), "20 mm");
        }
        else {
            // One decision, the face; the placement it produces is not a second conflict.
            ASSERT_EQ(report.conflicts.size(), 1U);
            EXPECT_EQ(report.conflicts.front().id, topUid);
            EXPECT_EQ(report.conflicts.front().detail.rfind("references", 0), 0U)
                << "both sides re-chose the face";
        }
        app.closeDocument(docA->getName());
        app.closeDocument(docB->getName());
    }
}

// A deletes a box; B mirrors it. A never saw B's mirror, so the merge does not drop it: it is a
// conflict, kept for the person settling it, and the report says so.
TEST_F(ObjectRecipeTest, mergeDocumentsRaisesAStepBuiltOnAnObjectTheOtherBranchDeleted)
{
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    Base::Uuid sharedId;
    sharedId.setValue(ancestorBox->Uid.getValueStr());

    auto& app = App::GetApplication();
    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxB = docB->addObject("Part::Box", "Box");
    boxB->Uid.setValue(sharedId);
    std::string cmd = "import FreeCAD as App\n";
    cmd += "doc = App.getDocument('" + std::string(docB->getName()) + "')\n";
    cmd += "doc.addObject('Part::Mirroring', 'Mirror').Source = doc.getObject('Box')\n";
    Base::Interpreter().runString(cmd.c_str());
    const std::string mirror = docB->getObject("Mirror")->Uid.getValueStr();

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    ASSERT_EQ(report.conflicts.size(), 1u);
    EXPECT_EQ(report.conflicts.front().kind, App::MergeConflict::Kind::Referential);
    EXPECT_EQ(report.conflicts.front().id, mirror);
    EXPECT_EQ(report.merged.count(mirror), 1u) << "the mirror is kept, not dropped";
    EXPECT_FALSE(report.conflicts.front().ancestor);
    EXPECT_FALSE(report.conflicts.front().branchA) << "A never had the mirror";
    EXPECT_TRUE(report.conflicts.front().branchB);
    EXPECT_TRUE(report.resolutions.empty());
    EXPECT_NE(
        App::formatDocumentReport(report).find("built on an object the other branch deleted"),
        std::string::npos
    );

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
    const App::MergeConflict& conflict = report.conflicts.front();
    EXPECT_EQ(conflict.id, uid);
    EXPECT_NE(conflict.detail.find("Length"), std::string::npos);
    // The person settling it sees what each version stated, not only A's.
    ASSERT_TRUE(conflict.ancestor && conflict.branchA && conflict.branchB);
    EXPECT_EQ(conflict.ancestor->fields.at("Length"), "10 mm");
    EXPECT_EQ(conflict.branchA->fields.at("Length"), "25 mm");
    EXPECT_EQ(conflict.branchB->fields.at("Length"), "30 mm");

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// A script settling a conflict reads what each version stated through the model, not only the
// text report (Clause 21.4 of the merge proposal, "readable through the model").
TEST_F(ObjectRecipeTest, aScriptReadsEachVersionsStatementOfAConflict)
{
    auto* ancestorBox = _doc->addObject("Part::Box");
    ASSERT_NE(ancestorBox, nullptr);
    Base::Uuid sharedId;
    sharedId.setValue(ancestorBox->Uid.getValueStr());

    auto& app = App::GetApplication();
    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    for (const auto& [doc, length] : {std::pair {docA, 25.0}, std::pair {docB, 30.0}}) {
        auto* box = doc->addObject("Part::Box");
        box->Uid.setValue(sharedId);
        dynamic_cast<App::PropertyFloat*>(box->getPropertyByName("Length"))->setValue(length);
    }

    std::string cmd = "import FreeCAD as App\n";
    cmd += "r = App.mergeDocuments(App.getDocument('" + std::string(_doc->getName())
        + "'), App.getDocument('" + docA->getName() + "'), App.getDocument('" + docB->getName()
        + "'))\n";
    cmd += "assert 'Length' in r['text']\n";
    cmd += "c, = r['conflicts']\n";
    cmd += "assert c['kind'] == 'value' and c['object'] == '" + ancestorBox->Uid.getValueStr()
        + "'\n";
    cmd += "assert c['ancestor']['fields']['Length'] == '10 mm'\n";
    cmd += "assert c['branchA']['fields']['Length'] == '25 mm'\n";
    cmd += "assert c['branchB']['fields']['Length'] == '30 mm'\n";
    EXPECT_NO_THROW(Base::Interpreter().runString(cmd.c_str()));

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

// Prop_Hidden says only "do not clutter the property editor". It is a display choice about a
// dialog, never a statement that a value was not authored -- so a hidden property that is
// nonetheless saved belongs in the recipe. A text document keeps its entire content in one such
// property; excluding hidden properties dropped the whole document and said nothing about it.
TEST_F(ObjectRecipeTest, hiddenButPersistedValueIsStillAuthoredContent)
{
    // Arrange: App::TextDocument declares Text as Prop_Hidden, and it is the object's content.
    auto* notes = _doc->addObject("App::TextDocument", "Notes");
    ASSERT_NE(notes, nullptr);
    auto* text = dynamic_cast<PropertyString*>(notes->getPropertyByName("Text"));
    ASSERT_NE(text, nullptr);
    ASSERT_NE(notes->getPropertyType(text) & Prop_Hidden, 0);
    text->setValue("the authored words");

    // Act
    const RecipeNode node = emitObjectRecipe(*notes);

    // Assert: the content is a field, and editing it is a change the recipe can see.
    ASSERT_EQ(node.fields.count("Text"), 1u);
    EXPECT_EQ(node.fields.at("Text"), "the authored words");

    text->setValue("edited");
    EXPECT_NE(emitObjectRecipe(*notes).fields.at("Text"), node.fields.at("Text"));
}

// The same rule applies to structure, not just to values: TechDraw records which page a view
// belongs to in a hidden link, so excluding hidden properties let a page, a view and a dimension
// all reach the recipe with nothing saying they belong together.
TEST_F(ObjectRecipeTest, hiddenLinkIsStillAnAuthoredReference)
{
    // Arrange: a box holding a hidden link to another object, as a view holds its page.
    auto* page = _doc->addObject("Part::Box");
    auto* view = _doc->addObject("Part::Box");
    ASSERT_NE(page, nullptr);
    ASSERT_NE(view, nullptr);
    auto* belongsTo = dynamic_cast<PropertyLink*>(
        view->addDynamicProperty("App::PropertyLink", "Page", "Base", nullptr, Prop_Hidden)
    );
    ASSERT_NE(belongsTo, nullptr);
    belongsTo->setValue(page);

    // Act
    const RecipeNode node = emitObjectRecipe(*view);

    // Assert: the membership edge is a ref, addressed by the target's durable Uid.
    std::set<std::string> refTargets;
    for (const auto& ref : node.refs) {
        refTargets.insert(ref.target);
    }
    EXPECT_EQ(refTargets.count(page->Uid.getValueStr()), 1u);
}

// A property the emitter reaches and cannot render must be named in the unrecorded list, because
// that list is the measurement of how far the recipe is from standing in for the document. Once
// hidden properties are walked, a hidden one it cannot render has to be reported like any other.
TEST_F(ObjectRecipeTest, hiddenPropertyItCannotRenderIsReportedAsAGap)
{
    // Arrange: a hidden property of a type the driver has no words for.
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(
        box->addDynamicProperty("App::PropertyMaterial", "Finish", "Base", nullptr, Prop_Hidden),
        nullptr
    );

    // Act
    const std::vector<std::string> gaps = unrecordedProperties(*box);

    // Assert: the file states the gap rather than hiding it.
    EXPECT_NE(std::find(gaps.begin(), gaps.end(), "Finish"), gaps.end());
}

// A drawing's title block is one property holding every box on it: title, drawn by, scale, sheet
// number. Each entry has to reach the recipe as a field of its own, or a changed drawing title
// produces no line at all and the whole block is reported as missing.
TEST_F(ObjectRecipeTest, titleBlockEntriesEachBecomeAFieldAndDistinguishAnEdit)
{
    // Arrange
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    auto* block = dynamic_cast<PropertyMap*>(
        box->addDynamicProperty("App::PropertyMap", "EditableTexts", "Base", nullptr, Prop_Hidden)
    );
    ASSERT_NE(block, nullptr);
    block->setValue("DRAWING_TITLE", "Gearbox housing");
    block->setValue("DRAWN_BY", "S. Barton");

    // Act
    const RecipeNode before = emitObjectRecipe(*box);

    // Assert: one field per box, named for the box it is.
    EXPECT_EQ(before.fields.count("EditableTexts.DRAWING_TITLE"), 1u);
    EXPECT_EQ(before.fields.at("EditableTexts.DRAWING_TITLE"), "Gearbox housing");
    EXPECT_EQ(before.fields.at("EditableTexts.DRAWN_BY"), "S. Barton");

    // ...and the block is no longer reported as content the recipe could not say.
    const std::vector<std::string> gaps = unrecordedProperties(*box);
    EXPECT_EQ(std::find(gaps.begin(), gaps.end(), "EditableTexts"), gaps.end());

    // Act: retitle the drawing, leaving every other box alone.
    block->setValue("DRAWING_TITLE", "Gearbox housing, rev B");
    const RecipeNode after = emitObjectRecipe(*box);

    // Assert: exactly the retitled field changed -- so a diff shows the retitle and nothing else,
    // and two people filling in different boxes of one block do not collide.
    EXPECT_NE(
        after.fields.at("EditableTexts.DRAWING_TITLE"),
        before.fields.at("EditableTexts.DRAWING_TITLE")
    );
    EXPECT_EQ(after.fields.at("EditableTexts.DRAWN_BY"), before.fields.at("EditableTexts.DRAWN_BY"));
}

// An empty title block is an authored state -- a template whose boxes nobody has filled in yet.
// It carries no fields, but it is not a gap either: nothing is missing from the record.
TEST_F(ObjectRecipeTest, anEmptyTitleBlockIsNotReportedAsMissingContent)
{
    // Arrange
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(
        box->addDynamicProperty("App::PropertyMap", "EditableTexts", "Base", nullptr, Prop_Hidden),
        nullptr
    );

    // Act
    const std::vector<std::string> gaps = unrecordedProperties(*box);

    // Assert
    EXPECT_EQ(std::find(gaps.begin(), gaps.end(), "EditableTexts"), gaps.end());
}

// Label2 is the tree's description column -- a human annotation like Label, not authored design.
// It is hidden, so walking hidden properties would otherwise have put it on every single object.
TEST_F(ObjectRecipeTest, descriptionIsAUserPreferenceNotRecipeContent)
{
    // Arrange
    auto* box = _doc->addObject("Part::Box");
    ASSERT_NE(box, nullptr);
    box->Label2.setValue("a note to myself");

    // Act
    const RecipeNode node = emitObjectRecipe(*box);
    const std::vector<std::string> gaps = unrecordedProperties(*box);

    // Assert: neither recorded as content nor reported as a gap -- it is simply out of scope.
    EXPECT_EQ(node.fields.count("Label2"), 0u);
    EXPECT_EQ(std::find(gaps.begin(), gaps.end(), "Label2"), gaps.end());
}

// A value with no recipe form was absent from the merge, so two branches setting it differently
// read as "merged cleanly". It is now compared whole: two different changes conflict, one is taken.
TEST_F(ObjectRecipeTest, mergeDocumentsComparesAValueItCannotWriteDownWhole)
{
    auto& app = App::GetApplication();
    const auto finishOf = [](App::DocumentObject* obj) {
        return dynamic_cast<App::PropertyMaterial*>(obj->getPropertyByName("Finish"));
    };
    const auto version = [&](App::Document* doc, const Base::Uuid& id) {
        auto* box = doc->addObject("Part::Box", "Box");
        box->Uid.setValue(id);
        box->addDynamicProperty("App::PropertyMaterial", "Finish");
        return box;
    };
    auto* ancestorBox = version(_doc, Base::Uuid());
    const std::string uid = ancestorBox->Uid.getValueStr();
    Base::Uuid sharedId;
    sharedId.setValue(uid);

    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxA = version(docA, sharedId);
    auto* boxB = version(docB, sharedId);
    finishOf(boxA)->setDiffuseColor(1.0F, 0.0F, 0.0F);

    const App::DocumentMergeReport lone = App::mergeDocuments(*_doc, *docA, *docB);
    EXPECT_TRUE(lone.conflicts.empty()) << "one branch's change is taken";

    finishOf(boxB)->setDiffuseColor(0.0F, 0.0F, 1.0F);
    const App::DocumentMergeReport both = App::mergeDocuments(*_doc, *docA, *docB);
    ASSERT_EQ(both.conflicts.size(), 1u);
    EXPECT_EQ(both.conflicts.front().id, uid);
    EXPECT_NE(both.conflicts.front().detail.find("Finish"), std::string::npos);

    finishOf(boxB)->setDiffuseColor(1.0F, 0.0F, 0.0F);
    EXPECT_TRUE(App::mergeDocuments(*_doc, *docA, *docB).conflicts.empty())
        << "the same change on both branches is one change";

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// An edit to a value with no recipe form, on an object the other branch deleted, was lost with
// nothing said. It is an edit like any other, so it conflicts with the delete.
TEST_F(ObjectRecipeTest, mergeDocumentsRaisesAnUnwrittenEditToAnObjectTheOtherBranchDeleted)
{
    auto& app = App::GetApplication();
    auto* ancestorBox = _doc->addObject("Part::Box", "Box");
    ancestorBox->addDynamicProperty("App::PropertyMaterial", "Finish");
    const std::string uid = ancestorBox->Uid.getValueStr();
    Base::Uuid sharedId;
    sharedId.setValue(uid);

    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    auto* boxA = docA->addObject("Part::Box", "Box");
    boxA->Uid.setValue(sharedId);
    boxA->addDynamicProperty("App::PropertyMaterial", "Finish");
    dynamic_cast<App::PropertyMaterial*>(boxA->getPropertyByName("Finish"))
        ->setDiffuseColor(1.0F, 0.0F, 0.0F);

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    ASSERT_EQ(report.conflicts.size(), 1u);
    EXPECT_EQ(report.conflicts.front().id, uid);
    EXPECT_EQ(App::formatDocumentReport(report).find("Merged cleanly"), std::string::npos);

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// A formula binds the object it reads by durable id; the name in its text is display (§4.5). So a
// rename, which rewrites that text, leaves the recipe's statement of the formula as it was, both
// for a reference by label and one by name.
TEST_F(ObjectRecipeTest, aFormulaIsStatedByWhatItReadsNotTheNameItShows)
{
    auto* box = _doc->addObject("Part::Box", "Box");
    box->Label.setValue("Bracket");
    auto* byLabel = _doc->addObject("Part::Box", "ByLabel");
    auto* byName = _doc->addObject("Part::Box", "ByName");
    std::string cmd = "import FreeCAD as App\n";
    cmd += "doc = App.getDocument('" + _docName + "')\n";
    cmd += "doc.getObject('ByLabel').setExpression('Length', '<<Bracket>>.Length / 4')\n";
    cmd += "doc.getObject('ByName').setExpression('Width', 'Box.Width * 2')\n";
    Base::Interpreter().runString(cmd.c_str());
    const std::string labelBefore = emitObjectRecipe(*byLabel).fields.at("Length");
    const std::string nameBefore = emitObjectRecipe(*byName).fields.at("Width");

    box->Label.setValue("Plate");

    const std::string uid = box->Uid.getValueStr();
    EXPECT_EQ(emitObjectRecipe(*byLabel).fields.at("Length"), labelBefore);
    EXPECT_EQ(emitObjectRecipe(*byName).fields.at("Width"), nameBefore);
    EXPECT_NE(labelBefore.find(uid), std::string::npos) << labelBefore;
    EXPECT_NE(nameBefore.find(uid), std::string::npos) << nameBefore;
    EXPECT_EQ(labelBefore.find("Bracket"), std::string::npos) << labelBefore;
}

// One branch renames an object; the other edits a formula that reads it. The rename is not an edit
// to the formula, so the edit is taken and nothing conflicts.
TEST_F(ObjectRecipeTest, aRenameOnOneBranchIsNotAnEditToTheFormulasThatReadIt)
{
    auto& app = App::GetApplication();
    const auto version = [](App::Document* doc, const Base::Uuid* boxId, const Base::Uuid* userId) {
        auto* box = doc->addObject("Part::Box", "Box");
        auto* user = doc->addObject("Part::Box", "User");
        if (boxId != nullptr) {
            box->Uid.setValue(*boxId);
            user->Uid.setValue(*userId);
        }
        box->Label.setValue("Bracket");
        std::string cmd = "import FreeCAD as App\n";
        cmd += "App.getDocument('" + std::string(doc->getName())
            + "').getObject('User').setExpression('Length', '<<Bracket>>.Length / 4')\n";
        Base::Interpreter().runString(cmd.c_str());
        return std::make_pair(box, user);
    };
    const auto [ancestorBox, ancestorUser] = version(_doc, nullptr, nullptr);
    Base::Uuid boxId;
    boxId.setValue(ancestorBox->Uid.getValueStr());
    Base::Uuid userId;
    userId.setValue(ancestorUser->Uid.getValueStr());

    App::Document* docA = app.newDocument(app.getUniqueDocumentName("branchA").c_str(), "u");
    App::Document* docB = app.newDocument(app.getUniqueDocumentName("branchB").c_str(), "u");
    const auto [boxA, userA] = version(docA, &boxId, &userId);
    const auto [boxB, userB] = version(docB, &boxId, &userId);
    boxA->Label.setValue("Plate");
    std::string cmd = "import FreeCAD as App\n";
    cmd += "App.getDocument('" + std::string(docB->getName())
        + "').getObject('User').setExpression('Length', '<<Bracket>>.Length / 5')\n";
    Base::Interpreter().runString(cmd.c_str());

    const App::DocumentMergeReport report = App::mergeDocuments(*_doc, *docA, *docB);

    EXPECT_TRUE(report.conflicts.empty());
    const std::string merged = report.merged.at(userId.getValue()).fields.at("Length");
    EXPECT_NE(merged.find("/ 5"), std::string::npos) << merged;

    app.closeDocument(docA->getName());
    app.closeDocument(docB->getName());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
