// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <map>
# include <string>
#endif

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <Standard_Failure.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

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
}

int Feature::recordFaceIdentities()
{
    // The signature is read in the feature-local (stored) frame, the same frame the
    // reference layer captures in, so an identity recorded here and a reference
    // captured on the same face agree on what that face is.
    const Part::TopoShape& stored = Shape.getShape();
    if (stored.isNull()) {
        return 0;
    }

    std::map<std::string, std::string> identities;
    const int faceCount = static_cast<int>(stored.countSubShapes(TopAbs_FACE));
    for (int i = 1; i <= faceCount; ++i) {
        const TopoDS_Shape face = stored.getSubShape(TopAbs_FACE, i, /*silent*/ true);
        if (face.IsNull()) {
            continue;
        }
        const std::string signature = Part::subShapeSignature(face);
        if (signature.empty()) {
            continue;
        }
        // A face already recorded under this signature means two faces of one shape
        // are geometrically indistinguishable. Recording the second over the first
        // would quietly claim there is one face where there are two, so the first
        // entry stands and the count reports what was actually recorded.
        identities.emplace(signature, signature);
    }

    if (identities != FaceIdentities.getValues()) {
        FaceIdentities.setValues(identities);
    }
    return static_cast<int>(identities.size());
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

    // What was read is now on record: the import can say what faces it produced,
    // without waiting for some later feature to reference one of them.
    recordFaceIdentities();

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
