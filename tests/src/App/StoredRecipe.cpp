// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "gtest/gtest.h"

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/FileInfo.h>

#include <App/Expression.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyExpressionEngine.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/StoredRecipe.h>
#include <App/Services.h>
#include <Base/ServiceProvider.h>

#include <cstring>
#include <map>
#include <sstream>
#include <string>

using namespace App;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

class StoredRecipeTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _sourceName = App::GetApplication().getUniqueDocumentName("stored");
        _source = App::GetApplication().newDocument(_sourceName.c_str(), "testUser");
        _rebuiltName = App::GetApplication().getUniqueDocumentName("rebuilt");
        _rebuilt = App::GetApplication().newDocument(_rebuiltName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_rebuiltName.c_str());
        if (_source != nullptr) {
            App::GetApplication().closeDocument(_sourceName.c_str());
        }
    }

    /// The objects half of a written recipe -- the half that must not depend on creation order.
    /// The document half carries identity, which two live documents may never share.
    static std::string objectsSection(const std::string& written)
    {
        const std::string::size_type start = written.find("<Objects");
        return start == std::string::npos ? written : written.substr(start);
    }

    /// Write the source document's stored recipe and read it back into the empty one, which is
    /// the whole claim being tested: the file returns the document, not a readable summary of it.
    void roundTrip()
    {
        std::istringstream text(formatStoredRecipe(*_source));
        restoreStoredRecipe(*_rebuilt, text);
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::string _sourceName {};
    std::string _rebuiltName {};
    App::Document* _source {};
    App::Document* _rebuilt {};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The spine: an object comes back with its type, its in-document name, its durable identity and
// its authored values -- and the geometry it computed does not come back at all, because a shape
// is rebuilt from the recipe rather than stored in it.
TEST_F(StoredRecipeTest, anObjectReturnsWithItsIdentityTypeAndAuthoredValues)
{
    // Arrange
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    box->Label.setValue("Bearing block");
    static_cast<PropertyLength*>(box->getPropertyByName("Length"))->setValue(40.0);
    static_cast<PropertyLength*>(box->getPropertyByName("Height"))->setValue(12.5);
    _source->recompute();

    // Act
    roundTrip();

    // Assert
    DocumentObject* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->getTypeId().getName(), std::string("Part::Box"));
    EXPECT_EQ(rebuilt->Uid.getValueStr(), box->Uid.getValueStr());
    EXPECT_EQ(std::string(rebuilt->Label.getValue()), std::string("Bearing block"));
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(rebuilt->getPropertyByName("Length"))->getValue(),
        40.0
    );
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(rebuilt->getPropertyByName("Height"))->getValue(),
        12.5
    );
}

// A value has to return EXACTLY, not to the four decimals a person reads. The readable view
// rounds an outcome on purpose; a file of record that did the same would quietly redesign the
// part every time it was read.
TEST_F(StoredRecipeTest, aValueReturnsExactlyNotRounded)
{
    // Arrange: a length no rounded rendering could reproduce.
    constexpr double awkward = 12.345678901234567;
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    static_cast<PropertyLength*>(box->getPropertyByName("Length"))->setValue(awkward);

    // Act
    roundTrip();

    // Assert: the same double, bit for bit.
    auto* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(static_cast<PropertyLength*>(rebuilt->getPropertyByName("Length"))->getValue(), awkward);
}

// A property added at runtime carries its declaration, or the value would have nowhere to land.
TEST_F(StoredRecipeTest, aPropertyAddedAtRuntimeIsDeclaredAndReturns)
{
    // Arrange
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    auto* note = static_cast<PropertyString*>(
        box->addDynamicProperty("App::PropertyString", "Supplier", "Sourcing", "who makes it")
    );
    ASSERT_NE(note, nullptr);
    note->setValue("Cruth Engineering");

    // Act
    roundTrip();

    // Assert
    auto* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);
    auto* returned = rebuilt->getPropertyByName("Supplier");
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(
        std::string(static_cast<PropertyString*>(returned)->getValue()),
        std::string("Cruth Engineering")
    );
    EXPECT_EQ(std::string(rebuilt->getPropertyGroup("Supplier")), std::string("Sourcing"));
}

// The document's own authored facts are part of the recipe. The readable view walks objects only,
// so a document's comment, its author and its identity had nowhere to go at all.
//
// The source document is closed before the file is read back, because that is the real case -- a
// file becomes a document when it is opened -- and because two documents open at once may not
// share one identity: the document model mints a new uuid for the second, on purpose.
TEST_F(StoredRecipeTest, theDocumentsOwnFactsAreCarried)
{
    // Arrange
    _source->Comment.setValue("first cut of the housing");
    _source->CreatedBy.setValue("S. Barton");
    const std::string written = formatStoredRecipe(*_source);
    const std::string identity = _source->Uid.getValueStr();
    App::GetApplication().closeDocument(_sourceName.c_str());
    _source = nullptr;

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert
    EXPECT_EQ(std::string(_rebuilt->Comment.getValue()), std::string("first cut of the housing"));
    EXPECT_EQ(std::string(_rebuilt->CreatedBy.getValue()), std::string("S. Barton"));
    EXPECT_EQ(_rebuilt->Uid.getValueStr(), identity);
}

// Objects are written in durable-id order, so the file does not depend on the order they were
// created in. Two documents holding the same design must produce the same file whichever object
// was made first -- otherwise a reordered session reads as a rewritten document.
TEST_F(StoredRecipeTest, theFileDoesNotDependOnCreationOrder)
{
    // Arrange: the same two objects, created in opposite orders, given matching identities.
    auto* first = _source->addObject("Part::Box", "First");
    auto* second = _source->addObject("Part::Box", "Second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    auto* laterSecond = _rebuilt->addObject("Part::Box", "Second");
    auto* laterFirst = _rebuilt->addObject("Part::Box", "First");
    ASSERT_NE(laterSecond, nullptr);
    ASSERT_NE(laterFirst, nullptr);
    laterFirst->Uid.setValue(first->Uid.getValueStr());
    laterSecond->Uid.setValue(second->Uid.getValueStr());

    // Act
    const std::string written = formatStoredRecipe(*_source);
    const std::string writtenInReverse = formatStoredRecipe(*_rebuilt);

    // Assert
    EXPECT_EQ(objectsSection(written), objectsSection(writtenInReverse));
}

// A reference that leaves the document is carried by the durable pair the link property already
// writes -- the target document's uuid beside the object's uuid -- with the file path left as a
// locator hint. The recipe does not invent a second, weaker way of saying it.
TEST_F(StoredRecipeTest, aReferenceThatLeavesTheDocumentIsCarriedByDurableIds)
{
    // Arrange: a link that leaves the document. Both documents must be saved first -- a link
    // between documents is addressed by file as well as by identity.
    const std::string here = Base::FileInfo::getTempPath() + "stored_recipe_here.FCStd";
    const std::string there = Base::FileInfo::getTempPath() + "stored_recipe_there.FCStd";
    auto* box = _source->addObject("Part::Box", "Block");
    auto* elsewhere = _rebuilt->addObject("Part::Box", "Elsewhere");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(elsewhere, nullptr);
    _source->saveAs(here.c_str());
    _rebuilt->saveAs(there.c_str());

    auto* link = static_cast<PropertyXLink*>(
        box->addDynamicProperty("App::PropertyXLink", "Neighbour")
    );
    ASSERT_NE(link, nullptr);
    link->setValue(elsewhere);

    // Act
    const std::string written = formatStoredRecipe(*_source);

    // Assert -- both halves of the binding are in the file, and it is no longer a stated gap.
    EXPECT_NE(written.find("docUuid=\"" + _rebuilt->Uid.getValueStr() + "\""), std::string::npos);
    EXPECT_NE(written.find("uuid=\"" + elsewhere->Uid.getValueStr() + "\""), std::string::npos);
    EXPECT_EQ(written.find("reason=\"cross-document reference\""), std::string::npos);

    Base::FileInfo(here).deleteFile();
    Base::FileInfo(there).deleteFile();
    Base::FileInfo(here + ".recipe").deleteFile();
    Base::FileInfo(there + ".recipe").deleteFile();
}

// A reference comes back pointing at the same object, and the file says so by durable id rather
// than by the target's name.
TEST_F(StoredRecipeTest, aReferenceReturnsAndIsWrittenByDurableId)
{
    // Arrange
    auto* target = _source->addObject("Part::Box", "Block");
    auto* holder = _source->addObject("Part::Box", "Holder");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(holder, nullptr);
    auto* link = static_cast<PropertyLink*>(
        holder->addDynamicProperty("App::PropertyLink", "BuiltOn", "Base", "what it sits on")
    );
    ASSERT_NE(link, nullptr);
    link->setValue(target);

    // Act
    const std::string written = formatStoredRecipe(*_source);
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert: the file binds by identity, not by the name "Block"...
    EXPECT_NE(
        written.find("<Target uuid=\"" + target->Uid.getValueStr() + "\" sub=\"\"/>"),
        std::string::npos
    );

    // ...and the rebuilt reference points at the rebuilt object.
    auto* rebuiltHolder = _rebuilt->getObject("Holder");
    auto* rebuiltTarget = _rebuilt->getObject("Block");
    ASSERT_NE(rebuiltHolder, nullptr);
    ASSERT_NE(rebuiltTarget, nullptr);
    auto* returned = static_cast<PropertyLink*>(rebuiltHolder->getPropertyByName("BuiltOn"));
    ASSERT_NE(returned, nullptr);
    EXPECT_EQ(returned->getValue(), rebuiltTarget);
}

// The binding survives the objects being renamed on the way in. This is the whole reason for
// binding by durable id: read the same file into a document that already holds a "Block" and the
// rebuilt objects get different names -- a reference written by name would land on the wrong
// object or on nothing at all.
TEST_F(StoredRecipeTest, aReferenceSurvivesTheObjectsBeingRenamed)
{
    // Arrange: the source, and a destination that already uses the names it will ask for.
    auto* target = _source->addObject("Part::Box", "Block");
    auto* holder = _source->addObject("Part::Box", "Holder");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(holder, nullptr);
    static_cast<PropertyLink*>(holder->addDynamicProperty("App::PropertyLink", "BuiltOn"))
        ->setValue(target);

    auto* squatter = _rebuilt->addObject("Part::Box", "Block");
    ASSERT_NE(squatter, nullptr);

    // Act
    std::istringstream text(formatStoredRecipe(*_source));
    restoreStoredRecipe(*_rebuilt, text);

    // Assert: the rebuilt target took a different name...
    auto* rebuiltHolder = _rebuilt->getObject("Holder");
    ASSERT_NE(rebuiltHolder, nullptr);
    auto* returned = static_cast<PropertyLink*>(rebuiltHolder->getPropertyByName("BuiltOn"));
    ASSERT_NE(returned, nullptr);
    ASSERT_NE(returned->getValue(), nullptr);
    EXPECT_NE(std::string(returned->getValue()->getNameInDocument()), std::string("Block"));

    // ...and the reference still found it, because it was bound to its identity.
    EXPECT_EQ(returned->getValue()->Uid.getValueStr(), target->Uid.getValueStr());
    EXPECT_NE(returned->getValue(), squatter);
}

// Which face was picked rides along with the identity. A reference binds by durable id, but a
// sketch drawn on one face of a part and the same sketch on the opposite face are not the same
// design, and only the picked element tells them apart.
TEST_F(StoredRecipeTest, thePickedFaceRidesAlongsideTheIdentity)
{
    // Arrange
    auto* target = _source->addObject("Part::Box", "Block");
    auto* holder = _source->addObject("Part::Box", "Holder");
    ASSERT_NE(target, nullptr);
    ASSERT_NE(holder, nullptr);
    auto* link = static_cast<PropertyLinkSub*>(
        holder->addDynamicProperty("App::PropertyLinkSub", "Face")
    );
    ASSERT_NE(link, nullptr);
    link->setValue(target, {"Face6"});

    // Act
    std::istringstream text(formatStoredRecipe(*_source));
    restoreStoredRecipe(*_rebuilt, text);

    // Assert
    auto* rebuiltHolder = _rebuilt->getObject("Holder");
    ASSERT_NE(rebuiltHolder, nullptr);
    auto* returned = static_cast<PropertyLinkSub*>(rebuiltHolder->getPropertyByName("Face"));
    ASSERT_NE(returned, nullptr);
    ASSERT_EQ(returned->getSubValues().size(), 1u);
    EXPECT_EQ(returned->getSubValues().front(), std::string("Face6"));
    EXPECT_EQ(returned->getValue(), _rebuilt->getObject("Block"));
}

// A formula is authored content -- the value a person typed is the formula, not the number it
// produced -- so it has to come back as a formula.
TEST_F(StoredRecipeTest, aFormulaReturnsAsAFormula)
{
    // Arrange
    auto* driver = _source->addObject("Part::Box", "Driver");
    auto* driven = _source->addObject("Part::Box", "Driven");
    ASSERT_NE(driver, nullptr);
    ASSERT_NE(driven, nullptr);
    static_cast<PropertyLength*>(driver->getPropertyByName("Length"))->setValue(30.0);
    driven->setExpression(
        App::ObjectIdentifier::parse(driven, "Length"),
        std::shared_ptr<App::Expression>(App::Expression::parse(driven, "Driver.Length * 2"))
    );
    _source->recompute();

    // Act
    std::istringstream text(formatStoredRecipe(*_source));
    restoreStoredRecipe(*_rebuilt, text);

    // Assert
    auto* rebuilt = _rebuilt->getObject("Driven");
    ASSERT_NE(rebuilt, nullptr);
    const App::ObjectIdentifier path = App::ObjectIdentifier::parse(rebuilt, "Length");
    const App::PropertyExpressionEngine::ExpressionInfo info = rebuilt->getExpression(path);
    ASSERT_NE(info.expression, nullptr);
    EXPECT_EQ(info.expression->toString(), std::string("Driver.Length * 2"));
}

// The file states its content and never a count of it, because a count is a landmine in a file
// whose whole purpose is that people diff and merge it.
//
// This is the measured failure, reconstructed exactly. One ancestor part; two people each add one
// independent feature; both sides therefore write the SAME larger number on the line that declares
// how many objects there are, so a textual merge takes that line without even raising a conflict.
// Resolved the natural way -- keep both sides' objects -- the document used to open with no warning
// of any kind and one of the two added features simply absent.
TEST_F(StoredRecipeTest, aTextualMergeThatKeepsBothSidesLosesNothing)
{
    // Arrange -- the common ancestor, and the two blocks the two people each added.
    auto* shared = _source->addObject("Part::Box", "Shared");
    ASSERT_NE(shared, nullptr);
    auto* mine = _source->addObject("Part::Box", "Mine");
    auto* yours = _source->addObject("Part::Box", "Yours");
    ASSERT_NE(mine, nullptr);
    ASSERT_NE(yours, nullptr);
    const std::string minesBlock = formatStoredRecipeObject(*mine);
    const std::string yoursBlock = formatStoredRecipeObject(*yours);
    _source->removeObject("Mine");
    _source->removeObject("Yours");

    // The ancestor, plus both added blocks: what a person or a merge tool produces by keeping
    // both sides. Any declared length the file still carried would name one fewer than is there.
    std::string merged = formatStoredRecipe(*_source);
    const std::string::size_type close = merged.find("</Objects>");
    ASSERT_NE(close, std::string::npos);
    merged.insert(close, minesBlock + yoursBlock);

    const std::string identity = _source->Uid.getValueStr();
    App::GetApplication().closeDocument(_sourceName.c_str());
    _source = nullptr;

    // Act
    std::istringstream text(merged);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- both people's work is there.
    EXPECT_NE(_rebuilt->getObject("Shared"), nullptr);
    EXPECT_NE(_rebuilt->getObject("Mine"), nullptr) << "one side's added feature was dropped";
    EXPECT_NE(_rebuilt->getObject("Yours"), nullptr) << "one side's added feature was dropped";
    EXPECT_EQ(_rebuilt->Uid.getValueStr(), identity);
}

// A file that restated its own content could disagree with itself. Nothing in the recipe declares
// how many of anything follows, so there is no second answer to keep in step.
TEST_F(StoredRecipeTest, theFileDeclaresNoLengths)
{
    // Arrange
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    box->Label.setValue("Bearing block");

    // Act
    const std::string written = formatStoredRecipe(*_source);

    // Assert
    EXPECT_EQ(written.find("Count=\""), std::string::npos);
}

/// A stand-in for the view layer, so that the App half of the appearance question can be tested
/// where there is no GUI to answer it.
///
/// The container it hands back is another document object, because what matters here is that the
/// file asks, carries what it is given, and hands it back to whoever answers the same question on
/// the way in -- not what kind of container the answer happens to be.
class StubAppearance: public App::DisplayStateProvider
{
public:
    App::PropertyContainer* appearanceOf(const App::DocumentObject& object) const override
    {
        const char* name = object.getNameInDocument();
        if (name == nullptr) {
            return nullptr;
        }
        const auto found = _byName.find(name);
        return found == _byName.end() ? nullptr : found->second;
    }

    void answerFor(const std::string& name, App::PropertyContainer* container)
    {
        _byName[name] = container;
    }

    /// A session with no view layer answers nothing, which is what a headless save does.
    void answerNothing()
    {
        _byName.clear();
    }

    static StubAppearance& theOne()
    {
        static StubAppearance* stub = [] {
            auto* made = new StubAppearance;
            Base::registerServiceImplementation<App::DisplayStateProvider>(made);
            return made;
        }();
        return *stub;
    }

private:
    std::map<std::string, App::PropertyContainer*> _byName;
};

// A colour a person chose is authored content: nothing in the document produces it, and a record
// that dropped it would come back a different-looking part. So the file of record carries it, in a
// block of its own inside the object -- and the deletable project cache, which is where the whole
// of the display state used to live, no longer decides whether it survives.
TEST_F(StoredRecipeTest, theChosenAppearanceIsCarriedInTheFile)
{
    // Arrange -- an object, and somewhere the view layer keeps how it looks.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    auto* chosen = _source->addObject("Part::Box", "Chosen");
    ASSERT_NE(chosen, nullptr);
    auto* colour = static_cast<PropertyLength*>(chosen->getPropertyByName("Length"));
    ASSERT_NE(colour, nullptr);
    colour->setValue(0.75);
    StubAppearance::theOne().answerFor("Block", chosen);

    // Act
    const std::string written = formatStoredRecipe(*_source);

    // Assert -- the object's block names an appearance and carries it.
    EXPECT_NE(written.find("display=\"1\""), std::string::npos);
    EXPECT_NE(written.find("<Display>"), std::string::npos);

    // And it is handed back to whoever answers the same question on the way in.
    auto* returned = _rebuilt->addObject("Part::Box", "Returned");
    ASSERT_NE(returned, nullptr);
    StubAppearance::theOne().answerFor("Block", returned);
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(returned->getPropertyByName("Length"))->getValue(),
        0.75
    );

    StubAppearance::theOne().answerNothing();
}

// A session with nowhere to put an appearance KEEPS the block rather than stepping over it.
//
// Measured before this: a headless open-and-save -- the batch script Clause 19.6 names as the actor
// most able to cause damage at scale -- stripped every colour a person had chosen from every part
// it touched, silently, and a test asserted that was correct. Nothing in this session depends on an
// appearance, so the block is kept, not honoured: it blocks nothing and the document is still
// whole. What would make the document less than its file is losing it.
TEST_F(StoredRecipeTest, aSessionWithNoViewLayerKeepsTheAppearanceItCannotApply)
{
    // Arrange -- a file written WITH an appearance.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    auto* chosen = _source->addObject("Part::Box", "Chosen");
    ASSERT_NE(chosen, nullptr);
    StubAppearance::theOne().answerFor("Block", chosen);
    const std::string withAppearance = formatStoredRecipe(*_source);
    ASSERT_NE(withAppearance.find("<Display>"), std::string::npos);

    // Act -- read and written again by a session that has no view layer.
    StubAppearance::theOne().answerNothing();
    std::istringstream text(withAppearance);
    restoreStoredRecipe(*_rebuilt, text);
    const std::string written = formatStoredRecipe(*_rebuilt);

    // Assert -- the words the file stated come back, in their own place, byte for byte. Byte
    // identity is the assertion that matters: a block re-emitted correctly but relocated produces
    // a diff on a save that changed nothing (Clause 18.1). Compared from <Objects> on, because a
    // second live copy of a document is minted a fresh identity by the model's own rule, so the
    // document block is the one part that is meant to differ here.
    EXPECT_NE(_rebuilt->getObject("Block"), nullptr);
    EXPECT_NE(written.find("<Display>"), std::string::npos);
    EXPECT_NE(written.find("display=\"1\""), std::string::npos);
    const std::string objects = written.substr(written.find("<Objects>"));
    const std::string stated = withAppearance.substr(withAppearance.find("<Objects>"));
    EXPECT_EQ(objects, stated);

    // And it costs the document nothing: nothing in this session depends on an appearance, so no
    // node is blocked by one and the document does not report itself short of its file.
    EXPECT_FALSE(_rebuilt->holdsUnreadContent());
}

// A file may name an object of a type this build cannot construct -- a module not compiled in, an
// add-on absent, a scripted class gone. Measured before this: the read ABORTED at that object, so
// the objects stated after it were lost too (three objects in, one out), and an ordinary save then
// wrote that loss into the file permanently. The object's own content is not this build's to
// discard, and neither is its neighbours'.
TEST_F(StoredRecipeTest, anObjectThisBuildCannotConstructKeepsItsNeighboursAndItself)
{
    // Arrange -- three objects, then one of them retyped to something no build here registers.
    for (const char* name : {"Alpha", "Beta", "Gamma"}) {
        ASSERT_NE(_source->addObject("App::VarSet", name), nullptr);
    }
    _source->recompute();

    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type at = written.find("type=\"App::VarSet\" name=\"Beta\"");
    ASSERT_NE(at, std::string::npos);
    written.replace(at, std::strlen("type=\"App::VarSet\""), "type=\"Absent::Feature\"");

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the neighbours are the control. Losing them is the measured failure.
    EXPECT_NE(_rebuilt->getObject("Alpha"), nullptr)
        << "an object stated before the unreadable one was lost";
    EXPECT_NE(_rebuilt->getObject("Gamma"), nullptr)
        << "an object stated after the unreadable one was lost";
    EXPECT_EQ(_rebuilt->getObject("Beta"), nullptr)
        << "a type this build cannot construct was constructed anyway";

    // The document says what it is: holding something its file states that it could not honour.
    EXPECT_TRUE(_rebuilt->holdsUnreadContent())
        << "the document reported itself whole while holding a statement it could not honour";
    ASSERT_EQ(_rebuilt->unreadObjects().size(), 1U);

    // And it gives the statement back exactly, in its own place: writing what was just read
    // reproduces the file it was read from.
    EXPECT_EQ(objectsSection(formatStoredRecipe(*_rebuilt)), objectsSection(written))
        << "a document that changed nothing did not write back what it was given";
}

// A file may state a reference to an object the document does not hold -- the ordinary result of
// a branch that deleted the target, which is the workflow this format exists for. Measured before
// this: the link resolved to nothing, and the next save wrote an empty reference over the uuid,
// so where it pointed was gone and no merge could ever see that anything had been lost.
TEST_F(StoredRecipeTest, aReferenceWhoseTargetIsAbsentKeepsWhereItPointed)
{
    // Arrange -- one object pointing at another.
    auto* holder = _source->addObject("App::VarSet", "Holder");
    auto* target = _source->addObject("App::VarSet", "Target");
    ASSERT_NE(holder, nullptr);
    ASSERT_NE(target, nullptr);
    auto* link = static_cast<PropertyLink*>(holder->addDynamicProperty("App::PropertyLink", "Uses"));
    ASSERT_NE(link, nullptr);
    link->setValue(target);
    _source->recompute();
    const std::string targetUuid = target->Uid.getValueStr();

    // ... and the target's block struck out of the file, leaving the reference naming something
    // this document does not have.
    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type names = written.find("uuid=\"" + targetUuid + "\" type=");
    ASSERT_NE(names, std::string::npos);
    const std::string::size_type start = written.rfind('\n', names) + 1;
    const std::string::size_type closes = written.find("</Object>", names);
    ASSERT_NE(closes, std::string::npos);
    const std::string::size_type end = written.find('\n', closes) + 1;
    written.erase(start, end - start);
    ASSERT_EQ(written.find("name=\"Target\""), std::string::npos);

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the holder is back, and the statement it carries is still where it pointed.
    DocumentObject* rebuilt = _rebuilt->getObject("Holder");
    ASSERT_NE(rebuilt, nullptr);

    const std::string again = formatStoredRecipe(*_rebuilt);
    EXPECT_NE(again.find(targetUuid), std::string::npos)
        << "a save erased the target a reference named because this document could not resolve it";

    // Said again exactly as it was stated, and in its own place: reading a file and writing it
    // back changes nothing, which is what lets a merge see a deletion as a deletion.
    EXPECT_EQ(objectsSection(again), objectsSection(written))
        << "a document that changed nothing did not write back what it was given";

    // The document says what it is, rather than presenting a reference to nothing as authored.
    EXPECT_TRUE(_rebuilt->holdsUnreadContent())
        << "the document reported itself whole while holding a reference it could not resolve";
}

// The other half of keeping a statement: it is kept only until something supersedes it. A person
// who points the reference somewhere real has authored a value, and the file must say THAT --
// otherwise the note that protected their work starts overwriting it.
TEST_F(StoredRecipeTest, aReferencePointedSomewhereRealNoLongerStatesWhatWasKept)
{
    // Arrange -- the same absent target, kept.
    auto* holder = _source->addObject("App::VarSet", "Holder");
    auto* target = _source->addObject("App::VarSet", "Target");
    ASSERT_NE(holder, nullptr);
    ASSERT_NE(target, nullptr);
    auto* link = static_cast<PropertyLink*>(holder->addDynamicProperty("App::PropertyLink", "Uses"));
    ASSERT_NE(link, nullptr);
    link->setValue(target);
    _source->recompute();
    const std::string targetUuid = target->Uid.getValueStr();

    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type names = written.find("uuid=\"" + targetUuid + "\" type=");
    ASSERT_NE(names, std::string::npos);
    const std::string::size_type start = written.rfind('\n', names) + 1;
    const std::string::size_type closes = written.find("</Object>", names);
    ASSERT_NE(closes, std::string::npos);
    written.erase(start, written.find('\n', closes) + 1 - start);

    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);
    DocumentObject* rebuilt = _rebuilt->getObject("Holder");
    ASSERT_NE(rebuilt, nullptr);
    ASSERT_TRUE(_rebuilt->holdsUnreadContent());

    // Act -- the person points it at an object that is here.
    auto* replacement = _rebuilt->addObject("App::VarSet", "Replacement");
    ASSERT_NE(replacement, nullptr);
    static_cast<PropertyLink*>(rebuilt->getPropertyByName("Uses"))->setValue(replacement);

    // Assert -- the file states what they authored, and no longer states what was kept.
    const std::string again = formatStoredRecipe(*_rebuilt);
    EXPECT_NE(again.find(replacement->Uid.getValueStr()), std::string::npos)
        << "a reference a person authored was not written";
    EXPECT_EQ(again.find(targetUuid), std::string::npos)
        << "a kept statement outlived the value that superseded it and was written over it";
    EXPECT_FALSE(_rebuilt->holdsUnreadContent())
        << "the document still reported itself not whole after the gap was filled";
}

// A file may state a property this build's class does not declare -- an add-on version that had
// one more, a property renamed since, a type this build does not have. Measured before this: the
// reader stepped over it in silence and the next save wrote the object without it, so a value a
// person authored was gone and the file no longer said it had ever been there.
TEST_F(StoredRecipeTest, aPropertyThisBuildDoesNotDeclareIsKeptAsStated)
{
    // Arrange -- an authored value on a property this build will not know about, by striking the
    // declaration the file carries for it: what a build without that add-on sees.
    auto* holder = _source->addObject("App::VarSet", "Holder");
    ASSERT_NE(holder, nullptr);
    auto* clearance = static_cast<PropertyLength*>(
        holder->addDynamicProperty("App::PropertyLength", "Clearance")
    );
    ASSERT_NE(clearance, nullptr);
    clearance->setValue(2.5);
    _source->recompute();

    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type declares = written.find(" dynamic=\"1\"");
    ASSERT_NE(declares, std::string::npos);
    written.erase(declares, std::strlen(" dynamic=\"1\""));

    // The words the file states for it, which are what must come back.
    const std::string::size_type opens = written.find("<Property name=\"Clearance\"");
    ASSERT_NE(opens, std::string::npos);
    const std::string::size_type lineStart = written.rfind('\n', opens) + 1;
    const std::string::size_type closes = written.find("</Property>", opens);
    ASSERT_NE(closes, std::string::npos);
    const std::string stated = written.substr(lineStart, written.find('\n', closes) + 1 - lineStart);

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the neighbours are the control, and the statement itself is the claim.
    DocumentObject* rebuilt = _rebuilt->getObject("Holder");
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(std::string(rebuilt->Label.getValue()), std::string("Holder"))
        << "a property stated beside the unknown one was lost";
    EXPECT_EQ(rebuilt->getPropertyByName("Clearance"), nullptr)
        << "a property this build does not declare was declared anyway";

    const std::string again = formatStoredRecipe(*_rebuilt);
    EXPECT_NE(again.find(stated), std::string::npos)
        << "a save dropped, or reworded, a property the file states that this build has no place "
           "for";

    EXPECT_TRUE(_rebuilt->holdsUnreadContent())
        << "the document reported itself whole while holding a property it could not honour";

    // Given back in its own place, so a save that changed nothing changes nothing.
    EXPECT_EQ(objectsSection(again), objectsSection(written))
        << "a document that changed nothing did not write back what it was given";
}

// The same duty one level up: a property the DOCUMENT ITSELF states.
//
// Measured before this: the document's own block was read with no words to fall back on, so a
// statement here was reported and then dropped -- a tracking code from a records system, a field an
// add-on added -- and the next save wrote the absence over it. The reader now lifts the document's
// own block for the same reason it lifts an object's.
TEST_F(StoredRecipeTest, aPropertyTheDocumentItselfStatesIsKeptAsStated)
{
    // Arrange -- a file whose document block states a property this build does not declare.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    std::string written = formatStoredRecipe(*_source);

    const std::string stated
        = "            <Property name=\"ZZTrackingCode\" type=\"App::PropertyString\">\n"
          "        <String value=\"ACME-PART-0042\"/>\n"
          "            </Property>\n";
    const std::string::size_type documentEnds = written.find("</Properties>");
    ASSERT_NE(documentEnds, std::string::npos);
    const std::string::size_type lineStart = written.rfind('\n', documentEnds) + 1;
    written.insert(lineStart, stated);

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the neighbours are the control, the statement itself is the claim.
    EXPECT_NE(_rebuilt->getObject("Block"), nullptr) << "an object stated after the unknown "
                                                        "document property was lost";
    EXPECT_EQ(_rebuilt->getPropertyByName("ZZTrackingCode"), nullptr)
        << "a property this build does not declare was declared anyway";

    const std::string again = formatStoredRecipe(*_rebuilt);
    EXPECT_NE(again.find(stated), std::string::npos)
        << "a save dropped, or reworded, a property the document's own block states";
    EXPECT_TRUE(_rebuilt->holdsUnreadContent())
        << "the document reported itself whole while holding a statement of its own that it could "
           "not honour";
}

// And one level across: a property stated inside an appearance block. The block belongs to whoever
// answers for the object's display, and a name in there may also name one of the object's OWN
// properties -- so the words are lifted from the appearance block alone. Lifting them from the
// object would give the appearance back a value the object stated, which nobody authored.
TEST_F(StoredRecipeTest, aPropertyStatedInsideAnAppearanceIsKeptFromTheAppearancesOwnWords)
{
    // Arrange -- an object with a Length of its own, and an appearance that has no such property.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    static_cast<PropertyLength*>(box->getPropertyByName("Length"))->setValue(10);
    auto* chosen = _source->addObject("App::VarSet", "Chosen");
    ASSERT_NE(chosen, nullptr);
    StubAppearance::theOne().answerFor("Block", chosen);
    std::string written = formatStoredRecipe(*_source);

    // The appearance states a Length too, and a different one. This build has no place for it
    // there, so it is the file's own words that must come back -- not the object's.
    const std::string stated
        = "                    <Property name=\"Length\" type=\"App::PropertyLength\">\n"
          "        <Float value=\"42\"/>\n"
          "                    </Property>\n";
    const std::string::size_type display = written.find("<Display>");
    ASSERT_NE(display, std::string::npos);
    const std::string::size_type closes = written.find("</Properties>", display);
    ASSERT_NE(closes, std::string::npos);
    written.insert(written.rfind('\n', closes) + 1, stated);

    // Act -- read by a session that does have somewhere to put an appearance.
    auto* returned = _rebuilt->addObject("App::VarSet", "Returned");
    ASSERT_NE(returned, nullptr);
    StubAppearance::theOne().answerFor("Block", returned);
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the appearance's own words come back, in the appearance's own block.
    const std::string again = formatStoredRecipe(*_rebuilt);
    const std::string::size_type displayAgain = again.find("<Display>");
    ASSERT_NE(displayAgain, std::string::npos);
    const std::string appearanceBlock
        = again.substr(displayAgain, again.find("</Display>", displayAgain) - displayAgain);
    EXPECT_NE(appearanceBlock.find(stated), std::string::npos)
        << "a save dropped, or reworded, a property stated inside an appearance";
    EXPECT_EQ(appearanceBlock.find("<Float value=\"10\"/>"), std::string::npos)
        << "the appearance was given back a value lifted from the object's own words";

    // And the object's own statement is untouched by the one that shares its name.
    EXPECT_DOUBLE_EQ(
        static_cast<PropertyLength*>(_rebuilt->getObject("Block")->getPropertyByName("Length"))
            ->getValue(),
        10
    );

    StubAppearance::theOne().answerNothing();
}

// The severe half of the same case: a property of a TYPE this build does not have -- an add-on's
// own kind of value. Declaring it fails, and measured before this the failure escaped the reader,
// so the read stopped at that property and every object stated after it was lost with it.
TEST_F(StoredRecipeTest, aPropertyOfATypeThisBuildDoesNotHaveKeepsItsNeighboursAndItself)
{
    // Arrange -- three objects, the middle one carrying a property whose type is then retyped to
    // something no build here registers.
    for (const char* name : {"Alpha", "Beta", "Gamma"}) {
        ASSERT_NE(_source->addObject("App::VarSet", name), nullptr);
    }
    auto* beta = _source->getObject("Beta");
    ASSERT_NE(beta, nullptr);
    auto* clearance = static_cast<PropertyLength*>(
        beta->addDynamicProperty("App::PropertyLength", "Clearance")
    );
    ASSERT_NE(clearance, nullptr);
    clearance->setValue(2.5);
    _source->recompute();

    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type at = written.find("name=\"Clearance\" type=\"App::PropertyLength\"");
    ASSERT_NE(at, std::string::npos);
    written.replace(
        at + std::strlen("name=\"Clearance\" "),
        std::strlen("type=\"App::PropertyLength\""),
        "type=\"Absent::PropertyThing\""
    );

    // Act
    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);

    // Assert -- the neighbours are the control. Losing them is the measured failure.
    EXPECT_NE(_rebuilt->getObject("Alpha"), nullptr)
        << "an object stated before the unreadable property was lost";
    EXPECT_NE(_rebuilt->getObject("Gamma"), nullptr)
        << "an object stated after the unreadable property was lost";
    ASSERT_NE(_rebuilt->getObject("Beta"), nullptr)
        << "the object carrying the unreadable property was lost";

    EXPECT_TRUE(_rebuilt->holdsUnreadContent())
        << "the document reported itself whole while holding a property it could not honour";
    EXPECT_EQ(objectsSection(formatStoredRecipe(*_rebuilt)), objectsSection(written))
        << "a document that changed nothing did not write back what it was given";
}

// Keeping a statement is half the duty; the other half is not pretending the result is finished
// without it. Measured before this: an object holding a statement this session could not honour
// recomputed anyway, from the part of its input that happened to be legible, and reported success.
TEST_F(StoredRecipeTest, anObjectHoldingWhatCouldNotBeHonouredIsNotRecomputed)
{
    // Arrange -- a box with a property whose declaration is then struck out, so this build has no
    // place for what the file states.
    auto* box = _source->addObject("Part::Box", "Block");
    ASSERT_NE(box, nullptr);
    auto* clearance = static_cast<PropertyLength*>(
        box->addDynamicProperty("App::PropertyLength", "Clearance")
    );
    ASSERT_NE(clearance, nullptr);
    clearance->setValue(2.5);
    _source->recompute();

    std::string written = formatStoredRecipe(*_source);
    const std::string::size_type declares = written.find(" dynamic=\"1\"");
    ASSERT_NE(declares, std::string::npos);
    written.erase(declares, std::strlen(" dynamic=\"1\""));

    std::istringstream text(written);
    restoreStoredRecipe(*_rebuilt, text);
    DocumentObject* rebuilt = _rebuilt->getObject("Block");
    ASSERT_NE(rebuilt, nullptr);

    // Blocked by what it holds, before anything asks it to rebuild -- an object whose geometry
    // came back from the rebuild store is never asked.
    EXPECT_TRUE(rebuilt->isError())
        << "an object reported itself sound while holding a statement it could not honour";

    // Act
    _rebuilt->recompute();

    // Assert -- blocked at the node that holds the statement, and said in the model.
    EXPECT_TRUE(rebuilt->isError())
        << "an object built something from input it could not read in full, and reported success";
    EXPECT_TRUE(rebuilt->isTouched())
        << "the object was marked up to date while holding a statement it could not honour";
    const auto unhonoured = rebuilt->unhonouredStatements();
    ASSERT_EQ(unhonoured.size(), 1U);
    EXPECT_EQ(unhonoured.front().first, std::string("Clearance"))
        << "the report does not name what could not be honoured";
    EXPECT_FALSE(unhonoured.front().second.empty()) << "the report does not say why";
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
