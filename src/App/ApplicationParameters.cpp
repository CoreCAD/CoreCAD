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


ParameterManager & Application::GetSystemParameter()
{
    return *_pcSysParamMngr;
}

ParameterManager & Application::GetUserParameter()
{
    return *_pcUserParamMngr;
}

ParameterManager * Application::GetParameterSet(const char* sName) const
{
    const auto it = mpcPramManager.find(sName);

    return it != mpcPramManager.end() ? it->second : nullptr;
}

const std::map<std::string,Base::Reference<ParameterManager>> &
Application::GetParameterSetList() const
{
    return mpcPramManager;
}

void Application::AddParameterSet(const char* sName)
{
    const auto it = mpcPramManager.find(sName);
    if ( it != mpcPramManager.end() )
        return;
    mpcPramManager[sName] = ParameterManager::Create();
}

void Application::RemoveParameterSet(const char* sName)
{
    const auto it = mpcPramManager.find(sName);
    // Must not delete user or system parameter
    if ( it == mpcPramManager.end() || it->second == _pcUserParamMngr || it->second == _pcSysParamMngr )
        return;
    mpcPramManager.erase(it);
}

Base::Reference<ParameterGrp>  Application::GetParameterGroupByPath(const char* sName)
{
    std::string cName = sName, cTemp;

    const std::string::size_type pos = cName.find(':');

    // is there a path separator ?
    if (pos == std::string::npos) {
        throw Base::ValueError("Application::GetParameterGroupByPath() no parameter set name specified");
    }
    // assigning the parameter set name
    cTemp.assign(cName,0,pos);
    cName.erase(0,pos+1);

    // test if name is valid
    const auto It = mpcPramManager.find(cTemp);
    if (It == mpcPramManager.end())
        throw Base::ValueError("Application::GetParameterGroupByPath() unknown parameter set name specified");

    return It->second->GetGroup(cName.c_str());
}

void Application::LoadParameters()
{
    // Init parameter sets ===========================================================
    //
    if (mConfig.find("UserParameter") == mConfig.end())
        mConfig["UserParameter"]   = mConfig["UserConfigPath"] + "user.cfg";
    if (mConfig.find("SystemParameter") == mConfig.end())
        mConfig["SystemParameter"] = mConfig["UserConfigPath"] + "system.cfg";

    // create standard parameter sets
    _pcSysParamMngr = ParameterManager::Create();
    _pcSysParamMngr->SetSerializer(new ParameterSerializer(mConfig["SystemParameter"]));

    _pcUserParamMngr = ParameterManager::Create();
    _pcUserParamMngr->SetSerializer(new ParameterSerializer(mConfig["UserParameter"]));

    try {
        if (_pcSysParamMngr->LoadOrCreateDocument() && mConfig["Verbose"] != "Strict") {
            // Configuration file optional when using as Python module
            if (!Py_IsInitialized()) {
                Base::Console().warning("   Parameter does not exist, writing initial one\n");
                Base::Console().message("   This warning normally means that FreeCAD is running for the first time\n"
                                  "   or the configuration was deleted or moved. FreeCAD is generating the standard\n"
                                  "   configuration.\n");
            }
        }
    }
    catch (const Base::Exception& e) {
        // try to proceed with an empty XML document
        Base::Console().error("%s in file %s.\n"
                              "Continue with an empty configuration.\n",
                              e.what(), mConfig["SystemParameter"].c_str());
        _pcSysParamMngr->CreateDocument();
    }

    try {
        if (_pcUserParamMngr->LoadOrCreateDocument() && mConfig["Verbose"] != "Strict") {
            // The user parameter file doesn't exist. When an alternative parameter file is offered
            // this will be used.
            const auto it = mConfig.find("UserParameterTemplate");
            if (it != mConfig.end()) {
                QString path = QString::fromUtf8(it->second.c_str());
                if (QDir(path).isRelative()) {
                    const QString home = QString::fromUtf8(mConfig["AppHomePath"].c_str());
                    path = QFileInfo(QDir(home), path).absoluteFilePath();
                }
                const QFileInfo fi(path);
                if (fi.exists()) {
                    _pcUserParamMngr->LoadDocument(path.toUtf8().constData());
                }
            }

            // Configuration file optional when using as Python module
            if (!Py_IsInitialized()) {
                Base::Console().warning("   User settings do not exist, writing initial one\n");
                Base::Console().message("   This warning normally means that FreeCAD is running for the first time\n"
                                  "   or your configuration was deleted or moved. The system defaults\n"
                                  "   will be automatically generated for you.\n");
            }
        }
    }
    catch (const Base::Exception& e) {
        // try to proceed with an empty XML document
        Base::Console().error("%s in file %s.\n"
                              "Continue with an empty configuration.\n",
                              e.what(), mConfig["UserParameter"].c_str());
        _pcUserParamMngr->CreateDocument();
    }
}
