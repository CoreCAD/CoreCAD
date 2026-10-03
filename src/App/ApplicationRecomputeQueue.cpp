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


namespace
{

RecomputeRequest takeNextRecomputeRequest(std::deque<RecomputeRequest>& requests)
{
    RecomputeRequest request = std::move(requests.front());
    requests.pop_front();
    return request;
}

bool requestTargetsDocument(const RecomputeRequest& request, const std::string& documentName)
{
    return request.documentName == documentName;
}

bool documentCanRecomputeOnWorker(const Document& document)
{
    try {
        const auto& objects = document.getObjects();
        std::vector<DocumentObject*> recomputeRoots(objects.begin(), objects.end());
        const auto recomputeObjects = Document::getDependencyList(recomputeRoots, Document::DepSort);

        return std::ranges::all_of(recomputeObjects, [](const DocumentObject* object) {
            return object && object->canRecomputeOnWorker();
        });
    }
    catch (const Base::BadGraphError&) {
        return false;
    }
}

void reportRecomputeException(const Base::Exception& exception)
{
    if (App::MainThreadSignalConfig::hasHooks()) {
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(
                app,
                [exception]() mutable { exception.reportException(); },
                Qt::QueuedConnection
            );
            return;
        }
    }

    exception.reportException();
}

RecomputeResult processRecomputeRequest(RecomputeRequest& request)
{
    RecomputeResult result;

    try {
        if (Document* document = request.resolveDocument()) {
            document->recompute({}, request.force, nullptr, request.options);
        }

        if (DocumentObject* documentObject = request.resolveDocumentObject()) {
            documentObject->recomputeFeature(request.recursive);
        }
    }
    catch (Base::BadGraphError& exception) {
        result.exception = std::make_unique<Base::BadGraphError>(std::move(exception));
        result.failure = RecomputeFailure::DependencyCycle;
        result.success = false;
    }
    catch (Base::Exception& exception) {
        reportRecomputeException(exception);
        result.exception = std::make_unique<Base::Exception>(std::move(exception));
        result.failure = RecomputeFailure::Exception;
        result.success = false;
    }

    return result;
}

}  // namespace
RecomputeRequest RecomputeRequest::fromDocument(const Document& document, bool force, int options)
{
    RecomputeRequest request;
    request.documentName = document.getName();
    request.force = force;
    request.options = options;
    return request;
}

RecomputeRequest RecomputeRequest::fromDocumentObject(const DocumentObject& documentObject, bool recursive)
{
    RecomputeRequest request;

    if (const Document* document = documentObject.getDocument()) {
        request.documentName = document->getName();
    }

    request.documentObjectName = documentObject.getNameInDocument();
    request.recursive = recursive;
    return request;
}

Document* RecomputeRequest::resolveDocument() const
{
    if (documentName.empty()) {
        return nullptr;
    }

    return GetApplication().getDocument(documentName.c_str());
}

DocumentObject* RecomputeRequest::resolveDocumentObject() const
{
    if (documentObjectName.empty()) {
        return nullptr;
    }

    if (Document* document = resolveDocument()) {
        return document->getObject(documentObjectName.c_str());
    }

    return nullptr;
}
bool Application::isAsyncRecomputeEnabled()
{
    static const ParameterGrp::handle hGrp = GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Document"
    );
    bool enableAsyncRecompute = hGrp->GetBool("EnableAsyncRecompute", true);
    return enableAsyncRecompute;
}

bool Application::canRecomputeRequestOnWorker(const RecomputeRequest& req) const
{
    if (DocumentObject* documentObject = req.resolveDocumentObject()) {
        return documentObject->canRecomputeOnWorker();
    }

    Document* document = req.resolveDocument();
    return !document || documentCanRecomputeOnWorker(*document);
}

void Application::queueRecomputeRequest(RecomputeRequest req)
{
    if (!canRecomputeRequestOnWorker(req)) {
        RecomputeResult result;

        // Requests that are not worker-safe stay on the caller thread unless a
        // GUI main-thread hop is required. In App-only/headless mode there are
        // no GUI hooks, so processing inline preserves the "stay off the
        // worker" guarantee without inventing a synthetic main thread.
        if (App::MainThreadSignalConfig::hasHooks()
            && !App::MainThreadSignalConfig::isMainThread()) {
            App::MainThreadSignalConfig::invoke(
                [&req, &result]() { result = processRecomputeRequest(req); },
                /*blocking=*/true
            );
        }
        else {
            result = processRecomputeRequest(req);
        }

        if (req.callback) {
            req.callback(req, result);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_recomputeMutex);
        _recomputeRequests.push_back(std::move(req));
    }
    notifyRecomputeWorker();
}

void Application::cancelRecomputeRequestsForDocument(const std::string& documentName)
{
    if (documentName.empty()) {
        return;
    }

    std::unique_lock<std::mutex> lock(_recomputeMutex);
    _recomputeStateChanged.wait(lock, [this, &documentName] {
        return !_recomputeDocumentsInProgress.contains(documentName);
    });

    // Cancellation runs on document-close boundaries, so a linear scan keeps
    // the queue simple without affecting the steady-state worker path.
    std::erase_if(_recomputeRequests, [&documentName](const RecomputeRequest& request) {
        return requestTargetsDocument(request, documentName);
    });
}

void Application::notifyRecomputeWorker()
{
    _recomputeRequestAvailable.notify_one();
}

void Application::recomputeWorker()
{
    while (!_stopRecomputeThread) {
        std::unique_lock<std::mutex> lock(_recomputeMutex);
        // Wait until either stop is signaled or there is at least one pending request.
        _recomputeRequestAvailable.wait(lock, [this] {
            return _stopRecomputeThread || !_recomputeRequests.empty();
        });
        if (_stopRecomputeThread) {
            break;
        }

        // Process all pending recompute requests.
        while (!_recomputeRequests.empty()) {
            RecomputeRequest request = takeNextRecomputeRequest(_recomputeRequests);
            if (!request.documentName.empty()) {
                _recomputeDocumentsInProgress.insert(request.documentName);
            }

            // Unlock while processing to allow other threads to add new requests.
            lock.unlock();

            RecomputeResult result = processRecomputeRequest(request);

            if (request.callback) {
                request.callback(request, result);
            }

            lock.lock();
            if (!request.documentName.empty()) {
                _recomputeDocumentsInProgress.erase(request.documentName);
                _recomputeStateChanged.notify_all();
            }
        }
    }
}
