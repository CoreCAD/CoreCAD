// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Configuration.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <Base/Interpreter.h>

#include <cstdlib>
#include <map>
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
 *  Nothing ever intended a document to carry a program. Every override anyone has written is a
 *  number.
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
    }

    /// One option carrying the given override for the part's Length, made active.
    void optionStating(const std::string& value)
    {
        _conf->InputName.setValue("Size");
        _conf->Options.setValues(std::vector<std::string> {"Only"});
        _conf->Overrides.setValues({{"Only|Part|Length", value}});
        _conf->ActiveOption.setValue("Only");
    }

    double lengthOfThePart() const
    {
        const auto* length = dynamic_cast<const App::PropertyFloat*>(
            _part->getPropertyByName("Length")
        );
        return length != nullptr ? length->getValue() : 0.0;
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
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The whole point: a stored override that is a program is not run.
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

    optionStating(aProgramThatLeavesAMark(ranMarker));

    EXPECT_FALSE(somethingRan(ranMarker)) << "a document's stored text was executed";
    EXPECT_EQ(lengthOfThePart(), 1.0) << "a value nobody stated was applied";
    EXPECT_TRUE(_part->isBlockedByAStatement())
        << "the value could not be honoured and the part it would have set rebuilds anyway";
}

// What the person is told: the text they wrote, and that it is not a value.
TEST_F(ConfigurationValuesAreNotProgramsTest, theReasonNamesTheTextAndCallsItNotAValue)
{
    optionStating("__import__('os')");

    ASSERT_TRUE(_part->isError());
    const char* why = _doc->getErrorDescription(_part);
    ASSERT_NE(why, nullptr);
    const std::string said = why;
    EXPECT_NE(said.find("__import__"), std::string::npos)
        << "the report does not say what was stated, so it cannot be corrected: " << said;
    EXPECT_NE(said.find("not a value"), std::string::npos) << said;
}

// The narrowing: the values anybody actually writes still apply.
TEST_F(ConfigurationValuesAreNotProgramsTest, aStatedValueStillApplies)
{
    optionStating("10.0");
    EXPECT_EQ(lengthOfThePart(), 10.0);
    EXPECT_FALSE(_part->isBlockedByAStatement());

    optionStating("-2.5");
    EXPECT_EQ(lengthOfThePart(), -2.5);
    EXPECT_FALSE(_part->isBlockedByAStatement());
}

// A container of values is a value too: a vector, a list of names, a colour.
TEST_F(ConfigurationValuesAreNotProgramsTest, aContainerOfValuesIsAValue)
{
    auto* names = dynamic_cast<App::PropertyStringList*>(
        _part->addDynamicProperty("App::PropertyStringList", "Names")
    );
    ASSERT_NE(names, nullptr);

    _conf->InputName.setValue("Size");
    _conf->Options.setValues(std::vector<std::string> {"Only"});
    _conf->Overrides.setValues({{"Only|Part|Names", "['a', 'b']"}});
    _conf->ActiveOption.setValue("Only");

    ASSERT_EQ(names->getValues().size(), 2U) << _doc->getErrorDescription(_part);
    EXPECT_EQ(names->getValues()[0], "a");
    EXPECT_EQ(names->getValues()[1], "b");
    EXPECT_FALSE(_part->isBlockedByAStatement());
}

// A name is not a value either -- the old reading resolved names against the interpreter, so a
// document could reach whatever the running program happened to have.
//
// The name is one that resolves to a number a float property would take. A name resolving to
// something the property refuses is blocked under both readings, and a test using one of those
// cannot tell the difference.
TEST_F(ConfigurationValuesAreNotProgramsTest, aNameIsNotAValue)
{
    {
        Base::PyGILStateLocker lock;
        Base::Interpreter().runString("cruth_test_value_in_the_interpreter = 42.0");
    }

    optionStating("cruth_test_value_in_the_interpreter");

    EXPECT_EQ(lengthOfThePart(), 1.0) << "the document reached a value held by the interpreter";
    EXPECT_TRUE(_part->isBlockedByAStatement());
}
