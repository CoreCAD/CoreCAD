// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <bitset>
#include <stack>
#include <deque>
#include <iostream>
#include <utility>
#include <set>
#include <memory>
#include <new>
#include <string>
#include <map>
#include <vector>
#include <list>
#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>

#include <boost/algorithm/string.hpp>
#include <boost/bimap.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>

#include <boost/regex.hpp>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include <QCryptographicHash>
#include <QCoreApplication>

#include <FCConfig.h>

#include <App/DocumentPy.h>
#include <Base/Interpreter.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/TimeInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <Base/Profiler.h>
#include <Base/Tools.h>
#include <Base/XMLTools.h>
#include <Base/Uuid.h>
#include <Base/Sequencer.h>
#include <Base/Stream.h>

#include "Document.h"
#include "RecipeText.h"
#include "private/DocumentP.h"
#include "Application.h"
#include "AutoTransaction.h"
#include "BackupPolicy.h"
#include "ExpressionParser.h"
#include "GeoFeature.h"
#include "License.h"
#include "Link.h"
#include "Origin.h"
#include "MergeDocuments.h"
#include "StringHasher.h"
#include "GeometryCache.h"
#include "SealedArchive.h"
#include "StoredRecipe.h"
#include "Transactions.h"



FC_LOG_LEVEL_INIT("App", true, true, true)

using Base::Console;
using Base::streq;
using Base::Writer;
using namespace App;
using namespace boost;

#if FC_DEBUG
#define FC_LOGFEATUREUPDATE
#endif

namespace fs = std::filesystem;

namespace App
{

bool globalIsRestoring;
static bool globalIsRelabeling;

DocumentP::DocumentP()
{
    static std::random_device rd;
    static std::mt19937 rgen(rd());
    static std::uniform_int_distribution<> rdist(0, 5000);
    // Set some random offset to reduce likelihood of ID collision when
    // copying shape from other document. It is probably better to randomize
    // on each object ID.
    lastObjectId = rdist(rgen);
    StatusBits.set((size_t)Document::Closable, true);
    StatusBits.set((size_t)Document::KeepTrailingDigits, true);
    StatusBits.set((size_t)Document::Restoring, false);
}

}  // namespace App

PROPERTY_SOURCE(App::Document, App::PropertyContainer)

bool Document::testStatus(const Status pos) const
{
    return d->StatusBits.test(static_cast<size_t>(pos));
}

void Document::setStatus(const Status pos, const bool on) // NOLINT
{
    d->StatusBits.set(static_cast<size_t>(pos), on);
}

// bool _has_cycle_dfs(const DependencyList & g, vertex_t u, default_color_type * color)
//{
//   color[u] = gray_color;
//   graph_traits < DependencyList >::adjacency_iterator vi, vi_end;
//   for (tie(vi, vi_end) = adjacent_vertices(u, g); vi != vi_end; ++vi)
//     if (color[*vi] == white_color)
//       if (has_cycle_dfs(g, *vi, color))
//         return true;            // cycle detected, return immediately
//       else if (color[*vi] == gray_color)        // *vi is an ancestor!
//         return true;
//   color[u] = black_color;
//   return false;
// }

bool Document::checkOnCycle()
{
    return false;
}

void Document::clearDocument() // NOLINT
{
    d->activeObject = nullptr;

    if (!d->objectArray.empty()) {
        GetApplication().signalDeleteDocument(*this);
        d->clearDocument();
        GetApplication().signalNewDocument(*this, false);
    }

    Base::FlagToggler<> flag(globalIsRestoring, false);

    setStatus(Document::PartialDoc, false);

    d->clearRecomputeLog();
    d->objectLabelManager.clear();
    d->objectArray.clear();
    d->objectMap.clear();
    d->objectNameManager.clear();
    d->objectIdMap.clear();
    d->objectUuidMap.clear();
    d->objectUuidMapDirty = true;
    d->lastObjectId = 0;
}


void Document::onBeforeChange(const Property* prop)
{
    if (prop == &Label) {
        oldLabel = Label.getValue();
    }
    // Content-scope marker is set-once (ARCHITECTURE §7.1, Amendment 8 Clause 8.2):
    // writable while the document is untyped (empty marker), frozen the moment it is
    // stamped. onBeforeChange still sees the pre-change value, so a non-empty current
    // value means this is a re-stamp — refuse it loud. The first stamp (from empty, via
    // applyDocumentType at creation) passes untouched. Restore is exempt: the on-disk
    // file is authoritative and re-restores the marker onto the same in-memory Document
    // on reload; the load door already validates that file's content scope.
    else if (prop == &DocumentType && !globalIsRestoring
             && !DocumentType.getStrValue().empty()) {
        throw Base::RuntimeError(
            "DocumentType marker is set-once and cannot be changed once stamped (current '"
            + DocumentType.getStrValue() + "').");
    }
    signalBeforeChange(*this, *prop);
}

void Document::onChanged(const Property* prop)
{
    if (prop != nullptr && !testStatus(Restoring) && theRecipeCarries(*prop, *this)) {
        d->movedOnFromItsFile = true;
    }
    signalChanged(*this, *prop);

    // the Name property is a label for display purposes
    if (prop == &Label) {
        Base::FlagToggler<> flag(globalIsRelabeling);
        GetApplication().signalRelabelDocument(*this);
    }
    else if (prop == &ShowHidden) {
        GetApplication().signalShowHidden(*this);
    }
    else if (prop == &Uid) {
        std::string new_dir =
            getTransientDirectoryName(this->Uid.getValueStr(), this->FileName.getStrValue());
        std::string old_dir = this->TransientDir.getStrValue();
        Base::FileInfo TransDirNew(new_dir);
        Base::FileInfo TransDirOld(old_dir);
        // this directory should not exist
        if (!TransDirNew.exists()) {
            if (TransDirOld.exists()) {
                if (!TransDirOld.renameFile(new_dir.c_str())) {
                    Base::Console().warning("Failed to rename '%s' to '%s'\n",
                                            old_dir.c_str(),
                                            new_dir.c_str());
                }
                else {
                    this->TransientDir.setValue(new_dir);
                }
            }
            else {
                if (!TransDirNew.createDirectories()) {
                    Base::Console().warning("Failed to create '%s'\n", new_dir.c_str());
                }
                else {
                    this->TransientDir.setValue(new_dir);
                }
            }
        }
        // when reloading an existing document the transient directory doesn't change
        // so we must avoid to generate a new uuid
        else if (TransDirNew.filePath() != TransDirOld.filePath()) {
            // make sure that the uuid is unique
            std::string uuid = this->Uid.getValueStr();
            Base::Uuid id;
            Base::Console().warning("Document with the UUID '%s' already exists, change to '%s'\n",
                                    uuid.c_str(),
                                    id.getValue().c_str());
            // recursive call of onChanged()
            this->Uid.setValue(id);
        }
    }
    else if (prop == &UseHasher) {
        for (auto obj : d->objectArray) {
            auto geofeature = freecad_cast<GeoFeature*>(obj);
            if (geofeature && geofeature->getPropertyOfGeometry()) {
                geofeature->enforceRecompute();
            }
        }
    }
}

void Document::onBeforeChangeProperty(const TransactionalObject* Who, const Property* What)
{
    if (Who->isDerivedFrom<DocumentObject>()) {
        signalBeforeChangeObject(*static_cast<const DocumentObject*>(Who), *What);
    }
    if (!d->rollback && !globalIsRelabeling) {
        _checkTransaction(nullptr, What, __LINE__);
        if (d->activeUndoTransaction) {
            d->activeUndoTransaction->addObjectChange(Who, What);
        }
    }
}

void Document::onChangedProperty(const DocumentObject* Who, const Property* What)
{
    // The durable-UUID index (Clause 3.6) keys on Uid, which is overwritten when
    // it is restored from file; mark the index stale so the next lookup rebuilds.
    if (What == &Who->Uid) {
        d->objectUuidMapDirty = true;
    }
    // Only a value the recipe carries makes the document differ from its file. A rebuild
    // produces solids the file never held, so producing one is not a disagreement with it --
    // and a document that called itself changed every time it recomputed could never say when
    // it had actually changed.
    // Reading from the file is the one thing that cannot be a disagreement with it. Everything
    // else counts, undo and import included: both move the document away from what was written.
    if (What != nullptr && Who != nullptr && !testStatus(Restoring)
        && theRecipeCarries(*What, *Who)) {
        d->movedOnFromItsFile = true;
    }
    signalChangedObject(*Who, *What);
}

void Document::setTransactionMode(const int iMode) // NOLINT
{
    d->iTransactionMode = iMode;
}

//--------------------------------------------------------------------------
// constructor
//--------------------------------------------------------------------------
Document::Document(const char* documentName)
    : d(new DocumentP), myName(documentName)
{
    // Remark: In a constructor we should never increment a Python object as we cannot be sure
    // if the Python interpreter gets a reference of it. E.g. if we increment but Python don't
    // get a reference then the object wouldn't get deleted in the destructor.
    // So, we must increment only if the interpreter gets a reference.
    // Remark: We force the document Python object to own the DocumentPy instance, thus we don't
    // have to care about ref counting any more.
    setAutoCreated(false);
    Base::PyGILStateLocker lock;
    d->DocumentPythonObject = Py::Object(new DocumentPy(this), true);

#ifdef FC_LOGUPDATECHAIN
    Console().log("+App::Document: %p\n", this);
#endif
    std::string CreationDateString = Base::Tools::currentDateTimeString();
    std::string Author = GetApplication()
                             .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                             ->GetASCII("prefAuthor", "");
    std::string AuthorComp =
        GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
            ->GetASCII("prefCompany", "");
    ADD_PROPERTY_TYPE(Label, ("Unnamed"), 0, Prop_ReadOnly, "The name of the document");
    ADD_PROPERTY_TYPE(FileName,
                      (""),
                      0,
                      PropertyType(Prop_Transient | Prop_ReadOnly),
                      "The path to the file where the document is saved to");
    ADD_PROPERTY_TYPE(CreatedBy, (Author.c_str()), 0, Prop_None, "The creator of the document");
    ADD_PROPERTY_TYPE(CreationDate,
                      (CreationDateString.c_str()),
                      0,
                      Prop_ReadOnly,
                      "Date of creation");
    ADD_PROPERTY_TYPE(LastModifiedBy, (""), 0, Prop_None, 0);
    // Cruth: the last-modified date is DERIVED, never stored.
    //
    // A save-time clock written into the document made the file a function of when it was
    // saved rather than of what it contains: two saves of an unchanged document differed,
    // and every real save carried a spurious change into the file's history alongside the
    // genuine one. Storing it also duplicated something already known -- the file's own
    // modification time -- which is a second source of truth for a fact nobody authored.
    //
    // The property remains, because consumers legitimately read it (a drawing title block,
    // a manufacturing report). It is filled from the file it was read from, and refreshed
    // in memory on save. What a person *does* author about a revision belongs in the
    // document's metadata, not in a clock.
    //
    // CreationDate stays stored: it is set once and never moves, so it does not make the
    // file depend on when it was written.
    ADD_PROPERTY_TYPE(LastModifiedDate,
                      ("Unknown"),
                      0,
                      PropertyType(Prop_Transient | Prop_ReadOnly),
                      "Date of last modification, derived from the file (never stored)");
    ADD_PROPERTY_TYPE(Company,
                      (AuthorComp.c_str()),
                      0,
                      Prop_None,
                      "Additional tag to save the name of the company");
    ADD_PROPERTY_TYPE(Comment, (""), 0, Prop_None, "Additional tag to save a comment");
    ADD_PROPERTY_TYPE(Meta, (), 0, Prop_None, "Map with additional meta information");
    ADD_PROPERTY_TYPE(Material, (), 0, Prop_None, "Map with material properties");
    // create the uuid for the document
    Base::Uuid id;
    ADD_PROPERTY_TYPE(Id, (""), 0, Prop_None, "ID of the document");
    ADD_PROPERTY_TYPE(Uid, (id), 0, Prop_ReadOnly, "UUID of the document");
    // Cruth: document-type marker. Empty = legacy/untyped; a Part document carries "Part".
    ADD_PROPERTY_TYPE(DocumentType,
                      (""),
                      0,
                      Prop_None,
                      "Cruth document-type marker (e.g. \"Part\"); drives content scoping");

    // license stuff
    auto paramGrp {GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Document")};
    auto index = static_cast<int>(paramGrp->GetInt("prefLicenseType", 0));
    auto name = "";
    std::string licenseUrl = "";
    if (index >= 0 && index < countOfLicenses) {
        name = licenseItems.at(index).at(posnOfFullName);
        auto url = licenseItems.at(index).at(posnOfUrl);
        licenseUrl = (paramGrp->GetASCII("prefLicenseUrl", url));
    }
    ADD_PROPERTY_TYPE(License, (name), 0, Prop_None, "License string of the Item");
    ADD_PROPERTY_TYPE(LicenseURL,
                      (licenseUrl.c_str()),
                      0,
                      Prop_None,
                      "URL to the license text/contract");
    ADD_PROPERTY_TYPE(ShowHidden,
                      (false),
                      0,
                      PropertyType(Prop_None),
                      "Whether to show hidden object items in the tree view");
    ADD_PROPERTY_TYPE(UseHasher,
                      (true),
                      0,
                      PropertyType(Prop_Hidden),
                      "Whether to use hasher on topological naming");

    // this creates and sets 'TransientDir' in onChanged()
    ADD_PROPERTY_TYPE(TransientDir,
                      (""),
                      0,
                      PropertyType(Prop_Transient | Prop_ReadOnly),
                      "Transient directory, where the files live while the document is open");
    ADD_PROPERTY_TYPE(Tip,
                      (nullptr),
                      0,
                      PropertyType(Prop_Transient),
                      "Link of the tip object of the document");
    ADD_PROPERTY_TYPE(TipName,
                      (""),
                      0,
                      PropertyType(Prop_Hidden | Prop_ReadOnly),
                      "Link of the tip object of the document");
    Uid.touch();
}

Document::~Document()
{
#ifdef FC_LOGUPDATECHAIN
    Console().log("-App::Document: %s %p\n", getName(), this);
#endif

    try {
        clearUndos();
    }
    catch (const boost::exception&) {
    }

#ifdef FC_LOGUPDATECHAIN
    Console().log("-Delete Features of %s \n", getName());
#endif

    d->clearDocument();

    // Remark: The API of Py::Object has been changed to set whether the wrapper owns the passed
    // Python object or not. In the constructor we forced the wrapper to own the object so we need
    // not to dec'ref the Python object any more.
    // But we must still invalidate the Python object because it doesn't need to be
    // destructed right now because the interpreter can own several references to it.
    Base::PyGILStateLocker lock;
    auto* doc = static_cast<Base::PyObjectBase*>(d->DocumentPythonObject.ptr());
    // Call before decrementing the reference counter, otherwise a heap error can occur
    doc->setInvalid();

    // remove Transient directory
    try {
        const Base::FileInfo TransDir(TransientDir.getValue());
        TransDir.deleteDirectoryRecursive();
    }
    catch (const Base::Exception& e) {
        std::cerr << "Removing transient directory failed: " << e.what() << '\n';
    }
    delete d;
}

std::string Document::getTransientDirectoryName(const std::string& uuid,
                                                const std::string& filename) const
{
    // Create a directory name of the form: {ExeName}_Doc_{UUID}_{HASH}_{PID}
    std::stringstream out;
    QCryptographicHash hash(QCryptographicHash::Sha1);
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    hash.addData(filename.c_str(), filename.size());
#else
    hash.addData(QByteArrayView(filename.c_str(), filename.size()));
#endif
    out << Application::getUserCachePath() << Application::getExecutableName() << "_Doc_"
        << uuid << "_" << hash.result().toHex().left(6).constData() << "_"
        << Application::applicationPid();
    return out.str();
}

//--------------------------------------------------------------------------
// Exported functions
//--------------------------------------------------------------------------

unsigned int Document::getMemSize() const
{
    unsigned int size = 0;

    // size of the DocObjects in the document
    for (const auto & it : d->objectArray) {
        size += it->getMemSize();
    }

    size += d->Hasher->getMemSize();

    // size of the document properties...
    size += PropertyContainer::getMemSize();

    // Undo Redo size
    size += getUndoMemSize();

    return size;
}

void Document::registerLabel(const std::string& newLabel)
{
    if (!newLabel.empty()) {
        d->objectLabelManager.addExactName(newLabel);
    }
}

void Document::unregisterLabel(const std::string& oldLabel)
{
    if (!oldLabel.empty()) {
        d->objectLabelManager.removeExactName(oldLabel);
    }
}

bool Document::containsLabel(const std::string& label)
{
    return d->objectLabelManager.containsName(label);
}

std::string Document::makeUniqueLabel(const std::string& modelLabel)
{
    if (modelLabel.empty()) {
        return {};
    }

    return d->objectLabelManager.makeUniqueName(modelLabel, 3);
}

bool Document::statesWhatItsFileStates() const
{
    return !d->movedOnFromItsFile;
}

bool Document::isSaved() const
{
    const std::string name = FileName.getValue();
    return !name.empty();
}

/** Label is the visible name of a document shown e.g. in the windows title
 * or in the tree view. The label almost (but not always e.g. if you manually change it)
 * matches with the file name where the document is stored to.
 * In contrast to Label the method getName() returns the internal name of the document that only
 * matches with Label when loading or creating a document because then both are set to the same
 * value. Since the internal name cannot be changed during runtime it must differ from the Label
 * after saving the document the first time or saving it under a new file name.
 * @ note More than one document can have the same label name.
 * @ note The internal is always guaranteed to be unique because @ref Application::newDocument()
 * checks for a document with the same name and makes it unique if needed. Hence you cannot rely on
 * that the internal name matches with the name you passed to Application::newDoument(). You should
 * use the method getName() instead.
 */
const char* Document::getName() const
{
    // return GetApplication().getDocumentName(this);
    return myName.c_str();
}

std::string Document::getFullName() const
{
    return myName;
}

void Document::setAutoCreated(bool value) {
    autoCreated = value;
}

bool Document::isAutoCreated() const {
    return autoCreated;
}

const char* Document::getProgramVersion() const
{
    return d->programVersion.c_str();
}

const char* Document::getFileName() const
{
    return testStatus(TempDoc) ? TransientDir.getValue() : FileName.getValue();
}

std::string Document::cacheDirectory() const
{
    const std::string file = FileName.getStrValue();
    if (file.empty()) {
        return {};
    }
    // Hidden and named once for the whole project, so a folder of parts stays a list of parts
    // and one line in a version-control ignore file covers everything that is rebuildable.
    const fs::path directory = fs::path(Base::FileInfo(file).filePath()).parent_path();
    return (directory / cacheFolderName / Uid.getValueStr()).string();
}

std::string Document::assetDirectory() const
{
    const std::string file = FileName.getStrValue();
    if (file.empty()) {
        return {};
    }
    // Visible and shared by the whole project: this is source material, and it is versioned and
    // handed over with the recipes that name it.
    const fs::path directory = fs::path(Base::FileInfo(file).filePath()).parent_path();
    return (directory / "assets").string();
}

/// Remove all modifications. After this call The document becomes valid again.
void Document::purgeTouched() // NOLINT
{
    for (const auto It : d->objectArray) {
        It->purgeTouched();
    }
}

bool Document::isTouched() const
{
    for (const auto It : d->objectArray) {
        if (It->isTouched()) {
            return true;
        }
    }
    return false;
}

std::vector<DocumentObject*> Document::getTouched() const
{
    std::vector<DocumentObject*> result;

    for (auto It : d->objectArray) {
        if (It->isTouched()) {
            result.push_back(It);
        }
    }

    return result;
}

void Document::setClosable(bool c) // NOLINT
{
    setStatus(Document::Closable, c);
}

bool Document::isClosable() const
{
    return testStatus(Document::Closable);
}

int Document::countObjects() const
{
    return static_cast<int>(d->objectArray.size());
}

DocumentObject* Document::getActiveObject() const
{
    return d->activeObject;
}

DocumentObject* Document::getObject(const char* Name) const
{
    const auto pos = d->objectMap.find(Name);

    return pos != d->objectMap.end() ?pos->second:nullptr;
}

DocumentObject* Document::getObjectByID(const long id) const
{
    const auto it = d->objectIdMap.find(id);

    return it != d->objectIdMap.end() ?it->second:nullptr;
}

DocumentObject* Document::getObjectByUuid(const Base::Uuid& uuid) const
{
    if (d->objectUuidMapDirty) {
        d->objectUuidMap.clear();
        for (auto* obj : d->objectArray) {
            const std::string& key = obj->Uid.getValueStr();
            if (!key.empty()) {
                d->objectUuidMap[key] = obj;
            }
        }
        d->objectUuidMapDirty = false;
    }

    const auto it = d->objectUuidMap.find(uuid.getValue());
    return it != d->objectUuidMap.end() ? it->second : nullptr;
}


// Note: This method is only used in Tree.cpp slotChangeObject(), see explanation there
bool Document::isIn(const DocumentObject* pFeat) const
{
    for (const auto& [key, object] : d->objectMap) {
        if (object == pFeat) {
            return true;
        }
    }

    return false;
}

const char* Document::getObjectName(const DocumentObject* pFeat) const
{
    for (const auto& [key, object] : d->objectMap) {
        if (object == pFeat) {
            return key.c_str();
        }
    }

    return nullptr;
}

std::string Document::getUniqueObjectName(const char* proposedName) const
{
    if (!proposedName || *proposedName == '\0') {
        return {};
    }
    std::string cleanName = Base::Tools::getIdentifier(proposedName);

    if (!d->objectNameManager.containsName(cleanName)) {
        // Not in use yet, name is OK
        return cleanName;
    }
    return d->objectNameManager.makeUniqueName(cleanName, 3);
}

    bool
Document::haveSameBaseName(const std::string& name, const std::string& label)
{
    // Both Labels and Names use the same decomposition rules for names,
    // i.e. the default one supplied by UniqueNameManager, so we can use either
    // of the name managers to do this test.
    return d->objectNameManager.haveSameBaseName(name, label);
}

std::string Document::getStandardObjectLabel(const char* modelName, int digitCount) const
{
    return d->objectLabelManager.makeUniqueName(modelName, digitCount);
}

std::vector<DocumentObject*> Document::getDependingObjects() const
{
    return getDependencyList(d->objectArray);
}

const std::vector<DocumentObject*>& Document::getObjects() const
{
    return d->objectArray;
}

std::vector<DocumentObject*> Document::getObjectsOfType(const Base::Type& typeId) const
{
    std::vector<DocumentObject*> Objects;
    for (auto it : d->objectArray) {
        if (it->isDerivedFrom(typeId)) {
            Objects.push_back(it);
        }
    }
    return Objects;
}

std::vector<DocumentObject*> Document::getObjectsOfType(const std::vector<Base::Type>& types) const
{
    std::vector<DocumentObject*> Objects;
    for (auto it : d->objectArray) {
        for (auto& typeId : types) {
            if (it->isDerivedFrom(typeId)) {
                Objects.push_back(it);
                break; // Prevent adding several times the same object.
            }
        }
    }
    return Objects;
}

std::vector<DocumentObject*> Document::getObjectsWithExtension(const Base::Type& typeId,
                                                               const bool derived) const
{

    std::vector<DocumentObject*> Objects;
    for (auto it : d->objectArray) {
        if (it->hasExtension(typeId, derived)) {
            Objects.push_back(it);
        }
    }
    return Objects;
}


std::vector<DocumentObject*>
Document::findObjects(const Base::Type& typeId, const char* objname, const char* label) const
{
    boost::cmatch what;
    boost::regex rx_name;
    boost::regex rx_label;

    if (objname) {
        rx_name.set_expression(objname);
    }

    if (label) {
        rx_label.set_expression(label);
    }

    std::vector<DocumentObject*> Objects;
    DocumentObject* found = nullptr;
    for (const auto it : d->objectArray) {
        if (it->isDerivedFrom(typeId)) {
            found = it;

            if (!rx_name.empty() && !boost::regex_search(it->getNameInDocument(), what, rx_name)) {
                found = nullptr;
            }

            if (!rx_label.empty() && !boost::regex_search(it->Label.getValue(), what, rx_label)) {
                found = nullptr;
            }

            if (found) {
                Objects.push_back(found);
            }
        }
    }
    return Objects;
}

int Document::countObjectsOfType(const Base::Type& typeId) const
{
    return std::count_if(d->objectMap.begin(), d->objectMap.end(), [&](const auto& it) {
        return it.second->isDerivedFrom(typeId);
    });
}

int Document::countObjectsOfType(const char* typeName) const
{
    const Base::Type type = Base::Type::fromName(typeName);
    return type.isBad() ? 0 : countObjectsOfType(type);
}

PyObject* Document::getPyObject()
{
    return Py::new_reference_to(d->DocumentPythonObject);
}

std::vector<DocumentObject*> Document::getRootObjects() const
{
    std::vector<DocumentObject*> ret;

    for (auto objectIt : d->objectArray) {
        if (objectIt->getInList().empty()) {
            ret.push_back(objectIt);
        }
    }

    return ret;
}

std::vector<DocumentObject*> Document::getRootObjectsIgnoreLinks() const
{
    std::vector<DocumentObject*> ret;

    for (const auto &objectIt : d->objectArray) {
        auto list = objectIt->getInList();
        bool noParents = list.empty();

        if (!noParents) {
            // App::Document getRootObjects returns the root objects of the dependency graph.
            // So if an object is referenced by an App::Link, it will not be returned by that
            // function. So here, as we want the tree-root level objects, we check if all the
            // parents are links. In which case it's still a root object.
            noParents = std::all_of(list.cbegin(), list.cend(), [](DocumentObject* obj) {
                return obj->isDerivedFrom<Link>();
            });
        }

        if (noParents) {
            ret.push_back(objectIt);
        }
    }

    return ret;
}
