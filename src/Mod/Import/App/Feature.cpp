// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <map>
# include <set>
# include <string>
# include <vector>
#endif

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <Standard_Failure.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Mod/Part/App/SubShapeSignature.h>

#include "Feature.h"
#include "ReaderGltf.h"
#include "ReaderIges.h"
#include "ReaderStep.h"
#include "Tools.h"

using namespace Import;

PROPERTY_SOURCE(Import::Feature, Part::Feature)

Feature::Feature()
{
    ADD_PROPERTY_TYPE(SourceFile, (""), "Import", App::Prop_None, "File this geometry was translated from");
    ADD_PROPERTY_TYPE(
        SourceNode,
        (""),
        "Import",
        App::Prop_ReadOnly,
        "Node of the source file this geometry was read from; empty means the whole file"
    );
    ADD_PROPERTY_TYPE(
        SourceNodeName,
        (""),
        "Import",
        App::Prop_ReadOnly,
        "Name that node carried when it was read"
    );
    ADD_PROPERTY_TYPE(
        SourceHash,
        (""),
        "Import",
        App::Prop_ReadOnly,
        "Fingerprint of the source file's contents, as read"
    );
    ADD_PROPERTY_TYPE(
        TranslatorSettings,
        (),
        "Import",
        App::Prop_ReadOnly,
        "Translator options this geometry was read under"
    );
    // Derived from the shape, not authored: read-only to the user, hidden from the
    // property view (there is one entry per face, which no one reads by hand), and
    // marked as output so writing it does not mark the feature as edited.
    ADD_PROPERTY_TYPE(
        FaceIdentities,
        (),
        "Import",
        App::PropertyType(App::Prop_ReadOnly | App::Prop_Hidden | App::Prop_Output),
        "Identity of each face this import produced, and the signature it now carries"
    );
    ADD_PROPERTY_TYPE(
        AmbiguousFaces,
        (),
        "Import",
        App::PropertyType(App::Prop_ReadOnly | App::Prop_Hidden | App::Prop_Output),
        "Face identities the last read found more than one candidate for"
    );
    ADD_PROPERTY_TYPE(
        LostFaces,
        (),
        "Import",
        App::PropertyType(App::Prop_ReadOnly | App::Prop_Hidden | App::Prop_Output),
        "Face identities the last read could not find"
    );
}

Feature::FaceMatch Feature::matchFaceIdentities()
{
    FaceMatch report;

    // The signature is read in the feature's own frame -- the shape's location taken
    // back off every face -- because where the part sits is not part of what a face
    // is. Reading it in place would mean dragging an import across the screen lost
    // the identity of every face it has, and would make the very first re-read of an
    // assembly part disagree with the read that produced it, since the importer
    // records before the node's transform is attached and the re-read records after.
    // It is the same frame the reference layer captures in, so an identity recorded
    // here and a reference captured on that face agree about what the face is.
    const Part::TopoShape& stored = Shape.getShape();
    if (stored.isNull()) {
        return report;
    }
    const TopLoc_Location toOwnFrame = stored.getShape().Location().Inverted();

    // How many faces of the shape in hand carry each signature. A count above one
    // is the whole reason the ambiguous case exists: two faces answering to one
    // identity cannot be told apart by geometry alone.
    std::map<std::string, int> present;
    const int faceCount = static_cast<int>(stored.countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= faceCount; ++i) {
        const TopoDS_Shape face = stored.getSubShape(TopAbs_FACE, i, /*silent*/ true);
        if (face.IsNull()) {
            continue;
        }
        const std::string signature = Part::subShapeSignature(face.Moved(toOwnFrame));
        if (!signature.empty()) {
            ++present[signature];
        }
    }

    // Ask of every identity on record what became of it.
    std::map<std::string, std::string> identities;
    std::set<std::string> claimed;
    for (const auto& entry : FaceIdentities.getValues()) {
        const std::string& identity = entry.first;
        const std::string& lastSeen = entry.second;
        const auto found = present.find(lastSeen);
        const int count = found == present.end() ? 0 : found->second;

        if (count == 1) {
            identities.emplace(identity, lastSeen);
            claimed.insert(lastSeen);
            ++report.carried;
        }
        else if (count > 1) {
            // Set aside, not guessed at, and not silently dropped either: the
            // identity is real, it is which face carries it that is unsettled. The
            // faces answering to it stay unidentified for the same reason -- handing
            // one of them the identity is the guess, and handing them new identities
            // would say the old one is gone when it is merely contested.
            report.ambiguous.push_back(identity);
            claimed.insert(lastSeen);
        }
        else {
            report.lost.push_back(identity);
        }
    }

    // Whatever no identity accounted for is new in this revision, and gets an
    // identity of its own. A signature already claimed above is not new: it is the
    // face an existing identity just carried onto.
    for (const auto& face : present) {
        if (claimed.count(face.first) > 0) {
            continue;
        }
        if (identities.emplace(face.first, face.first).second) {
            ++report.added;
        }
    }

    if (identities != FaceIdentities.getValues()) {
        FaceIdentities.setValues(identities);
    }
    if (report.ambiguous != AmbiguousFaces.getValues()) {
        AmbiguousFaces.setValues(report.ambiguous);
    }
    if (report.lost != LostFaces.getValues()) {
        LostFaces.setValues(report.lost);
    }
    return report;
}

std::string Feature::hashFile(const char* path)
{
    if (!path || *path == '\0') {
        return {};
    }

    QFile file(QString::fromUtf8(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha1);
    if (!hasher.addData(&file)) {
        return {};
    }

    const QByteArray digest = hasher.result().toHex();
    return {digest.constData(), static_cast<std::size_t>(digest.size())};
}

bool Feature::refreshSourceHash()
{
    const std::string digest = hashFile(SourceFile.getValue());
    if (digest == SourceHash.getStrValue()) {
        return false;
    }

    SourceHash.setValue(digest);
    return true;
}

namespace
{

/// The label a stored address points at, or a null label when it does not match.
TDF_Label labelAt(const Handle(TDocStd_Document) & doc, const std::string& node, const std::string& nodeName)
{
    if (node.empty()) {
        return {};
    }

    TDF_Label label;
    TDF_Tool::Label(doc->GetData(), node.c_str(), label);
    if (label.IsNull()) {
        return {};
    }
    if (!nodeName.empty() && Import::Tools::labelName(label) != nodeName) {
        // Something else stands here now -- almost always because a part was
        // added ahead of this one and shifted every position after it.
        return {};
    }
    return label;
}

/// Every shape label in the file carrying the given name.
TDF_LabelSequence labelsNamed(const Handle(XCAFDoc_ShapeTool) & shapes, const std::string& nodeName)
{
    TDF_LabelSequence all;
    shapes->GetShapes(all);

    TDF_LabelSequence found;
    for (Standard_Integer i = 1; i <= all.Length(); ++i) {
        if (Import::Tools::labelName(all.Value(i)) == nodeName) {
            found.Append(all.Value(i));
        }
    }
    return found;
}

}  // namespace

TopoDS_Shape Feature::translate(
    const Base::FileInfo& file,
    const std::string& node,
    const std::string& nodeName,
    std::string* resolvedNode
)
{
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument(TCollection_ExtendedString("MDTV-CAF"), doc);

    if (file.hasExtension({"stp", "step"})) {
        ReaderStep(file).read(doc);
    }
    else if (file.hasExtension({"igs", "iges"})) {
        ReaderIges(file).read(doc);
    }
    else if (file.hasExtension({"glb", "gltf"})) {
        ReaderGltf(file).read(doc);
    }
    else {
        app->Close(doc);
        throw Base::RuntimeError("No translator for this file format");
    }

    Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());

    if (!node.empty() || !nodeName.empty()) {
        TDF_Label label = labelAt(doc, node, nodeName);

        if (label.IsNull() && !nodeName.empty()) {
            // The position no longer holds what it held, so go by name. One match
            // is the part; none or several is a question this cannot answer on
            // its own, and guessing is how a chamfer silently lands on the wrong
            // face three revisions later.
            const TDF_LabelSequence named = labelsNamed(shapes, nodeName);
            if (named.Length() == 1) {
                label = named.First();
            }
            else {
                app->Close(doc);
                throw Base::RuntimeError(
                    named.IsEmpty() ? "The source file no longer holds a node named '" + nodeName + "'"
                                    : "The source file now holds several nodes named '" + nodeName
                            + "'; which one this is cannot be decided without asking"
                );
            }
        }

        // Hand back where the node was actually found, so a caller storing an
        // address can correct one the file has moved rather than keep pointing at
        // a position that now means something else.
        if (resolvedNode && !label.IsNull()) {
            TCollection_AsciiString entry;
            TDF_Tool::Entry(label, entry);
            *resolvedNode = entry.ToCString();
        }

        // A reference is an instance placed in an assembly; the geometry lives on
        // the prototype it points at, so follow the chain to the part itself.
        while (!label.IsNull() && XCAFDoc_ShapeTool::IsReference(label)) {
            TDF_Label referred;
            if (!XCAFDoc_ShapeTool::GetReferredShape(label, referred)) {
                break;
            }
            label = referred;
        }
        if (label.IsNull() || !shapes->IsShape(label)) {
            app->Close(doc);
            throw Base::RuntimeError("The source file no longer holds this node");
        }
        const TopoDS_Shape located = shapes->GetShape(label);
        app->Close(doc);
        return located;
    }

    TDF_LabelSequence roots;
    shapes->GetFreeShapes(roots);

    // One free shape is the whole answer for a file holding a single part. More
    // than one, with no node named, means the caller has not said which of the
    // file's separately-named things it wants. Fusing them into one compound
    // would answer by destroying the very distinction the address preserves, so
    // refuse instead.
    if (roots.Length() != 1) {
        app->Close(doc);
        throw Base::RuntimeError(
            roots.IsEmpty() ? "The file holds no shape"
                            : "The file holds more than one top-level shape; importing an "
                              "assembly as a single feature is not supported"
        );
    }

    const TopoDS_Shape shape = shapes->GetShape(roots.First());
    app->Close(doc);
    return shape;
}

App::DocumentObjectExecReturn* Feature::execute()
{
    const char* path = SourceFile.getValue();
    if (!path || *path == '\0') {
        // No source declared, so there is nothing to re-read; whatever shape the
        // object was handed stands.
        return Part::Feature::execute();
    }

    Base::FileInfo file(path);
    if (!file.exists()) {
        return new App::DocumentObjectExecReturn("Source file not found", this);
    }

    TopoDS_Shape shape;
    std::string resolvedNode;
    try {
        shape = translate(file, SourceNode.getStrValue(), SourceNodeName.getStrValue(), &resolvedNode);
    }
    catch (const Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString(), this);
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what(), this);
    }

    if (shape.IsNull()) {
        return new App::DocumentObjectExecReturn("The translated shape is empty", this);
    }

    Shape.setValue(shape);

    // What was read is now matched against what was read before, so the import can
    // say what this revision did to the faces it had -- and say it once, here, in
    // place of every downstream feature discovering it separately.
    const FaceMatch match = matchFaceIdentities();
    if (!match.lost.empty() || !match.ambiguous.empty()) {
        Base::Console().warning(
            "%s: %d faces carried over from the previous read, %d added, %d no longer "
            "present, %d matching more than one face. Anything built on a face that is "
            "gone will say so at its own feature.\n",
            Label.getValue(),
            match.carried,
            match.added,
            static_cast<int>(match.lost.size()),
            static_cast<int>(match.ambiguous.size())
        );
    }

    if (!resolvedNode.empty() && resolvedNode != SourceNode.getStrValue()) {
        SourceNode.setValue(resolvedNode);
    }

    // Record the contents this geometry was actually built from, so the stored
    // fingerprint always describes the shape being held rather than whatever was
    // asked for. Writing it only on a change keeps the next recompute a no-op.
    const std::string digest = hashFile(path);
    if (digest != SourceHash.getStrValue()) {
        SourceHash.setValue(digest);
    }

    return App::DocumentObject::StdReturn;
}
