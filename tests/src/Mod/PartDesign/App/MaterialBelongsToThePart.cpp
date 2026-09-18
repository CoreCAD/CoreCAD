// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/ModelUuids.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Part/App/MaterialExtension.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/Sketcher/App/SketchObject.h>

#ifndef FC_OS_WIN32
# include <unistd.h>
#endif

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/** #121: what a part is made of belongs to the part, and a feature that builds one is not a part.
 *
 *  Measured before: one part -- a sketch, a Body and a Pad -- and the document stated three
 *  materials, all the same Default, because "is made of" sat on the base class every shape-carrying
 *  object inherits. Each copy cost eleven lines of the file a person reads, and the count grew with
 *  the part: twenty features, twenty-one materials. Two blocks of code existed purely to keep the
 *  copies agreeing with each other.
 *
 *  A Body is what stands as a part, so a Body is what is made of something. A Pad builds the part;
 *  it is made of nothing, and asking what it is made of answers with the Body's material.
 */
class MaterialBelongsToThePartTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument(
            "MaterialBelongsToThePart_test",
            "testUser",
            {.documentType = "Part"}
        );
        _body = _doc->addObject<PartDesign::Body>();
        _sketch = _doc->addObject<Sketcher::SketchObject>("Sketch");
        _body->addFeature(_sketch);

        _sketch->AttachmentSupport.setValue(_doc->getObject("XY_Plane"), "");
        _sketch->MapMode.setValue("FlatFace");
        Part::GeomCircle circle;
        circle.setRadius(10.0);
        _sketch->addGeometry(&circle, false);
        _doc->recompute();

        _pad = _doc->addObject<PartDesign::Pad>("Pad");
        _body->addFeature(_pad);
        _pad->Profile.setValue(_sketch, {""});
        _pad->Length.setValue(10.0);
        _doc->recompute();
    }

    void TearDown() override
    {
        if (_doc != nullptr) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        if (!_scratch.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(_scratch, ec);
            _scratch.clear();
        }
    }

    /// A path under a directory belonging to this case alone -- ctest may run many at once.
    std::string tempPath(const char* leaf)
    {
        if (_scratch.empty()) {
            const ::testing::TestInfo* running
                = ::testing::UnitTest::GetInstance()->current_test_info();
            std::string leafDir = running != nullptr ? std::string(running->name()) : "case";
            leafDir += '.' + std::to_string(static_cast<long long>(getpid()));
            _scratch = std::filesystem::temp_directory_path() / ("MaterialBelongs." + leafDir);
            std::error_code ec;
            std::filesystem::remove_all(_scratch, ec);
            std::filesystem::create_directories(_scratch, ec);
        }
        return (_scratch / leaf).string();
    }

    static std::string readAll(const std::string& path)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static std::size_t countOf(const std::string& words, const std::string& needle)
    {
        std::size_t found = 0;
        for (std::size_t at = words.find(needle); at != std::string::npos;
             at = words.find(needle, at + needle.size())) {
            ++found;
        }
        return found;
    }

    /// A material that is not the default, so a copy of it is recognisable in the file, and
    /// authored here rather than looked up so the test does not depend on a library's contents.
    static Materials::Material shopSteel()
    {
        Materials::Material steel;
        steel.setUUID(QString::fromStdString(steelUUID));
        steel.setName(QStringLiteral("Shop Steel"));
        steel.addPhysical(Materials::ModelUUIDs::ModelUUID_Mechanical_Density);
        steel.setPhysicalValue(QStringLiteral("Density"), QStringLiteral("7900 kg/m^3"));
        return steel;
    }

    static constexpr const char* steelUUID = "deadbeef-0000-0000-0000-000000000000";

    App::Document* _doc = nullptr;
    PartDesign::Body* _body = nullptr;
    Sketcher::SketchObject* _sketch = nullptr;
    PartDesign::Pad* _pad = nullptr;
    std::filesystem::path _scratch;
};

// The capability says which objects can answer "what are you made of". A Body answers; the Pad
// that builds it is not a thing anyone melts down.
TEST_F(MaterialBelongsToThePartTest, onlyWhatStandsAsAPartIsMadeOfSomething)
{
    EXPECT_TRUE(Part::hasMaterial(_body)) << "a Body is what stands as a part, and carries none";
    EXPECT_FALSE(Part::hasMaterial(_pad)) << "a feature inside a Body is still made of something";

    EXPECT_EQ(_pad->getPropertyByName("Material"), nullptr)
        << "the feature still holds a material property of its own";
    EXPECT_NE(_body->getPropertyByName("Material"), nullptr)
        << "the Body holds no material property";
}

// The question is still answerable for a feature -- it is just answered by the part the feature
// builds, rather than by a copy the feature keeps in step with its Body.
TEST_F(MaterialBelongsToThePartTest, whatAFeatureIsMadeOfIsWhatItsPartIsMadeOf)
{
    Materials::PropertyMaterial* held = Part::materialPropertyOf(_body);
    ASSERT_NE(held, nullptr);
    held->setValue(shopSteel());

    const Materials::Material* ofPad = Part::materialOfPart(_pad);
    ASSERT_NE(ofPad, nullptr) << "a feature cannot say what the part it builds is made of";
    EXPECT_EQ(ofPad->getUUID().toStdString(), steelUUID);
    // Not a copy that agrees for now: the value the feature answers with is the Body's own.
    EXPECT_EQ(ofPad, &held->getValue())
        << "the feature answered from a material of its own that happens to match";

    const Materials::Material* ofBody = Part::materialOfPart(_body);
    ASSERT_NE(ofBody, nullptr);
    EXPECT_EQ(ofBody->getUUID().toStdString(), steelUUID);
}

// The measurement the ticket was filed on: the part is steel once, not once per object that
// happens to carry a shape. A carried copy is eleven lines of the file (Amendment 18 Clause 18.3),
// so a repeated one is visible to a person reading it.
TEST_F(MaterialBelongsToThePartTest, aPartStatesWhatItIsMadeOfOnce)
{
    Materials::PropertyMaterial* held = Part::materialPropertyOf(_body);
    ASSERT_NE(held, nullptr);
    held->setValue(shopSteel());
    _doc->recompute();

    const std::string path = tempPath("onematerial.cpart");
    ASSERT_TRUE(_doc->saveAs(path.c_str()));
    const std::string words = readAll(path);

    EXPECT_EQ(countOf(words, "uuid=\"" + std::string(steelUUID) + "\""), 1U)
        << "the document states the same material more than once";
    EXPECT_NE(words.find("7900 kg/m^3"), std::string::npos)
        << "the part does not carry what it is made of, only a reference to it";

    // The shape of the defect, independent of which material was chosen: a material block per
    // object that carries a shape. Fewer blocks than shape-carrying objects is what says the
    // material stopped riding along with the geometry.
    std::size_t carryingShape = 0;
    for (App::DocumentObject* obj : _doc->getObjects()) {
        if (Part::hasShape(obj)) {
            ++carryingShape;
        }
    }
    EXPECT_LT(countOf(words, "<PropertyMaterial "), carryingShape)
        << "the document still states one material per object that carries a shape";
}

// The two blocks that kept the copies agreeing are gone, and nothing takes their place: choosing
// what the part is made of reaches no feature, because no feature has anything to reach.
TEST_F(MaterialBelongsToThePartTest, choosingAMaterialTouchesNothingElse)
{
    Materials::PropertyMaterial* held = Part::materialPropertyOf(_body);
    ASSERT_NE(held, nullptr);
    held->setValue(shopSteel());
    _doc->recompute();

    for (App::DocumentObject* obj : _doc->getObjects()) {
        if (obj == _body) {
            continue;
        }
        auto* elsewhere = dynamic_cast<Materials::PropertyMaterial*>(
            obj->getPropertyByName("Material")
        );
        if (elsewhere == nullptr) {
            continue;
        }
        EXPECT_NE(elsewhere->getValue().getUUID().toStdString(), steelUUID)
            << obj->getNameInDocument() << " was handed the part's material as a copy of its own";
    }
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
