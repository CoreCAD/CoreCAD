// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors
// Cruth

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Writer.h>
#include <Mod/Part/App/FeatureChamfer.h>
#include <Mod/Part/App/FeatureFillet.h>
#include <Mod/Part/App/PropertyTopoShape.h>

#include <memory>
#include <string>
#include <vector>

#include <src/App/InitApplication.h>

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
}  // namespace

/** A chamfer is not a fillet, and its document should not say that it is.
 *
 *  Cruth: a chamfer takes a distance along each of the two faces an edge joins. Measured before
 *  this: a chamfer's document stated `<Fillet edge="1" radius1="2" radius2="3"/>` under a property
 *  typed `Part::PropertyFilletEdges` -- the operation named wrong, the numbers named wrong, and
 *  the property named wrong. Every reader of that file, person or merge, was told something
 *  untrue about a design decision somebody made deliberately.
 */
class ChamferStatesItsOwnWordsTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
};

// The words a chamfer's file carries are a chamfer's.
TEST_F(ChamferStatesItsOwnWordsTest, aChamferStatesDistancesRatherThanRadii)
{
    Part::PropertyChamferEdges measured;
    measured.setValues({Part::FilletElement(1, 2.0, 3.0)});

    const std::string words = statedForm(measured);
    EXPECT_NE(words.find("<ChamferEdges>"), std::string::npos) << words;
    EXPECT_NE(words.find("<Chamfer edge=\"1\" size=\"2\" size2=\"3\"/>"), std::string::npos) << words;
    EXPECT_EQ(words.find("Fillet"), std::string::npos)
        << "the chamfer still calls itself a fillet: " << words;
    EXPECT_EQ(words.find("radius"), std::string::npos)
        << "the chamfer still calls its distances radii: " << words;
}

// The narrowing: a fillet is unchanged. Its numbers really are radii and it still says so.
TEST_F(ChamferStatesItsOwnWordsTest, aFilletStillStatesRadii)
{
    Part::PropertyFilletEdges measured;
    measured.setValues({Part::FilletElement(1, 1.5, 2.5)});

    const std::string words = statedForm(measured);
    EXPECT_NE(words.find("<FilletEdges>"), std::string::npos) << words;
    EXPECT_NE(words.find("<Fillet edge=\"1\" radius1=\"1.5\" radius2=\"2.5\"/>"), std::string::npos)
        << words;
}

// Each operation holds measurements of its own kind, so each writes its own words.
TEST_F(ChamferStatesItsOwnWordsTest, eachOperationHoldsMeasurementsOfItsOwnKind)
{
    auto& app = App::GetApplication();
    App::Document* doc = app.newDocument(app.getUniqueDocumentName("edges").c_str(), "testUser");
    auto* chamfer = doc->addObject<Part::Chamfer>();
    auto* fillet = doc->addObject<Part::Fillet>();
    ASSERT_NE(chamfer, nullptr);
    ASSERT_NE(fillet, nullptr);

    EXPECT_EQ(chamfer->Edges.getTypeId(), Part::PropertyChamferEdges::getClassTypeId());
    EXPECT_EQ(fillet->Edges.getTypeId(), Part::PropertyFilletEdges::getClassTypeId());

    // The shared base reaches them without knowing which it has.
    EXPECT_EQ(&chamfer->edgeMeasurements(), &chamfer->Edges);
    EXPECT_EQ(&fillet->edgeMeasurements(), &fillet->Edges);

    app.closeDocument(doc->getName());
}

// A copy has to come back as the same kind of measurement, or the copy states itself in the
// other operation's words the first time it is written.
TEST_F(ChamferStatesItsOwnWordsTest, aCopiedChamferIsStillAChamfer)
{
    Part::PropertyChamferEdges measured;
    measured.setValues({Part::FilletElement(4, 5.0, 6.0)});

    const std::unique_ptr<App::Property> copied(measured.Copy());
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied->getTypeId(), Part::PropertyChamferEdges::getClassTypeId());
    EXPECT_NE(statedForm(*copied).find("<Chamfer edge=\"4\" size=\"5\" size2=\"6\"/>"), std::string::npos)
        << statedForm(*copied);
}
