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


void Application::addImportType(const char* filter, const char* moduleName)
{
    FileTypeItem item;
    item.filter = filter;
    item.module = moduleName;

    // Extract each filetype from 'Type' literal
    std::string::size_type pos = item.filter.find("*.");
    while ( pos != std::string::npos ) {
        const std::string::size_type next = item.filter.find_first_of(" )", pos + 1);
        const std::string::size_type len = next-pos-2;
        std::string type = item.filter.substr(pos+2,len);
        item.types.push_back(std::move(type));
        pos = item.filter.find("*.", next);
    }

    // Due to branding stuff replace "FreeCAD" with the branded application name
    if (strncmp(filter, "FreeCAD", 7) == 0) {
        std::string AppName = getExecutableName();
        AppName += item.filter.substr(7);
        item.filter = std::move(AppName);
        // put to the front of the array
        _mImportTypes.insert(_mImportTypes.begin(),std::move(item));
    }
    else {
        _mImportTypes.push_back(std::move(item));
    }
}

void Application::changeImportModule(const char* filter, const char* oldModuleName, const char* newModuleName)
{
    for (auto& it : _mImportTypes) {
        if (it.filter == filter && it.module == oldModuleName) {
            it.module = newModuleName;
            break;
        }
    }
}

std::vector<std::string> Application::getImportModules(const std::string& extension) const
{
    std::vector<std::string> modules;
    for (const auto & it : _mImportTypes) {
        const std::vector<std::string>& types = it.types;
        for (const auto & jt : types) {
            if (boost::iequals(extension, jt)) {
                modules.push_back(it.module);
            }
        }
    }

    return modules;
}

std::vector<std::string> Application::getImportModules() const
{
    std::vector<std::string> modules;
    modules.reserve(_mImportTypes.size());
    for (const auto& it : _mImportTypes) {
        modules.push_back(it.module);
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return modules;
}

std::vector<std::string> Application::getImportTypes(const std::string& Module) const
{
    std::vector<std::string> types;
    for (const auto & it : _mImportTypes) {
        if (boost::iequals(Module, it.module)) {
            types.insert(types.end(), it.types.begin(), it.types.end());
        }
    }

    return types;
}

std::vector<std::string> Application::getImportTypes() const
{
    std::vector<std::string> types;
    for (const auto & it : _mImportTypes) {
        types.insert(types.end(), it.types.begin(), it.types.end());
    }

    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());

    return types;
}

std::map<std::string, std::string> Application::getImportFilters(const std::string& extension) const
{
    std::map<std::string, std::string> moduleFilter;
    for (const auto & it : _mImportTypes) {
        const std::vector<std::string>& types = it.types;
        for (const auto & jt : types) {
            if (boost::iequals(extension, jt)) {
                moduleFilter[it.filter] = it.module;
            }
        }
    }

    return moduleFilter;
}

std::map<std::string, std::string> Application::getImportFilters() const
{
    std::map<std::string, std::string> filter;
    for (const auto & it : _mImportTypes) {
        filter[it.filter] = it.module;
    }

    return filter;
}

void Application::addExportType(const char* filter, const char* moduleName)
{
    FileTypeItem item;
    item.filter = filter;
    item.module = moduleName;

    // Extract each filetype from 'Type' literal
    std::string::size_type pos = item.filter.find("*.");
    while ( pos != std::string::npos ) {
        const std::string::size_type next = item.filter.find_first_of(" )", pos + 1);
        const std::string::size_type len = next-pos-2;
        std::string type = item.filter.substr(pos+2,len);
        item.types.push_back(std::move(type));
        pos = item.filter.find("*.", next);
    }

    // Due to branding stuff replace "FreeCAD" with the branded application name
    if (strncmp(filter, "FreeCAD", 7) == 0) {
        std::string AppName = getExecutableName();
        AppName += item.filter.substr(7);
        item.filter = std::move(AppName);
        // put to the front of the array
        _mExportTypes.insert(_mExportTypes.begin(),std::move(item));
    }
    else {
        _mExportTypes.push_back(std::move(item));
    }
}

namespace {
    // To enable changing languages while the program is running, cache the translatable export type
    // entries so that their addition can be "replayed" when the language changes (after removing
    // the originals).

    struct TranslatableTypeCacheEntry {
        std::string description;
        const std::vector<std::string> extensions;
        std::string moduleName;
    };

    class TranslatableTypeCache {
    public:
        TranslatableTypeCache() = default;
        void addCacheEntry(TranslatableTypeCacheEntry entry) {
            _cache.push_back(std::move(entry));
        }
        std::vector<TranslatableTypeCacheEntry> getCache() const {
            return _cache;
        }
        void clear()
        {
            _cache.clear();
        }
    private:
        std::vector<TranslatableTypeCacheEntry> _cache;
    };

    TranslatableTypeCache translatableExportTypeCache;

    // Given a description string and a list of extensions, construct a type string that Qt's file
    // dialogs will recognize
    void appendTypeString(std::string &description, const std::vector<std::string> &extensions) {
        description = fmt::format("{} (*.{})", description, fmt::join(extensions, " *."));
    }
}

void Application::addTranslatableExportType(const std::string &description,
                                            const std::vector<std::string> &extensions,
                                            const std::string &moduleName)
{
    assert(!extensions.empty());  // Programming error, there must be extensions

    // Branding: replace "FreeCAD" in a file type description with the branded application name
    auto replaceFreeCAD =
    [](std::string& s)
    {
        constexpr std::string_view freecad = "FreeCAD";
        if (auto pos = s.find(freecad); pos != std::string::npos) {
            s.replace(pos, freecad.size(), getExecutableName());
            return true;  // Contained the app name
        }
        return false;  // Did NOT contain the app name
    };

    translatableExportTypeCache.addCacheEntry({description, extensions, moduleName});
    auto translatedDescription = QCoreApplication::translate("FileFormat", description.c_str()).toStdString();
    bool containsAppName = replaceFreeCAD(translatedDescription);  // Run *AFTER* translation
    appendTypeString(translatedDescription, extensions);

    FileTypeItem item;
    item.filter = translatedDescription;
    item.module = moduleName;
    item.types = extensions;
    item.translatable = true;

    if (containsAppName) {
        // put to the front of the array
        _mExportTypes.insert(_mExportTypes.begin(),std::move(item));
    }
    else {
        _mExportTypes.push_back(std::move(item));
    }
}

void Application::retranslateExportTypes()
{
    auto cache = translatableExportTypeCache.getCache();
    translatableExportTypeCache.clear();
    std::erase_if(_mExportTypes, [](const FileTypeItem& item) {
        return item.translatable;
    });
    for (const auto &cacheEntry : cache) {
        addTranslatableExportType(cacheEntry.description, cacheEntry.extensions, cacheEntry.moduleName);
    }
}

void Application::changeExportModule(const char* filter, const char* oldModuleName, const char* newModuleName)
{
    for (auto& it : _mExportTypes) {
        if (it.filter == filter && it.module == oldModuleName) {
            it.module = newModuleName;
            break;
        }
    }
}

std::vector<std::string> Application::getExportModules(const std::string& extension) const
{
    std::vector<std::string> modules;
    for (const auto & it : _mExportTypes) {
        const std::vector<std::string>& types = it.types;
        for (const auto & jt : types) {
            if (boost::iequals(extension, jt)) {
                modules.push_back(it.module);
            }
        }
    }

    return modules;
}

std::vector<std::string> Application::getExportModules() const
{
    std::vector<std::string> modules;
    modules.reserve(_mExportTypes.size());
    for (const auto& it : _mExportTypes) {
        modules.push_back(it.module);
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return modules;
}

std::vector<std::string> Application::getExportTypes(const std::string& Module) const
{
    std::vector<std::string> types;
    for (const auto & it : _mExportTypes) {
        if (boost::iequals(Module, it.module)) {
            types.insert(types.end(), it.types.begin(), it.types.end());
        }
    }

    return types;
}

std::vector<std::string> Application::getExportTypes() const
{
    std::vector<std::string> types;
    for (const FileTypeItem& it : _mExportTypes) {
        types.insert(types.end(), it.types.begin(), it.types.end());
    }

    std::sort(types.begin(), types.end());
    types.erase(std::unique(types.begin(), types.end()), types.end());

    return types;
}

std::map<std::string, std::string> Application::getExportFilters(const std::string& extension) const
{
    std::map<std::string, std::string> moduleFilter;
    for (const auto & it : _mExportTypes) {
        const std::vector<std::string>& types = it.types;
        for (const auto & jt : types) {
            if (boost::iequals(extension, jt)) {
                moduleFilter[it.filter] = it.module;
            }
        }
    }

    return moduleFilter;
}

std::map<std::string, std::string> Application::getExportFilters() const
{
    std::map<std::string, std::string> filter;
    for (const FileTypeItem& it : _mExportTypes) {
        filter[it.filter] = it.module;
    }

    return filter;
}
