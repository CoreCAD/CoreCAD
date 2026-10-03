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

#ifdef _MSC_VER
#include <zipios++/zipios-config.h>
#endif
#include <zipios++/zipfile.h>
#include <zipios++/zipinputstream.h>
#include <zipios++/zipoutputstream.h>
#include <zipios++/meta-iostreams.h>


FC_LOG_LEVEL_INIT("App", true, true, true)

using Base::Console;
using Base::streq;
using Base::Writer;
using namespace App;
using namespace boost;
using namespace zipios;

#if FC_DEBUG
#define FC_LOGFEATUREUPDATE
#endif

namespace fs = std::filesystem;

void Document::Save(Base::Writer& writer) const
{
    d->hashers.clear();
    addStringHasher(d->Hasher);

    writer.Stream() << R"(<Document SchemaVersion="4" ProgramVersion=")"
                    << Application::Config()["BuildVersionMajor"] << "."
                    << Application::Config()["BuildVersionMinor"] << "R"
                    << Application::Config()["BuildRevision"] << "\" FileVersion=\""
                    << writer.getFileVersion() << "\" StringHasher=\"1\">\n";

    writer.incInd();

    // NOTE: This differs from LS3 Code. Persisting this table
    //       forces the assertion in Writer.addFile(...): assert(!isForceXML()); to be removed
    //       see: https://github.com/FreeCAD/FreeCAD/issues/27489
    //
    // Original code in LS3:
    //       d->Hasher->setPersistenceFileName(0);
    d->Hasher->setPersistenceFileName("StringHasher.Table");

    for (const auto o : d->objectArray) {
        o->beforeSave();
    }
    beforeSave();

    d->Hasher->Save(writer);

    writer.decInd();

    PropertyContainer::Save(writer);

    // writing the features types
    writeObjects(d->objectArray, writer);
    writer.Stream() << "</Document>" << '\n';
}

void Document::Restore(Base::XMLReader& reader)
{
    d->hashers.clear();
    d->touchedObjs.clear();
    addStringHasher(d->Hasher);
    setStatus(Document::PartialDoc, false);

    reader.readElement("Document");
    const long scheme = reader.getAttribute<long>("SchemaVersion");
    reader.DocumentSchema = static_cast<int>(scheme);
    if (reader.hasAttribute("ProgramVersion")) {
        reader.ProgramVersion = reader.getAttribute<const char*>("ProgramVersion");
    }
    else {
        reader.ProgramVersion = "pre-0.14";
    }
    if (reader.hasAttribute("FileVersion")) {
        reader.FileVersion = static_cast<int>(reader.getAttribute<unsigned long>("FileVersion"));
    }
    else {
        reader.FileVersion = 0;
    }

    if (reader.hasAttribute("StringHasher")) {
        d->Hasher->Restore(reader);
    }
    else {
        d->Hasher->clear();
    }

    // When this document was created the FileName and Label properties
    // were set to the absolute path or file name, respectively. To save
    // the document to the file it was loaded from or to show the file name
    // in the tree view we must restore them after loading the file because
    // they will be overridden.
    // Note: This does not affect the internal name of the document in any way
    // that is kept in Application.
    const std::string FilePath = FileName.getValue();
    const std::string DocLabel = Label.getValue();

    // read the Document Properties, when reading in Uid the transient directory gets renamed
    // automatically
    PropertyContainer::Restore(reader);

    // We must restore the correct 'FileName' property again because the stored
    // value could be invalid.
    FileName.setValue(FilePath.c_str());
    Label.setValue(DocLabel.c_str());

    // SchemeVersion "2"
    if (scheme == 2) {
        // read the feature types
        reader.readElement("Features");
        for (auto i = 0; i < reader.getAttribute<long>("Count"); i++) {
            reader.readElement("Feature");
            string type = reader.getAttribute<const char*>("type");
            string name = reader.getAttribute<const char*>("name");
            try {
                addObject(type.c_str(), name.c_str(), /*isNew=*/false);
            }
            catch (Base::Exception&) {
                Base::Console().message("Cannot create object '%s'\n", name.c_str());
            }
        }
        reader.readEndElement("Features");

        // read the features itself
        reader.readElement("FeatureData");
        for (auto i = 0; i < reader.getAttribute<long>("Count"); i++) {
            reader.readElement("Feature");
            string name = reader.getAttribute<const char*>("name");
            DocumentObject* pObj = getObject(name.c_str());
            if (pObj) {  // check if this feature has been registered
                pObj->setStatus(ObjectStatus::Restore, true);
                pObj->Restore(reader);
                pObj->setStatus(ObjectStatus::Restore, false);
            }
            reader.readEndElement("Feature");
        }
        reader.readEndElement("FeatureData");
    }  // SchemeVersion "3" or higher
    else if (scheme >= 3) {
        // read the feature types
        readObjects(reader);

        // tip object handling. First the whole document has to be read, then we
        // can restore the Tip link out of the TipName Property:
        Tip.setValue(getObject(TipName.getValue()));
    }

    reader.readEndElement("Document");
}

void DocumentP::checkStringHasher(const Base::XMLReader& reader)
{
    if (reader.hasReadFailed("StringHasher.Table.txt")) {
        Base::Console().error(QT_TRANSLATE_NOOP(
            "Notifications",
            "\nIt is recommended that the user right-click the root of "
            "the document and select Mark to recompute.\n"
            "The user should then click the Refresh button in the main toolbar.\n"));
    }
}

std::pair<bool, int> Document::addStringHasher(const StringHasherRef& hasher) const
{
    if (!hasher) {
        return std::make_pair(false, 0);
    }
    auto ret =
        d->hashers.left.insert(HasherMap::left_map::value_type(hasher, static_cast<int>(d->hashers.size())));
    if (ret.second) {
        hasher->clearMarks();
    }
    return std::make_pair(ret.second, ret.first->second);
}

StringHasherRef Document::getStringHasher(const int idx) const
{
    StringHasherRef hasher;
    if (idx < 0) {
        if (UseHasher.getValue()) {
            return d->Hasher;
        }
        return hasher;
    }
    const auto it = d->hashers.right.find(idx);
    if (it == d->hashers.right.end()) {
        hasher = new StringHasher;
        d->hashers.right.insert(HasherMap::right_map::value_type(idx, hasher));
    }
    else {
        hasher = it->second;
    }
    return hasher;
}

struct DocExportStatus
{
    Document::ExportStatus status;
    std::set<const DocumentObject*> objs;
};

static DocExportStatus exportStatus;

// Exception-safe exporting status setter
class DocumentExporting
{
public:
    explicit DocumentExporting(const std::vector<DocumentObject*>& objs)
    {
        exportStatus.status = Document::Exporting;
        exportStatus.objs.insert(objs.begin(), objs.end());
    }

    ~DocumentExporting()
    {
        exportStatus.status = Document::NotExporting;
        exportStatus.objs.clear();
    }
};

// The current implementation choose to use a static variable for exporting
// status because we can be exporting multiple objects from multiple documents
// at the same time. I see no benefits in distinguish which documents are
// exporting, so just use a static variable for global status. But the
// implementation can easily be changed here if necessary.
Document::ExportStatus Document::isExporting(const DocumentObject* obj) const
{
    if (exportStatus.status != Document::NotExporting
        && ((obj == nullptr) || exportStatus.objs.find(obj) != exportStatus.objs.end())) {
        return exportStatus.status;
    }
    return Document::NotExporting;
}
ExportInfo Document::exportInfo() const
{
    return d->exportInfo;
}
void Document::setExportInfo(const ExportInfo& info)
{
    d->exportInfo = info;
}

void Document::exportObjects(const std::vector<DocumentObject*>& obj, std::ostream& out)
{

    DocumentExporting exporting(obj);
    d->hashers.clear();

    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_LOG)) {
        for (auto o : obj) {
            if (o && o->isAttachedToDocument()) {
                FC_LOG("exporting " << o->getFullName());
                if (!o->getPropertyByName("_ObjectUUID")) {
                    auto prop = static_cast<PropertyUUID*>(
                        o->addDynamicProperty("App::PropertyUUID",
                                              "_ObjectUUID",
                                              nullptr,
                                              nullptr,
                                              Prop_Output | Prop_Hidden));
                    prop->setValue(Base::Uuid::createUuid());
                }
            }
        }
    }

    Base::ZipWriter writer(out);
    writer.putNextEntry("Document.xml");
    writer.Stream() << "<?xml version='1.0' encoding='utf-8'?>" << '\n';
    writer.Stream() << R"(<Document SchemaVersion="4" ProgramVersion=")"
                    << Application::Config()["BuildVersionMajor"] << "."
                    << Application::Config()["BuildVersionMinor"] << "R"
                    << Application::Config()["BuildRevision"] << R"(" FileVersion="1">)"
                    << '\n';
    // Add this block to have the same layout as for normal documents
    writer.Stream() << "<Properties Count=\"0\">" << '\n';
    writer.Stream() << "</Properties>" << '\n';

    // writing the object types
    writeObjects(obj, writer);
    writer.Stream() << "</Document>" << '\n';

    // Hook for others to add further data.
    signalExportObjects(obj, writer);

    // write additional files
    writer.writeFiles();
    d->hashers.clear();
}

constexpr auto fcAttrDependencies {"Dependencies"};
constexpr auto fcElementObjectDeps {"ObjectDeps"};
constexpr auto fcAttrDepCount {"Count"};
constexpr auto fcAttrDepObjName {"Name"};
constexpr auto fcAttrDepAllowPartial {"AllowPartial"};
constexpr auto fcElementObjectDep {"Dep"};

void Document::writeObjectDeps(const std::vector<DocumentObject*>& objs,
                               Base::Writer& writer) const
{
    for (auto o : objs) {
        // clang-format off
        const auto& outList = o->getOutList(DocumentObject::OutListNoHidden |
                                            DocumentObject::OutListNoXLinked);
        // clang-format on

        auto objName = o->getNameInDocument();
        writer.Stream() << writer.ind()
                        << "<" << fcElementObjectDeps
                        << " " << fcAttrDepObjName << "=\""
                        << (objName ? objName : "") << "\" "
                        << fcAttrDepCount << "=\""
                        << outList.size();
        if (outList.empty()) {
            writer.Stream() << "\"/>\n";
            continue;
        }
        int partial = o->canLoadPartial();
        if (partial > 0) {
            writer.Stream() << "\" " << fcAttrDepAllowPartial << "=\"" << partial;
        }
        writer.Stream() << "\">\n";
        writer.incInd();
        for (auto dep : outList) {
            auto depName = dep ? dep->getNameInDocument() : "";
            writer.Stream() << writer.ind()
                            << "<" << fcElementObjectDep
                            << " " << fcAttrDepObjName << "=\""
                            << (depName ? depName : "") << "\"/>\n";
        }
        writer.decInd();
        writer.Stream() << writer.ind() << "</" << fcElementObjectDeps << ">\n";
    }
}

void Document::writeObjectType(const std::vector<DocumentObject*>& objs,
                               Base::Writer& writer) const
{
    for (auto it : objs) {
        writer.Stream() << writer.ind() << "<Object "
                        << "type=\"" << it->getTypeId().getName() << "\" "
                        << "name=\"" << it->getExportName() << "\" "
                        << "id=\"" << it->getID() << "\" "
                        // Durable UUID (Amendment 3, Clause 3.6) written in phase 1 so the
                        // document's UUID index is populated before any property (phase 2)
                        // restores, letting a forward reference resolve by UUID on load.
                        << "uuid=\"" << it->Uid.getValueStr() << "\" ";

        // Only write out custom view provider types
        std::string viewType = it->getViewProviderNameStored();
        if (viewType != it->getViewProviderName()) {
            writer.Stream() << "ViewType=\"" << viewType << "\" ";
        }

        // See DocumentObjectPy::getState
        if (it->testStatus(ObjectStatus::Touch)) {
            writer.Stream() << "Touched=\"1\" ";
        }
        if (it->testStatus(ObjectStatus::Error)) {
            writer.Stream() << "Invalid=\"1\" ";
            auto desc = getErrorDescription(it);
            if (desc) {
                writer.Stream() << "Error=\"" << Property::encodeAttribute(desc) << "\" ";
            }
        }
        if (it->isFreezed()) {
            writer.Stream() << "Freeze=\"1\" ";
        }
        writer.Stream() << "/>\n";
    }
}

void Document::writeObjectData(const std::vector<DocumentObject*>& objs,
                               Base::Writer& writer) const
{
    // writing the features itself
    writer.Stream() << writer.ind() << "<ObjectData Count=\"" << objs.size() << "\">\n";

    writer.incInd();  // indentation for 'Object name'
    for (auto it : objs) {
        writer.Stream() << writer.ind() << "<Object name=\"" << it->getExportName() << "\"";
        if (it->hasExtensions()) {
            writer.Stream() << " Extensions=\"True\"";
        }

        writer.Stream() << ">\n";
        it->Save(writer);
        writer.Stream() << writer.ind() << "</Object>\n";
    }
    writer.decInd();  // indentation for 'Object name'

    writer.Stream() << writer.ind() << "</ObjectData>\n";
}

void Document::writeObjects(const std::vector<DocumentObject*>& objs,
                            Base::Writer& writer) const
{
    std::ostream& str = writer.Stream();

    // writing the features types
    writer.incInd();  // indentation for 'Objects count'
    str << writer.ind() << "<Objects Count=\"" << objs.size();
    if (!isExporting(nullptr)) {
        str << "\" " << fcAttrDependencies << "=\"1";
    }
    str << "\">\n";

    writer.incInd();  // indentation for 'Object type'

    if (!isExporting(nullptr)) {
        writeObjectDeps(objs, writer);
    }

    writeObjectType(objs, writer);

    writer.decInd();  // indentation for 'Object type'
    str << writer.ind() << "</Objects>\n";

    writeObjectData(objs, writer);
    writer.decInd();  // indentation for 'Objects count'

    // check for errors
    if (writer.hasFailed()) {
        std::cerr << "Output stream is in error state. As a result the "
                     "Document.xml file may be incomplete.\n";
        // reset the error flags to try to safe the data files
        writer.clear();
    }
}

struct DepInfo
{
    std::unordered_set<std::string> deps;
    int canLoadPartial = 0;
};

static void loadDeps(const std::string& name,
                      std::unordered_map<std::string, bool>& objs,
                      const std::unordered_map<std::string, DepInfo>& deps)
{
    const auto it = deps.find(name);
    if (it == deps.end()) {
        objs.emplace(name, true);
        return;
    }
    if (it->second.canLoadPartial != 0) {
        if (it->second.canLoadPartial == 1) {
            // canLoadPartial==1 means all its children will be created but not
            // restored, i.e. exists as if newly created object, and therefore no
            // need to load dependency of the children
            for (auto& dep : it->second.deps) {
                objs.emplace(dep, false);
            }
            objs.emplace(name, true);
        }
        else {
            objs.emplace(name, false);
        }
        return;
    }
    objs[name] = true;
    // If cannot load partial, then recurse to load all children dependency
    for (auto& dep : it->second.deps) {
        if (auto found = objs.find(dep); found != objs.end() && found->second) {
            continue;
        }
        loadDeps(dep, objs, deps);
    }
}

std::vector<DocumentObject*> Document::readObjects(Base::XMLReader& reader)
{
    d->touchedObjs.clear();
    bool keepDigits = testStatus(Document::KeepTrailingDigits);
    setStatus(Document::KeepTrailingDigits, !reader.doNameMapping());
    std::vector<DocumentObject*> objs;


    // read the object types
    reader.readElement("Objects");
    int Cnt = static_cast<int>(reader.getAttribute<long>("Count"));

    if (!reader.hasAttribute(fcAttrDependencies)) {
        d->partialLoadObjects.clear();
    }
    else if (!d->partialLoadObjects.empty()) {
        std::unordered_map<std::string, DepInfo> deps;
        for (int i = 0; i < Cnt; i++) {
            reader.readElement(fcElementObjectDeps);
            int dcount = static_cast<int>(reader.getAttribute<long>(fcAttrDepCount));
            if (dcount == 0) {
                continue;
            }
            auto& info = deps[reader.getAttribute<const char*>(fcAttrDepObjName)];
            if (reader.hasAttribute(fcAttrDepAllowPartial)) {
                info.canLoadPartial =
                    static_cast<int>(reader.getAttribute<long>(fcAttrDepAllowPartial));
            }
            for (int j = 0; j < dcount; ++j) {
                reader.readElement(fcElementObjectDep);
                const char* name = reader.getAttribute<const char*>(fcAttrDepObjName);
                if (!Base::Tools::isNullOrEmpty(name)) {
                    info.deps.insert(name);
                }
            }
            reader.readEndElement(fcElementObjectDeps);
        }
        std::vector<std::string> strings;
        strings.reserve(d->partialLoadObjects.size());
        for (auto& v : d->partialLoadObjects) {
            strings.emplace_back(v.first.c_str());
        }
        for (auto& name : strings) {
            loadDeps(name, d->partialLoadObjects, deps);
        }
        if (Cnt > static_cast<int>(d->partialLoadObjects.size())) {
            setStatus(Document::PartialDoc, true);
        }
        else {
            for (auto& v : d->partialLoadObjects) {
                if (!v.second) {
                    setStatus(Document::PartialDoc, true);
                    break;
                }
            }
            if (!testStatus(Document::PartialDoc)) {
                d->partialLoadObjects.clear();
            }
        }
    }

    long lastId = 0;
    for (int i = 0; i < Cnt; i++) {
        reader.readElement("Object");
        std::string type = reader.getAttribute<const char*>("type");
        std::string name = reader.getAttribute<const char*>("name");
        std::string viewType =
            reader.hasAttribute("ViewType") ? reader.getAttribute<const char*>("ViewType") : "";

        bool partial = false;
        if (!d->partialLoadObjects.empty()) {
            auto it = d->partialLoadObjects.find(name);
            if (it == d->partialLoadObjects.end()) {
                continue;
            }
            partial = !it->second;
        }

        if (!testStatus(Status::Importing) && reader.hasAttribute("id")) {
            // if not importing, then temporary reset lastObjectId and make the
            // following addObject() generate the correct id for this object.
            d->lastObjectId = reader.getAttribute<long>("id") - 1;
        }

        // To prevent duplicate name when export/import of objects from
        // external documents, we append those external object name with
        // @<document name>. Before importing (here means we are called by
        // importObjects), we shall strip the postfix. What the caller
        // (MergeDocument) sees is still the unstripped name mapped to a new
        // internal name, and the rest of the link properties will be able to
        // correctly unmap the names.
        auto pos = name.find('@');
        std::string _obj_name;
        const char* obj_name {nullptr};
        if (pos != std::string::npos) {
            _obj_name = name.substr(0, pos);
            obj_name = _obj_name.c_str();
        }
        else {
            obj_name = name.c_str();
        }

        try {
            // Use name from XML as is and do NOT remove trailing digits because
            // otherwise we may cause a dependency to itself
            // Example: Object 'Cut001' references object 'Cut' and removing the
            // digits we make an object 'Cut' referencing itself.
            DocumentObject* obj =
                addObject(type.c_str(), obj_name, /*isNew=*/false, viewType.c_str(), partial);
            if (obj) {
                if (lastId < obj->_Id) {
                    lastId = obj->_Id;
                }
                objs.push_back(obj);
                // use this name for the later access because an object with
                // the given name may already exist
                reader.addName(name.c_str(), obj->getNameInDocument());

                // Stamp the durable UUID now (phase 1) so getObjectByUuid resolves
                // during phase-2 property restore, even for forward references
                // (Amendment 3, Clause 3.6). The phase-2 Uid property re-sets the
                // same value harmlessly.
                if (reader.hasAttribute("uuid")) {
                    Base::Uuid uuid;
                    uuid.setValue(reader.getAttribute<const char*>("uuid"));
                    obj->Uid.setValue(uuid);
                }

                // restore touch/error status flags
                if (reader.hasAttribute("Touched")) {
                    if (reader.getAttribute<long>("Touched") != 0) {
                        d->touchedObjs.insert(obj);
                    }
                }
                if (reader.hasAttribute("Invalid")) {
                    obj->setStatus(ObjectStatus::Error,
                                   reader.getAttribute<bool>("Invalid"));
                    if (obj->isError() && reader.hasAttribute("Error")) {
                        d->addRecomputeLog(reader.getAttribute<const char*>("Error"), obj);
                    }
                }
                if (reader.hasAttribute("Freeze")) {
                    if (reader.getAttribute<long>("Freeze") != 0) {
                        obj->freeze();
                    }
                }
            }
        }
        catch (const DocumentContentScopeError&) {
            // A content-scope violation is fatal to the load (Amendment 8 Clause 8.1):
            // the offending object exists on disk, so "fail loud" here means the load
            // itself fails — surfacing the violation — never the silent drop the generic
            // catch below performs for recoverable per-object failures. Rethrow past it.
            throw;
        }
        catch (const Base::Exception& e) {
            Base::Console().error("Cannot create object '%s': (%s)\n", name.c_str(), e.what());
            // Recorded on the read, not on the document: this same path reads objects being
            // imported from someone else's file, and what that file loses is not what a write
            // from THIS document would lose. Document::restore() is what takes it over.
            reader.setPartialRestore(true);
            reader.recordUnreadStatement("the object '" + name + "' (" + type
                                         + "), which this session could not create");
        }
    }
    if (!testStatus(Status::Importing)) {
        d->lastObjectId = lastId;
    }

    reader.readEndElement("Objects");
    setStatus(Document::KeepTrailingDigits, keepDigits);

    // read the features itself
    const auto lostTheRestOf = [&reader](const std::string& name, const Base::Exception& why) {
        reader.setPartialRestore(true);
        reader.recordUnreadStatement("everything '" + name + "' states from where its read "
                                     "stopped: " + why.what());
    };
    reader.clearPartialRestoreDocumentObject();
    reader.readElement("ObjectData");
    Cnt = static_cast<int>(reader.getAttribute<long>("Count"));
    for (int i = 0; i < Cnt; i++) {
        reader.readElement("Object");
        std::string name = reader.getName(reader.getAttribute<const char*>("name"));
        if (DocumentObject* pObj = getObject(name.c_str()); pObj
            && !pObj->testStatus(
                PartialObject)) {  // check if this feature has been registered
            pObj->setStatus(ObjectStatus::Restore, true);
            try {
                FC_TRACE("restoring " << pObj->getFullName());
                pObj->Restore(reader);
            }
            // Try to continue only for certain exception types if not handled
            // by the feature type. For all other exception types abort the process.
            // Cruth (Amendment 19): an object's read that stops part way through takes every
            // statement its file makes after that point with it, and several types read their own
            // properties with no handling of their own -- the failure surfaces here or nowhere.
            // What was not read is named on the READ, which Document::restore() then takes over;
            // this path also reads objects imported from someone else's file, and what that file
            // loses is not what a write from THIS document would lose.
            catch (const Base::UnicodeError& e) {
                e.reportException();
                lostTheRestOf(name, e);
            }
            catch (const Base::ValueError& e) {
                e.reportException();
                lostTheRestOf(name, e);
            }
            catch (const Base::IndexError& e) {
                e.reportException();
                lostTheRestOf(name, e);
            }
            catch (const Base::RuntimeError& e) {
                e.reportException();
                lostTheRestOf(name, e);
            }
            catch (const Base::XMLAttributeError& e) {
                e.reportException();
                lostTheRestOf(name, e);
            }

            pObj->setStatus(ObjectStatus::Restore, false);

            if (reader.testStatus(Base::XMLReader::ReaderStatus::PartialRestoreInDocumentObject)) {
                Base::Console().error("Object \"%s\" was subject to a partial restore. As a result "
                                      "geometry may have changed or be incomplete.\n",
                                      name.c_str());
                reader.clearPartialRestoreDocumentObject();
            }
        }
        reader.readEndElement("Object");
    }
    reader.readEndElement("ObjectData");

    return objs;
}

std::vector<DocumentObject*> Document::importObjects(Base::XMLReader& reader)
{
    d->hashers.clear();
    Base::FlagToggler<> flag(globalIsRestoring, false);
    Base::ObjectStatusLocker<Status, Document> restoreBit(Status::Restoring, this);
    Base::ObjectStatusLocker<Status, Document> restoreBit2(Status::Importing, this);
    ExpressionParser::ExpressionImporter expImporter(reader);
    reader.readElement("Document");
    const long scheme = reader.getAttribute<long>("SchemaVersion");
    reader.DocumentSchema = static_cast<int>(scheme);
    if (reader.hasAttribute("ProgramVersion")) {
        reader.ProgramVersion = reader.getAttribute<const char*>("ProgramVersion");
    }
    else {
        reader.ProgramVersion = "pre-0.14";
    }
    if (reader.hasAttribute("FileVersion")) {
        reader.FileVersion = static_cast<int>(reader.getAttribute<unsigned long>("FileVersion"));
    }
    else {
        reader.FileVersion = 0;
    }

    // Import is the copy path (copy-paste, insert-as-copy, drag-duplicate): it
    // drops NEW authored objects into a document, so they must not inherit the
    // source's durable identity (§10.7) — two coexisting objects sharing one id
    // would break "same id means the same object". A relocation is not
    // duplication and keeps the identity it arrived with, at every grain.
    const bool isDuplication = !testStatus(Relocating);
    std::vector<DocumentObject*> objs = readObjects(reader);
    for (const auto o : objs) {
        if (o && o->isAttachedToDocument()) {
            o->setStatus(ObjImporting, true);
            FC_LOG("importing " << o->getFullName());
            if (isDuplication) {
                o->mintDurableIdentity();
            }
        }
    }

    reader.readEndElement("Document");

    signalImportObjects(objs, reader);
    afterRestore(objs, true);

    signalFinishImportObjects(objs);

    for (const auto o : objs) {
        if (o && o->isAttachedToDocument()) {
            o->setStatus(ObjImporting, false);
        }
    }

    d->hashers.clear();
    return objs;
}

std::vector<std::pair<std::string, DocumentObject*>>
Document::acceptStoredRecipeObjects(std::istream& rendering, const std::string& assetDirectory)
{
    d->hashers.clear();
    Base::FlagToggler<> flag(globalIsRestoring, false);
    Base::ObjectStatusLocker<Status, Document> restoreBit(Status::Restoring, this);
    Base::ObjectStatusLocker<Status, Document> restoreBit2(Status::Importing, this);

    // A copy drops NEW authored objects into a document, so they must not inherit the source's
    // durable identity (§10.7) -- two coexisting objects sharing one id would break "same id means
    // the same object". A relocation is not duplication and keeps the identity it arrived with.
    const bool isDuplication = !testStatus(Relocating);

    std::vector<std::pair<std::string, DocumentObject*>> arrived;
    RecipeArrival how;
    // The second pass runs below instead, because the new identities have to be in place before
    // anything binds against them -- the order the archive path already uses.
    how.finish = false;
    how.assetDirectory = assetDirectory;
    how.intoExistingContent = true;
    how.arrived = &arrived;
    restoreStoredRecipe(*this, rendering, how);

    std::vector<DocumentObject*> objs;
    objs.reserve(arrived.size());
    for (const auto& [stated, obj] : arrived) {
        objs.push_back(obj);
    }

    for (auto o : objs) {
        if (o && o->isAttachedToDocument()) {
            o->setStatus(ObjImporting, true);
            FC_LOG("importing " << o->getFullName());
            if (isDuplication) {
                o->mintDurableIdentity();
            }
        }
    }

    afterRestore(objs, true);
    signalFinishImportObjects(objs);

    for (auto o : objs) {
        if (o && o->isAttachedToDocument()) {
            o->setStatus(ObjImporting, false);
        }
    }

    d->hashers.clear();
    return arrived;
}

// Cruth: extensions the save path recognizes as an already-complete document name,
// so it does not append the type-derived one on top (which produced ".cpart.FCStd" and
// ".cassembly.cassembly"). Delegates to the single source of truth for native document
// extensions so a new document type's extension is honored here automatically.
static bool isKnownDocumentExtension(const char* ext)
{
    return Document::isNativeFormatExtension(ext);
}

// docExt is the document's own extension (without dot), derived from its type marker.
static std::string checkFileName(const char* file, const std::string& docExt)
{
    std::string fn(file);

    // Append extension if missing. This option is added for security reason, so
    // that the user won't accidentally overwrite other file that may be critical.
    if (GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
            ->GetBool("CheckExtension", true)) {
        constexpr std::size_t backupExtLen = sizeof(".fcbak") - 1;

        // A backup name (".fcbak", optionally trailed by a document extension) is
        // normalized to the document's own extension.
        std::string base = fn;
        if (const char* ext = strrchr(base.c_str(), '.');
            ext && isKnownDocumentExtension(ext + 1)) {
            base.erase(static_cast<std::size_t>(ext - base.c_str()));
        }
        if (boost::iends_with(base, ".fcbak")) {
            base.erase(base.size() - backupExtLen);
            base += ".";
            base += docExt;
            return base;
        }

        const char* ext = strrchr(fn.c_str(), '.');
        if ((ext == nullptr) || !isKnownDocumentExtension(ext + 1)) {
            if (ext && ext[1] == 0) {
                fn += docExt;
            }
            else {
                fn += ".";
                fn += docExt;
            }
        }
    }
    return fn;
}

bool Document::saveAs(const char* _file)
{
    const std::string file = checkFileName(_file, documentFileExtension());
    const Base::FileInfo fi(file.c_str());
    // Asked before the document is renamed, not after: a refusal that has already pointed the
    // document at the file it declined to write has performed half of that write (Amendment 19
    // Clause 19.3 -- a refused save changes nothing).
    if (!mayWrite()) {
        return false;
    }
    if (this->FileName.getStrValue() != file) {
        this->FileName.setValue(file);
        this->Label.setValue(fi.fileNamePure());
        this->Uid.touch();  // this forces a rename of the transient directory
    }

    return save();
}

bool Document::saveCopy(const char* file)
{
    // A rule attached to the destination is walked around by writing elsewhere and writing back,
    // and a fragment written to a new path is still a document that denies what it states.
    if (!mayWrite()) {
        return false;
    }
    const std::string checked = checkFileName(file, documentFileExtension());
    return this->FileName.getStrValue() != checked ? saveToFile(checked.c_str()) : false;
}

bool Document::canWriteRecoverySnapshot() const
{
    return !testStatus(Document::PartialDoc) && !testStatus(Document::TempDoc)
        && !testStatus(Document::Recomputing) && !transactionStateBlocksRecoveryWrite(*d)
        && !isPerformingTransaction();
}

// Save the document under the name it has been opened
bool Document::save()
{
    if (!mayWrite()) {
        return false;
    }

    if (testStatus(Document::PartialDoc)) {
        FC_ERR("Partial loaded document '" << Label.getValue() << "' cannot be saved");
        // TODO We don't make this a fatal error and return 'true' to make it possible to
        // save other documents that depends on this partial opened document. We need better
        // handling to avoid touching partial documents.
        return true;
    }

    if (*(FileName.getValue()) != '\0') {
        // Save the name of the tip object in order to handle in Restore()
        if (Tip.getValue()) {
            TipName.setValue(Tip.getValue()->getNameInDocument());
        }

        // Cruth: in memory only -- the property is transient and is not written to the
        // file, so this keeps a live document's answer current without making the saved
        // bytes depend on when the save happened. On the next open it is derived from the
        // file itself (see Document::restore).
        const std::string LastModifiedDateString = Base::Tools::currentDateTimeString();
        LastModifiedDate.setValue(LastModifiedDateString.c_str());
        // set author if needed
        const bool saveAuthor =
            GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                ->GetBool("prefSetAuthorOnSave", false);
        if (saveAuthor) {
            const std::string Author =
                GetApplication()
                    .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                    ->GetASCII("prefAuthor", "");
            LastModifiedBy.setValue(Author.c_str());
        }

        bool result = saveToFile(FileName.getValue());
        if (result) {
            d->programVersion = Application::Config()["BuildVersionMajor"] + "."
                + Application::Config()["BuildVersionMinor"] + "R"
                + Application::Config()["BuildRevision"];
        }
        return result;
    }

    return false;
}

bool Document::saveToFile(const char* filename) const
{
    signalStartSave(*this, filename);

    // Where the handed-in geometry goes. Derived from the path being written rather than from
    // the document's own FileName, because Save As writes somewhere the document does not live
    // yet, and its source material has to arrive with it.
    const auto assetsFor = [](const std::string& path) {
        return (fs::path(path).parent_path() / "assets").string();
    };

    // Documents used to be written as an archive, uncompressed, so that version control could
    // at least see which bytes changed. Writing the recipe as plain text finishes that thought:
    // there is no container left to see through.
    bool policy = GetApplication()
                      .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                      ->GetBool("BackupPolicy", true);

    auto canonical_path = [](const char* filename) {
        try {
#ifdef FC_OS_WIN32
            QString utf8Name = QString::fromUtf8(filename);
            auto realpath = fs::weakly_canonical(fs::absolute(fs::path(utf8Name.toStdWString())));
            std::string nativePath = QString::fromStdWString(realpath.native()).toStdString();
#else
            auto realpath = fs::weakly_canonical(fs::absolute(fs::path(filename)));
            std::string nativePath = realpath.native();
#endif
            // In case some folders in the path do not exist
            auto parentPath = realpath.parent_path();
            fs::create_directories(parentPath);

            return nativePath;
        }
        catch (const std::exception&) {
#ifdef FC_OS_WIN32
            QString utf8Name = QString::fromUtf8(filename);
            auto parentPath = fs::absolute(fs::path(utf8Name.toStdWString())).parent_path();
#else
            auto parentPath = fs::absolute(fs::path(filename)).parent_path();
#endif
            fs::create_directories(parentPath);

            return std::string(filename);
        }
    };

    // realpath is canonical filename i.e. without symlink
    std::string nativePath = canonical_path(filename);

    // check if file is writeable, then block the save if it is not.
    Base::FileInfo originalFileInfo(nativePath);
    if (originalFileInfo.exists() && !originalFileInfo.isWritable()) {
        throw Base::FileException("Unable to save document because file is marked as read-only or write permission is not available.", originalFileInfo);
    }

    // make a tmp. file where to save the project data first and then rename to
    // the actual file name. This may be useful if overwriting an existing file
    // fails so that the data of the work up to now isn't lost.
    std::string uuid = Base::Uuid::createUuid();
    std::string fn = nativePath;
    if (policy) {
        fn += ".";
        fn += uuid;
    }


    // Cruth: the document file IS the recipe.
    //
    // What a part is made of -- its features, their values, what each one was built on -- is the
    // record; the solid those steps produce is what the record builds, and is rebuilt on opening.
    // Written as one readable text file, a saved document can be read, differenced and merged by
    // ordinary tools with nothing installed to interpret it, which an archive of compressed
    // members never could. A sealed archive remains the right shape for a release or for a
    // records system, and is a separate operation rather than the everyday save.
    {
        Base::FileInfo tmp(fn);
        Base::ofstream file(tmp, std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            throw Base::FileException("Failed to open file", tmp);
        }

        // The registry of string hashers is reset first, exactly as the archive's own writer
        // resets it. Without this the first save of a session writes a shape's hasher table into
        // the source store and every later save leaves it out, so a document nobody edited is
        // stored twice under two names -- and the line in the recipe that names it changes, which
        // is the diff noise this whole form exists to remove.
        d->hashers.clear();
        addStringHasher(d->Hasher);

        for (const auto o : d->objectArray) {
            o->beforeSave();
        }
        beforeSave();

        file << formatStoredRecipe(*this, assetsFor(nativePath));
        if (file.fail()) {
            throw Base::FileException("Failed to write the document", tmp);
        }
        file.close();

        // The two agree again: what was just written is what is held.
        d->movedOnFromItsFile = false;
        GetApplication().signalSaveDocument(*this);
    }

    // The solids the recipe just described how to make. They are not the record -- the file is --
    // but rebuilding a hundred features to look at a part that has not changed is a cost with
    // nothing bought by it, so what a rebuild would produce is kept where a rebuild can find it.
    try {
        const fs::path cache = fs::path(nativePath).parent_path() / cacheFolderName / Uid.getValueStr();
        storeBuiltGeometry(*this, cache.string(), assetsFor(nativePath));
    }
    catch (const std::exception& e) {
        Base::Console().warning("Could not keep the built geometry for %s: %s\n",
                                getName(),
                                e.what());
    }

    // Display state -- colours, draw style, the camera -- is not what the part is made of, so it
    // is not in the recipe. It is not nothing either: a person chose it. It goes to the project
    // cache, beside the geometry, where it can be deleted without losing anything designed.
    if (signalSaveDocument.num_slots() > 0) {
        try {
            const fs::path cache = fs::path(nativePath).parent_path() / cacheFolderName
                / Uid.getValueStr();
            fs::create_directories(cache);
            Base::FileWriter writer(cache.string().c_str());
            signalSaveDocument(writer);
            writer.writeFiles();
        }
        catch (const std::exception& e) {
            // A cache that cannot be written costs a redraw, never a design. Saving the file
            // itself has already succeeded and must not be undone by this.
            Base::Console().warning("Could not store the display state for %s: %s\n",
                                    getName(),
                                    e.what());
        }
    }

    if (policy) {
        // if saving the project data succeeded rename to the actual file name
        int count_bak = static_cast<int>(GetApplication()
                            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                            ->GetInt("CountBackupFiles", 1));
        bool backup = GetApplication()
                          .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                          ->GetBool("CreateBackupFiles", true);
        if (!backup) {
            count_bak = -1;
        }
        bool useFCBakExtension =
            GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                ->GetBool("UseFCBakExtension", true);
        std::string saveBackupDateFormat =
            GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                ->GetASCII("SaveBackupDateFormat", "%Y%m%d-%H%M%S");

        BackupPolicy backupPolicy;
        if (useFCBakExtension) {
            backupPolicy.setPolicy(BackupPolicy::TimeStamp);
            backupPolicy.useBackupExtension(useFCBakExtension);
            backupPolicy.setDateFormat(saveBackupDateFormat);
        }
        else {
            backupPolicy.setPolicy(BackupPolicy::Standard);
        }
        backupPolicy.setNumberOfFiles(count_bak);
        backupPolicy.apply(fn, nativePath);
    }

    // The readable rendering of the recipe -- the steps a person took, one fact per line -- used
    // to be written beside every save, because the file of record was an archive nobody could
    // read. The record is now the recipe itself, so a second file saying the same thing in
    // softer words would only be one more file in the folder and one more thing to disagree
    // with. The rendering remains available to anyone who asks for it (RecipeText.h).

    signalFinishSave(*this, filename);

    return true;
}

bool Document::isAnyRestoring()
{
    return globalIsRestoring;
}

// Open the document
void Document::restore(const char* filename,
                       bool delaySignal,
                       const std::vector<std::string>& objNames)
{
    clearUndos();
    d->activeObject = nullptr;

    bool signal = false;
    Document* activeDoc = GetApplication().getActiveDocument();
    if (!d->objectArray.empty()) {
        signal = true;
        GetApplication().signalDeleteDocument(*this);
        d->clearDocument();
    }

    Base::FlagToggler<> flag(globalIsRestoring, false);

    setStatus(Document::PartialDoc, false);
    // A read is a fresh account of a file. What an earlier read of this document could not bring
    // back, or kept because it could not honour it, belongs to that read and not to this one.
    setStatus(Document::PartialRestore, false);
    setStatus(Document::RestoreError, false);
    _unkeptStatements.clear();
    _unkeptAgainst.clear();
    _unreadObjects.clear();

    d->clearRecomputeLog();
    d->objectLabelManager.clear();
    d->objectArray.clear();
    d->objectNameManager.clear();
    d->objectMap.clear();
    d->objectIdMap.clear();
    d->objectUuidMap.clear();
    d->objectUuidMapDirty = true;
    d->lastObjectId = 0;

    if (signal) {
        GetApplication().signalNewDocument(*this, true);
        if (activeDoc == this) {
            GetApplication().setActiveDocument(this);
        }
    }

    if (!filename) {
        filename = FileName.getValue();
    }
    Base::FileInfo fi(filename);
    Base::ifstream file(fi, std::ios::in | std::ios::binary);
    std::streambuf* buf = file.rdbuf();
    std::streamoff size = buf->pubseekoff(0, std::ios::end, std::ios::in);
    buf->pubseekoff(0, std::ios::beg, std::ios::in);
    if (size < 22) {  // an empty zip archive has 22 bytes
        throw Base::FileException("Invalid project file", filename);
    }

    // Which shape this document file has: the recipe this program writes, a sealed archive
    // holding one -- what an export of this build hands over -- or a legacy container, from an
    // older version. The file says which itself; nothing has to be configured or remembered.
    const bool container = buf->sgetc() == 'P';
    buf->pubseekoff(0, std::ios::beg, std::ios::in);

    /// What an unpacked archive is read out of, removed however this read ends.
    struct Unpacked
    {
        ~Unpacked()
        {
            if (!where.empty()) {
                std::error_code ignored;
                fs::remove_all(where, ignored);
            }
        }
        fs::path where;
        Base::ifstream recipe;
    } unpacked;

    // A sealed archive of this build's own making is read as the recipe it holds: unpacked beside
    // its source material, laid out as a project folder, and handed to the one reader that reads a
    // document. An export is a rendering of a document (Amendment 18 Clause 18.1) -- reading one
    // back is reading a document, and it is not owed a second reader of its own.
    std::istream* source = &file;
    std::string sourceAssets = (fs::path(filename).parent_path() / "assets").string();
    if (container && holdsSealedRecipe(filename)) {
        file.close();
        unpacked.where = fs::temp_directory_path() / ("cruth-archive-" + Base::Uuid::createUuid());
        std::error_code failed;
        fs::create_directories(unpacked.where, failed);
        const std::string held = unpackSealedArchive(filename, unpacked.where.string());
        unpacked.recipe.open(Base::FileInfo(held), std::ios::in | std::ios::binary);
        if (!unpacked.recipe) {
            throw Base::FileException("Could not read the recipe the archive holds", filename);
        }
        source = &unpacked.recipe;
        sourceAssets = (unpacked.where / "assets").string();
    }

    const bool sealedArchive = container && source == &file;
    if (!sealedArchive) {
        GetApplication().signalStartRestoreDocument(*this);
        setStatus(Document::Restoring, true);

        const std::string filePath = FileName.getValue();
        const std::string docLabel = Label.getValue();
        d->rebuildOnOpen = true;
        // What a read that will not finish leaves behind, whatever stopped it: the reasons differ
        // and the protection does not. Written once so the two refusals below cannot drift into
        // protecting the document to different degrees.
        const auto refuseWhatWasRead = [&](const Base::Exception& e) {
            Base::Console().error("Invalid recipe: %s\n", e.what());
            setStatus(Document::RestoreError, true);
            // Named, not only printed: a save from here would publish this session's beginning of
            // the file over the whole of it, and a caller asked to accept that has to be able to
            // read what it is accepting (Amendment 19 Clause 19.3).
            recordUnkeptStatement("everything '" + filePath
                                  + "' states from where the read stopped: " + e.what());
            // The file names the document it came from, and a read that stopped must not leave
            // this document renamed or pointed at some other file -- true whether it goes on or
            // refuses, so it is done before the refusal.
            FileName.setValue(filePath.c_str());
            Label.setValue(docLabel.c_str());
            _unkeptAgainst = FileName.getStrValue();
            // What the stopped read did manage to build is thrown away, not kept. The opener
            // discards the whole document when the file is one it is opening; a REVERT has no
            // such door -- the document existed before and goes on existing -- so a fragment
            // would be left sitting in it, wearing the file's name and looking like the part.
            // An empty document is visibly not the record; a nearly complete one is not.
            if (!d->objectArray.empty()) {
                GetApplication().signalDeleteDocument(*this);
                d->clearDocument();
                d->objectLabelManager.clear();
                d->objectArray.clear();
                d->objectNameManager.clear();
                d->objectMap.clear();
                d->objectIdMap.clear();
                d->objectUuidMap.clear();
                d->objectUuidMapDirty = true;
                d->lastObjectId = 0;
                GetApplication().signalNewDocument(*this, true);
            }
            // And now the refusal itself, which is TOTAL. What has been read so far is the
            // beginning of a file, not a document: it looks like one, a save publishes it, and
            // the file it came from is the only remaining copy of the rest. The write guard of
            // Clause 19.3 stands behind this and would refuse that save, but a second line of
            // defence is not the first one -- a person should never be holding the fragment
            // (Amendment 19 Clause 19.2). The opener discards it; what was wrong and where is
            // carried in the message so the file can be repaired in a text editor.
        };

        try {
            restoreStoredRecipe(*this, *source, /*finish=*/false, sourceAssets);
        }
        catch (const DocumentContentScopeError&) {
            throw;
        }
        // The file is written to a format this build does not read (Clause 19.7). Protected on
        // exactly the terms above -- a document read by rules it was not written to is the same
        // danger as one read half way -- but said in its own words and kept as its own type. "This
        // file is broken" is the wrong thing to tell a person whose file is fine and whose program
        // is the wrong one for it.
        catch (const DocumentFormatUnknownError& e) {
            refuseWhatWasRead(e);
            throw DocumentFormatUnknownError("'" + filePath + "' did not open: " + e.what());
        }
        catch (const Base::Exception& e) {
            refuseWhatWasRead(e);
            throw DocumentMalformedError("'" + filePath
                                         + "' does not state a record this reader can determine, "
                                           "and a part of one is not a document: "
                                         + e.what());
        }
        // The file names the document it came from, and reading it must not rename the document
        // it is being read into or point it at some other file.
        FileName.setValue(filePath.c_str());
        Label.setValue(docLabel.c_str());

        // Whatever this read could not bring back is missing from here and present there. A write
        // over that same file is what would destroy it; a write anywhere else leaves it standing.
        if (!_unkeptStatements.empty()) {
            _unkeptAgainst = FileName.getStrValue();
        }

        // The display state the last session left behind, if the cache still holds it. Its
        // absence is ordinary -- a cache is disposable -- and costs only default colours.
        const std::string cache = cacheDirectory();
        if (signalRestoreDocument.num_slots() > 0 && !cache.empty()
            && Base::FileInfo(cache).isDir()) {
            try {
                std::istringstream head("<?xml version='1.0' encoding='utf-8'?><Display/>");
                Base::XMLReader viewReader("DisplayState", head);
                signalRestoreDocument(viewReader);
                viewReader.readFiles(cache);
            }
            catch (const std::exception& e) {
                Base::Console().warning("Could not read the display state for %s: %s\n",
                                        getName(),
                                        e.what());
            }
        }

        LastModifiedDate.setValue(
            Base::Tools::dateTimeString(fi.lastModified().getTime_t()).c_str());

        if (!delaySignal) {
            afterRestore(true);
        }
        return;
    }

    zipios::ZipInputStream zipstream(file);
    Base::XMLReader reader(filename, zipstream);

    if (!reader.isValid()) {
        throw Base::FileException("Error reading compression file", filename);
    }

    GetApplication().signalStartRestoreDocument(*this);
    setStatus(Document::Restoring, true);

    d->partialLoadObjects.clear();
    for (auto& name : objNames) {
        d->partialLoadObjects.emplace(name, true);
    }
    try {
        Document::Restore(reader);
    }
    catch (const DocumentContentScopeError&) {
        // Fatal: a typed document on disk carries out-of-scope content (Amendment 8
        // Clause 8.1). The load fails loudly rather than opening a half-document with
        // the offending object silently dropped. Propagate to the opener, which
        // discards the partially built document.
        throw;
    }
    catch (const Base::Exception& e) {
        Base::Console().error("Invalid Document.xml: %s\n", e.what());
        setStatus(Document::RestoreError, true);
        recordUnkeptStatement("everything '" + std::string(filename)
                              + "' states from where the read stopped: " + e.what());
    }

    d->partialLoadObjects.clear();
    d->programVersion = reader.ProgramVersion;

    // Special handling for Gui document, the view representations must already
    // exist, what is done in Restore().
    // Note: This file doesn't need to be available if the document has been created
    // without GUI. But if available then follow after all data files of the App document.
    signalRestoreDocument(reader);
    reader.readFiles(zipstream);

    DocumentP::checkStringHasher(reader);

    // The read said which statements it could not bring back. They are this document's to carry
    // now: what is in this session is what was read, the file is what was written, and an ordinary
    // save replaces the second with the first (Amendment 19 Clause 19.3). Asked of the read
    // itself, not of a status bit -- a bit says that something once went wrong, and the question
    // at the moment of a write is what that write would take out of the file.
    for (const std::string& lost : reader.unreadStatements()) {
        recordUnkeptStatement(lost);
    }
    if (reader.testStatus(Base::XMLReader::ReaderStatus::PartialRestore)) {
        setStatus(Document::PartialRestore, true);
        Base::Console().error("There were errors while loading the file. Some data might have been "
                              "modified or not recovered at all. Look above for more specific "
                              "information about the objects involved.\n");
        if (reader.unreadStatements().empty()) {
            recordUnkeptStatement("content of '" + std::string(filename)
                                  + "' this session could not read back; the messages above name "
                                    "what was involved");
        }
    }

    // Whatever this read could not bring back is missing from here and present there.
    if (!_unkeptStatements.empty()) {
        _unkeptAgainst = FileName.getStrValue();
    }

    // Cruth: derive the last-modified date from the file we just read, rather than trusting
    // a value stored inside it. The property is transient, so a document written by this
    // version carries no such value at all; a document written by an older version still
    // carries one, and deriving unconditionally means both answer the same way.
    auto modified = fi.lastModified();
    LastModifiedDate.setValue(Base::Tools::dateTimeString(modified.getTime_t()).c_str());

    if (!delaySignal) {
        afterRestore(true);
    }
}

bool Document::afterRestore(const bool checkPartial)
{
    Base::FlagToggler<> flag(globalIsRestoring, false);
    if (!afterRestore(d->objectArray, checkPartial)) {
        FC_WARN("Reload partial document " << getName());
        GetApplication().signalPendingReloadDocument(*this);
        return false;
    }
    GetApplication().signalFinishRestoreDocument(*this);
    setStatus(Document::Restoring, false);
    // Just read: what is held IS what the file states, whatever was set along the way.
    d->movedOnFromItsFile = testStatus(Document::GivenNewIdentity);

    // A document read from a recipe carries the steps and not the solid they make, so opening
    // it includes building it. Reading and rebuilding stay separate acts -- that is what lets a
    // rebuild that fails be reported instead of quietly producing an empty part -- and this is
    // the point at which every document in the set has been read and can be built in order.
    if (d->rebuildOnOpen) {
        d->rebuildOnOpen = false;
        // Only what the project cache cannot hand back. An object whose recipe text and inputs
        // are the ones that produced the solid last time gets that solid returned to it; every
        // other object is built. Marked here rather than when the file was read: restoring ends
        // by declaring every object settled, which is the right answer for a file that carried
        // its geometry and the wrong one for a file that carried the steps to make it.
        std::set<DocumentObject*> toBuild;
        try {
            toBuild = restoreBuiltGeometry(*this, cacheDirectory(), assetDirectory());
        }
        catch (const std::exception& e) {
            Base::Console().warning("Could not reuse the built geometry for %s: %s\n",
                                    getName(),
                                    e.what());
            toBuild.insert(d->objectArray.begin(), d->objectArray.end());
        }
        for (DocumentObject* obj : toBuild) {
            obj->enforceRecompute();
        }
        recompute();
    }
    return true;
}

bool Document::afterRestore(const std::vector<DocumentObject*>& objArray, bool checkPartial)
{
    checkPartial = checkPartial && testStatus(Document::PartialDoc);
    if (checkPartial && !d->touchedObjs.empty()) {
        return false;
    }

    // Some link type properties cannot restore link information until other
    // objects have been restored. For example, PropertyExpressionEngine and
    // PropertySheet with expressions containing a label reference. So we add
    // the Property::afterRestore() interface to let them sort it out. Note,
    // this API is not called in object dependency order, because the order
    // information is not ready yet.
    std::map<DocumentObject*, std::vector<Property*>> propMap;
    for (auto obj : objArray) {
        auto& props = propMap[obj];
        obj->getPropertyList(props);
        for (auto prop : props) {
            try {
                prop->afterRestore();
            }
            catch (const Base::Exception& e) {
                FC_ERR("Failed to restore " << obj->getFullName() << '.' << prop->getName() << ": "
                                            << e.what());
            }
        }
    }

    if (checkPartial && !d->touchedObjs.empty()) {
        // partial document touched, signal full reload
        return false;
    }

    std::set<DocumentObject*> objSet(objArray.begin(), objArray.end());
    auto objs = getDependencyList(objArray.empty() ? d->objectArray : objArray, DepSort);
    for (auto obj : objs) {
        if (objSet.find(obj) == objSet.end()) {
            continue;
        }
        try {
            for (auto prop : propMap[obj]) {
                prop->onContainerRestored();
            }
            bool touched = false;
            auto returnCode =
                obj->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteOnRestore, &touched);
            if (returnCode != DocumentObject::StdReturn) {
                FC_ERR("Expression engine failed to restore " << obj->getFullName() << ": "
                                                              << returnCode->Why);
                d->addRecomputeLog(returnCode);
            }
            obj->onDocumentRestored();
            if (touched) {
                d->touchedObjs.insert(obj);
            }
        }
        catch (const Base::Exception& e) {
            d->addRecomputeLog(e.what(), obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << e.what());
        }
        catch (std::exception& e) {
            d->addRecomputeLog(e.what(), obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << e.what());
        }
        catch (...) {

            // If a Python exception occurred, it must be cleared immediately.
            // Otherwise, the interpreter remains in a dirty state, causing
            // Segfaults later when FreeCAD interacts with Python.
            if (PyErr_Occurred()) {
                Base::Console().error("Python error during object restore:\n");
                PyErr_Print(); // Print the traceback to stderr/Console
                PyErr_Clear(); // Reset the interpreter state
            }

            d->addRecomputeLog("Unknown exception on restore", obj);
            FC_ERR("Failed to restore " << obj->getFullName() << ": " << "unknown exception");
        }
        if (obj->isValid()) {
            auto& props = propMap[obj];
            props.clear();
            // refresh properties in case the object changes its property list
            obj->getPropertyList(props);
            for (auto prop : props) {
                auto link = freecad_cast<PropertyLinkBase*>(prop);
                int res {0};
                std::string errMsg;
                if (link && ((res = link->checkRestore(&errMsg)) != 0)) {
                    d->touchedObjs.insert(obj);
                    if (res == 1 || checkPartial) {
                        FC_WARN(obj->getFullName() << '.' << prop->getName() << ": " << errMsg);
                        setStatus(Document::LinkStampChanged, true);
                        if (checkPartial) {
                            return false;
                        }
                    }
                    else {
                        FC_ERR(obj->getFullName() << '.' << prop->getName() << ": " << errMsg);
                        d->addRecomputeLog(errMsg, obj);
                        setStatus(Document::PartialRestore, true);
                    }
                }
            }
        }

        if (checkPartial && !d->touchedObjs.empty()) {
            // partial document touched, signal full reload
            return false;
        }
        if (!d->touchedObjs.contains(obj)) {
            obj->purgeTouched();
        }

        signalFinishRestoreObject(*obj);
    }

    d->touchedObjs.clear();
    return true;
}
