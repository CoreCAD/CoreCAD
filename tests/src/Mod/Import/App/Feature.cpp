// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include <fstream>

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
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
#include <Base/Placement.h>
#include <Base/FileInfo.h>
#include <Mod/Import/App/Feature.h>
#include <Mod/Part/App/SpatialInterference.h>
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

    /// Writes a STEP file holding one box of the given dimensions.
    std::string writeBoxStep(const char* name, double dx, double dy, double dz)
    {
        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        Part::TopoShape(BRepPrimAPI_MakeBox(dx, dy, dz).Shape()).exportStep(file.filePath().c_str());
        _written.push_back(file.filePath());
        return file.filePath();
    }

    /// Writes a STEP file holding one box and a second, identical box in the same
    /// place -- the same part delivered twice, stacked exactly on itself. The copy
    /// is a deep one, because the kernel folds a repeated shape back to one entry.
    std::string writeDoubledBoxStep(const char* name, double side)
    {
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(side, side, side).Shape();
        BRep_Builder builder;
        TopoDS_Compound doubled;
        builder.MakeCompound(doubled);
        builder.Add(doubled, box);
        builder.Add(doubled, BRepBuilderAPI_Copy(box).Shape());

        Base::FileInfo file(Base::FileInfo::getTempPath() + name);
        Part::TopoShape(doubled).exportStep(file.filePath().c_str());
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

// The point of keeping a record: a revision is matched against it. A part that
// grew taller keeps the face it did not move, and says plainly that the five faces
// it did move are not the faces it had before -- rather than quietly renumbering
// and leaving everything downstream to find out for itself.
TEST_F(ImportFeature, aRevisionCarriesTheFacesThatDidNotChange)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_faces_rev.step", 10.0, 10.0, 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const auto before = feature->FaceIdentities.getValues();
    ASSERT_EQ(before.size(), 6U);

    // Taller, same footprint: the bottom face is untouched, the other five are not
    // the faces they were -- the walls grew and the top moved.
    writeBoxStep("cc_import_faces_rev.step", 10.0, 10.0, 20.0);
    feature->touch();
    _doc->recompute();
    ASSERT_FALSE(feature->isError());

    const auto after = feature->FaceIdentities.getValues();
    EXPECT_EQ(after.size(), 6U) << "a taller box is still a box: six faces";

    std::size_t survived = 0;
    for (const auto& entry : before) {
        survived += after.count(entry.first);
    }
    EXPECT_EQ(survived, 1U) << "the face that did not move keeps its identity";
    EXPECT_EQ(feature->LostFaces.getValues().size(), 5U) << "and the five that moved are named";
    EXPECT_TRUE(feature->AmbiguousFaces.getValues().empty());
}

// Reading the same file again settles: every identity carries, nothing is lost,
// nothing is added, and there is nothing to report.
TEST_F(ImportFeature, anUnchangedRereadCarriesEveryFace)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_faces_same.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const auto before = feature->FaceIdentities.getValues();
    ASSERT_EQ(before.size(), 6U);

    feature->touch();
    _doc->recompute();

    EXPECT_EQ(feature->FaceIdentities.getValues(), before);
    EXPECT_TRUE(feature->LostFaces.getValues().empty());
    EXPECT_TRUE(feature->AmbiguousFaces.getValues().empty());
}

// Where a part sits is not part of what its faces are. Dragging an import across
// the document must not lose the identity of every face it has -- and the same
// reading is why an assembly part, whose transform is attached after the importer
// builds it, used to disagree with its own first re-read about all of its faces.
TEST_F(ImportFeature, movingAnImportKeepsTheIdentityOfItsFaces)
{
    auto* feature = addImport();

    const std::string path = writeBoxStep("cc_import_moved.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    const auto before = feature->FaceIdentities.getValues();
    ASSERT_EQ(before.size(), 6U);

    feature->Placement.setValue(Base::Placement(Base::Vector3d(100, -40, 7), Base::Rotation()));
    feature->touch();
    _doc->recompute();

    EXPECT_EQ(feature->FaceIdentities.getValues(), before) << "the part moved; its faces did not";
    EXPECT_TRUE(feature->LostFaces.getValues().empty());
}

// The yellow case. When a revision holds more than one face answering to an
// identity, nothing in the geometry says which one was meant -- the four identical
// bolt holes of a symmetric flange. The identity is set aside for the user to
// settle, and is neither handed to one of the candidates nor written off as gone.
TEST_F(ImportFeature, anIdentityWithTwoCandidatesIsSetAsideNotGuessedAt)
{
    auto* feature = addImport();

    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    feature->Shape.setValue(box);
    const auto recorded = feature->matchFaceIdentities();
    ASSERT_EQ(recorded.added, 6);
    ASSERT_EQ(feature->FaceIdentities.getValues().size(), 6U);

    // The revision holds the same part twice over, in the same place: every face
    // now has an indistinguishable twin. The copy is a deep one -- the kernel folds
    // a repeated shape back to a single entry, which would hide the ambiguity.
    BRep_Builder builder;
    TopoDS_Compound twins;
    builder.MakeCompound(twins);
    builder.Add(twins, box);
    builder.Add(twins, BRepBuilderAPI_Copy(box).Shape());
    feature->Shape.setValue(twins);

    const auto match = feature->matchFaceIdentities();
    EXPECT_EQ(match.carried, 0);
    EXPECT_EQ(match.added, 0) << "a contested face is not a new face";
    EXPECT_TRUE(match.lost.empty()) << "the identity is contested, not gone";
    EXPECT_EQ(match.ambiguous.size(), 6U);
    EXPECT_EQ(feature->AmbiguousFaces.getValues().size(), 6U);
    EXPECT_TRUE(feature->FaceIdentities.getValues().empty())
        << "no identity may be bound to one of two candidates without being asked";
}

// The same part delivered twice, stacked exactly on itself, is a duplicate a user
// almost never wants -- and nothing in the document looks for it. The spatial
// interference check (ARCHITECTURE 8.6) compares whole Bodies against one another,
// so it cannot see two solids inside a single object, and an import is not a Body
// in any case. The face record notices it for nothing, because it is already
// reading every face: a part present twice has two faces answering to every one
// identity, so the count of identities comes out at half the count of faces.
TEST_F(ImportFeature, aPartDeliveredTwiceShowsAsFewerIdentitiesThanFaces)
{
    auto* feature = addImport();

    const std::string path = writeDoubledBoxStep("cc_import_doubled.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();

    const Part::TopoShape& shape = feature->Shape.getShape();
    ASSERT_EQ(shape.countSubShapes(TopAbs_SOLID), 2UL) << "the file holds the part twice";
    ASSERT_EQ(shape.countSubShapes(TopAbs_FACE), 12UL);
    EXPECT_EQ(feature->FaceIdentities.getValues().size(), 6U)
        << "twelve faces, six identities: every face has an indistinguishable twin";
}

// The same duplicate seen from the other side. Once there is a record to match
// against, every identity in it has two candidates, so the whole part reads as
// contested -- nothing carried, nothing added, nothing lost. Two consequences
// worth naming: this is a distinctive reading no ordinary revision produces, and
// a doubled import keeps no face record at all, because no identity may be bound
// to one of two indistinguishable candidates without being asked.
TEST_F(ImportFeature, rereadingADoubledPartContestsEveryIdentity)
{
    auto* feature = addImport();

    const std::string path = writeDoubledBoxStep("cc_import_doubled_reread.step", 10.0);
    feature->SourceFile.setValue(path.c_str());
    _doc->recompute();
    ASSERT_EQ(feature->FaceIdentities.getValues().size(), 6U);

    feature->touch();
    _doc->recompute();

    EXPECT_EQ(feature->AmbiguousFaces.getValues().size(), 6U);
    EXPECT_TRUE(feature->LostFaces.getValues().empty()) << "contested, not gone";
    EXPECT_TRUE(feature->FaceIdentities.getValues().empty());
}

// The case the widened overlap check exists for: two imported parts landing on top
// of one another. An import is not a Body, so until the check asked about every
// independent solid rather than only Bodies, nothing in the document looked for
// this at all.
TEST_F(ImportFeature, twoImportedPartsOverlappingAreReported)
{
    const std::string path = writeBoxStep("cc_import_overlap.step", 10.0);

    auto* first = addImport();
    first->SourceFile.setValue(path.c_str());
    auto* second = addImport();
    second->SourceFile.setValue(path.c_str());
    second->Placement.setValue(Base::Placement(Base::Vector3d(5, 0, 0), Base::Rotation()));
    _doc->recompute();

    const auto pairs = Part::overlappingPairs(_doc);
    ASSERT_EQ(pairs.size(), 1U);
    const bool matches = (pairs[0].first == first && pairs[0].second == second)
        || (pairs[0].first == second && pairs[0].second == first);
    EXPECT_TRUE(matches);

    // Moved clear of each other, they stop being reported: the answer is the
    // geometry, not the fact that there are two imports in the document.
    second->Placement.setValue(Base::Placement(Base::Vector3d(60, 0, 0), Base::Rotation()));
    second->touch();
    _doc->recompute();
    EXPECT_TRUE(Part::overlappingPairs(_doc).empty());
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
