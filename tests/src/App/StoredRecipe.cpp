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

// A session with nowhere to put an appearance steps over the block rather than guessing at a place
// for it -- and one with nowhere to GET an appearance writes no block at all, which is honest: it
// chose none.
TEST_F(StoredRecipeTest, aSessionWithNoViewLayerNeitherWritesNorReadsAnAppearance)
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
    const std::string withoutAppearance = formatStoredRecipe(*_rebuilt);

    // Assert -- the block was stepped over on the way in, and none was invented on the way out.
    EXPECT_NE(_rebuilt->getObject("Block"), nullptr);
    EXPECT_EQ(withoutAppearance.find("<Display>"), std::string::npos);
    EXPECT_EQ(withoutAppearance.find("display=\"1\""), std::string::npos);
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

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
