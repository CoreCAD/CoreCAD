// SPDX-License-Identifier: LGPL-2.1-or-later

#include <FCConfig.h>

#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Sketcher/App/Constraint.h>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <optional>

#include <gtest/gtest.h>
#include <xercesc/util/PlatformUtils.hpp>
#include <QTemporaryFile>

#include <src/App/InitApplication.h>


// Ensure Xerces is initialized before running tests which uses xml
class XercesEnvironment: public ::testing::Environment
{
public:
    void SetUp() override
    {
        try {
            xercesc::XMLPlatformUtils::Initialize();
        }
        catch (const xercesc::XMLException& e) {
            FAIL() << "Xerces init failed: " << xercesc::XMLString::transcode(e.getMessage());
        }
    }

    void TearDown() override
    {
        xercesc::XMLPlatformUtils::Terminate();
    }
};

::testing::Environment* const xercesEnv = ::testing::AddGlobalTestEnvironment(new XercesEnvironment);

class ConstraintPointsAccess: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

TEST_F(ConstraintPointsAccess, testDefaultGeoElementIdsAreSane)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act - no action needed, we are testing the default state

    // Assert
#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
    // Old way of accessing elements
    EXPECT_EQ(constraint.First, Sketcher::GeoEnum::GeoUndef);
    EXPECT_EQ(constraint.FirstPos, Sketcher::PointPos::none);

    EXPECT_EQ(constraint.Second, Sketcher::GeoEnum::GeoUndef);
    EXPECT_EQ(constraint.SecondPos, Sketcher::PointPos::none);

    EXPECT_EQ(constraint.Third, Sketcher::GeoEnum::GeoUndef);
    EXPECT_EQ(constraint.ThirdPos, Sketcher::PointPos::none);

    // New way of accessing elements
#endif
    EXPECT_EQ(
        constraint.getElement(0),
        Sketcher::GeoElementId(Sketcher::GeoEnum::GeoUndef, Sketcher::PointPos::none)
    );
    EXPECT_EQ(
        constraint.getElement(1),
        Sketcher::GeoElementId(Sketcher::GeoEnum::GeoUndef, Sketcher::PointPos::none)
    );
    EXPECT_EQ(
        constraint.getElement(2),
        Sketcher::GeoElementId(Sketcher::GeoEnum::GeoUndef, Sketcher::PointPos::none)
    );
}

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
TEST_F(ConstraintPointsAccess, testOldWriteIsReadByNew)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.First = 23;
    constraint.FirstPos = Sketcher::PointPos::start;
    constraint.Second = 34;
    constraint.SecondPos = Sketcher::PointPos::end;
    constraint.Third = 45;
    constraint.ThirdPos = Sketcher::PointPos::mid;

    // Assert
    EXPECT_EQ(
        constraint.getElement(0),
        Sketcher::GeoElementId(Sketcher::GeoElementId(23, Sketcher::PointPos::start))
    );
    EXPECT_EQ(
        constraint.getElement(1),
        Sketcher::GeoElementId(Sketcher::GeoElementId(34, Sketcher::PointPos::end))
    );
    EXPECT_EQ(
        constraint.getElement(2),
        Sketcher::GeoElementId(Sketcher::GeoElementId(45, Sketcher::PointPos::mid))
    );
}

TEST_F(ConstraintPointsAccess, testNewWriteIsReadByOld)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.setElement(0, Sketcher::GeoElementId(23, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(34, Sketcher::PointPos::end));
    constraint.setElement(2, Sketcher::GeoElementId(45, Sketcher::PointPos::mid));

    // Assert
    EXPECT_EQ(constraint.First, 23);
    EXPECT_EQ(constraint.FirstPos, Sketcher::PointPos::start);
    EXPECT_EQ(constraint.Second, 34);
    EXPECT_EQ(constraint.SecondPos, Sketcher::PointPos::end);
    EXPECT_EQ(constraint.Third, 45);
    EXPECT_EQ(constraint.ThirdPos, Sketcher::PointPos::mid);
}
#endif

TEST_F(ConstraintPointsAccess, testThreeElementsByDefault)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act - no action needed, we are testing the default state

    // Assert
    EXPECT_EQ(constraint.getElementsSize(), 3);
}

TEST_F(ConstraintPointsAccess, testFourElementsWhenAddingOne)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.addElement(Sketcher::GeoElementId(1, Sketcher::PointPos::start));

    // Assert
    EXPECT_EQ(constraint.getElementsSize(), 4);
}

// The constraint's durable identity must be written to the file. Without it, two
// versions of a sketch cannot be lined up on a merge — the whole point of the tag.
TEST_F(ConstraintPointsAccess, testTagIsSerialized)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;

    // Act
    Base::StringWriter writer;
    constraint.Save(writer);

    // Assert
    EXPECT_TRUE(writer.getString().find("Tag=\"") != std::string::npos)
        << "constraint identity is not written to the file";
}

// Identity must survive a save/restore round-trip unchanged.
TEST_F(ConstraintPointsAccess, testTagRestoredFromSerialization)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    const std::string originalTag = boost::uuids::to_string(constraint.getTag());

    Base::StringWriter writer;
    writer.Stream() << "<root>\n";  // Wrap in a root element to make constraint.Save happy
    constraint.Save(writer);
    writer.Stream() << "</root>";

    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write(writer.getString().c_str(), writer.getString().size());
    tempFile.flush();

    std::ifstream inputFile(tempFile.fileName().toStdString());
    ASSERT_TRUE(inputFile.is_open());

    // Act
    Base::XMLReader reader(tempFile.fileName().toStdString().c_str(), inputFile);
    Sketcher::Constraint restoredConstraint;
    restoredConstraint.Restore(reader);
    inputFile.close();

    // Assert
    EXPECT_EQ(boost::uuids::to_string(restoredConstraint.getTag()), originalTag)
        << "constraint identity did not survive save/restore";
}

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
TEST_F(ConstraintPointsAccess, testElementSerializationWhenAccessingOldWay)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.First = 23;
    constraint.FirstPos = Sketcher::PointPos::start;
    constraint.Second = 34;
    constraint.SecondPos = Sketcher::PointPos::end;
    constraint.Third = 45;
    constraint.ThirdPos = Sketcher::PointPos::mid;

    Base::StringWriter writer = {};
    constraint.Save(writer);

    // Assert -- each reference stated once, in its own element. No geometry context here, so
    // there is no durable identity to state and the GeoId is all there is.
    std::string serialized = writer.getString();
    EXPECT_NE(serialized.find("<Element geoId=\"23\" at=\"start\"/>"), std::string::npos)
        << serialized;
    EXPECT_NE(serialized.find("<Element geoId=\"34\" at=\"end\"/>"), std::string::npos) << serialized;
    EXPECT_NE(serialized.find("<Element geoId=\"45\" at=\"mid\"/>"), std::string::npos) << serialized;
    EXPECT_EQ(serialized.find("First="), std::string::npos)
        << "the reference is stated a second time: " << serialized;
}
#endif

TEST_F(ConstraintPointsAccess, testElementSerializationWhenAccessingNewWay)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.setElement(0, Sketcher::GeoElementId(23, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(34, Sketcher::PointPos::end));
    constraint.setElement(2, Sketcher::GeoElementId(45, Sketcher::PointPos::mid));

    Base::StringWriter writer = {};
    constraint.Save(writer);

    // Assert -- each reference stated once, in its own element. No geometry context here, so
    // there is no durable identity to state and the GeoId is all there is.
    std::string serialized = writer.getString();
    EXPECT_NE(serialized.find("<Element geoId=\"23\" at=\"start\"/>"), std::string::npos)
        << serialized;
    EXPECT_NE(serialized.find("<Element geoId=\"34\" at=\"end\"/>"), std::string::npos) << serialized;
    EXPECT_NE(serialized.find("<Element geoId=\"45\" at=\"mid\"/>"), std::string::npos) << serialized;
    EXPECT_EQ(serialized.find("First="), std::string::npos)
        << "the reference is stated a second time: " << serialized;
}

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
TEST_F(ConstraintPointsAccess, testElementSerializationWhenMixingOldAndNew)  // NOLINT
{
    // Arrange
    auto constraint = Sketcher::Constraint();

    // Act
    constraint.setElement(0, Sketcher::GeoElementId(23, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(34, Sketcher::PointPos::end));
    constraint.Second = 45;  // Old way
    constraint.SecondPos = Sketcher::PointPos::mid;

    Base::StringWriter writer = {};
    constraint.Save(writer);

    // Assert
    std::string serialized = writer.getString();
    EXPECT_NE(serialized.find("<Element geoId=\"23\" at=\"start\"/>"), std::string::npos)
        << serialized;

    // The old members are the same storage under another name, so what was set through them is
    // what the file states.
    EXPECT_NE(serialized.find("<Element geoId=\"45\" at=\"mid\"/>"), std::string::npos) << serialized;

    // The third element holds nothing, and what stands in for nothing is not stated. Counted
    // rather than searched for: a padded element states neither an identity nor an index, so
    // looking for the sentinel's number would pass either way.
    EXPECT_EQ(
        serialized.find("<Element", serialized.find("<Element", serialized.find("<Element") + 1) + 1),
        std::string::npos
    ) << "an element holding nothing was padded into the file: "
      << serialized;
}
#endif

TEST_F(ConstraintPointsAccess, testElementsRestoredFromSerialization)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(23, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(34, Sketcher::PointPos::end));
    constraint.setElement(2, Sketcher::GeoElementId(45, Sketcher::PointPos::mid));

    Base::StringWriter writer;
    writer.Stream() << "<root>\n";  // Wrap in a root element to make constraint.Save happy
    constraint.Save(writer);
    writer.Stream() << "</root>";

    // Write to temporary file
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write(writer.getString().c_str(), writer.getString().size());
    tempFile.flush();

    // Open with std::ifstream and parse
    std::string filename = tempFile.fileName().toStdString();
    std::ifstream inputFile(filename);
    ASSERT_TRUE(inputFile.is_open());

    Base::XMLReader reader(tempFile.fileName().toStdString().c_str(), inputFile);
    Sketcher::Constraint restoredConstraint;
    restoredConstraint.Restore(reader);

    // Assert
    EXPECT_EQ(restoredConstraint.getElement(0), Sketcher::GeoElementId(23, Sketcher::PointPos::start));
    EXPECT_EQ(restoredConstraint.getElement(1), Sketcher::GeoElementId(34, Sketcher::PointPos::end));
    EXPECT_EQ(restoredConstraint.getElement(2), Sketcher::GeoElementId(45, Sketcher::PointPos::mid));

    inputFile.close();
}

/** A document written by an older program states its references positionally, and is read.
 *
 *  Cruth: the positional GeoId is no longer how this build states a reference -- see the element
 *  form above -- but a file written before that still says First/FirstPos, and a position can only
 *  be read as a position. It is read here and stated by durable identity on the next save.
 *
 *  What was here before this was a pair of tests pinning down WHICH of three competing statements
 *  of the same reference won. That question is what the change removed: there is one statement now,
 *  so there is nothing to prefer.
 */
TEST_F(ConstraintPointsAccess, testAnOlderDocumentsPositionalReferenceIsRead)  // NOLINT
{
    const std::string serializedConstraint = fmt::format(
        "<Constrain "
        R"(Name="" )"
        R"(Type="0" )"
        R"(Value="0" )"
        R"(LabelDistance="10" )"
        R"(LabelPosition="0" )"
        R"(IsDriving="1" )"
        R"(IsInVirtualSpace="0" )"
        R"(IsActive="1" )"
        R"(First="{}" )"
        R"(Second="{}" )"
        R"(Third="{}" )"
        R"(FirstPos="{}" )"
        R"(SecondPos="{}" )"
        R"(ThirdPos="{}" )"
        "/>",
        67,
        78,
        89,
        static_cast<int>(Sketcher::PointPos::mid),
        static_cast<int>(Sketcher::PointPos::start),
        static_cast<int>(Sketcher::PointPos::end)
    );

    Base::StringWriter writer;
    auto& stream {writer.Stream()};
    stream << "<root>\n";  // Wrap in a root element to make constraint.
    stream << serializedConstraint;
    stream << "</root>";

    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write(writer.getString().c_str(), writer.getString().size());
    tempFile.flush();

    const std::string filename = tempFile.fileName().toStdString();
    std::ifstream inputFile(filename);
    ASSERT_TRUE(inputFile.is_open());

    Base::XMLReader reader(tempFile.fileName().toStdString().c_str(), inputFile);
    Sketcher::Constraint restoredConstraint;
    restoredConstraint.Restore(reader);

    EXPECT_EQ(restoredConstraint.getElement(0), Sketcher::GeoElementId(67, Sketcher::PointPos::mid));
    EXPECT_EQ(restoredConstraint.getElement(1), Sketcher::GeoElementId(78, Sketcher::PointPos::start));
    EXPECT_EQ(restoredConstraint.getElement(2), Sketcher::GeoElementId(89, Sketcher::PointPos::end));

    inputFile.close();
}

TEST_F(ConstraintPointsAccess, testSubstituteIndex)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(10, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(20, Sketcher::PointPos::end));
    constraint.setElement(2, Sketcher::GeoElementId(10, Sketcher::PointPos::mid));  // same GeoId as 0

    // Act
    constraint.substituteIndex(10, 99);

    // Assert
    EXPECT_EQ(constraint.getElement(0), Sketcher::GeoElementId(99, Sketcher::PointPos::start));
    EXPECT_EQ(constraint.getElement(1), Sketcher::GeoElementId(20, Sketcher::PointPos::end));
    EXPECT_EQ(constraint.getElement(2), Sketcher::GeoElementId(99, Sketcher::PointPos::mid));
}

TEST_F(ConstraintPointsAccess, testSubstituteIndexAndPos)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(10, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(20, Sketcher::PointPos::start));
    constraint.setElement(2, Sketcher::GeoElementId(10, Sketcher::PointPos::mid));

    // Act
    constraint.substituteIndexAndPos(10, Sketcher::PointPos::start, 42, Sketcher::PointPos::end);

    // Assert
    EXPECT_EQ(constraint.getElement(0), Sketcher::GeoElementId(42, Sketcher::PointPos::end));
    EXPECT_EQ(constraint.getElement(1), Sketcher::GeoElementId(20, Sketcher::PointPos::start));
    EXPECT_EQ(constraint.getElement(2), Sketcher::GeoElementId(10, Sketcher::PointPos::mid));  // unchanged
}

TEST_F(ConstraintPointsAccess, testInvolvesGeoId)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(10, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(20, Sketcher::PointPos::end));

    // Act & Assert
    EXPECT_TRUE(constraint.involvesGeoId(10));
    EXPECT_TRUE(constraint.involvesGeoId(20));
    EXPECT_FALSE(constraint.involvesGeoId(99));
}

TEST_F(ConstraintPointsAccess, testInvolvesGeoIdAndPosId)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(10, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(20, Sketcher::PointPos::mid));
    constraint.setElement(2, Sketcher::GeoElementId(30, Sketcher::PointPos::end));

    // Act & Assert
    EXPECT_TRUE(constraint.involvesGeoIdAndPosId(10, Sketcher::PointPos::start));
    EXPECT_TRUE(constraint.involvesGeoIdAndPosId(20, Sketcher::PointPos::mid));
    EXPECT_FALSE(constraint.involvesGeoIdAndPosId(20, Sketcher::PointPos::start));
    EXPECT_FALSE(constraint.involvesGeoIdAndPosId(99, Sketcher::PointPos::end));
}

#if SKETCHER_CONSTRAINT_USE_LEGACY_ELEMENTS
TEST_F(ConstraintPointsAccess, testLegacyWriteReflectedInInvolvesAndSubstitute)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.First = 10;
    constraint.FirstPos = Sketcher::PointPos::start;
    constraint.Second = 20;
    constraint.SecondPos = Sketcher::PointPos::end;

    // Act & Assert
    EXPECT_TRUE(constraint.involvesGeoId(10));
    EXPECT_TRUE(constraint.involvesGeoIdAndPosId(20, Sketcher::PointPos::end));

    // Substitute the legacy-indexed element
    constraint.substituteIndex(10, 99);

    // Should now reflect the substituted value
    EXPECT_TRUE(constraint.involvesGeoId(99));
    EXPECT_FALSE(constraint.involvesGeoId(10));
}

TEST_F(ConstraintPointsAccess, testSubstituteUpdatesLegacyFieldsToo)  // NOLINT
{
    // Arrange
    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(10, Sketcher::PointPos::start));

    // Act
    constraint.substituteIndex(10, 42);

    // Assert
    EXPECT_EQ(constraint.getElement(0), Sketcher::GeoElementId(42, Sketcher::PointPos::start));
    EXPECT_EQ(constraint.First, 42);
    EXPECT_EQ(constraint.FirstPos, Sketcher::PointPos::start);
}
#endif

namespace
{
/// Save a constraint (recording durable tags) and restore it through a real reader into
/// `restored` — the serialization boundary the tag has to cross. (Constraint is not
/// copyable, so the caller owns the destination.)
void saveRestoreWithTags(
    const Sketcher::Constraint& constraint,
    const Sketcher::Constraint::GeoIdToTagFn& geoIdToTag,
    Sketcher::Constraint& restored
)
{
    Base::StringWriter writer;
    writer.Stream() << "<root>\n";  // wrap so Constraint::Save has a parent element
    constraint.Save(writer, geoIdToTag);
    writer.Stream() << "</root>";

    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    tempFile.open();
    tempFile.write(writer.getString().c_str(), writer.getString().size());
    tempFile.flush();

    std::ifstream inputFile(tempFile.fileName().toStdString());
    Base::XMLReader reader(tempFile.fileName().toStdString().c_str(), inputFile);
    restored.Restore(reader);
}
}  // namespace

// Trait 1, brick two: a constraint reference resolves through the referenced geometry's
// durable tag, not its positional GeoId. When the geometry list reorders (a merge, an
// upstream edit) the stored GeoId goes stale but the tag does not — the reference must
// follow the tag. Three stages, each the discriminator for the next:
//   (1) Save records the tag alongside the GeoId, so a durable handle exists on disk.
//   (2) A plain restore keeps the saved GeoId, so the rebind in (3) is doing real work.
//   (3) Rebinding against a list where the tag now sits at a DIFFERENT GeoId rewrites the
//       element to that GeoId — the tag wins over the stale positional index.
TEST_F(ConstraintPointsAccess, testConstraintReferenceFollowsDurableGeometryTag)  // NOLINT
{
    boost::uuids::string_generator toUuid;
    const boost::uuids::uuid tagAtGeo0 = toUuid("11111111-1111-1111-1111-111111111111");
    const boost::uuids::uuid tagAtGeo1 = toUuid("22222222-2222-2222-2222-222222222222");

    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(1, Sketcher::PointPos::start));

    const Sketcher::Constraint::GeoIdToTagFn geoIdToTag = [&](int geoId) -> boost::uuids::uuid {
        switch (geoId) {
            case 0:
                return tagAtGeo0;
            case 1:
                return tagAtGeo1;
            default:
                return boost::uuids::nil_uuid();
        }
    };

    // Stage 1: the durable tag is written into the serialized form.
    Base::StringWriter probe;
    probe.Stream() << "<root>\n";
    constraint.Save(probe, geoIdToTag);
    probe.Stream() << "</root>";
    EXPECT_NE(probe.getString().find(boost::uuids::to_string(tagAtGeo1)), std::string::npos)
        << "Save did not record the referenced geometry's durable tag";

    Sketcher::Constraint restored;
    saveRestoreWithTags(constraint, geoIdToTag, restored);

    // Stage 2: the file states the durable identity and no index at all, so until the rebind
    // there is no GeoId to hold. That is the point of the form -- there is no stale index left
    // lying around for anything to read by mistake.
    EXPECT_EQ(restored.getElement(0).GeoId, Sketcher::GeoEnum::GeoUndef)
        << "the file stated an index the reference was supposed to resolve without";

    // Stage 3: the geometry carrying tagAtGeo1 now lives at GeoId 5 (list reordered).
    const Sketcher::Constraint::TagToGeoIdFn tagToGeoId =
        [&](const boost::uuids::uuid& tag) -> std::optional<int> {
        if (tag == tagAtGeo1) {
            return 5;
        }
        if (tag == tagAtGeo0) {
            return 9;
        }
        return std::nullopt;
    };
    restored.bindElementsToDurableGeometry(tagToGeoId);

    EXPECT_EQ(restored.getElement(0).GeoId, 5)
        << "reference did not follow the durable tag to the geometry's new GeoId";
    EXPECT_EQ(restored.getElement(0).Pos, Sketcher::PointPos::start)
        << "rebinding must preserve the element's PointPos";
}

// The other half of the rule: an element with no durable handle (a sketch axis, GeoId -1)
// keeps its GeoId, while a durable handle whose geometry is gone is marked GeoUndef so the
// loss fails loud (§10.1) instead of being silently patched. A blind implementation that
// reset every unresolved element would corrupt the axis; one that kept every loaded GeoId
// would leave the dangling reference aliasing whatever now sits at that index.
TEST_F(ConstraintPointsAccess, testConstraintReferenceWithLostTagIsMarkedDangling)  // NOLINT
{
    boost::uuids::string_generator toUuid;
    const boost::uuids::uuid tagAtGeo3 = toUuid("33333333-3333-3333-3333-333333333333");

    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(-1, Sketcher::PointPos::start));  // H axis
    constraint.setElement(1, Sketcher::GeoElementId(3, Sketcher::PointPos::end));     // real geo

    const Sketcher::Constraint::GeoIdToTagFn geoIdToTag = [&](int geoId) -> boost::uuids::uuid {
        return geoId == 3 ? tagAtGeo3 : boost::uuids::nil_uuid();
    };

    Sketcher::Constraint restored;
    saveRestoreWithTags(constraint, geoIdToTag, restored);

    // The referenced geometry is gone: its tag resolves to nothing.
    const Sketcher::Constraint::TagToGeoIdFn tagToGeoId =
        [](const boost::uuids::uuid&) -> std::optional<int> {
        return std::nullopt;
    };
    const bool dangling = restored.bindElementsToDurableGeometry(tagToGeoId);

    EXPECT_TRUE(dangling) << "a lost durable reference must report itself for disclosure";
    EXPECT_EQ(restored.getElement(0).GeoId, -1)
        << "an axis reference has no durable tag and must keep its GeoId";
    EXPECT_EQ(restored.getElement(1).GeoId, Sketcher::GeoEnum::GeoUndef)
        << "a lost durable reference must be marked GeoUndef, not left on a stale index";
}

// The aliasing case the rule exists for: the referenced geometry is gone, but the stale
// loaded GeoId is still IN RANGE and now belongs to a different, surviving element. Keeping
// the GeoId here would silently re-point the constraint at the wrong geometry — the exact
// failure §10.1 forbids. The discriminator is that a *live* GeoId is offered for a
// different tag, so "no resolution" cannot be mistaken for "empty document".
TEST_F(ConstraintPointsAccess, testLostReferenceDoesNotAliasSurvivingElement)  // NOLINT
{
    boost::uuids::string_generator toUuid;
    const boost::uuids::uuid tagGone = toUuid("44444444-4444-4444-4444-444444444444");
    const boost::uuids::uuid tagAlive = toUuid("55555555-5555-5555-5555-555555555555");

    Sketcher::Constraint constraint;
    constraint.setElement(0, Sketcher::GeoElementId(2, Sketcher::PointPos::start));  // -> tagGone

    const Sketcher::Constraint::GeoIdToTagFn geoIdToTag = [&](int geoId) -> boost::uuids::uuid {
        return geoId == 2 ? tagGone : boost::uuids::nil_uuid();
    };

    Sketcher::Constraint restored;
    saveRestoreWithTags(constraint, geoIdToTag, restored);
    ASSERT_EQ(restored.getElement(0).GeoId, Sketcher::GeoEnum::GeoUndef)
        << "the file stated an index the reference was supposed to resolve without";

    // tagGone is absent; a different, surviving element now occupies GeoId 2 under tagAlive.
    const Sketcher::Constraint::TagToGeoIdFn tagToGeoId =
        [&](const boost::uuids::uuid& tag) -> std::optional<int> {
        return tag == tagAlive ? std::optional<int>(2) : std::nullopt;
    };
    restored.bindElementsToDurableGeometry(tagToGeoId);

    EXPECT_EQ(restored.getElement(0).GeoId, Sketcher::GeoEnum::GeoUndef)
        << "the lost reference must not alias the surviving element now at GeoId 2";
}
