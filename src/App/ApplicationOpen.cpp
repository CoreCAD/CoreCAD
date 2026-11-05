// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License (LGPL)   *
 *   as published by the Free Software Foundation; either version 2 of     *
 *   the License, or (at your option) any later version.                   *
 *   for detail see the LICENCE text file.                                 *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful,            *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with FreeCAD; if not, write to the Free Software        *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
 *   USA                                                                   *
 *                                                                         *
 ***************************************************************************/

#include <FCConfig.h>

# if defined(FC_OS_LINUX) || defined(FC_OS_MACOSX) || defined(FC_OS_BSD)
#  include <unistd.h>
#  include <pwd.h>
#  include <sys/types.h>
# elif defined(__MINGW32__)
#  undef WINVER
#  define WINVER 0x502 // needed for SetDllDirectory
#  include <Windows.h>
# endif

# include <boost/algorithm/string.hpp>
# include <boost/program_options.hpp>
# include <boost/date_time/posix_time/posix_time.hpp>
# include <boost/scope_exit.hpp>
# include <chrono>
# include <optional>
# include <random>
# include <memory>
# include <utility>
# include <set>
# include <string>
# include <list>
# include <algorithm>
# include <iostream>
# include <map>
# include <tuple>
# include <vector>
# include <fmt/format.h>

#ifdef FC_OS_WIN32
# include <Shlobj.h>
# include <codecvt>
#endif

#if defined(FC_OS_BSD)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <LibraryVersions.h>

#include <App/MaterialPy.h>
#include <App/MetadataPy.h>
// FreeCAD Base header
#include <Base/AxisPy.h>
#include <Base/BaseClass.h>
#include <Base/BoundBoxPy.h>
#include <Base/ConsoleObserver.h>
#include <Base/ServiceProvider.h>
#include <Base/CoordinateSystemPy.h>
#include <Base/Exception.h>
#include <Base/ExceptionFactory.h>
#include <Base/FileInfo.h>
#include <Base/GeometryPyCXX.h>
#include <Base/Interpreter.h>
#include <Base/MatrixPy.h>
#include <Base/QuantityPy.h>
#include <Base/Parameter.h>
#include <Base/Persistence.h>
#include <Base/PlacementPy.h>
#include <Base/PrecisionPy.h>
#include <Base/ProgressIndicatorPy.h>
#include <Base/RotationPy.h>
#include <Base/UniqueNameManager.h>
#include <Base/Uuid.h>
#include <Base/TimeInfo.h>
#include <Base/SystemHandler.h>
#include <Base/Tools.h>
#include <Base/Translate.h>
#include <Base/Type.h>
#include <Base/TypePy.h>
#include <Base/UnitPy.h>
#include <Base/UnitsApi.h>
#include <Base/VectorPy.h>

#include "Annotation.h"
#include "Application.h"
#include "ApplicationDirectories.h"
#include "ApplicationDirectoriesPy.h"
#include "ApplicationPy.h"
#include "CleanupProcess.h"
#include "ComplexGeoData.h"
#include "ConsoleQtBridge.h"
#include "TranslationQtBridge.h"
#include "Services.h"
#include "DocumentObjectFileIncluded.h"
#include "DocumentObjectGroup.h"
#include "DocumentObjectGroupPy.h"
#include "DocumentObserver.h"
#include "DocumentPy.h"
#include "ExpressionParser.h"
#include "FeatureTest.h"
#include "FeaturePython.h"
#include "GeoFeature.h"
#include "GeoFeatureGroupExtension.h"
#include "GeoFeatureGroupExtensionPy.h"
#include "GroupExtensionPy.h"
#include "ImagePlane.h"
#include "InventorObject.h"
#include "Link.h"
#include "LinkBaseExtensionPy.h"
#include "VarSet.h"
#include "Configuration.h"
#include "MaterialObject.h"
#include "MeasureManagerPy.h"
#include "Origin.h"
#include "Datums.h"
#include "PlacementExtension.h"
#include "ProgramInformation.h"
#include "SuppressibleExtension.h"
#include "GeoFeaturePy.h"
#include "Placement.h"
#include "Property.h"
#include "PropertyContainer.h"
#include "PropertyExpressionEngine.h"
#include "PropertyFile.h"
#include "PropertyLinks.h"
#include "PropertyPythonObject.h"
#include "StringHasherPy.h"
#include "StringIDPy.h"
#include "TextDocument.h"
#include "Transactions.h"
#include "VRMLObject.h"

// If you stumble here, run the target "BuildExtractRevision" on Windows systems
// or the Python script "SubWCRev.py" on Linux based systems which builds
// src/Build/Version.h. Or create your own from src/Build/Version.h.in!
#include <Build/Version.h>
#include "Branding.h"



#include "SafeMode.h"

#ifdef FC_OS_WIN32
#include <windows.h>
#endif


FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;
namespace sp = std::placeholders;
namespace fs = std::filesystem;


int Application::addPendingDocument(const char *FileName, const char *objName, bool allowPartial)
{
    if(!_isRestoring)
        return 0;
    if(allowPartial && _allowPartial)
        return -1;
    assert(!Base::Tools::isNullOrEmpty(FileName));
    assert(!Base::Tools::isNullOrEmpty(objName));
    if(!_docReloadAttempts[FileName].emplace(objName).second)
        return -1;
    const auto ret = _pendingDocMap.emplace(FileName,std::vector<std::string>());
    ret.first->second.emplace_back(objName);
    if(ret.second) {
        _pendingDocs.emplace_back(ret.first->first.c_str());
        return 1;
    }
    return -1;
}

struct DocTiming {
    std::chrono::duration<double> d1;
    std::chrono::duration<double> d2;
    DocTiming()
        : d1(std::chrono::duration<double>(0))
        , d2(std::chrono::duration<double>(0))
    {}
};

class DocOpenGuard {
public:
    bool &flag;
    fastsignals::signal<void ()> &signal;
    DocOpenGuard(bool &f, fastsignals::signal<void ()> &s)
        :flag(f),signal(s)
    {
        flag = true;
    }
    ~DocOpenGuard() {
        if(flag) {
            flag = false;
            try {
                signal();
            }
            catch (const boost::exception&) {
                // reported by code analyzers
                Base::Console().warning("~DocOpenGuard: Unexpected boost exception\n");
            }
        }
    }
};

Document* Application::openDocument(const char * FileName, DocumentInitFlags initFlags) {
    std::vector<std::string> filenames(1,FileName);
    auto docs = openDocuments(filenames, nullptr, nullptr, nullptr, initFlags);
    if(!docs.empty())
        return docs.front();
    return nullptr;
}

Document *Application::getDocumentByPath(const char *path, PathMatchMode checkCanonical) const {
    if(Base::Tools::isNullOrEmpty(path))
        return nullptr;
    if (DocFileMap.empty()) {
        for(const auto &v : DocMap) {
            const auto &file = v.second->FileName.getStrValue();
            if(!file.empty())
                DocFileMap[Base::FileInfo(file.c_str()).filePath()] = v.second;
        }
    }
    const auto it = DocFileMap.find(Base::FileInfo(path).filePath());
    if(it != DocFileMap.end())
        return it->second;

    if (checkCanonical == PathMatchMode::MatchAbsolute) {
        return nullptr;
    }

    const std::string filepath = Base::FileInfo(path).filePath();
    const QString canonicalPath = QFileInfo(QString::fromUtf8(path)).canonicalFilePath();
    for (const auto &v : DocMap) {
        QFileInfo fi(QString::fromUtf8(v.second->FileName.getValue()));
        if (canonicalPath == fi.canonicalFilePath()) {
            if (checkCanonical == PathMatchMode::MatchCanonical) {
                return v.second;
            }
            const bool samePath = (canonicalPath == QString::fromUtf8(filepath.c_str()));
            FC_WARN("Identical physical path '" << canonicalPath.toUtf8().constData() << "'\n"
                    << (samePath?"":"  for file '") << (samePath?"":filepath.c_str()) << (samePath?"":"'\n")
                    << "  with existing document '" << v.second->Label.getValue()
                    << "' in path: '" << v.second->FileName.getValue() << "'");
            break;
        }
    }
    return nullptr;
}

std::vector<Document*> Application::openDocuments(const std::vector<std::string> &filenames,
                                                  const std::vector<std::string> *paths,
                                                  const std::vector<std::string> *labels,
                                                  std::vector<std::string> *errs,
                                                  DocumentInitFlags initFlags)
{
    std::vector<Document*> res(filenames.size(), nullptr);
    if (filenames.empty())
        return res;

    if (errs)
        errs->resize(filenames.size());

    DocOpenGuard guard(_isRestoring, signalFinishOpenDocument);
    _pendingDocs.clear();
    _pendingDocsReopen.clear();
    _pendingDocMap.clear();
    _docReloadAttempts.clear();

    signalStartOpenDocument();

    ParameterGrp::handle hGrp = GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document");
    _allowPartial = !hGrp->GetBool("NoPartialLoading",false);

    for (auto &name : filenames)
        _pendingDocs.emplace_back(name.c_str());

    std::map<DocumentT, DocTiming> timings;

    Base::TimeTracker tracker("Application::openDocuments");

    std::vector<DocumentT> openedDocs;

    int pass = 0;
    do {
        std::set<DocumentT> newDocs;
        for (std::size_t count=0;; ++count) {
            std::string name = std::move(_pendingDocs.front());
            _pendingDocs.pop_front();
            bool isMainDoc = (pass == 0 && count < filenames.size());

            try {
                _objCount = -1;
                std::vector<std::string> objNames;
                if (_allowPartial) {
                    auto it = _pendingDocMap.find(name);
                    if (it != _pendingDocMap.end()) {
                        if(isMainDoc)
                            it->second.clear();
                        else
                            objNames.swap(it->second);
                        _pendingDocMap.erase(it);
                    }
                }

                DocTiming timing;
                std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

                const char *path = name.c_str();
                const char *label = nullptr;
                if (isMainDoc) {
                    if (paths && paths->size()>count)
                        path = (*paths)[count].c_str();

                    if (labels && labels->size()>count)
                        label = (*labels)[count].c_str();
                }

                auto doc = openDocumentPrivate(path, name.c_str(), label, isMainDoc, initFlags, std::move(objNames));
                timing.d1 += std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - startTime);
                if (doc) {
                    timings[doc].d1 += timing.d1;
                    newDocs.emplace(doc);
                }

                if (isMainDoc)
                    res[count] = doc;
                _objCount = -1;
            }
            catch (const Base::Exception &e) {
                e.reportException();
                if (!errs && isMainDoc)
                    throw;
                if (errs && isMainDoc)
                    (*errs)[count] = e.what();
                else
                    Base::Console().error("Exception opening file: %s [%s]\n", name.c_str(), e.what());
            }
            catch (const std::exception &e) {
                if (!errs && isMainDoc)
                    throw;
                if (errs && isMainDoc)
                    (*errs)[count] = e.what();
                else
                    Base::Console().error("Exception opening file: %s [%s]\n", name.c_str(), e.what());
            }
            catch (...) {
                if (errs) {
                    if (isMainDoc)
                        (*errs)[count] = "unknown error";
                }
                else {
                    _pendingDocs.clear();
                    _pendingDocsReopen.clear();
                    _pendingDocMap.clear();
                    throw;
                }
            }

            if (_pendingDocs.empty()) {
                if(_pendingDocsReopen.empty())
                    break;
                _pendingDocs = std::move(_pendingDocsReopen);
                _pendingDocsReopen.clear();
                for(const auto &file : _pendingDocs) {
                    auto doc = getDocumentByPath(file.c_str());
                    if(doc)
                        closeDocument(doc->getName());
                }
            }
        }

        ++pass;
        _pendingDocMap.clear();

        std::vector<Document*> docs;
        docs.reserve(newDocs.size());
        for(const auto &d : newDocs) {
            auto doc = d.getDocument();
            if(!doc)
                continue;
            // Notify PropertyXLink to attach newly opened documents and restore
            // relevant external links
            PropertyXLink::restoreDocument(*doc);
            docs.push_back(doc);
        }

        Base::SequencerLauncher seq("Postprocessing...", docs.size());

        // After external links has been restored, we can now sort the document
        // according to their dependency order.
        try {
            docs = Document::getDependentDocuments(docs, true);
        } catch (Base::Exception &e) {
            e.reportException();
        }
        for(auto it=docs.begin(); it!=docs.end();) {
            auto doc = *it;

            // It is possible that the newly opened document depends on an existing
            // document, which will be included with the above call to
            // Document::getDependentDocuments(). Make sure to exclude that.
            if(!newDocs.contains(doc)) {
                it = docs.erase(it);
                continue;
            }

            auto &timing = timings[doc];
            std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

            // Finalize document restoring with the correct order
            if(doc->afterRestore(true)) {
                openedDocs.emplace_back(doc);
                it = docs.erase(it);
            } else {
                ++it;
                // Here means this is a partial loaded document, and we need to
                // reload it fully because of touched objects. The reason of
                // reloading a partial document with touched object is because
                // partial document is supposed to be readonly, while a
                // 'touched' object requires recomputation. And an object may
                // become touched during restoring if externally linked
                // document time stamp mismatches with the stamp saved.
                _pendingDocs.emplace_back(doc->FileName.getValue());
                _pendingDocMap.erase(doc->FileName.getValue());
            }
            timing.d2 += std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::high_resolution_clock::now() - startTime);
            seq.next();
        }
        // Close the document for reloading
        for(const auto doc : docs)
            closeDocument(doc->getName());

    }while(!_pendingDocs.empty());

    // Set the active document using the first successfully restored main
    // document (i.e. documents explicitly asked for by caller).
    for (auto doc : res) {
        if (doc) {
            setActiveDocument(doc);
            break;
        }
    }

    for (auto &doc : openedDocs) {
        auto &timing = timings[doc];
        Base::Console().log("%s restore time: %f\n", doc.getDocumentName(), timing.d1.count());
        Base::Console().log("%s postprocess time: %f\n", doc.getDocumentName(), timing.d2.count());
    }
    PropertyLinkBase::updateAllElementReferences();
    _isRestoring = false;

    signalFinishOpenDocument();
    return res;
}

Document* Application::openDocumentPrivate(const char * FileName,
        const char *propFileName, const char *label,
        bool isMainDoc, DocumentInitFlags initFlags,
        std::vector<std::string> &&objNames)
{
    Base::FileInfo File(FileName);

    if (!File.exists()) {
        std::stringstream str;
        str << "File '" << FileName << "' does not exist!";
        throw Base::FileSystemError(str.str());
    }

    // Before creating a new document we check whether the document is already open
    auto doc = getDocumentByPath(File.filePath().c_str(), PathMatchMode::MatchCanonicalWarning);
    if(doc) {
        if(doc->testStatus(Document::PartialDoc)
                || doc->testStatus(Document::PartialRestore)) {
            // Here means a document is already partially loaded, but the document
            // is requested again, either partial or not. We must check if the
            // document contains the required object

            if(isMainDoc) {
                // Main document must be open fully, so close and reopen
                closeDocument(doc->getName());
                doc = nullptr;
            } else if(_allowPartial) {
                bool reopen = false;
                for(const auto &name : objNames) {
                    auto obj = doc->getObject(name.c_str());
                    if(!obj || obj->testStatus(PartialObject)) {
                        reopen = true;
                        // NOTE: We are about to reload this document with
                        // extra objects. However, it is possible to repeat
                        // this process several times, if it is linked by
                        // multiple documents and each with a different set of
                        // objects. To partially solve this problem, we do not
                        // close and reopen the document immediately here, but
                        // add it to _pendingDocsReopen to delay reloading.
                        for(auto obj2 : doc->getObjects())
                            objNames.emplace_back(obj2->getNameInDocument());
                        _pendingDocMap[doc->FileName.getValue()] = objNames;
                        break;
                    }
                }
                if(!reopen)
                    return nullptr;
            }

            if(doc) {
                _pendingDocsReopen.emplace_back(FileName);
                return nullptr;
            }
        }

        if (!isMainDoc) {
            return nullptr;
        }

        if (doc) {
            return doc;
        }
    }

    std::string name;
    if(propFileName != FileName) {
        Base::FileInfo fi(propFileName);
        name = fi.fileNamePure();
    }else
        name = File.fileNamePure();

    // Use the same name for the internal and user name.
    // The file name is UTF-8 encoded which means that the internal name will be modified
    // to only contain valid ASCII characters but the user name will be kept.
    if(!label)
        label = name.c_str();

    initFlags.createView &= isMainDoc;
    Document* newDoc = newDocument(name.c_str(), label, initFlags);
    newDoc->FileName.setValue(propFileName==FileName?File.filePath():propFileName);

    try {
        // read the document
        newDoc->restore(File.filePath().c_str(),true,objNames);
        if (!settleDuplicate(*newDoc, isMainDoc ? initFlags.duplicate : DuplicateAnswer::Unanswered)) {
            closeDocument(newDoc->getName());
            return nullptr;
        }
        if(!DocFileMap.empty())
            DocFileMap[Base::FileInfo(newDoc->FileName.getValue()).filePath()] = newDoc;
        return newDoc;
    }
    // if the project file itself is corrupt then
    // close the document
    catch (const Base::FileException&) {
        closeDocument(newDoc->getName());
        throw;
    }
    catch (const std::ios_base::failure&) {
        closeDocument(newDoc->getName());
        throw;
    }
    // A typed document carrying out-of-scope content refuses to open (Amendment 8
    // Clause 8.1): discard the partially built document and fail loud, rather than
    // leaving a half-loaded document with the offending object silently dropped.
    catch (const DocumentContentScopeError&) {
        closeDocument(newDoc->getName());
        throw;
    }
    // A file whose record cannot be determined does not open at all (Amendment 19
    // Clause 19.2): discard what was read rather than hand back the beginning of the
    // file as though it were the document. Leaving it open is the leniency that makes
    // sense for a document that merely failed to rebuild, and is exactly wrong here --
    // the fragment looks like the part, and a save would publish it over the whole of
    // the only remaining copy. The exception carries what was wrong and where.
    catch (const DocumentMalformedError&) {
        closeDocument(newDoc->getName());
        throw;
    }
    catch (const DocumentDuplicateError&) {
        closeDocument(newDoc->getName());
        throw;
    }
    // but for any other exceptions leave it open to give the
    // user a chance to fix it
    catch (...) {
        throw;
    }
}

bool Application::settleDuplicate(Document& newcomer, DuplicateAnswer answer)
{
    const Document* live = nullptr;
    for (const auto& [name, doc] : DocMap) {
        if (doc != &newcomer && doc->Uid.getValueStr() == newcomer.Uid.getValueStr()) {
            live = doc;
            break;
        }
    }
    if (!live) {
        return true;
    }
    const std::string path = newcomer.FileName.getStrValue();
    if (answer == DuplicateAnswer::Unanswered) {
        if (auto* question = Base::provideService<DuplicateDocumentQuestion>()) {
            answer = question->ask(path, *live);
        }
    }
    switch (answer) {
        case DuplicateAnswer::SamePart:
            return true;
        case DuplicateAnswer::NewPart:
            newcomer.Uid.setValue(Base::Uuid());
            newcomer.setStatus(Document::GivenNewIdentity, true);
            return true;
        case DuplicateAnswer::DontOpen:
            return false;
        case DuplicateAnswer::Unanswered:
            break;
    }
    throw DocumentDuplicateError(
        "'" + path + "' is a copy of '" + live->FileName.getStrValue()
        + "', which is already open. Say whether it is the same part or a new one: "
          "openDocument(path, duplicate='same') or openDocument(path, duplicate='new')."
    );
}
