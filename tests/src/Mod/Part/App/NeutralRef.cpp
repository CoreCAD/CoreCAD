// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <TopExp.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>

#include <App/Application.h>
#include <App/Document.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <Base/Placement.h>
#include <src/App/InitApplication.h>

#include "Mod/Part/App/PrimitiveFaceRoleRef.h"
#include "Mod/Part/App/FeaturePartBox.h"
#include "Mod/Part/App/FeaturePartCut.h"
#include "Mod/Part/App/NeutralRef.h"
#include "Mod/Part/App/PrimitiveFeature.h"
#include "Mod/Part/App/TopoShape.h"

using Part::bindInDocument;
using Part::captureFaceRef;
using Part::capturePrimitiveFaceRole;
using Part::fromNeutralString;
using Part::NRef;
using Part::NRefBinding;
using Part::NRefResolution;
using Part::RefMatch;
using Part::resolveFaceRef;
using Part::toNeutralString;

namespace
{
// Rebuild a solid with its faces enumerated in the reverse order -- same geometry,
// a different internal face numbering. This stands in for what an independent
// rebuild or a re-import of the same shape produces: the physical faces are
// identical, but the kernel's positional "FaceN" ordinals no longer line up.
TopoDS_Shape withReversedFaceOrder(const TopoDS_Shape& solid)
{
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(solid, TopAbs_FACE, faces);

    BRep_Builder builder;
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    for (int i = faces.Extent(); i >= 1; --i) {
        builder.Add(shell, faces(i));
    }
    TopoDS_Solid rebuilt;
    builder.MakeSolid(rebuilt);
    builder.Add(rebuilt, shell);
    return rebuilt;
}

// A box-minus-box cut in @p doc: the smallest design with genuinely DERIVED faces
// (walls the cut created, carrying element-map provenance names). Returns the cut
// and its two operands so a test can align their durable Uids across two documents.
struct CutDesign
{
    Part::Cut* cut {nullptr};
    Part::Box* base {nullptr};
    Part::Box* tool {nullptr};
};

CutDesign buildCut(App::Document* doc)
{
    auto* b1 = doc->addObject<Part::Box>();
    b1->Length.setValue(30.0);
    b1->Width.setValue(20.0);
    b1->Height.setValue(10.0);

    auto* b2 = doc->addObject<Part::Box>();
    b2->Length.setValue(10.0);
    b2->Width.setValue(10.0);
    b2->Height.setValue(40.0);
    b2->Placement.setValue(Base::Placement(Base::Vector3d(10, 5, -5), Base::Rotation()));

    auto* cut = doc->addObject<Part::Cut>();
    cut->Base.setValue(b1);
    cut->Tool.setValue(b2);
    doc->recompute();
    return {cut, b1, b2};
}
}  // namespace

// NRef is the neutral stored form of a leaf reference. These tests exercise the
// two-regime capture/resolve on live features: role-bearing primitives (a box, a
// cylinder -- symmetry-proof) and an ellipsoid, a primitive deliberately outside
// the role allow-list, standing in for the signature regime a true import falls to.
class NeutralRefTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("neutralRef");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    Part::Box* makeBox()
    {
        auto* box = _doc->addObject<Part::Box>();
        box->Length.setValue(30.0);
        box->Width.setValue(20.0);
        box->Height.setValue(10.0);
        _doc->recompute();
        return box;
    }

    Part::Cylinder* makeCylinder()
    {
        auto* cyl = _doc->addObject<Part::Cylinder>();
        cyl->Radius.setValue(6.0);
        cyl->Height.setValue(15.0);
        _doc->recompute();
        return cyl;
    }

    // A primitive outside the role allow-list: its faces carry no role, so it falls
    // to the signature regime -- the stand-in for a history-less import leaf.
    Part::Ellipsoid* makeEllipsoid()
    {
        auto* ell = _doc->addObject<Part::Ellipsoid>();
        ell->Radius1.setValue(4.0);
        ell->Radius2.setValue(2.0);
        ell->Radius3.setValue(3.0);
        _doc->recompute();
        return ell;
    }

    App::Document* _doc {nullptr};
};

// A box face captures the full leaf identity: the owning feature's Uid, the
// symmetry-proof role, and a geometric signature as cross-check.
TEST_F(NeutralRefTest, captureFillsRoleAndSignatureForBox)
{
    Part::Box* box = makeBox();

    const NRef ref = captureFaceRef(*box, "Face1");
    EXPECT_EQ(ref.kind, "face");
    EXPECT_EQ(ref.featureUid, box->Uid.getValueStr());
    EXPECT_FALSE(ref.featureUid.empty());
    EXPECT_FALSE(ref.signature.empty());

    const std::set<std::string> axes {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    EXPECT_EQ(axes.count(ref.role), 1U);
}

// The role regime resolves back to the captured sub-name, and carries the
// reference through a real recompute that rebuilds the whole shape.
TEST_F(NeutralRefTest, roleRegimeRoundTripsAndSurvivesEdit)
{
    Part::Box* box = makeBox();

    const std::array<const char*, 6> faces {"Face1", "Face2", "Face3", "Face4", "Face5", "Face6"};
    for (const char* sub : faces) {
        const NRef ref = captureFaceRef(*box, sub);
        ASSERT_FALSE(ref.role.empty());
        EXPECT_EQ(resolveFaceRef(ref, *box).subName, sub);
    }

    const NRef ref = captureFaceRef(*box, "Face3");
    box->Length.setValue(48.0);
    box->Height.setValue(6.0);
    _doc->recompute();

    const std::string resolved = resolveFaceRef(ref, *box).subName;
    ASSERT_FALSE(resolved.empty());
    EXPECT_EQ(captureFaceRef(*box, resolved).role, ref.role);
}

// A cylinder is now a role-bearing primitive: every face captures a role from the
// round-primitive vocabulary (Side, +Z, -Z) and resolves back through it.
TEST_F(NeutralRefTest, roleRegimeForCylinderRoundTrips)
{
    Part::Cylinder* cyl = makeCylinder();

    const int faceCount = static_cast<int>(cyl->Shape.getShape().countSubShapes(TopAbs_FACE));
    ASSERT_GT(faceCount, 1);

    std::set<std::string> roles;
    for (int i = 1; i <= faceCount; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        const NRef ref = captureFaceRef(*cyl, sub);
        EXPECT_EQ(ref.kind, "face");
        ASSERT_FALSE(ref.role.empty()) << "a cylinder face should carry a role";
        ASSERT_FALSE(ref.signature.empty());
        EXPECT_EQ(resolveFaceRef(ref, *cyl).subName, sub);
        roles.insert(ref.role);
    }
    EXPECT_EQ(roles, (std::set<std::string> {"Side", "+Z", "-Z"}));
}

// A primitive outside the role allow-list (an ellipsoid) falls to the signature
// regime: no role captured, but the geometric signature identifies the face and
// resolves back to it -- the path a true history-less import leaf takes.
TEST_F(NeutralRefTest, signatureRegimeForExcludedPrimitiveRoundTrips)
{
    Part::Ellipsoid* ell = makeEllipsoid();

    const int faceCount = static_cast<int>(ell->Shape.getShape().countSubShapes(TopAbs_FACE));
    ASSERT_GE(faceCount, 1);

    for (int i = 1; i <= faceCount; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        const NRef ref = captureFaceRef(*ell, sub);
        EXPECT_EQ(ref.kind, "face");
        EXPECT_TRUE(ref.role.empty()) << "an excluded primitive's face carries no role";
        ASSERT_FALSE(ref.signature.empty());
        EXPECT_EQ(resolveFaceRef(ref, *ell).subName, sub);
    }
}

// A ref that is not a face, or whose leaf target matches nothing, yields no
// sub-name; a capture on a non-existent sub-name yields a null ref. The two cases
// are told apart: one reference asks nothing, the other asked and lost.
TEST_F(NeutralRefTest, degradesOnBadRef)
{
    Part::Box* box = makeBox();

    EXPECT_TRUE(captureFaceRef(*box, "Face99").kind.empty());  // null ref, no such face

    NRef notAFace;
    notAFace.kind = "edge";
    notAFace.role = "+X";
    const NRefResolution notAsked = resolveFaceRef(notAFace, *box);
    EXPECT_EQ(notAsked.match, RefMatch::None);
    EXPECT_TRUE(notAsked.subName.empty());

    NRef goneSignature;
    goneSignature.kind = "face";
    goneSignature.signature = "not-a-real-signature";
    const NRefResolution lost = resolveFaceRef(goneSignature, *box);
    EXPECT_EQ(lost.match, RefMatch::Lost);
    EXPECT_TRUE(lost.subName.empty());
}

// The distinction the protocol turns on: a reference whose target is GONE and one
// whose target is there SEVERAL TIMES OVER are different answers, and resolution
// says which. Both withhold a sub-name -- the point is that the caller can now tell
// an honest failure from a question only the user can settle.
TEST_F(NeutralRefTest, tellsAmbiguousApartFromLost)
{
    Part::Ellipsoid* ell = makeEllipsoid();
    const NRef ref = captureFaceRef(*ell, "Face1");
    ASSERT_TRUE(ref.role.empty()) << "the signature regime is the one under test";
    ASSERT_FALSE(ref.signature.empty());
    ASSERT_EQ(resolveFaceRef(ref, *ell).match, RefMatch::Matched);

    // Two coincident duplicates of the very same surface: both carry the captured
    // signature, so nothing in the geometry says which one the reference meant.
    // This is the four-identical-bolt-holes case in miniature. The duplicate is a
    // deep copy, not the same shape added twice -- the kernel's sub-shape map folds
    // a repeated shape back to one entry, which would hide the ambiguity.
    const TopoDS_Shape one = ell->Shape.getValue();
    BRep_Builder builder;
    TopoDS_Compound twins;
    builder.MakeCompound(twins);
    builder.Add(twins, one);
    builder.Add(twins, BRepBuilderAPI_Copy(one).Shape());
    ell->Shape.setValue(twins);

    const NRefResolution ambiguous = resolveFaceRef(ref, *ell);
    EXPECT_EQ(ambiguous.match, RefMatch::Ambiguous);
    EXPECT_TRUE(ambiguous.subName.empty()) << "an ambiguous match must never hand back a face";

    // Now replace the geometry entirely: the captured signature matches nothing at
    // all, which is the other failure and must not read as ambiguity.
    Part::Box* box = makeBox();
    ell->Shape.setValue(box->Shape.getValue());
    EXPECT_EQ(resolveFaceRef(ref, *ell).match, RefMatch::Lost);
}

// A reference is to a face, not to a place. Moving the feature the reference points
// at must not lose it -- read in the world frame, every signature would shift with
// the part and the reference would come back Lost the moment a user dragged it.
TEST_F(NeutralRefTest, aLeafReferenceSurvivesTheFeatureBeingMoved)
{
    Part::Ellipsoid* ell = makeEllipsoid();
    const NRef ref = captureFaceRef(*ell, "Face1");
    ASSERT_TRUE(ref.role.empty()) << "the signature regime is the one under test";
    ASSERT_EQ(resolveFaceRef(ref, *ell).match, RefMatch::Matched);

    ell->Placement.setValue(Base::Placement(Base::Vector3d(120, -35, 8), Base::Rotation()));
    _doc->recompute();

    const NRefResolution moved = resolveFaceRef(ref, *ell);
    EXPECT_EQ(moved.match, RefMatch::Matched);
    EXPECT_EQ(moved.subName, "Face1");
    EXPECT_EQ(captureFaceRef(*ell, "Face1").signature, ref.signature)
        << "the same face in a new place is the same face";
}

// The payoff. Capture a reference on one box, then resolve it against an
// independently-numbered rebuild of the same box -- the merge situation, where the
// two branches wrote the same geometry with different kernel face ordinals. The
// stored "FaceN" now denotes a DIFFERENT physical face on the rebuild (the bug the
// whole layer exists to cure); the NRef, held by role, still finds the correct +X
// face at its new ordinal.
TEST_F(NeutralRefTest, nRefBindsAcrossIndependentRebuildWhereRawNumberFails)
{
    Part::Box* branchA = makeBox();

    // Find the +X face's sub-name on branch A and capture a reference to it.
    std::string plusXOnA;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (capturePrimitiveFaceRole(*branchA, sub) == "+X") {
            plusXOnA = sub;
        }
    }
    ASSERT_FALSE(plusXOnA.empty());
    const NRef ref = captureFaceRef(*branchA, plusXOnA);
    ASSERT_EQ(ref.role, "+X");

    // Branch B: the same box, rebuilt with a different internal face numbering.
    Part::Box* branchB = makeBox();
    branchB->Shape.setValue(withReversedFaceOrder(branchA->Shape.getValue()));
    // Sanity: the rebuild is a well-formed box that still has all six axis faces.
    std::set<std::string> rolesOnB;
    for (int i = 1; i <= 6; ++i) {
        rolesOnB.insert(capturePrimitiveFaceRole(*branchB, "Face" + std::to_string(i)));
    }
    ASSERT_EQ(rolesOnB, (std::set<std::string> {"+X", "-X", "+Y", "-Y", "+Z", "-Z"}));

    // The raw stored ordinal is now STALE: on branch B it names a different face,
    // not the +X face the reference meant.
    EXPECT_NE(capturePrimitiveFaceRole(*branchB, plusXOnA), "+X");

    // The NRef resolves correctly: a different ordinal than was stored, but genuinely
    // the +X face -- the reference survived the renumbering the raw index could not.
    const std::string resolvedOnB = resolveFaceRef(ref, *branchB).subName;
    ASSERT_FALSE(resolvedOnB.empty());
    EXPECT_NE(resolvedOnB, plusXOnA);
    EXPECT_EQ(capturePrimitiveFaceRole(*branchB, resolvedOnB), "+X");
}

// A serialized NRef round-trips every field through its neutral string form.
TEST_F(NeutralRefTest, neutralStringRoundTripsAllFields)
{
    Part::Box* box = makeBox();
    const NRef ref = captureFaceRef(*box, "Face1");

    const NRef back = fromNeutralString(toNeutralString(ref));
    EXPECT_EQ(back.featureUid, ref.featureUid);
    EXPECT_EQ(back.kind, ref.kind);
    EXPECT_EQ(back.role, ref.role);
    EXPECT_EQ(back.prov, ref.prov);
    EXPECT_EQ(back.signature, ref.signature);
}

// The signature-only case (empty role) survives the round-trip too -- an empty
// field must not collapse the format.
TEST_F(NeutralRefTest, neutralStringRoundTripsSignatureOnly)
{
    Part::Ellipsoid* ell = makeEllipsoid();
    const NRef ref = captureFaceRef(*ell, "Face1");
    ASSERT_TRUE(ref.role.empty());

    const NRef back = fromNeutralString(toNeutralString(ref));
    EXPECT_EQ(back.kind, ref.kind);
    EXPECT_TRUE(back.role.empty());
    EXPECT_EQ(back.signature, ref.signature);
}

// Anything that is not a well-formed NRef string of a known version parses to a
// null ref rather than a half-populated guess.
TEST_F(NeutralRefTest, fromNeutralStringRejectsMalformed)
{
    EXPECT_TRUE(fromNeutralString("").kind.empty());
    EXPECT_TRUE(fromNeutralString("garbage").kind.empty());
    EXPECT_TRUE(fromNeutralString("NRef|1|uid|face|+X|sig").kind.empty());    // superseded version
    EXPECT_TRUE(fromNeutralString("NRef|9|uid|face|+X|p|sig").kind.empty());  // unknown version
    EXPECT_TRUE(fromNeutralString("NRef|2|uid|face|+X").kind.empty());        // too few fields
}

// End to end: a reference written out as a neutral string, read back as if from a
// saved file, still binds across an independent rebuild -- serialization carries
// the durable identity, not just the in-memory struct.
TEST_F(NeutralRefTest, serializedRefBindsAcrossRebuild)
{
    Part::Box* branchA = makeBox();

    std::string plusXOnA;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (capturePrimitiveFaceRole(*branchA, sub) == "+X") {
            plusXOnA = sub;
        }
    }
    ASSERT_FALSE(plusXOnA.empty());

    // Capture, serialize, and re-parse -- the trip through a file.
    const std::string stored = toNeutralString(captureFaceRef(*branchA, plusXOnA));
    const NRef reloaded = fromNeutralString(stored);
    ASSERT_EQ(reloaded.role, "+X");

    Part::Box* branchB = makeBox();
    branchB->Shape.setValue(withReversedFaceOrder(branchA->Shape.getValue()));

    const std::string resolvedOnB = resolveFaceRef(reloaded, *branchB).subName;
    ASSERT_FALSE(resolvedOnB.empty());
    EXPECT_EQ(capturePrimitiveFaceRole(*branchB, resolvedOnB), "+X");
}

// The merge consumer. Two independent documents -- two files -- each with the same
// authored box (shared durable Uid, the identity that survives a branch) but its own
// kernel face numbering. A reference captured and serialized on doc A is read back and
// bound against doc B WITHOUT any live pointer into B: the consumer finds the feature
// by its Uid and resolves the current sub-name. This is the file-to-file merge step,
// and the only test where featureUid does real work.
TEST_F(NeutralRefTest, bindInDocumentFindsFeatureByUidAcrossFiles)
{
    Part::Box* boxA = makeBox();

    std::string plusXOnA;
    for (int i = 1; i <= 6; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        if (capturePrimitiveFaceRole(*boxA, sub) == "+X") {
            plusXOnA = sub;
        }
    }
    ASSERT_FALSE(plusXOnA.empty());
    const std::string stored = toNeutralString(captureFaceRef(*boxA, plusXOnA));

    // Doc B: a second file. The same authored box carries the same Uid (its identity
    // rode across the branch), but the rebuild numbered its faces differently.
    App::Document* docB = App::GetApplication().newDocument("neutralRefMergeTarget");
    auto* boxB = docB->addObject<Part::Box>();
    boxB->Length.setValue(30.0);
    boxB->Width.setValue(20.0);
    boxB->Height.setValue(10.0);
    docB->recompute();
    boxB->Uid.setValue(boxA->Uid.getValueStr());
    boxB->Shape.setValue(withReversedFaceOrder(boxA->Shape.getValue()));

    // Consume the stored reference against the whole document -- no pointer to boxB.
    const NRefBinding bound = bindInDocument(fromNeutralString(stored), *docB);
    ASSERT_NE(bound.feature, nullptr);
    EXPECT_EQ(bound.feature, boxB);  // found by Uid alone
    ASSERT_FALSE(bound.subName.empty());
    EXPECT_EQ(capturePrimitiveFaceRole(*boxB, bound.subName), "+X");  // the correct physical face

    App::GetApplication().closeDocument(docB->getName());
}

// A reference whose feature is absent from the target document binds to nothing --
// no feature carries its Uid, so there is no face to guess at.
TEST_F(NeutralRefTest, bindInDocumentReportsUnboundWhenFeatureAbsent)
{
    Part::Box* box = makeBox();
    const std::string stored = toNeutralString(captureFaceRef(*box, "Face1"));

    App::Document* other = App::GetApplication().newDocument("neutralRefEmptyTarget");
    const NRefBinding bound = bindInDocument(fromNeutralString(stored), *other);
    EXPECT_EQ(bound.feature, nullptr);
    EXPECT_TRUE(bound.subName.empty());

    App::GetApplication().closeDocument(other->getName());
}

// The DERIVED regime across files. A cut creates walls that no primitive owns; each
// carries a provenance name in the element map that embeds the OPERAND's per-document
// object tag. Captured, neutralized (tag -> durable Uid), serialized on branch A, the
// reference binds the same wall on a second document -- and the RAW provenance name,
// still carrying branch A's tags, does NOT, proving portability comes from the
// neutralization, not from the name happening to match.
TEST_F(NeutralRefTest, derivedFaceBindsAcrossFilesWhereRawProvenanceFails)
{
    const CutDesign a = buildCut(_doc);

    // Pick a face the cut genuinely created (its provenance names the CUT op).
    std::string cutFaceOnA;
    NRef ref;
    const int faceCount = static_cast<int>(a.cut->Shape.getShape().countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= faceCount; ++i) {
        const std::string sub = "Face" + std::to_string(i);
        const NRef r = captureFaceRef(*a.cut, sub);
        if (r.prov.find(";:M;CUT") != std::string::npos) {
            cutFaceOnA = sub;
            ref = r;
            break;
        }
    }
    ASSERT_FALSE(cutFaceOnA.empty()) << "no cut-created wall found";
    EXPECT_TRUE(ref.role.empty()) << "a derived face carries no primitive role";
    ASSERT_FALSE(ref.prov.empty());

    const std::string stored = toNeutralString(ref);

    // Branch B: the same authored design in a SECOND document -- same durable Uids
    // (identities rode across the fork), its own per-document object tags.
    App::Document* docB = App::GetApplication().newDocument("neutralRefCutTarget");
    const CutDesign b = buildCut(docB);
    b.base->Uid.setValue(a.base->Uid.getValueStr());
    b.tool->Uid.setValue(a.tool->Uid.getValueStr());
    b.cut->Uid.setValue(a.cut->Uid.getValueStr());

    // The RAW provenance name (branch A's tags) must NOT bind on branch B.
    const Data::MappedName rawA = a.cut->Shape.getShape().getMappedName(
        Data::IndexedName(cutFaceOnA.c_str())
    );
    ASSERT_FALSE(rawA.empty());
    // Positive control: the raw name resolves on its OWN document (so the null on B
    // below is specifically the tag mismatch, not a name the map never resolves).
    EXPECT_EQ(a.cut->Shape.getShape().getIndexedName(rawA).toString(), cutFaceOnA);
    EXPECT_TRUE(b.cut->Shape.getShape().getIndexedName(rawA).isNull())
        << "raw provenance name carrying branch A tags must not bind on branch B";

    // The neutralized reference DOES bind: found by Uid, resolved by provenance.
    const NRefBinding bound = bindInDocument(fromNeutralString(stored), *docB);
    ASSERT_NE(bound.feature, nullptr);
    EXPECT_EQ(bound.feature, b.cut);
    ASSERT_FALSE(bound.subName.empty());
    // The bound face carries the same neutral provenance identity as the reference.
    EXPECT_EQ(captureFaceRef(*b.cut, bound.subName).prov, ref.prov);

    App::GetApplication().closeDocument(docB->getName());
}

// The reference layer is keyed on the UNPLACED shape base, not on the placed
// Part::Feature. This matters because every derived feature -- a boolean here, and
// the whole PartDesign feature line by the same rule -- holds no authored placement
// (Amendment 4) and so is not a Part::Feature at all. Keyed on the placed class, the
// document scan below simply does not see such a feature and the binding comes back
// empty: references into every derived feature in a document are silently lost.
//
// The type assertions state the premise the keying rests on, so a future narrowing
// of the layer back to Part::Feature fails here loudly rather than going quiet.
TEST_F(NeutralRefTest, bindInDocumentFindsAnUnplacedDerivedFeature)
{
    const CutDesign a = buildCut(_doc);

    // Premise: the cut is a shape feature, but NOT a placed one.
    ASSERT_TRUE(a.cut->isDerivedFrom<Part::ShapeFeature>());
    EXPECT_FALSE(a.cut->isDerivedFrom<Part::Feature>());
    EXPECT_FALSE(a.cut->holdsAuthoredPlacement());
    // Its operand, a primitive, IS an anchor -- the contrast the rule turns on.
    EXPECT_TRUE(a.base->holdsAuthoredPlacement());

    // A reference into the derived feature survives a round trip through the file
    // form and binds against the document by Uid.
    const NRef ref = captureFaceRef(*a.cut, "Face1");
    ASSERT_EQ(ref.kind, "face");
    ASSERT_EQ(ref.featureUid, a.cut->Uid.getValueStr());

    const NRefBinding bound = bindInDocument(fromNeutralString(toNeutralString(ref)), *_doc);
    ASSERT_NE(bound.feature, nullptr) << "the derived feature was not found by the document scan";
    EXPECT_EQ(bound.feature, a.cut);
    EXPECT_EQ(bound.subName, "Face1");
}
