// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 The CoreCAD contributors

#ifndef IMPORT_FEATURE_H
#define IMPORT_FEATURE_H

#include <string>

#include <TopoDS_Shape.hxx>

#include <App/PropertyFile.h>
#include <App/PropertyStandard.h>
#include <Mod/Part/App/PartFeature.h>

#include <Mod/Import/ImportGlobal.h>

namespace Import
{

/**
 * Geometry read from an external file, which remembers where it came from.
 *
 * Importing is a translation, not a one-way dump: the same supplier sends
 * `bracket.step` today and `bracket_v2.step` next month, and the model built on
 * top of the first has to survive the second. That is only possible if the
 * imported geometry is a live object whose *inputs* name the source, rather than
 * a bare shape with no memory of its origin.
 *
 * The declared inputs are therefore the source file, a fingerprint of that
 * file's contents, and the translator settings used to read it. The output is
 * the translated shape. Re-import is mechanically an edit to the fingerprint:
 * the file changed, so the feature is out of date and the recompute walks
 * downstream from it like any other feature edit.
 *
 * An import authors its geometry from raw input rather than from objects it
 * references, so it is an anchor and carries a placement of its own, in the same
 * way a primitive does.
 *
 * What this type does *not* yet carry is an address *within* the source file. A
 * file holding a single part needs none — the file is the whole answer. A file
 * holding an assembly becomes many objects, and each one must record which node
 * of the file it is before it can be re-read on its own.
 */
class ImportExport Feature: public Part::Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Import::Feature);

public:
    Feature();

    /// Path to the file this geometry was translated from.
    App::PropertyFile SourceFile;
    /// Fingerprint of the source file's contents, as read.
    App::PropertyString SourceHash;
    /// Translator options this geometry was read under.
    App::PropertyMap TranslatorSettings;

    /**
     * Fingerprint of a file's contents.
     *
     * Reads the file's bytes and returns a lowercase hexadecimal SHA-1 digest,
     * the same digest the element-map string hasher uses. Returns an empty
     * string when the file cannot be read, so an unreadable source is
     * distinguishable from an empty one.
     */
    static std::string hashFile(const char* path);

    /**
     * Re-read the source file's fingerprint into SourceHash.
     *
     * Returns true when the fingerprint differs from the stored one, which is
     * exactly the condition "the source file has changed since import".
     */
    bool refreshSourceHash();

    /**
     * Reads the one shape a file holds.
     *
     * Throws when the format has no translator, when the file holds nothing, or
     * when it holds more than one top-level shape -- the assembly case, which
     * needs an address within the file before a single feature can stand for
     * one of its parts.
     */
    static TopoDS_Shape translate(const Base::FileInfo& file);

    /// Re-reads the source file into this feature's shape.
    App::DocumentObjectExecReturn* execute() override;
};

}  // namespace Import

#endif  // IMPORT_FEATURE_H
