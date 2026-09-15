// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/GeoEnum.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
/// What one constraint says about itself, in the words the file would carry.
std::string statedForm(const Sketcher::Constraint& constraint)
{
    Base::StringWriter writer;
    constraint.Save(writer);
    return writer.getString();
}

/// Read a constraint back from words a file could have stated.
void restoreFrom(Sketcher::Constraint& constraint, const std::string& words)
{
    // Wrapped in a root element, the way the document these words come from wraps them.
    std::string text = "<?xml version='1.0' encoding='utf-8'?>\n<root>\n";
    text.append(words);
    text.append("</root>\n");
    std::stringstream data(text);
    Base::XMLReader reader("Document.xml", data);
    constraint.Restore(reader);
}
}  // namespace

/** A constraint says what it is, and a build that cannot place it says so.
 *
 *  Cruth: a sketch is the part of a document a merge has the most to do with, and a constraint
 *  stored as `Type="10"` tells a person reading the file nothing at all. Worse, the number is a
 *  position in a C++ enumeration, so the meaning of every file ever written depends on a list
 *  that is free to grow. The names already exist in the class; only the stored form used numbers.
 */
class ConstraintTypesByNameTest: public ::testing::Test
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
};

// The type, the point, and the orientation, all readable in the file.
TEST_F(ConstraintTypesByNameTest, aConstraintStatesWhatItIsByName)
{
    Sketcher::Constraint constraint;
    constraint.Type = Sketcher::Perpendicular;
    constraint.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::end));

    const std::string words = statedForm(constraint);
    EXPECT_NE(words.find("Type=\"Perpendicular\""), std::string::npos)
        << "the constraint does not say what it is: " << words;
    EXPECT_NE(words.find("FirstPos=\"end\""), std::string::npos)
        << "the point it holds is still a number: " << words;
    EXPECT_NE(words.find("Orientation=\"None\""), std::string::npos)
        << "the orientation is still a number: " << words;
}

// An internal alignment names its kind too -- the one constraint type that carries a second.
TEST_F(ConstraintTypesByNameTest, anInternalAlignmentNamesItsKind)
{
    Sketcher::Constraint constraint;
    constraint.Type = Sketcher::InternalAlignment;
    constraint.AlignmentType = Sketcher::EllipseFocus2;

    const std::string words = statedForm(constraint);
    EXPECT_NE(words.find("InternalAlignmentType=\"EllipseFocus2\""), std::string::npos) << words;

    Sketcher::Constraint read;
    restoreFrom(read, words);
    EXPECT_EQ(read.Type, Sketcher::InternalAlignment);
    EXPECT_EQ(read.AlignmentType, Sketcher::EllipseFocus2);
}

// The round trip a merge depends on: what went in is what comes back.
TEST_F(ConstraintTypesByNameTest, whatWasStatedByNameIsWhatComesBack)
{
    Sketcher::Constraint constraint;
    constraint.Type = Sketcher::DistanceX;
    constraint.setElement(0, Sketcher::GeoElementId(3, Sketcher::PointPos::start));
    constraint.setElement(1, Sketcher::GeoElementId(7, Sketcher::PointPos::mid));

    // The words in between are checked as well: a round trip on its own passes whether the file
    // says "DistanceX" or "7", so a test that only reads it back cannot tell the two apart.
    const std::string words = statedForm(constraint);
    EXPECT_NE(words.find("Type=\"DistanceX\""), std::string::npos) << words;
    EXPECT_NE(words.find("ElementPositions=\"start mid"), std::string::npos) << words;

    Sketcher::Constraint read;
    restoreFrom(read, words);

    EXPECT_EQ(read.Type, Sketcher::DistanceX);
    EXPECT_EQ(read.getElement(0).GeoId, 3);
    EXPECT_EQ(read.getElement(0).Pos, Sketcher::PointPos::start);
    EXPECT_EQ(read.getElement(1).Pos, Sketcher::PointPos::mid);
}

// A document written before the form changed states positions. Reading it positionally is the
// only way a position can be read, and it is named on the way back out.
TEST_F(ConstraintTypesByNameTest, aPositionStatedByAnOlderFileIsStillRead)
{
    Sketcher::Constraint read;
    restoreFrom(
        read,
        "<Constrain Name=\"\" Type=\"10\" Value=\"0\" First=\"4\" FirstPos=\"2\" "
        "Second=\"-2000\" SecondPos=\"0\" Third=\"-2000\" ThirdPos=\"0\"/>\n"
    );

    EXPECT_EQ(read.Type, Sketcher::Perpendicular) << "an upstream document no longer opens";
    EXPECT_EQ(read.getElement(0).Pos, Sketcher::PointPos::end);
    EXPECT_NE(statedForm(read).find("Type=\"Perpendicular\""), std::string::npos)
        << "what was read positionally was not named on the way back out";
}

// A type nothing here can place is refused rather than dropped. Dropping it is how an author's
// constraint leaves their own file: the next save writes the absence over the record.
TEST_F(ConstraintTypesByNameTest, aTypeThisBuildCannotPlaceIsRefusedRatherThanDropped)
{
    Sketcher::Constraint read;
    EXPECT_THROW(
        restoreFrom(
            read,
            "<Constrain Name=\"\" Type=\"Skewed\" Value=\"0\" First=\"0\" "
            "FirstPos=\"none\"/>\n"
        ),
        Base::ValueError
    );
}

// The same, stated the way an older document would stateit: a position past the end of the list.
TEST_F(ConstraintTypesByNameTest, aPositionPastTheEndOfTheListIsRefusedToo)
{
    Sketcher::Constraint read;
    EXPECT_THROW(
        restoreFrom(
            read,
            "<Constrain Name=\"\" Type=\"99\" Value=\"0\" First=\"0\" "
            "FirstPos=\"none\"/>\n"
        ),
        Base::ValueError
    );
}

/** A sketch whose constraints this build cannot read keeps them, and says it is not whole.
 *
 *  This is the case the change exists for. Measured before it: a four-constraint sketch whose
 *  Perpendicular named a type this build had no place for opened with three constraints, with no
 *  message, reporting IsWhole True -- and the next save wrote the file without it. The author's
 *  constraint left their own document because they opened it and pressed save.
 */
class ConstraintsKeptWhenUnreadableTest: public ::testing::Test
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
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    App::Document* _doc {};
};

TEST_F(ConstraintsKeptWhenUnreadableTest, aTypeThisBuildCannotPlaceIsKeptAndSurvivesTheSave)
{
    auto& app = App::GetApplication();
    const std::string path = Base::FileInfo::getTempFileName() + ".cpart";

    App::Document* doc = app.newDocument(app.getUniqueDocumentName("sk").c_str(), "testUser");
    auto* sketch = static_cast<Sketcher::SketchObject*>(doc->addObject("Sketcher::SketchObject", "Sk"));
    auto* along = new Part::GeomLineSegment();
    along->setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
    auto* up = new Part::GeomLineSegment();
    up->setPoints(Base::Vector3d(10, 0, 0), Base::Vector3d(10, 8, 0));
    sketch->addGeometry(along, false);
    sketch->addGeometry(up, false);

    auto* perpendicular = new Sketcher::Constraint();
    perpendicular->Type = Sketcher::Perpendicular;
    perpendicular->setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::none));
    perpendicular->setElement(1, Sketcher::GeoElementId(1, Sketcher::PointPos::none));
    sketch->addConstraint(perpendicular);
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    app.closeDocument(doc->getName());

    // A type a later build has and this one does not -- which is what "upward compatibility"
    // was silently deleting.
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const std::string::size_type at = text.find("Type=\"Perpendicular\"");
    ASSERT_NE(at, std::string::npos) << "the file does not state the constraint type by name";
    text.replace(at, std::string("Type=\"Perpendicular\"").size(), "Type=\"Skewed\"");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();

    _doc = app.openDocument(path.c_str());
    ASSERT_NE(_doc, nullptr) << "a constraint this build cannot place stopped the document opening";
    EXPECT_FALSE(
        _doc->isWhole()
    ) << "the document reported itself whole while holding a constraint it could not read";
    EXPECT_FALSE(_doc->heldStatements().empty()) << "nothing was kept for the constraint list";

    // The half that a migration pass used to undo: the note has to survive the load finishing
    // itself, or the save writes the absence over the file's own words.
    const std::string resaved = Base::FileInfo::getTempFileName() + ".cpart";
    ASSERT_TRUE(_doc->saveAs(resaved.c_str()));
    std::ifstream back(resaved, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(back)), std::istreambuf_iterator<char>());
    EXPECT_NE(written.find("Type=\"Skewed\""), std::string::npos)
        << "the save wrote this build's reading over what the file stated";
}
