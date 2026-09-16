// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Configuration.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyOverrideTable.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <src/App/InitApplication.h>

/** A value stated in a document is read as a value, never run as a program.
 *
 *  Cruth: a document is a text a person reads before deciding whether to trust it, and that is
 *  worth nothing if reading it is what runs it. Measured before this: a document whose stored
 *  override was `__import__('pathlib').Path(...).write_text(...)` wrote that file the moment it
 *  was opened -- in a fresh session, with no prompt and nothing for the person to decline. The
 *  stored text was handed to the interpreter as source, because the property system's generic
 *  setter takes a Python object and evaluating the text was the shortest way to have one.
 *
 *  That was closed first by reading the stored text as a literal rather than as source. It is
 *  closed here by there being no stored text to interpret: an override is held as a property of
 *  the kind it is for and written by that property's own serializer, the one dialect the format
 *  has for an authored value. Nothing on the way in or out of the file passes through a language.
 *
 *  So these tests attack the FILE. Nothing a person can author through the program can be a
 *  program any more -- a float property refuses a string while they are typing it -- which leaves
 *  a hand-written document as the whole of the threat, and that is what a file from anyone at all
 *  is.
 */
class ConfigurationValuesAreNotProgramsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        auto& app = App::GetApplication();
        _doc = app.newDocument(app.getUniqueDocumentName("conf").c_str(), "testUser");
        _part = _doc->addObject("App::VarSet", "Part");
        auto* length = dynamic_cast<App::PropertyFloat*>(
            _part->addDynamicProperty("App::PropertyFloat", "Length")
        );
        ASSERT_NE(length, nullptr);
        length->setValue(1.0);
        _conf = dynamic_cast<App::Configuration*>(_doc->addObject("App::Configuration", "Conf"));
        ASSERT_NE(_conf, nullptr);
        unsetenv(ranMarker);
        unsetenv(proofMarker);
    }

    void TearDown() override
    {
        unsetenv(ranMarker);
        unsetenv(proofMarker);
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        if (!_file.empty()) {
            Base::FileInfo(_file).deleteFile();
            _file.clear();
        }
    }

    /// One option stating the given number for the part's Length, made active.
    void optionStating(double value)
    {
        App::PropertyFloat stating;
        stating.setValue(value);
        optionStating(stating);
    }

    /// One option stating the given value for the part's Length, made active.
    void optionStating(const App::Property& value, const std::string& property = "Length")
    {
        _conf->InputName.setValue("Size");
        _conf->Options.setValues(std::vector<std::string> {"Only"});
        _conf->Overrides.clear();
        _conf->Overrides.stateValue({"Only", "Part", property}, value);
        _conf->ActiveOption.setValue("Only");
    }

    double lengthOfThePart() const
    {
        const auto* length = dynamic_cast<const App::PropertyFloat*>(
            _part->getPropertyByName("Length")
        );
        return length != nullptr ? length->getValue() : 0.0;
    }

    /** Save the document, rewrite one run of text in the file, and open what results.
     *
     *  A document arriving from somebody else is a file, not a session, and every guard that only
     *  holds while a person is authoring holds for nobody. This is the only way to state in a
     *  document something the program would not let anyone state.
     */
    App::Document* reopenWithTheFileSaying(const std::string& was, const std::string& now)
    {
        auto& app = App::GetApplication();
        _file = Base::FileInfo::getTempFileName() + ".cpart";
        EXPECT_TRUE(_doc->saveAs(_file.c_str()));
        app.closeDocument(_doc->getName());
        _doc = nullptr;

        // Inside the override table and nowhere else. The value an option states is applied, so
        // the part carries the same number in its own block and a search of the whole file would
        // as readily rewrite the part as the override -- which would test something else entirely.
        std::string text = whatTheFileSays();
        const std::string::size_type from = text.find("<Overrides>");
        const std::string::size_type to = text.find("</Overrides>");
        EXPECT_NE(from, std::string::npos) << "the file states no override table:\n" << text;
        EXPECT_NE(to, std::string::npos);
        const std::string::size_type at = text.find(was, from);
        EXPECT_TRUE(at != std::string::npos && at < to)
            << "the override table does not say '" << was << "':\n"
            << text;
        if (at != std::string::npos && at < to) {
            text.replace(at, was.size(), now);
            std::ofstream out(_file, std::ios::binary | std::ios::trunc);
            out << text;
        }
        _doc = app.openDocument(_file.c_str());
        if (_doc != nullptr) {
            _part = _doc->getObject("Part");
            _conf = dynamic_cast<App::Configuration*>(_doc->getObject("Conf"));
        }
        return _doc;
    }

    std::string whatTheFileSays() const
    {
        std::ifstream in(_file, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    /** Text that leaves a mark in the environment if anything ever runs it.
     *
     * Assignment rather than setdefault, and a mark this test never sets by any other route:
     * Python caches `os.environ`, so a key cleared underneath it with `unsetenv` is still in that
     * cache, and setdefault would then quietly do nothing and leave the mark absent. The first
     * version of this test marked with setdefault after proving the text marks -- and so passed
     * whether or not the text ran.
     */
    static std::string aProgramThatLeavesAMark(const char* mark)
    {
        return std::string("__import__('os').environ.__setitem__('") + mark + "', 'yes')";
    }

    static bool somethingRan(const char* mark)
    {
        return getenv(mark) != nullptr;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    static constexpr const char* ranMarker = "CRUTH_TEST_DOCUMENT_RAN_A_PROGRAM";
    static constexpr const char* proofMarker = "CRUTH_TEST_THE_MARKER_TEXT_WORKS";
    App::Document* _doc {};
    App::DocumentObject* _part {};
    App::Configuration* _conf {};
    std::string _file;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The whole point: a document that states a program where a value belongs does not run it.
TEST_F(ConfigurationValuesAreNotProgramsTest, aStoredProgramIsNeverRun)
{
    // The control on the control, on a mark of its own: text of this shape really does leave one
    // when something runs it, so an absent mark below means nothing ran rather than nothing marks.
    {
        Base::PyGILStateLocker lock;
        Base::Interpreter().runString(aProgramThatLeavesAMark(proofMarker).c_str());
    }
    ASSERT_TRUE(somethingRan(proofMarker))
        << "text of this shape marks nothing when run, so this test cannot tell";

    optionStating(12.5);
    ASSERT_NE(
        reopenWithTheFileSaying("value=\"12.5\"", "value=\"" + aProgramThatLeavesAMark(ranMarker) + "\""),
        nullptr
    );

    EXPECT_FALSE(somethingRan(ranMarker)) << "a document's stored text was executed";
    // The part comes back at what the file states for IT, which is the value the option set while
    // the file was being written. Nothing the program text could have returned reached it.
    EXPECT_EQ(lengthOfThePart(), 12.5) << "a value nobody stated was applied";
    EXPECT_TRUE(_part->isBlockedByAStatement())
        << "the value could not be honoured and the part it would have set rebuilds anyway";
    EXPECT_FALSE(_doc->isWhole());
}

// What the person is told: the text the file states, and that it is not a value.
TEST_F(ConfigurationValuesAreNotProgramsTest, theReasonNamesTheTextAndCallsItNotAValue)
{
    optionStating(12.5);
    ASSERT_NE(reopenWithTheFileSaying("value=\"12.5\"", "value=\"__import__\""), nullptr);

    ASSERT_TRUE(_part->isError());
    // Read from what the part itself carries rather than from the document's recompute log: the
    // log is written while the file is being read and cleared by the first recompute after it, so
    // asking it after an open answers for the recompute and not for the read.
    ASSERT_FALSE(_part->statementsSetFromElsewhere().empty());
    const std::string said = _part->statementsSetFromElsewhere().front().why;
    EXPECT_NE(said.find("__import__"), std::string::npos)
        << "the report does not say what was stated, so it cannot be corrected: " << said;
    EXPECT_NE(said.find("not a value"), std::string::npos) << said;
}

// A value this build could not read is still a value the file states, so a save gives it back --
// or opening a document from a build that knew more than this one, and saving it, would quietly
// throw away what this one could not understand (Amendment 19 Clause 19.1).
TEST_F(ConfigurationValuesAreNotProgramsTest, aValueThisBuildCouldNotReadSurvivesASave)
{
    optionStating(12.5);
    ASSERT_NE(reopenWithTheFileSaying("value=\"12.5\"", "value=\"not a number\""), nullptr);
    ASSERT_TRUE(_part->isBlockedByAStatement());

    ASSERT_TRUE(_doc->save());
    const std::string text = whatTheFileSays();
    EXPECT_NE(text.find("not a number"), std::string::npos)
        << "the save wrote the file's own words out of the file:\n"
        << text;
}

// A kind of property this build does not have is the ordinary way a file states something this
// one cannot read -- an add-on's own kind, with the add-on absent. Kept, named, and blocking.
TEST_F(ConfigurationValuesAreNotProgramsTest, anOverrideOfAKindThisBuildDoesNotHaveIsKept)
{
    optionStating(12.5);
    ASSERT_NE(
        reopenWithTheFileSaying("type=\"App::PropertyFloat\"", "type=\"Addon::PropertyDial\""),
        nullptr
    );

    ASSERT_FALSE(_part->statementsSetFromElsewhere().empty());
    const std::string said = _part->statementsSetFromElsewhere().front().why;
    EXPECT_NE(said.find("Addon::PropertyDial"), std::string::npos) << said;

    ASSERT_TRUE(_doc->save());
    const std::string text = whatTheFileSays();
    EXPECT_NE(text.find("Addon::PropertyDial"), std::string::npos)
        << "the kind this build has no place for was written out of the file:\n"
        << text;
    EXPECT_NE(text.find("value=\"12.5\""), std::string::npos)
        << "the value under it went with it:\n"
        << text;
}

// Nothing a person authors can be a program: the property it is for refuses it as they type it,
// rather than the document carrying it and declining to honour it on every open.
TEST_F(ConfigurationValuesAreNotProgramsTest, aProgramCannotBeAuthoredAsAValue)
{
    Base::PyGILStateLocker lock;
    Py::Dict byProperty;
    byProperty.setItem("Length", Py::String(aProgramThatLeavesAMark(ranMarker)));
    Py::Dict byObject;
    byObject.setItem("Part", byProperty);
    Py::Dict byOption;
    byOption.setItem("Only", byObject);

    EXPECT_THROW(_conf->Overrides.setPyObject(byOption.ptr()), Base::Exception);
    EXPECT_FALSE(somethingRan(ranMarker)) << "refusing the value ran it";
    EXPECT_TRUE(_conf->Overrides.getValues().empty()) << "the refused value was kept anyway";
}

// The narrowing: the values anybody actually writes still apply.
TEST_F(ConfigurationValuesAreNotProgramsTest, aStatedValueStillApplies)
{
    optionStating(10.0);
    EXPECT_EQ(lengthOfThePart(), 10.0);
    EXPECT_FALSE(_part->isBlockedByAStatement());

    optionStating(-2.5);
    EXPECT_EQ(lengthOfThePart(), -2.5);
    EXPECT_FALSE(_part->isBlockedByAStatement());
}

// A container of values is a value too: a vector, a list of names, a colour. Each is written and
// read by its own property, so the table needs to know nothing about what any of them look like.
TEST_F(ConfigurationValuesAreNotProgramsTest, aContainerOfValuesIsAValue)
{
    auto* names = dynamic_cast<App::PropertyStringList*>(
        _part->addDynamicProperty("App::PropertyStringList", "Names")
    );
    ASSERT_NE(names, nullptr);

    App::PropertyStringList stating;
    stating.setValues(std::vector<std::string> {"a", "b"});
    optionStating(stating, "Names");

    ASSERT_EQ(names->getValues().size(), 2U) << _doc->getErrorDescription(_part);
    EXPECT_EQ(names->getValues()[0], "a");
    EXPECT_EQ(names->getValues()[1], "b");
    EXPECT_FALSE(_part->isBlockedByAStatement());
}

// A value of one kind is not a value of another. The old reading coerced through Python, so a
// string could become a number; the value is now stored as the property it is for, and a property
// of a different kind is a mistake said out loud rather than a conversion performed quietly.
TEST_F(ConfigurationValuesAreNotProgramsTest, aValueOfTheWrongKindIsNotConverted)
{
    App::PropertyString words;
    words.setValue("10.0");
    optionStating(words);

    EXPECT_EQ(lengthOfThePart(), 1.0) << "a string was converted into the number it looks like";
    ASSERT_TRUE(_part->isBlockedByAStatement());
    const std::string said = _doc->getErrorDescription(_part);
    EXPECT_NE(said.find("App::PropertyString"), std::string::npos) << said;
    EXPECT_NE(said.find("App::PropertyFloat"), std::string::npos) << said;
}

// The value a person authored comes back as the value they authored, through the file and out the
// other side -- which is the whole reason for storing it as the property it is for.
TEST_F(ConfigurationValuesAreNotProgramsTest, aStatedValueComesBackFromTheFileUnchanged)
{
    App::PropertyFloat stating;
    stating.setValue(0.1 + 0.2);
    optionStating(stating);
    const double authored = lengthOfThePart();

    auto& app = App::GetApplication();
    _file = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(_file.c_str()));
    app.closeDocument(_doc->getName());
    _doc = app.openDocument(_file.c_str());
    ASSERT_NE(_doc, nullptr);
    _part = _doc->getObject("Part");

    EXPECT_EQ(lengthOfThePart(), authored) << "the value did not survive the round trip exactly";
    EXPECT_TRUE(_doc->isWhole());
}

// A length is a length on both sides of the file. The architecture's own example of a
// configuration is a Pad whose Length reads 100mm under one option and 60mm under another, and a
// dimensioned value is the case a property's generic `Copy` gets wrong: it answers with the kind
// that IMPLEMENTS the property rather than the kind it IS, so a length came back a plain float,
// failed to match the length it was for, and was never applied.
TEST_F(ConfigurationValuesAreNotProgramsTest, aDimensionedValueKeepsItsKindAndItsUnit)
{
    auto* span = dynamic_cast<App::PropertyLength*>(
        _part->addDynamicProperty("App::PropertyLength", "Span")
    );
    ASSERT_NE(span, nullptr);
    span->setValue(60.0);

    App::PropertyLength stating;
    stating.setValue(100.0);
    optionStating(stating, "Span");

    ASSERT_FALSE(_part->isBlockedByAStatement()) << _doc->getErrorDescription(_part);
    EXPECT_EQ(span->getValue(), 100.0) << "the option did not apply to the length it states";

    auto& app = App::GetApplication();
    _file = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(_file.c_str()));
    EXPECT_NE(whatTheFileSays().find("App::PropertyLength"), std::string::npos)
        << "the file states the value as some other kind of property than the one it is for";
    app.closeDocument(_doc->getName());
    _doc = app.openDocument(_file.c_str());
    ASSERT_NE(_doc, nullptr);
    _part = _doc->getObject("Part");

    const auto* reread = dynamic_cast<const App::PropertyLength*>(_part->getPropertyByName("Span"));
    ASSERT_NE(reread, nullptr);
    EXPECT_EQ(reread->getValue(), 100.0);
    EXPECT_FALSE(_part->isBlockedByAStatement());
    EXPECT_TRUE(_doc->isWhole());
}
