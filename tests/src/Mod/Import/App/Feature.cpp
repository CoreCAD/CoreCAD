// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include <fstream>

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pnt.hxx>

#include <App/Document.h>
#include <Base/FileInfo.h>
#include <Mod/Import/App/Feature.h>
#include <Mod/Part/App/SubShapeSignature.h>
#include <Mod/Part/App/TopoShape.h>

class ImportFeature: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("ImportFeature");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
        for (const auto& path : _written) {
            Base::FileInfo(path).deleteFile();
        }
    }

    /// Writes \a contents to a throwaway file and returns its path.
    std::string writeFile(const char* name, const char* contents)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        std::ofstream out(file.filePath(), std::ios::binary | std::ios::trunc);
        out << contents;
        out.close();
        _written.push_back(file.filePath());
        return file.filePath();
    }

    /// Writes a STEP file holding one box of the given size, and returns its path.
    std::string writeBoxStep(const char* name, double side)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        Part::TopoShape(BRepPrimAPI_MakeBox(side, side, side).Shape())
            .exportStep(file.filePath().c_str());
        _written.push_back(file.filePath());
        return file.filePath();
    }

    /// Writes a STEP file holding two separate boxes as two top-level shapes.
    std::string writeTwoBoxStep(const char* name)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        STEPControl_Writer writer;
        writer.Transfer(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), STEPControl_AsIs);
        writer.Transfer(
            BRepPrimAPI_MakeBox(gp_Pnt(50.0, 0.0, 0.0), 10.0, 10.0, 10.0).Shape(),
            STEPControl_AsIs
        );
        writer.Write(file.filePath().c_str());
        _written.push_back(file.filePath());
        return file.filePath();
    }

    /// Writes a STEP file whose top-level shapes carry the given names.
    std::string writeNamedStep(const char* name, const std::vector<std::string>& names)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);

        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        Handle(TDocStd_Document) doc;
        app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), doc);
        Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

        double offset = 0.0;
        for (const auto& label : names) {
            const TDF_Label added = shapes->AddShape(
                BRepPrimAPI_MakeBox(gp_Pnt(offset, 0.0, 0.0), 10.0, 10.0, 10.0).Shape(),
                false
            );
            TDataStd_Name::Set(added, TCollection_ExtendedString(label.c_str()));
            offset += 50.0;
        }

        STEPCAFControl_Writer writer;
        writer.Transfer(doc, STEPControl_AsIs);
        writer.Write(file.filePath().c_str());
        app->Close(doc);

        _written.push_back(file.filePath());
        return file.filePath();
    }

    static double volumeOf(const Part::TopoShape& shape)
    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape.getShape(), props);
        return props.Mass();
    }

    Import::Feature* addImport()
    {
        auto* feature = _doc->addObject<Import::Feature>("Import");
        _doc->recompute();
        return feature;
    }

    App::Document* _doc {nullptr};
    std::vector<std::string> _written;
};

TEST_F(ImportFeature, anImportRemembersTheFileItCameFrom)
{
    auto* feature = addImport();

    // The point of the type: the source is a declared input, not something the
    // importer knows only while it is running.
    ASSERT_NE(feature->getPropertyByName("SourceFile"), nullptr);
    ASSERT_NE(feature->getPropertyByName("SourceHash"), nullptr);
    ASSERT_NE(feature->getPropertyByName("TranslatorSettings"), nullptr);

    const std::string path = writeFile("cc_import_source.txt", "bracket v1");
    feature->SourceFile.setValue(path.c_str());

    EXPECT_EQ(std::string(feature->SourceFile.getValue()), path);
}

TEST_F(ImportFeature, anImportIsAnAnchorSoItHoldsItsOwnPlacement)
{
    auto* feature = addImport();

    // An import builds from raw input rather than from objects it references, so
    // unlike a boolean or a dress-up it authors a placement of its own.
    EXPECT_TRUE(feature->holdsAuthoredPlacement());
}

TEST_F(ImportFeature, theFingerprintFollowsTheFileContents)
{
    auto* feature = addImport();

    const std::string path = writeFile("cc_import_change.txt", "bracket v1");
    feature->SourceFile.setValue(path.c_str());

    EXPECT_TRUE(feature->refreshSourceHash());
    const std::string first = feature->SourceHash.getStrValue();
    EXPECT_FALSE(first.empty());

    // Re-reading an unchanged file is not a change -- otherwise every recompute
    // would look like a re-import.
    EXPECT_FALSE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), first);

    // The supplier sends a new revision under the same name.
    writeFile("cc_import_change.txt", "bracket v2");

    EXPECT_TRUE(feature->refreshSourceHash());
    EXPECT_NE(feature->SourceHash.getStrValue(), first);
}

TEST_F(ImportFeature, anUnreadableSourceFingerprintsAsEmpty)
{
    auto* feature = addImport();

    // Distinguishable from a file that is genuinely empty, which hashes to the
    // SHA-1 of no bytes rather than to nothing at all.
    feature->SourceFile.setValue("/no/such/file/anywhere.step");
    EXPECT_FALSE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), "");

    const std::string path = writeFile("cc_import_empty.txt", "");
    feature->SourceFile.setValue(path.c_str());
    EXPECT_TRUE(feature->refreshSourceHash());
    EXPECT_EQ(feature->SourceHash.getStrValue(), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_F(ImportFeature, theSourceFileBecomesTheShape)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_box.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();

    EXPECT_FALSE(feature->isError());
    EXPECT_NEAR(volumeOf(feature->Shape.getShape()), 1000.0, 1e-6);

    // Having built the shape, the feature knows which contents it built from.
    EXPECT_EQ(feature->SourceHash.getStrValue(), Import::Feature::hashFile(path.c_str()));
}

TEST_F(ImportFeature, aNewRevisionOfTheFileRebuildsTheShape)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_rev.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const std::string before = feature->SourceHash.getStrValue();
    ASSERT_NEAR(volumeOf(feature->Shape.getShape()), 1000.0, 1e-6);

    // The supplier sends a bigger bracket under the same name.
    writeBoxStep("cc_import_rev.step", 20.0);
    feature->touch();
    _doc->recompute();

    EXPECT_FALSE(feature->isError());
    EXPECT_NEAR(volumeOf(feature->Shape.getShape()), 8000.0, 1e-6);
    EXPECT_NE(feature->SourceHash.getStrValue(), before);
}

// An import can say what it produced. Before this, a face of an imported shape had
// an identity only where some later feature happened to reference it -- the import
// itself knew nothing about its own faces, so a re-import had nothing to compare
// against and could not report which faces survived it.
TEST_F(ImportFeature, anImportRecordsTheFacesItProduced)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_faces.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    ASSERT_FALSE(feature->isError());

    const auto identities = feature->FaceIdentities.getValues();
    EXPECT_EQ(identities.size(), 6U) << "a box arrives with six faces, each on record";
    EXPECT_EQ(
        identities.size(),
        static_cast<std::size_t>(feature->Shape.getShape().countSubShapes(TopAbs_FACE))
    ) << "every face of the shape is accounted for, and nothing that is not a face";

    // The identity and the current signature start out as the same reading, and the
    // reading is the geometry's own: it is what the reference layer captures for a
    // face of this shape, so an identity here and a reference to that face agree.
    for (const auto& entry : identities) {
        EXPECT_FALSE(entry.first.empty());
        EXPECT_EQ(entry.first, entry.second);
    }
    const TopoDS_Shape firstFace
        = feature->Shape.getShape().getSubShape(TopAbs_FACE, 1, /*silent*/ true);
    ASSERT_FALSE(firstFace.IsNull());
    EXPECT_EQ(identities.count(Part::subShapeSignature(firstFace)), 1U)
        << "the recorded identity has to be the same reading a reference would take";
}

// Reading the same file again produces the same record. If it did not, the record
// could not be used to tell what changed between two revisions -- every re-import
// would look like a wholesale replacement.
TEST_F(ImportFeature, theSameFileRecordsTheSameFacesEveryTime)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_faces_again.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const auto first = feature->FaceIdentities.getValues();
    ASSERT_EQ(first.size(), 6U);

    feature->touch();
    _doc->recompute();

    EXPECT_EQ(feature->FaceIdentities.getValues(), first);
}

// A revision that changes the geometry changes the record with it. This is the raw
// material the green / yellow / red pass will work from: today the faces are simply
// re-read, so a changed part is a changed record.
TEST_F(ImportFeature, aChangedPartIsRecordedAsChangedFaces)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_faces_rev.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const auto before = feature->FaceIdentities.getValues();
    ASSERT_EQ(before.size(), 6U);

    writeBoxStep("cc_import_faces_rev.step", 20.0);
    feature->touch();
    _doc->recompute();

    const auto after = feature->FaceIdentities.getValues();
    EXPECT_EQ(after.size(), 6U) << "a bigger box is still a box: six faces";
    EXPECT_NE(after, before) << "and they are not the same six faces";
}

// A guard, not a bug-catcher: this passes whether or not execute() guards its
// write of SourceHash, because a recompute purges the touched flag on its way
// out. It is here to catch a future execute() that dirties something the
// recompute does not clear, which would leave the document unable to settle.
TEST_F(ImportFeature, aCompletedImportLeavesTheDocumentAtRest)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_settle.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const std::string first = feature->SourceHash.getStrValue();

    EXPECT_EQ(_doc->recompute(), 0);
    EXPECT_EQ(feature->SourceHash.getStrValue(), first);
}

TEST_F(ImportFeature, aMissingSourceFailsRatherThanEmptyingTheShape)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_gone.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    ASSERT_NEAR(volumeOf(feature->Shape.getShape()), 1000.0, 1e-6);

    Base::FileInfo(path).deleteFile();
    feature->touch();
    _doc->recompute();

    EXPECT_TRUE(feature->isError());
    // The geometry it last built stands; a vanished file is not a reason to
    // silently drop what downstream features are anchored to.
    EXPECT_NEAR(volumeOf(feature->Shape.getShape()), 1000.0, 1e-6);
}

TEST_F(ImportFeature, anAssemblyFileIsRefusedRatherThanFused)
{
    auto* feature = addImport();

    const std::string path = writeTwoBoxStep("cc_import_two.step");
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();

    // Two separately-named parts must not arrive as one anonymous lump. Until a
    // feature can address a node inside the file, refusing is the honest answer.
    // The message is asserted so the test cannot pass on some unrelated failure
    // to read the file at all.
    ASSERT_TRUE(feature->isError());
    EXPECT_NE(
        std::string(feature->getStatusString()).find("more than one top-level shape"),
        std::string::npos
    );
    EXPECT_TRUE(feature->Shape.getShape().isNull());
}

TEST_F(ImportFeature, anUntranslatableFormatFailsHonestly)
{
    auto* feature = addImport();

    const std::string path = writeFile("cc_import_notcad.txt", "not a CAD file");
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();

    EXPECT_TRUE(feature->isError());
}

TEST_F(ImportFeature, aNamedNodeIsFoundEvenAfterThePositionsShift)
{
    const std::string path = writeNamedStep("cc_import_shift.step", {"PartA", "PartB"});
    std::string node;
    ASSERT_NO_THROW(Import::Feature::translate(Base::FileInfo(path), "", "PartB", &node));
    ASSERT_FALSE(node.empty());

    // The supplier adds a part ahead of PartB, which shifts every position after
    // it. Trusting the position alone would hand back the new part instead.
    writeNamedStep("cc_import_shift.step", {"PartA", "PartNew", "PartB"});

    std::string moved;
    TopoDS_Shape shape;
    ASSERT_NO_THROW(shape = Import::Feature::translate(Base::FileInfo(path), node, "PartB", &moved));
    EXPECT_NE(moved, node);

    // Same box, so the test cannot rest on volume alone: the new part sits where
    // PartB used to be only if the wrong node was taken.
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    double xMin {}, yMin {}, zMin {}, xMax {}, yMax {}, zMax {};
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    EXPECT_NEAR(xMin, 100.0, 1e-6);
}

TEST_F(ImportFeature, aVanishedNodeSaysWhatIsMissing)
{
    const std::string path = writeNamedStep("cc_import_dropped.step", {"Keep", "Gone"});
    std::string node;
    ASSERT_NO_THROW(Import::Feature::translate(Base::FileInfo(path), "", "Gone", &node));

    writeNamedStep("cc_import_dropped.step", {"Keep"});

    try {
        Import::Feature::translate(Base::FileInfo(path), node, "Gone");
        FAIL() << "a node that is no longer in the file must not resolve";
    }
    catch (const Base::Exception& e) {
        EXPECT_NE(std::string(e.what()).find("no longer holds a node named"), std::string::npos);
    }
}

TEST_F(ImportFeature, twoNodesOfTheSameNameAreRefusedRatherThanGuessedBetween)
{
    const std::string path = writeNamedStep("cc_import_twin.step", {"Keep", "Twin"});
    std::string node;
    ASSERT_NO_THROW(Import::Feature::translate(Base::FileInfo(path), "", "Twin", &node));

    // The next revision has two parts carrying that name, and the position no
    // longer picks one of them out. Which is meant is a question for the user,
    // not something to settle by taking the first.
    writeNamedStep("cc_import_twin.step", {"Twin", "Keep", "Twin"});

    try {
        Import::Feature::translate(Base::FileInfo(path), "0:1:1:99", "Twin");
        FAIL() << "an ambiguous name must not resolve";
    }
    catch (const Base::Exception& e) {
        EXPECT_NE(std::string(e.what()).find("several nodes named"), std::string::npos);
    }
}
