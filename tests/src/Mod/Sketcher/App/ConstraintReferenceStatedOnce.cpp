// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Constraint.h>
#include <Mod/Sketcher/App/GeoEnum.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef FC_OS_WIN32
# include <process.h>
#else
# include <unistd.h>
#endif

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
    std::string text = "<?xml version='1.0' encoding='utf-8'?>\n<root>\n";
    text.append(words);
    text.append("</root>\n");
    std::stringstream data(text);
    Base::XMLReader reader("Document.xml", data);
    constraint.Restore(reader);
}

/// How many elements a stated constraint actually states.
size_t statedElements(const std::string& words)
{
    size_t count = 0;
    for (std::string::size_type at = words.find("<Element "); at != std::string::npos;
         at = words.find("<Element ", at + 1)) {
        ++count;
    }
    return count;
}

std::string readAll(const std::string& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

/** A constraint states each reference it holds once, and states it durably.
 *
 *  Cruth: every reference used to be said three times over -- the deprecated First/FirstPos pair,
 *  the parallel ElementIds/ElementPositions lists, and the durable ElementTags -- and the
 *  authoritative one was the one that did not win first: the positional GeoIds were loaded on
 *  restore and then overwritten from the tags. Three statements of one fact can disagree, and a
 *  merge is exactly where they do. Measured before this: a file whose positional statement said
 *  geometry 1 while its tag still named geometry 0 opened as geometry 0, said nothing about it,
 *  and reported the document whole -- two of the three statements in the file were editable and
 *  inert.
 *
 *  A sketch is also the part of a document a merge has the most to do with, and most of a
 *  constraint's line was sentinels standing in for elements it does not hold.
 */
class ConstraintReferenceStatedOnceTest: public ::testing::Test
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
        App::GetApplication().closeAllDocuments();
        std::error_code ec;
        std::filesystem::remove_all(_scratch, ec);
        _scratch.clear();
    }

    /// A path under a directory belonging to this case alone -- ctest may run many at once.
    std::string tempPath(const char* leaf)
    {
        if (_scratch.empty()) {
            const ::testing::TestInfo* running
                = ::testing::UnitTest::GetInstance()->current_test_info();
            std::string leafDir = running != nullptr ? std::string(running->name()) : "case";
#ifdef FC_OS_WIN32
            leafDir += '.' + std::to_string(static_cast<long long>(_getpid()));
#else
            leafDir += '.' + std::to_string(static_cast<long long>(getpid()));
#endif
            _scratch = std::filesystem::temp_directory_path() / ("ConstraintRef." + leafDir);
            std::error_code ec;
            std::filesystem::remove_all(_scratch, ec);
            std::filesystem::create_directories(_scratch, ec);
        }
        return (_scratch / leaf).string();
    }

    /// Two joined lines, held together by a coincidence and pinned horizontal.
    Sketcher::SketchObject* buildSketch(App::Document* doc)
    {
        auto* sketch = static_cast<Sketcher::SketchObject*>(
            doc->addObject("Sketcher::SketchObject", "S")
        );

        Part::GeomLineSegment first;
        first.setPoints(Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 0));
        sketch->addGeometry(&first);

        Part::GeomLineSegment second;
        second.setPoints(Base::Vector3d(10, 0, 0), Base::Vector3d(10, 10, 0));
        sketch->addGeometry(&second);

        Sketcher::Constraint joined;
        joined.Type = Sketcher::Coincident;
        joined.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::end));
        joined.setElement(1, Sketcher::GeoElementId(1, Sketcher::PointPos::start));
        sketch->addConstraint(&joined);

        Sketcher::Constraint flat;
        flat.Type = Sketcher::Horizontal;
        flat.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::none));
        sketch->addConstraint(&flat);

        doc->recompute();
        return sketch;
    }

    std::filesystem::path _scratch;
};

// The reference in the file is the geometry's durable identity, and the index it currently
// occupies is not in the file at all. There is nothing left for a second statement to disagree
// with.
TEST_F(ConstraintReferenceStatedOnceTest, aReferenceIsStatedOnceAndByDurableIdentity)
{
    App::Document* doc = App::GetApplication().newDocument("stated");
    Sketcher::SketchObject* sketch = buildSketch(doc);
    const std::string path = tempPath("stated.cpart");
    ASSERT_TRUE(doc->saveAs(path.c_str()));

    const std::string words = readAll(path);
    const std::string firstLineTag = boost::uuids::to_string(
        sketch->getInternalGeometry().front()->getTag()
    );

    EXPECT_NE(words.find("<Element tag=\"" + firstLineTag + "\""), std::string::npos)
        << "the constraint does not name the geometry it holds by its durable identity";
    EXPECT_EQ(words.find("First="), std::string::npos)
        << "the reference is still stated a second time, positionally";
    EXPECT_EQ(words.find("ElementIds="), std::string::npos)
        << "the reference is still stated a third time, as parallel lists";
    EXPECT_EQ(words.find("ElementTags="), std::string::npos)
        << "the durable identity is still stated as a parallel list as well";
}

// A Horizontal holds one line. The other two slots are the shape the constraint is held in, not
// something the sketch says, and padding them into the file put two sentinels in front of every
// person diffing two sketches.
TEST_F(ConstraintReferenceStatedOnceTest, anElementHoldingNothingIsNotStated)
{
    Sketcher::Constraint flat;
    flat.Type = Sketcher::Horizontal;
    flat.setElement(0, Sketcher::GeoElementId(4, Sketcher::PointPos::none));

    const std::string words = statedForm(flat);
    EXPECT_NE(words.find("<Element geoId=\"4\" at=\"none\"/>"), std::string::npos) << words;

    // Counted, not searched for. Looking for the sentinel's number is blind: an element holding
    // nothing has no durable identity and no index to state either, so a padded one writes as an
    // empty <Element at="none"/> and the number never appears. Measured -- with the padding put
    // back this check still passed.
    EXPECT_EQ(statedElements(words), 1U)
        << "an element holding nothing was padded into the file: " << words;
}

// The other half of the rule: a reference the sketch does not author -- an axis, external
// geometry -- has no durable identity to state, so the index is all there is and it is stated.
TEST_F(ConstraintReferenceStatedOnceTest, aReferenceWithNoDurableIdentityStatesItsIndex)
{
    Sketcher::Constraint toAxis;
    toAxis.Type = Sketcher::PointOnObject;
    toAxis.setElement(0, Sketcher::GeoElementId(0, Sketcher::PointPos::start));
    toAxis.setElement(1, Sketcher::GeoElementId(-1, Sketcher::PointPos::none));

    // No geometry context in this overload, so nothing has a durable identity here.
    const std::string words = statedForm(toAxis);
    EXPECT_NE(words.find("<Element geoId=\"-1\" at=\"none\"/>"), std::string::npos) << words;
}

// The round trip the whole form exists for: what the file states is what comes back, resolved
// through the durable identity rather than through a position that any edit can shift.
TEST_F(ConstraintReferenceStatedOnceTest, whatTheFileStatesIsWhatComesBack)
{
    App::Document* doc = App::GetApplication().newDocument("roundtrip");
    buildSketch(doc);
    const std::string path = tempPath("roundtrip.cpart");
    ASSERT_TRUE(doc->saveAs(path.c_str()));
    App::GetApplication().closeDocument(doc->getName());

    App::Document* reopened = App::GetApplication().openDocument(path.c_str());
    ASSERT_NE(reopened, nullptr);
    auto* sketch = static_cast<Sketcher::SketchObject*>(reopened->getObject("S"));
    ASSERT_NE(sketch, nullptr);

    const std::vector<Sketcher::Constraint*>& constraints = sketch->Constraints.getValues();
    ASSERT_EQ(constraints.size(), 2U);

    EXPECT_EQ(constraints[0]->getElement(0), Sketcher::GeoElementId(0, Sketcher::PointPos::end));
    EXPECT_EQ(constraints[0]->getElement(1), Sketcher::GeoElementId(1, Sketcher::PointPos::start));
    EXPECT_EQ(constraints[1]->getElement(0), Sketcher::GeoElementId(0, Sketcher::PointPos::none));
    EXPECT_EQ(sketch->solve(), 0) << "the reopened sketch does not solve";
}

// A durable identity this build cannot read is refused, not quietly turned into a reference to
// nothing. Dropping it is how an author's constraint leaves their own file: the next save writes
// the absence over the record (Amendment 19).
TEST_F(ConstraintReferenceStatedOnceTest, aDurableIdentityThatCannotBeReadIsRefused)
{
    Sketcher::Constraint read;
    EXPECT_THROW(
        restoreFrom(
            read,
            "<Constrain Name=\"\" Type=\"Coincident\" Value=\"0\">\n"
            "    <Element tag=\"banana\" at=\"start\"/>\n"
            "</Constrain>\n"
        ),
        Base::ValueError
    );
}

// A point this build has no name for is refused for the same reason.
TEST_F(ConstraintReferenceStatedOnceTest, aPointThisBuildHasNoNameForIsRefused)
{
    Sketcher::Constraint read;
    EXPECT_THROW(
        restoreFrom(
            read,
            "<Constrain Name=\"\" Type=\"Coincident\" Value=\"0\">\n"
            "    <Element geoId=\"0\" at=\"corner\"/>\n"
            "</Constrain>\n"
        ),
        Base::ValueError
    );
}
