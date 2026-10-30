// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Interpreter.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Mod/Part/App/FeatureChamfer.h>
#include <Mod/Part/App/PropertyTopoShape.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

namespace
{
/// What one set of measurements says about itself, in the words the file would carry.
std::string statedForm(const App::Property& prop)
{
    Base::StringWriter writer;
    writer.setForceXML(true);
    prop.Save(writer);
    return writer.getString();
}

/// Read the measurements back from words a file could have stated.
void restoreFrom(App::Property& prop, const std::string& words)
{
    std::string text = "<?xml version='1.0' encoding='utf-8'?>\n";
    text.append("<Property name='Edges' type='Part::PropertyChamferEdges'>\n");
    text.append(words);
    text.append("</Property>\n");
    std::stringstream data(text);
    Base::XMLReader reader("Document.xml", data);
    // Positioned the way the document reader leaves it: on the property's own element, with what
    // it states still to be read.
    reader.readElement("Property");
    prop.Restore(reader);
}
}  // namespace

/** A chamfer states which of the three kinds it takes, and the Part operation can take all three.
 *
 *  Cruth: `Part::Chamfer` only ever asked the kernel for two distances, while the PartDesign
 *  feature beside it has offered equal distance, two distances, and a distance and an angle all
 *  along -- so the concept exists in the product and one operation simply could not express it.
 *  The kind travels with the measurements of the edge it governs, because the two numbers cannot
 *  say which they are: an angle written into a field a reader takes for a distance is a file
 *  saying something untrue about the operation a person performed.
 */
class ChamferByDistanceAndAngleTest: public ::testing::Test,
                                     public PartTestHelpers::PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
        _boxes[0]->Length.setValue(length);
        _boxes[0]->Width.setValue(width);
        _boxes[0]->Height.setValue(height);
        _boxes[0]->Placement.setValue(
            Base::Placement(Base::Vector3d(), Base::Rotation(), Base::Vector3d())
        );
        _doc->recompute();
    }

    /// The volume one chamfered edge leaves behind, measured on the built shape.
    double volumeChamfering(const Part::FilletElement& measured)
    {
        auto* chamfer = _doc->addObject<Part::Chamfer>();
        chamfer->Base.setValue(_boxes[0]);
        chamfer->Edges.setValues({measured});
        chamfer->execute();
        return PartTestHelpers::getVolume(chamfer->Shape.getValue());
    }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    const double length = 4.0;
    const double width = 5.0;
    const double height = 6.0;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

// Each kind states itself, and then only the numbers that kind measures.
TEST_F(ChamferByDistanceAndAngleTest, aChamferStatesTheKindAndWhatThatKindMeasures)
{
    Part::PropertyChamferEdges measured;

    measured.setValues({Part::FilletElement(1, 2.0, 0.0, Part::ChamferType::equalDistance)});
    std::string words = statedForm(measured);
    EXPECT_NE(words.find("<Chamfer edge=\"1\" kind=\"Equal distance\" size=\"2\"/>"), std::string::npos)
        << words;
    EXPECT_EQ(words.find("size2"), std::string::npos)
        << "one distance stated as two numbers, one of which means nothing: " << words;
    EXPECT_EQ(words.find("angle"), std::string::npos) << words;

    measured.setValues({Part::FilletElement(1, 2.0, 3.0, Part::ChamferType::twoDistances)});
    words = statedForm(measured);
    EXPECT_NE(
        words.find("<Chamfer edge=\"1\" kind=\"Two distances\" size=\"2\" size2=\"3\"/>"),
        std::string::npos
    ) << words;
    EXPECT_EQ(words.find("angle"), std::string::npos) << words;

    measured.setValues({Part::FilletElement(1, 2.0, 0.0, Part::ChamferType::distanceAngle, 30.0)});
    words = statedForm(measured);
    EXPECT_NE(
        words.find("<Chamfer edge=\"1\" kind=\"Distance and Angle\" size=\"2\" angle=\"30\"/>"),
        std::string::npos
    ) << words;
    EXPECT_EQ(words.find("size2"), std::string::npos)
        << "an angle stated in a field a reader takes for a distance: " << words;
}

// What each kind stated comes back as the same kind, measuring the same things.
TEST_F(ChamferByDistanceAndAngleTest, whatWasStatedComesBackAsItWentIn)
{
    const std::vector<Part::FilletElement> authored = {
        Part::FilletElement(1, 2.0, 2.0, Part::ChamferType::equalDistance),
        Part::FilletElement(2, 2.0, 3.0, Part::ChamferType::twoDistances),
        Part::FilletElement(3, 2.0, 0.0, Part::ChamferType::distanceAngle, 30.0),
    };

    Part::PropertyChamferEdges stated;
    stated.setValues(authored);

    Part::PropertyChamferEdges read;
    restoreFrom(read, statedForm(stated));

    ASSERT_EQ(read.getValues().size(), authored.size());
    for (size_t i = 0; i < authored.size(); ++i) {
        const Part::FilletElement& back = read.getValues()[i];
        EXPECT_EQ(back.edgeid, authored[i].edgeid);
        EXPECT_EQ(back.kind, authored[i].kind);
        EXPECT_DOUBLE_EQ(back.radius1, authored[i].radius1);
        if (authored[i].kind == Part::ChamferType::distanceAngle) {
            EXPECT_DOUBLE_EQ(back.angle, authored[i].angle);
        }
        else {
            EXPECT_DOUBLE_EQ(back.radius2, authored[i].radius2);
        }
    }
}

// A chamfer written before the kind was stated gave two distances, and that is what its silence
// means. Read as anything else, a chamfer nobody touched would come back a different shape.
TEST_F(ChamferByDistanceAndAngleTest, aChamferThatStatesNoKindIsTwoDistances)
{
    Part::PropertyChamferEdges read;
    restoreFrom(
        read,
        "<ChamferEdges>\n  <Chamfer edge=\"1\" size=\"2\" size2=\"3\"/>\n</ChamferEdges>\n"
    );

    ASSERT_EQ(read.getValues().size(), 1U);
    EXPECT_EQ(read.getValues()[0].kind, Part::ChamferType::twoDistances);
    EXPECT_DOUBLE_EQ(read.getValues()[0].radius1, 2.0);
    EXPECT_DOUBLE_EQ(read.getValues()[0].radius2, 3.0);
}

// A kind this build does not take is unfamiliar, not malformed: the read fails so that the reader
// keeps the whole block as the file worded it, rather than guessing a kind and building a shape
// nobody asked for (Amendment 19 Clauses 19.1, 19.2).
TEST_F(ChamferByDistanceAndAngleTest, anUnfamiliarKindIsNotGuessedAt)
{
    Part::PropertyChamferEdges read;
    EXPECT_THROW(
        restoreFrom(
            read,
            "<ChamferEdges>\n  <Chamfer edge=\"1\" kind=\"Two angles\" size=\"2\"/>\n"
            "</ChamferEdges>\n"
        ),
        Base::Exception
    );
    EXPECT_TRUE(read.getValues().empty())
        << "a kind this build cannot honour was replaced by one it can";
}

// The kind reaches the kernel: an angle is used as an angle, and on the same face the two
// distances are taken along.
TEST_F(ChamferByDistanceAndAngleTest, anAngleIsBuiltAsAnAngle)
{
    const double size = 1.0;
    const double angle = 60.0;
    // What the angle means: the second distance is the first turned through it.
    const double second = size * std::tan(angle * M_PI / 180.0);

    const double byAngle = volumeChamfering({1, size, 0.0, Part::ChamferType::distanceAngle, angle});
    const double byTwoDistances = volumeChamfering({1, size, second, Part::ChamferType::twoDistances});
    const double bySizeTwice = volumeChamfering({1, size, size, Part::ChamferType::twoDistances});

    ASSERT_GT(byAngle, 0.0) << "the distance-and-angle chamfer did not build at all";
    EXPECT_NEAR(byAngle, byTwoDistances, 1e-6);
    // The control: had the angle been taken for a distance, or ignored for the second number
    // beside it, this is the volume that would have come back instead.
    EXPECT_GT(std::abs(byAngle - bySizeTwice), 1e-3)
        << "the angle changed nothing about the shape that was built";
}

// Equal distance takes the one number along both faces.
TEST_F(ChamferByDistanceAndAngleTest, equalDistanceTakesTheOneNumberTwice)
{
    const double size = 1.0;
    const double byOneNumber = volumeChamfering({1, size, 0.0, Part::ChamferType::equalDistance});
    const double bySizeTwice = volumeChamfering({1, size, size, Part::ChamferType::twoDistances});

    ASSERT_GT(byOneNumber, 0.0) << "the equal-distance chamfer did not build at all";
    EXPECT_NEAR(byOneNumber, bySizeTwice, 1e-6);
}

// An angle no chamfer can be built at is refused in words that name it, not left to come back as
// a kernel failure that names neither the edge nor the number.
TEST_F(ChamferByDistanceAndAngleTest, anAngleOutsideItsRangeIsRefusedByName)
{
    auto* chamfer = _doc->addObject<Part::Chamfer>();
    chamfer->Base.setValue(_boxes[0]);
    chamfer->Edges.setValues({{1, 1.0, 0.0, Part::ChamferType::distanceAngle, 180.0}});

    const std::unique_ptr<App::DocumentObjectExecReturn> refused(chamfer->execute());
    ASSERT_NE(refused, nullptr);
    ASSERT_NE(refused.get(), App::DocumentObject::StdReturn);
    EXPECT_NE(refused->Why.find("angle"), std::string::npos) << refused->Why;
    EXPECT_NE(refused->Why.find("180"), std::string::npos) << refused->Why;
}

// The Python form says what the file says, so a script and a document cannot mean two things.
TEST_F(ChamferByDistanceAndAngleTest, thePythonFormNamesTheKind)
{
    Py_Initialize();
    Base::PyGILStateLocker lock;

    Part::PropertyChamferEdges measured;

    Py::List authored;
    Py::Tuple byAngle(4);
    byAngle.setItem(0, Py::Long(1));
    byAngle.setItem(1, Py::String("Distance and Angle"));
    byAngle.setItem(2, Py::Float(2.0));
    byAngle.setItem(3, Py::Float(30.0));
    authored.append(byAngle);
    Py::Tuple byOneNumber(3);
    byOneNumber.setItem(0, Py::Long(2));
    byOneNumber.setItem(1, Py::String("Equal distance"));
    byOneNumber.setItem(2, Py::Float(1.5));
    authored.append(byOneNumber);
    Py::Tuple plainNumbers(3);
    plainNumbers.setItem(0, Py::Long(3));
    plainNumbers.setItem(1, Py::Float(4.0));
    plainNumbers.setItem(2, Py::Float(5.0));
    authored.append(plainNumbers);

    measured.setPyObject(authored.ptr());

    ASSERT_EQ(measured.getValues().size(), 3U);
    EXPECT_EQ(measured.getValues()[0].kind, Part::ChamferType::distanceAngle);
    EXPECT_DOUBLE_EQ(measured.getValues()[0].radius1, 2.0);
    EXPECT_DOUBLE_EQ(measured.getValues()[0].angle, 30.0);
    EXPECT_EQ(measured.getValues()[1].kind, Part::ChamferType::equalDistance);
    EXPECT_DOUBLE_EQ(measured.getValues()[1].radius1, 1.5);
    EXPECT_DOUBLE_EQ(measured.getValues()[1].radius2, 1.5);
    // Three plain numbers still mean two distances, which is what they have always built as.
    EXPECT_EQ(measured.getValues()[2].kind, Part::ChamferType::twoDistances);
    EXPECT_DOUBLE_EQ(measured.getValues()[2].radius1, 4.0);
    EXPECT_DOUBLE_EQ(measured.getValues()[2].radius2, 5.0);

    // And what it gives back names the kind too, stating only what that kind measures.
    const Py::List given(measured.getPyObject(), true);
    ASSERT_EQ(given.size(), 3U);
    const Py::Tuple first(given.getItem(0));
    EXPECT_EQ(first.size(), 4U);
    EXPECT_EQ((std::string)Py::String(first.getItem(1)), "Distance and Angle");
    EXPECT_DOUBLE_EQ((double)Py::Float(first.getItem(3)), 30.0);
    const Py::Tuple second(given.getItem(1));
    EXPECT_EQ(second.size(), 3U);
    EXPECT_EQ((std::string)Py::String(second.getItem(1)), "Equal distance");
}

// A kind nothing offers, and a number a kind has no use for, are both refused rather than taken
// in and quietly ignored.
TEST_F(ChamferByDistanceAndAngleTest, aPythonFormThatCannotBeMeantIsRefused)
{
    Py_Initialize();
    Base::PyGILStateLocker lock;

    Part::PropertyChamferEdges measured;

    Py::List unknownKind;
    Py::Tuple entry(3);
    entry.setItem(0, Py::Long(1));
    entry.setItem(1, Py::String("Two angles"));
    entry.setItem(2, Py::Float(2.0));
    unknownKind.append(entry);
    EXPECT_THROW(measured.setPyObject(unknownKind.ptr()), Py::Exception);
    PyErr_Clear();

    Py::List tooManyNumbers;
    Py::Tuple padded(4);
    padded.setItem(0, Py::Long(1));
    padded.setItem(1, Py::String("Equal distance"));
    padded.setItem(2, Py::Float(2.0));
    padded.setItem(3, Py::Float(3.0));
    tooManyNumbers.append(padded);
    EXPECT_THROW(measured.setPyObject(tooManyNumbers.ptr()), Py::Exception);
    PyErr_Clear();

    EXPECT_TRUE(measured.getValues().empty());
}
