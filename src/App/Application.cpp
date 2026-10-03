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


//==========================================================================
// Application
//==========================================================================

Base::Reference<ParameterManager> Application::_pcSysParamMngr;
Base::Reference<ParameterManager> Application::_pcUserParamMngr;
Base::ConsoleObserverStd  *Application::_pConsoleObserverStd = nullptr;
Base::ConsoleObserverFile *Application::_pConsoleObserverFile = nullptr;

AppExport std::map<std::string, std::string> Application::mConfig;
std::unique_ptr<ApplicationDirectories> Application::_appDirs;



//**************************************************************************
// Construction and destruction

Application::Application(std::map<std::string,std::string> &mConfig)
  : _mConfig(mConfig)
{
    mpcPramManager["System parameter"] = _pcSysParamMngr;
    mpcPramManager["User parameter"] = _pcUserParamMngr;

    _stopRecomputeThread = false;
    _recomputeThread = std::thread(&Application::recomputeWorker, this);

    setupPythonTypes();
}

Application::~Application()
{
    // Signal the recompute worker thread to stop and join it.
    _stopRecomputeThread = true;
    _recomputeRequestAvailable.notify_all();

    if (_recomputeThread.joinable()) {
        _recomputeThread.join();
    }
}

//**************************************************************************
// Interface

Document* Application::newDocument(const char * proposedName, const char * proposedLabel, DocumentInitFlags CreateFlags)
{
    bool isUsingDefaultName = Base::Tools::isNullOrEmpty(proposedName);
    // get a valid name anyway!
    if (isUsingDefaultName) {
        proposedName = "Unnamed";
    }
    std::string name(getUniqueDocumentName(proposedName, CreateFlags.temporary));

    // return the temporary document if it exists
    if (CreateFlags.temporary) {
        auto it = DocMap.find(name);
        if (it != DocMap.end() && it->second->testStatus(Document::TempDoc)) {
            return it->second;
        }
    }

    // Determine the document's Label
    std::string label;
    if (!Base::Tools::isNullOrEmpty(proposedLabel)) {
        // If a label is supplied it is used even if not unique
        label = proposedLabel;
    }
    else {
        label = isUsingDefaultName ? QObject::tr("Unnamed").toStdString() : proposedName;

        if (!DocMap.empty()) {
            // The assumption here is that there are not many documents and
            // documents are rarely created so the cost
            // of building this manager each time is inconsequential
            Base::UniqueNameManager names;
            for (const auto& pos : DocMap) {
                names.addExactName(pos.second->Label.getValue());
            }

            label = names.makeUniqueName(label);
        }
    }
    // create the FreeCAD document
    auto doc = new Document(name.c_str());
    doc->setStatus(Document::TempDoc, CreateFlags.temporary);

    // add the document to the internal list
    DocMap[name] = doc;

    //NOLINTBEGIN
    // clang-format off
    // connect the signals to the application for the new document
    doc->signalBeforeChange.connect(std::bind(&Application::slotBeforeChangeDocument, this, sp::_1, sp::_2));
    doc->signalChanged.connect(std::bind(&Application::slotChangedDocument, this, sp::_1, sp::_2));
    doc->signalNewObject.connect(std::bind(&Application::slotNewObject, this, sp::_1));
    doc->signalDeletedObject.connect(std::bind(&Application::slotDeletedObject, this, sp::_1));
    doc->signalBeforeChangeObject.connect(std::bind(&Application::slotBeforeChangeObject, this, sp::_1, sp::_2));
    doc->signalChangedObject.connect(std::bind(&Application::slotChangedObject, this, sp::_1, sp::_2));
    doc->signalRelabelObject.connect(std::bind(&Application::slotRelabelObject, this, sp::_1));
    doc->signalActivatedObject.connect(std::bind(&Application::slotActivatedObject, this, sp::_1));
    doc->signalUndo.connect(std::bind(&Application::slotUndoDocument, this, sp::_1));
    doc->signalRedo.connect(std::bind(&Application::slotRedoDocument, this, sp::_1));
    doc->signalRecomputedObject.connect(std::bind(&Application::slotRecomputedObject, this, sp::_1));
    doc->signalRecomputed.connect(std::bind(&Application::slotRecomputed, this, sp::_1));
    doc->signalBeforeRecompute.connect(std::bind(&Application::slotBeforeRecompute, this, sp::_1));
    doc->signalOpenTransaction.connect(std::bind(&Application::slotOpenTransaction, this, sp::_1, sp::_2));
    doc->signalCommitTransaction.connect(std::bind(&Application::slotCommitTransaction, this, sp::_1));
    doc->signalAbortTransaction.connect(std::bind(&Application::slotAbortTransaction, this, sp::_1));
    doc->signalStartSave.connect(std::bind(&Application::slotStartSaveDocument, this, sp::_1, sp::_2));
    doc->signalFinishSave.connect(std::bind(&Application::slotFinishSaveDocument, this, sp::_1, sp::_2));
    doc->signalChangePropertyEditor.connect(std::bind(&Application::slotChangePropertyEditor, this, sp::_1, sp::_2));
    // clang-format on
    //NOLINTEND

    // (temporarily) make this the active document for the upcoming notifications.
    // Signal NewDocument rather than ActiveDocument (which is what setActiveDocument would do)
    auto oldActiveDoc = _pActiveDoc;
    setActiveDocumentNoSignal(doc);
    signalNewDocument(*doc, CreateFlags.createView);

    doc->Label.setValue(label);

    // Cruth: stamp the document type at creation. A typed CAD document (e.g. "Part")
    // owns its world frame and mints it here, so a fresh document has planes/axes
    // immediately — no body is needed to bring the coordinate system into existence.
    if (!CreateFlags.documentType.empty()) {
        doc->applyDocumentType(CreateFlags.documentType.c_str());
    }

    // set the old document active again if the new is temporary
    if (CreateFlags.temporary && oldActiveDoc) {
        setActiveDocument(oldActiveDoc);
    }
    return doc;
}

bool Application::closeDocument(const Document* doc)
{
    return closeDocument(doc->getName());
}

bool Application::closeDocument(const char* name)
{
    const std::string documentName(name);

    cancelRecomputeRequestsForDocument(documentName);

    const auto pos = DocMap.find( name );
    if (pos == DocMap.end()) // no such document
        return false;

    Base::ConsoleRefreshDisabler disabler;

    // Trigger observers before removing the document from the internal map.
    // Some observers might rely on this document still being there.
    signalDeleteDocument(*pos->second);

    // For exception-safety use a smart pointer
    if (_pActiveDoc == pos->second) {
        setActiveDocument(static_cast<Document*>(nullptr));
    }
    const std::unique_ptr<Document> delDoc (pos->second);
    DocMap.erase( pos );
    DocFileMap.erase(Base::FileInfo(delDoc->FileName.getValue()).filePath());

    _objCount = -1;

    // Trigger observers after removing the document from the internal map.
    signalDeletedDocument();

    return true;
}

void Application::closeAllDocuments()
{
    Base::FlagToggler<bool> flag(_isClosingAll);
    std::map<std::string,Document*>::iterator pos;
    while((pos = DocMap.begin()) != DocMap.end())
        closeDocument(pos->first.c_str());
}

Document* Application::getDocument(const char *Name) const
{

    const auto pos = DocMap.find(Name);

    if (pos == DocMap.end())
        return nullptr;

    return pos->second;
}
Document* Application::getDocumentOrActive(const char *Name) const
{
    if (!Base::Tools::isNullOrEmpty(Name)) {
        return getDocument(Name);
    }
    else {
        return getActiveDocument();
    }
}

const char * Application::getDocumentName(const Document* doc) const
{
    for (const auto & it : DocMap) {
        if (it.second == doc) {
            return it.first.c_str();
        }
    }

    return nullptr;
}

std::vector<Document*> Application::getDocuments() const
{
    std::vector<Document*> docs;
    docs.reserve(DocMap.size());
    for (const auto & it : DocMap)
        docs.push_back(it.second);
    return docs;
}

Document* Application::getDocumentByUuid(const Base::Uuid& uuid) const
{
    const std::string& key = uuid.getValue();
    if (key.empty()) {
        return nullptr;
    }
    // Documents in one session are few; a linear scan over the durable UUID is
    // cheap enough. (If this ever becomes hot, mirror Document::getObjectByUuid
    // and maintain a UUID -> document map invalidated on open/close.)
    for (const auto& it : DocMap) {
        if (it.second->Uid.getValueStr() == key) {
            return it.second;
        }
    }
    return nullptr;
}

std::string Application::getUniqueDocumentName(const char* Name, bool tempDoc) const
{
    if (!Name || *Name == '\0') {
        return {};
    }
    std::string CleanName = Base::Tools::getIdentifier(Name);

    // name in use?
    auto pos = DocMap.find(CleanName);

    if (pos == DocMap.end() || (tempDoc && pos->second->testStatus(Document::TempDoc))) {
        // if not, name is OK
        return CleanName;
    }
    // The assumption here is that there are not many documents and
    // documents are rarely created so the cost
    // of building this manager each time is inconsequential
    Base::UniqueNameManager names;
    for (const auto& pos : DocMap) {
        if (!tempDoc || !pos.second->testStatus(Document::TempDoc)) {
            names.addExactName(pos.first);
        }
    }

    return names.makeUniqueName(CleanName);
}

bool Application::isRestoring() const {
    return _isRestoring || Document::isAnyRestoring();
}

bool Application::isClosingAll() const {
    return _isClosingAll;
}

Document* Application::getActiveDocument() const
{
    return _pActiveDoc;
}

void Application::setActiveDocument(Document* pDoc)
{
    setActiveDocumentNoSignal(pDoc);

    if (pDoc) {
        signalActiveDocument(*pDoc);
    }
}

void Application::setActiveDocumentNoSignal(Document* pDoc)
{
    _pActiveDoc = pDoc;

    // make sure that the active document is set in case no GUI is up
    if (pDoc) {
        Base::PyGILStateLocker lock;
        const Py::Object active(pDoc->getPyObject(), true);
        Py::Module("FreeCAD").setAttr(std::string("ActiveDocument"), active);
    }
    else {
        Base::PyGILStateLocker lock;
        Py::Module("FreeCAD").setAttr(std::string("ActiveDocument"), Py::None());
    }
}

void Application::setActiveDocument(const char* Name)
{
    // If no active document is set, resort to a default.
    if (*Name == '\0') {
        _pActiveDoc = nullptr;
        return;
    }

    if (const auto pos = DocMap.find(Name); pos != DocMap.end()) {
        setActiveDocument(pos->second);
    }
    else {
        std::stringstream s;
        s << "Try to activate unknown document '" << Name << "'";
        throw Base::RuntimeError(s.str());
    }
}

static int _TransSignalCount;
static bool _TransSignalled;
Application::TransactionSignaller::TransactionSignaller(bool abort, bool signal)
    :abort(abort)
{
    ++_TransSignalCount;
    if(signal && !_TransSignalled) {
        _TransSignalled = true;
        GetApplication().signalBeforeCloseTransaction(abort);
    }
}

Application::TransactionSignaller::~TransactionSignaller() {
    if(--_TransSignalCount == 0 && _TransSignalled) {
        _TransSignalled = false;
        try {
            GetApplication().signalCloseTransaction(abort);
        }
        catch (const boost::exception&) {
            // reported by code analyzers
            Base::Console().warning("~TransactionSignaller: Unexpected boost exception\n");
        }
    }
}

int64_t Application::applicationPid()
{
    static int64_t randomNumber = []() {
        const auto tp = std::chrono::high_resolution_clock::now();
        const auto dur = tp.time_since_epoch();
        const auto seed = dur.count();
        std::mt19937 generator(static_cast<unsigned>(seed));
        constexpr int64_t minValue {1};
        constexpr int64_t maxValue {1000000};
        std::uniform_int_distribution<int64_t> distribution(minValue, maxValue);
        return distribution(generator);
    }();
    return randomNumber;
}

std::string Application::getHomePath()
{
    return Base::FileInfo::pathToString(Application::directories()->getHomePath()) + PATHSEP;
}

std::string Application::getExecutableName()
{
    return mConfig["ExeName"];
}

std::string Application::getNameWithVersion()
{
    auto appname = QCoreApplication::applicationName().toStdString();
    auto config = Application::Config();

    // Prefer CoreCAD display version (set via branding.xml) over the internal FreeCAD
    // build version, so workbenches and addons continue to see the real FreeCAD version
    // for compatibility checks (e.g. FreeCAD.Version()[1]).
    auto coreIt = config.find("CoreCADVersionMajor");
    if (coreIt != config.end() && !coreIt->second.empty()) {
        auto coreMajor = coreIt->second;
        auto coreMinor = config["CoreCADVersionMinor"];
        auto corePatch = config["CoreCADVersionPatch"];
        auto coreSuffix = config["CoreCADVersionSuffix"];
        return fmt::format("{} {}.{}.{}{}", appname, coreMajor, coreMinor, corePatch, coreSuffix);
    }

    auto major = config["BuildVersionMajor"];
    auto minor = config["BuildVersionMinor"];
    auto point = config["BuildVersionPoint"];
    auto suffix = config["BuildVersionSuffix"];
    return fmt::format("{} {}.{}.{}{}", appname, major, minor, point, suffix);
}

bool Application::isDevelopmentVersion()
{
    static std::string suffix = []() constexpr {
        return FCVersionSuffix;
    }();
    return suffix == "dev";
}

const std::unique_ptr<ApplicationDirectories>& Application::directories() {
    return _appDirs;
}

std::string Application::getTempPath()
{
    return Base::FileInfo::pathToString(_appDirs->getTempPath()) + PATHSEP;
}

std::string Application::getTempFileName(const char* FileName)
{
    return Base::FileInfo::pathToString(_appDirs->getTempFileName(FileName ? FileName : std::string()));
}

std::string Application::getUserCachePath()
{
    return Base::FileInfo::pathToString(_appDirs->getUserCachePath()) + PATHSEP;
}

std::string Application::getUserConfigPath()
{
    return Base::FileInfo::pathToString(_appDirs->getUserConfigPath()) + PATHSEP;
}

std::string Application::getUserAppDataDir()
{
    return Base::FileInfo::pathToString(_appDirs->getUserAppDataDir()) + PATHSEP;
}

std::string Application::getUserMacroDir()
{
    return Base::FileInfo::pathToString(_appDirs->getUserMacroDir()) + PATHSEP;
}

std::string Application::getResourceDir()
{
    return Base::FileInfo::pathToString(_appDirs->getResourceDir()) + PATHSEP;
}

std::string Application::getLibraryDir()
{
    return Base::FileInfo::pathToString(_appDirs->getLibraryDir()) + PATHSEP;
}

std::string Application::getHelpDir()
{
    return Base::FileInfo::pathToString(_appDirs->getHelpDir()) + PATHSEP;
}

int Application::checkLinkDepth(int depth, MessageOption option)
{
    if (_objCount < 0) {
        _objCount = 0;
        for (const auto &v : DocMap) {
            _objCount += v.second->countObjects();
        }
    }

    if (depth > _objCount + 2) {
        const auto msg = "Link recursion limit reached. "
                "Please check for cyclic reference.";
        switch (option) {
        case MessageOption::Quiet:
            return 0;
        case MessageOption::Error:
            FC_ERR(msg);
            return 0;
        case MessageOption::Throw:
            throw Base::RuntimeError(msg);
        }
    }

    return _objCount + 2;
}

std::set<DocumentObject *> Application::getLinksTo(
        const DocumentObject *obj, int options, int maxCount) const
{
    std::set<DocumentObject *> links;
    if(!obj) {
        for(auto &v : DocMap) {
            v.second->getLinksTo(links,obj,options,maxCount);
            if(maxCount && static_cast<int>(links.size())>=maxCount)
                break;
        }
    } else {
        std::set<Document*> docs;
        for (const auto o : obj->getInList()) {
            if(o && o->isAttachedToDocument() && docs.insert(o->getDocument()).second) {
                o->getDocument()->getLinksTo(links,obj,options,maxCount);
                if(maxCount && static_cast<int>(links.size())>=maxCount)
                    break;
            }
        }
    }
    return links;
}

bool Application::hasLinksTo(const DocumentObject *obj) const {
    return !getLinksTo(obj,0,1).empty();
}


//**************************************************************************
// signaling
void Application::slotBeforeChangeDocument(const Document& doc, const Property& prop)
{
    this->signalBeforeChangeDocument(doc, prop);
}

void Application::slotChangedDocument(const Document& doc, const Property& prop)
{
    this->signalChangedDocument(doc, prop);
}

void Application::slotNewObject(const DocumentObject& obj)
{
    this->signalNewObject(obj);
    _objCount = -1;
}

void Application::slotDeletedObject(const DocumentObject& obj)
{
    this->signalDeletedObject(obj);
    _objCount = -1;
}

void Application::slotBeforeChangeObject(const DocumentObject& obj, const Property& prop)
{
    this->signalBeforeChangeObject(obj, prop);
}

void Application::slotChangedObject(const DocumentObject& obj, const Property& prop)
{
    this->signalChangedObject(obj, prop);
}

void Application::slotRelabelObject(const DocumentObject& obj)
{
    this->signalRelabelObject(obj);
}

void Application::slotActivatedObject(const DocumentObject& obj)
{
    this->signalActivatedObject(obj);
}

void Application::slotUndoDocument(const Document& doc)
{
    this->signalUndoDocument(doc);
}

void Application::slotRedoDocument(const Document& doc)
{
    this->signalRedoDocument(doc);
}

void Application::slotRecomputedObject(const DocumentObject& obj)
{
    this->signalObjectRecomputed(obj);
}

void Application::slotRecomputed(const Document& doc)
{
    this->signalRecomputed(doc);
}

void Application::slotBeforeRecompute(const Document& doc)
{
    this->signalBeforeRecomputeDocument(doc);
}

void Application::slotOpenTransaction(const Document &doc, std::string name)
{
    this->signalOpenTransaction(doc, std::move(name));
}

void Application::slotCommitTransaction(const Document& doc)
{
    this->signalCommitTransaction(doc);
}

void Application::slotAbortTransaction(const Document& doc)
{
    this->signalAbortTransaction(doc);
}

void Application::slotStartSaveDocument(const Document& doc, const std::string& filename)
{
    this->signalStartSaveDocument(doc, filename);
}

void Application::slotFinishSaveDocument(const Document& doc, const std::string& filename)
{
    DocFileMap.clear();
    this->signalFinishSaveDocument(doc, filename);
}

void Application::slotChangePropertyEditor(const Document& doc, const Property& prop)
{
    this->signalChangePropertyEditor(doc, prop);
}

//**************************************************************************
// Init, Destruct and singleton

Application * Application::_pcSingleton = nullptr;

int Application::_argc;
char ** Application::_argv;


void Application::cleanupUnits()
{
    try {
        Base::PyGILStateLocker lock;
        Py::Module mod (Py::Module("FreeCAD").getAttr("Units").ptr());

        Py::List attr(mod.dir());
        for (Py::List::iterator it = attr.begin(); it != attr.end(); ++it) {
            mod.delAttr(Py::String(*it));
        }
    }
    catch (Py::Exception& e) {
        Base::PyGILStateLocker lock;
        e.clear();
    }
}

void Application::destruct()
{
    // saving system parameter
    if (_pcSysParamMngr->IgnoreSave()) {
        Base::Console().warning("Discard system parameter\n");
    }
    else {
        Base::Console().log("Saving system parameter...\n");
        _pcSysParamMngr->SaveDocument();
        Base::Console().log("Saving system parameter...done\n");
    }
    // saving the User parameter
    if (_pcUserParamMngr->IgnoreSave()) {
        Base::Console().warning("Discard user parameter\n");
    }
    else {
        Base::Console().log("Saving user parameter...\n");
        _pcUserParamMngr->SaveDocument();
        Base::Console().log("Saving user parameter...done\n");
    }

    // now save all other parameter files
    auto& paramMgr = _pcSingleton->mpcPramManager;
    for (const auto &it : paramMgr) {
        if ((it.second != _pcSysParamMngr) && (it.second != _pcUserParamMngr)) {
            if (it.second->HasSerializer() && !it.second->IgnoreSave()) {
                Base::Console().log("Saving %s...\n", it.first.c_str());
                it.second->SaveDocument();
                Base::Console().log("Saving %s...done\n", it.first.c_str());
            }
        }
    }

    paramMgr.clear();
    _pcSysParamMngr = nullptr;
    _pcUserParamMngr = nullptr;

#ifdef FC_DEBUG
    // Do this only in debug mode for memory leak checkers
    cleanupUnits();
#endif

    CleanupProcess::callCleanup();

    // not initialized or double destruct!
    assert(_pcSingleton);
    delete _pcSingleton;

    // We must detach from console and delete the observer to save our file
    destructObserver();

    Base::Interpreter().finalize();

    Base::ScriptFactorySingleton::Destruct();
    Base::InterpreterSingleton::Destruct();
    Base::Type::destruct();
    ParameterManager::Terminate();
    SafeMode::Destruct();
}

void Application::destructObserver()
{
    if ( _pConsoleObserverFile ) {
        Base::Console().detachObserver(_pConsoleObserverFile);
        delete _pConsoleObserverFile;
        _pConsoleObserverFile = nullptr;
    }
    if ( _pConsoleObserverStd ) {
        Base::Console().detachObserver(_pConsoleObserverStd);
        delete _pConsoleObserverStd;
        _pConsoleObserverStd = nullptr;
    }
}
