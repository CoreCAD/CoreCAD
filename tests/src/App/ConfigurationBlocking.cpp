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
#include <Base/FileInfo.h>

#include <map>
#include <vector>
#include <string>

#include <src/App/InitApplication.h>

/** A configuration that cannot be honoured blocks the object whose value it would have set.
 *
 *  Amendment 19 Clause 19.6: blocking follows the VALUE, not the file. Usually a statement sits on
 *  the object it blocks, and that half is built. A configuration is the case where it does not --
 *  it sets values on other objects from outside the dependency graph (§3.4, §7.7) -- and blocking
 *  only the holder there lets a part rebuild at its base value and report success under the name
 *  of the option that failed to apply: a shape nobody designed, presented as finished.
 *
 *  Measured before this was built: a part sat at its base value, reported Up-to-date, and its
 *  document reported itself whole, with the active option naming something it had never applied.
 */
class ConfigurationBlockingTest: public ::testing::Test
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
        length->setValue(10.0);
        _conf = dynamic_cast<App::Configuration*>(_doc->addObject("App::Configuration", "Conf"));
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
    }

    /// One configuration input of two options, the second of which states the given override.
    void withLargeStating(const App::PropertyOverrideTable::Address& address, const App::Property& value)
    {
        _conf->InputName.setValue("Size");
        _conf->Options.setValues(std::vector<std::string> {"Small", "Large"});
        _conf->Overrides.clear();
        state({"Small", "Part", "Length"}, 10.0);
        _conf->Overrides.stateValue(address, value);
        _conf->ActiveOption.setValue("Small");
    }

    /// The ordinary case: an option states a number for the part's Length.
    void state(const App::PropertyOverrideTable::Address& address, double value)
    {
        App::PropertyFloat stating;
        stating.setValue(value);
        _conf->Overrides.stateValue(address, stating);
    }

    /// A value of a kind the property it is for is not: authorable, and not applicable.
    App::PropertyString& wordsWhereANumberBelongs()
    {
        _wrongKind.setValue("wrecked");
        return _wrongKind;
    }

    double lengthOfThePart() const
    {
        const auto* length = dynamic_cast<const App::PropertyFloat*>(
            _part->getPropertyByName("Length")
        );
        return length != nullptr ? length->getValue() : 0.0;
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
    App::DocumentObject* _part {};
    App::Configuration* _conf {};
    App::PropertyString _wrongKind;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// The whole clause in one case: the part is blocked, and the holder is not what was blocked.
TEST_F(ConfigurationBlockingTest, anOptionThatCannotBeAppliedBlocksThePartAndNotItsHolder)
{
    withLargeStating({"Large", "Part", "Length"}, wordsWhereANumberBelongs());
    ASSERT_FALSE(_part->isBlockedByAStatement()) << "blocked before anything was asked of it";

    _conf->ActiveOption.setValue("Large");

    EXPECT_TRUE(_part->isBlockedByAStatement())
        << "the option failed to apply and the part it would have set was left free to rebuild";
    EXPECT_EQ(lengthOfThePart(), 10.0) << "a value that could not be read was applied anyway";
    EXPECT_TRUE(_conf->statementsSetFromElsewhere().empty())
        << "the holder was blocked, which is the behaviour this clause replaces";
}

// A node is blocked by what it HOLDS, not by having been asked to rebuild -- nothing here
// recomputes, and an object whose geometry comes back from the rebuild store never would.
TEST_F(ConfigurationBlockingTest, theBlockIsReportedWithoutAnybodyAskingForARebuild)
{
    withLargeStating({"Large", "Part", "Length"}, wordsWhereANumberBelongs());
    _conf->ActiveOption.setValue("Large");

    ASSERT_TRUE(_part->isError()) << "the part did not report itself blocked";
    const char* why = _doc->getErrorDescription(_part);
    ASSERT_NE(why, nullptr);
    const std::string said = why;
    EXPECT_NE(said.find("Length"), std::string::npos) << "the report does not name the property";
    EXPECT_NE(said.find("Conf"), std::string::npos)
        << "the report does not say where the value is stated, so a person cannot go and change it";
    EXPECT_NE(said.find("Large"), std::string::npos) << "the report does not name the option";
}

// The disclosure is state of the model, not of an interface (P8): a script that opens a directory
// of documents and saves them has to be able to ask.
TEST_F(ConfigurationBlockingTest, theDocumentIsNotWholeWhileAnOptionCannotBeHonoured)
{
    withLargeStating({"Large", "Part", "Length"}, wordsWhereANumberBelongs());
    ASSERT_TRUE(
        _doc->isWhole()
    ) << "not whole before the option that cannot be honoured was picked";

    _conf->ActiveOption.setValue("Large");
    EXPECT_FALSE(_doc->isWhole());

    // The control: the same switch with an option this build can honour leaves it whole.
    state({"Large", "Part", "Length"}, 100.0);
    EXPECT_TRUE(_doc->isWhole()) << "an option that applied cleanly still counted against it";
    EXPECT_EQ(lengthOfThePart(), 100.0) << "the option that could be honoured was not applied";
}

// Released rather than resolved: what the holder no longer states no longer blocks, or the only
// way out of a mistyped value would be to abandon the document.
TEST_F(ConfigurationBlockingTest, whatTheHolderNoLongerStatesNoLongerBlocks)
{
    withLargeStating({"Large", "Part", "Length"}, wordsWhereANumberBelongs());
    _conf->ActiveOption.setValue("Large");
    ASSERT_TRUE(_part->isBlockedByAStatement());

    state({"Large", "Part", "Length"}, 100.0);

    EXPECT_FALSE(_part->isBlockedByAStatement()) << "the corrected option still blocked the part";
    EXPECT_FALSE(_part->isError()) << "the part still reports a failure that has been dealt with";
    EXPECT_TRUE(_doc->isWhole());
}

// A configuration that leaves the document takes its refusals with it.
TEST_F(ConfigurationBlockingTest, aHolderThatIsRemovedStopsBlocking)
{
    withLargeStating({"Large", "Part", "Length"}, wordsWhereANumberBelongs());
    _conf->ActiveOption.setValue("Large");
    ASSERT_TRUE(_part->isBlockedByAStatement());

    _doc->removeObject(_conf->getNameInDocument());
    _conf = nullptr;

    EXPECT_FALSE(_part->isBlockedByAStatement());
    EXPECT_TRUE(_doc->isWhole());
}

// Where the object whose value it would have set is not in the document, there is nothing else to
// block -- and the report says which object it was looking for (§3.6).
TEST_F(ConfigurationBlockingTest, anOptionNamingNoObjectBlocksTheHolderItself)
{
    App::PropertyFloat hundred;
    hundred.setValue(100.0);
    withLargeStating({"Large", "Absent", "Length"}, hundred);
    _conf->ActiveOption.setValue("Large");

    ASSERT_FALSE(_conf->statementsSetFromElsewhere().empty());
    EXPECT_NE(_conf->statementsSetFromElsewhere().front().why.find("Absent"), std::string::npos);
    EXPECT_FALSE(_part->isBlockedByAStatement()) << "an unrelated object was blocked";
    EXPECT_FALSE(_doc->isWhole());
}

// An option naming a property this build has no place for is the ordinary case of a file arriving
// from a build that had one -- and it is silent today: the entry is stepped over.
TEST_F(ConfigurationBlockingTest, anOptionNamingNoPropertyBlocksThePart)
{
    App::PropertyFloat two;
    two.setValue(2.0);
    withLargeStating({"Large", "Part", "Clearance"}, two);
    _conf->ActiveOption.setValue("Large");

    ASSERT_FALSE(_part->statementsSetFromElsewhere().empty());
    const auto& stated = _part->statementsSetFromElsewhere().front();
    EXPECT_EQ(stated.holder, "Conf");
    EXPECT_EQ(stated.property, "Clearance");
    EXPECT_FALSE(_doc->isWhole());
}

// The block is recorded when the document is READ. A session that only opened the file would
// otherwise report the part finished at a value nobody chose, until somebody happened to switch
// the option and discover it.
TEST_F(ConfigurationBlockingTest, theBlockComesBackWhenTheDocumentIsReadAgain)
{
    App::PropertyFloat two;
    two.setValue(2.0);
    withLargeStating({"Large", "Part", "Clearance"}, two);
    _conf->ActiveOption.setValue("Large");

    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    auto& app = App::GetApplication();
    app.closeDocument(_doc->getName());
    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);

    App::DocumentObject* part = _doc->getObject("Part");
    ASSERT_NE(part, nullptr);
    EXPECT_TRUE(part->isBlockedByAStatement())
        << "reading the file back did not find what the option could not honour";
    EXPECT_FALSE(_doc->isWhole());
    Base::FileInfo(path).deleteFile();
}

// The narrowing, held open: a configuration this build CAN honour blocks nothing, and a document
// carrying one reads back whole. A refusal that crept outward would make configurations unusable.
TEST_F(ConfigurationBlockingTest, aConfigurationThatIsHonouredBlocksNothing)
{
    App::PropertyFloat hundred;
    hundred.setValue(100.0);
    withLargeStating({"Large", "Part", "Length"}, hundred);
    _conf->ActiveOption.setValue("Large");
    ASSERT_EQ(lengthOfThePart(), 100.0);
    ASSERT_TRUE(_doc->isWhole());

    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    auto& app = App::GetApplication();
    app.closeDocument(_doc->getName());
    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr);

    EXPECT_TRUE(_doc->isWhole());
    EXPECT_FALSE(_doc->getObject("Part")->isBlockedByAStatement());
    Base::FileInfo(path).deleteFile();
}
