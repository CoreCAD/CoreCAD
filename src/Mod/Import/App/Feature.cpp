// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <string>
#endif

#include <QCryptographicHash>
#include <QFile>
#include <QString>

#include <Standard_Failure.hxx>
#include <TDF_LabelSequence.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Base/FileInfo.h>

#include "Feature.h"
#include "ReaderGltf.h"
#include "ReaderIges.h"
#include "ReaderStep.h"

using namespace Import;

PROPERTY_SOURCE(Import::Feature, Part::Feature)

Feature::Feature()
{
    ADD_PROPERTY_TYPE(SourceFile, (""), "Import", App::Prop_None, "File this geometry was translated from");
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

TopoDS_Shape Feature::translate(const Base::FileInfo& file)
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

    TDF_LabelSequence roots;
    XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetFreeShapes(roots);

    // One free shape is the whole answer for a file holding a single part. More
    // than one means the file is an assembly of separately-named things, and
    // this feature has no way yet to say which of them it is -- see the class
    // note. Fusing them into one compound would answer by destroying the very
    // distinction the addressing has to preserve, so refuse instead.
    if (roots.Length() != 1) {
        app->Close(doc);
        throw Base::RuntimeError(
            roots.IsEmpty() ? "The file holds no shape"
                            : "The file holds more than one top-level shape; importing an "
                              "assembly as a single feature is not supported"
        );
    }

    const TopoDS_Shape shape = XCAFDoc_DocumentTool::ShapeTool(doc->Main())->GetShape(roots.First());
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
    try {
        shape = translate(file);
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

    // Record the contents this geometry was actually built from, so the stored
    // fingerprint always describes the shape being held rather than whatever was
    // asked for. Writing it only on a change keeps the next recompute a no-op.
    const std::string digest = hashFile(path);
    if (digest != SourceHash.getStrValue()) {
        SourceHash.setValue(digest);
    }

    return App::DocumentObject::StdReturn;
}
