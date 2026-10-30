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


namespace {

/*!
 * \brief getUserHome
 * Returns the user's home directory.
 */
QString getUserHome()
{
    QString path;
#if defined(FC_OS_LINUX) || defined(FC_OS_CYGWIN) || defined(FC_OS_BSD) || defined(FC_OS_MACOSX)
    // Default paths for the user specific stuff
    struct passwd pwd;
    struct passwd *result;
    const std::size_t buflen = 16384;
    std::vector<char> buffer(buflen);
    const int error = getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result);
    Q_UNUSED(error)
    if (!result)
        throw Base::RuntimeError("Getting HOME path from system failed!");
    path = QString::fromUtf8(result->pw_dir);
#else
    path = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
#endif

    return path;
}

/*!
 * \brief getOldGenericDataLocation
 * Returns a directory location where persistent data shared across applications can be stored.
 * This method returns the old non-XDG-compliant root path where to store config files and application data.
 */
#if defined(FC_OS_WIN32)
QString getOldGenericDataLocation(QString home)
{
#if defined(FC_OS_WIN32)
    WCHAR szPath[MAX_PATH];
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, szPath))) {
        return QString::fromStdString(converter.to_bytes(szPath));
    }
#elif defined(FC_OS_MACOSX)
    QFileInfo fi(home, QStringLiteral("Library/Preferences"));
    home = fi.absoluteFilePath();
#endif

    return home;
}
#endif

/*!
 * \brief getSubDirectories
 * To a given path it adds the sub-directories where to store application specific files.
 */
void getSubDirectories(std::map<std::string,std::string>& mConfig, std::vector<std::string>& appData)
{
    // If 'AppDataSkipVendor' is defined, the value of 'ExeVendor' must not be part of
    // the path.
    if (mConfig.find("AppDataSkipVendor") == mConfig.end()) {
        appData.push_back(mConfig["ExeVendor"]);
    }
    appData.push_back(mConfig["ExeName"]);
}

/*!
 * \brief getOldDataLocation
 * To a given path it adds the sub-directories where to store application specific files.
 * On Linux or BSD a hidden directory (i.e. starting with a dot) is added.
 */
void getOldDataLocation(std::map<std::string,std::string>& mConfig, std::vector<std::string>& appData)
{
    // Actually the name of the directory where the parameters are stored should be the name of
    // the application due to branding reasons.
#if defined(FC_OS_LINUX) || defined(FC_OS_CYGWIN) || defined(FC_OS_BSD)
    // If 'AppDataSkipVendor' is defined, the value of 'ExeVendor' must not be part of
    // the path.
    if (mConfig.find("AppDataSkipVendor") == mConfig.end()) {
        appData.push_back(std::string(".") + mConfig["ExeVendor"]);
        appData.push_back(mConfig["ExeName"]);
    } else {
        appData.push_back(std::string(".") + mConfig["ExeName"]);
    }

#elif defined(FC_OS_MACOSX) || defined(FC_OS_WIN32)
    getSubDirectories(mConfig, appData);
#endif
}

/*!
 * \brief findUserHomePath
 * If the passed path name is not empty it will be returned, otherwise
 * the user home path of the system will be returned.
 */
QString findUserHomePath(const QString& userHome)
{
    return userHome.isEmpty() ? getUserHome() : userHome;
}

/*!
 * \brief findPath
 * Returns the path where to store application files to.
 * If \a customHome is not empty it will be used, otherwise a path starting from \a stdHome will be used.
 */
std::filesystem::path findPath(const QString& stdHome, const QString& customHome,
                                 const std::vector<std::string>& paths, bool create)
{
    QString dataPath = customHome;
    if (dataPath.isEmpty()) {
        dataPath = stdHome;
    }

    std::filesystem::path appData(Base::FileInfo::stringToPath(dataPath.toStdString()));

    // If a custom user home path is given then don't modify it
    if (customHome.isEmpty()) {
        for (const auto& it : paths)
            appData = appData / it;
    }

    // In order to write to our data path, we must create some directories, first.
    if (create && !std::filesystem::exists(appData) && !Py_IsInitialized()) {
        try {
            std::filesystem::create_directories(appData);
        } catch (const std::filesystem::filesystem_error& e) {
            throw Base::FileSystemError("Could not create directories. Failed with: " + e.code().message());
        }
    }

    return appData;
}

/*!
 * \brief getCustomPaths
 * Returns a tuple of path names where to store config, data and temp. files.
 * The method therefore reads the environment variables:
 * \list
 * \li FREECAD_USER_HOME
 * \li FREECAD_USER_DATA
 * \li FREECAD_USER_TEMP
 * \endlist
 */
std::tuple<QString, QString, QString> getCustomPaths()
{
    const QProcessEnvironment env(QProcessEnvironment::systemEnvironment());
    QString userHome = env.value(QStringLiteral("FREECAD_USER_HOME"));
    QString userData = env.value(QStringLiteral("FREECAD_USER_DATA"));
    QString userTemp = env.value(QStringLiteral("FREECAD_USER_TEMP"));

    auto toNativePath = [](QString& path) {
        if (!path.isEmpty()) {
            const QDir dir(path);
            if (dir.exists())
                path = QDir::toNativeSeparators(dir.canonicalPath());
            else
                path.clear();
        }
    };

    // verify env. variables
    toNativePath(userHome);
    toNativePath(userData);
    toNativePath(userTemp);

    // if FREECAD_USER_HOME is set but not FREECAD_USER_DATA
    if (!userHome.isEmpty() && userData.isEmpty()) {
        userData = userHome;
    }

    // if FREECAD_USER_HOME is set but not FREECAD_USER_TEMP
    if (!userHome.isEmpty() && userTemp.isEmpty()) {
        const QDir dir(userHome);
        dir.mkdir(QStringLiteral("temp"));
        const QFileInfo fi(dir, QStringLiteral("temp"));
        userTemp = fi.absoluteFilePath();
    }

    return {userHome, userData, userTemp};
}

/*!
 * \brief getStandardPaths
 * Returns a tuple of XDG-compliant standard paths names where to store config, data and cached files.
 * The method therefore reads the environment variables:
 * \list
 * \li XDG_CONFIG_HOME
 * \li XDG_DATA_HOME
 * \li XDG_CACHE_HOME
 * \endlist
 */
std::tuple<QString, QString, QString, QString> getStandardPaths()
{
    QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString cacheHome = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    // Keep the old behaviour
#if defined(FC_OS_WIN32)
    configHome = getOldGenericDataLocation(QString());
    dataHome = configHome;

    // On systems with non-7-bit-ASCII application data directories
    // GetTempPathW will return a path in DOS format. This path will be
    // accepted by boost's file_lock class.
    // Since boost 1.76 there is now a version that accepts a wide string.
#if (BOOST_VERSION < 107600)
    tempPath = QString::fromStdString(Base::FileInfo::getTempPath());
    cacheHome = tempPath;
#endif
#endif

    return std::make_tuple(configHome, dataHome, cacheHome, tempPath);
}
}

void Application::ExtractUserPath()
{
    bool keepDeprecatedPaths = mConfig.contains("KeepDeprecatedPaths");

    // std paths
    mConfig["BinPath"] = mConfig["AppHomePath"] + "bin" + PATHSEP;
    mConfig["DocPath"] = mConfig["AppHomePath"] + "doc" + PATHSEP;

    // this is to support a portable version of FreeCAD
    auto paths = getCustomPaths();
    QString customHome = std::get<0>(paths);
    QString customData = std::get<1>(paths);
    QString customTemp = std::get<2>(paths);

    // get the system standard paths
    auto stdPaths = getStandardPaths();
    QString configHome = std::get<0>(stdPaths);
    QString dataHome = std::get<1>(stdPaths);
    QString cacheHome = std::get<2>(stdPaths);
    QString tempPath = std::get<3>(stdPaths);

    // User home path
    //
    QString homePath = findUserHomePath(customHome);
    mConfig["UserHomePath"] = homePath.toUtf8().data();

    // the old path name to save config and data files
    std::vector<std::string> subdirs;
    if (keepDeprecatedPaths) {
        configHome = homePath;
        dataHome = homePath;
        cacheHome = homePath;
        getOldDataLocation(mConfig, subdirs);
    }
    else {
        getSubDirectories(mConfig, subdirs);
    }

    // User data path
    //
    std::filesystem::path data = findPath(dataHome, customData, subdirs, true);
    mConfig["UserAppData"] = Base::FileInfo::pathToString(data) + PATHSEP;


    // User config path
    //
    std::filesystem::path config = findPath(configHome, customHome, subdirs, true);
    mConfig["UserConfigPath"] = Base::FileInfo::pathToString(config) + PATHSEP;


    // User cache path
    //
    std::vector<std::string> cachedirs = subdirs;
    cachedirs.emplace_back("Cache");
    std::filesystem::path cache = findPath(cacheHome, customTemp, cachedirs, true);
    mConfig["UserCachePath"] = Base::FileInfo::pathToString(cache) + PATHSEP;


    // Set application tmp. directory
    //
    std::vector<std::string> empty;
    std::filesystem::path tmp = findPath(tempPath, customTemp, empty, true);
    mConfig["AppTempPath"] = Base::FileInfo::pathToString(tmp) + PATHSEP;


    // Set the default macro directory
    //
    std::vector<std::string> macrodirs = std::move(subdirs);  // Last use in this method, just move
    macrodirs.emplace_back("Macro");
    std::filesystem::path macro = findPath(dataHome, customData, macrodirs, true);
    mConfig["UserMacroPath"] = Base::FileInfo::pathToString(macro) + PATHSEP;
}

// TODO: Consider using this for all UNIX-like OSes
#if defined(__OpenBSD__)
#include <cstdio>
#include <cstdlib>
#include <sys/param.h>
#include <QCoreApplication>

std::string Application::FindHomePath(const char* sCall)
{
    // We have three ways to start this application either use one of the two executables or
    // import the FreeCAD.so module from a running Python session. In the latter case the
    // Python interpreter is already initialized.
    std::string absPath;
    std::string homePath;
    if (Py_IsInitialized()) {
        // Note: realpath is known to cause a buffer overflow because it
        // expands the given path to an absolute path of unknown length.
        // Even setting PATH_MAX does not necessarily solve the problem
        // for sure but the risk of overflow is rather small.
        char resolved[PATH_MAX];
        char* path = realpath(sCall, resolved);
        if (path)
            absPath = path;
    }
    else {
        int argc = 1;
        QCoreApplication app(argc, (char**)(&sCall));
        absPath = QCoreApplication::applicationFilePath().toStdString();
    }

    // should be an absolute path now
    std::string::size_type pos = absPath.find_last_of("/");
    homePath.assign(absPath,0,pos);
    pos = homePath.find_last_of("/");
    homePath.assign(homePath,0,pos+1);

    return homePath;
}

#elif defined (FC_OS_LINUX) || defined(FC_OS_CYGWIN) || defined(FC_OS_BSD)
#include <cstdio>
#include <cstdlib>
#include <sys/param.h>

std::string Application::FindHomePath(const char* sCall)
{
    // We have three ways to start this application either use one of the two executables or
    // import the FreeCAD.so module from a running Python session. In the latter case the
    // Python interpreter is already initialized.
    std::string absPath;
    std::string homePath;
    if (Py_IsInitialized()) {
        // Note: realpath is known to cause a buffer overflow because it
        // expands the given path to an absolute path of unknown length.
        // Even setting PATH_MAX does not necessarily solve the problem
        // for sure but the risk of overflow is rather small.
        char resolved[PATH_MAX];
        char* path = realpath(sCall, resolved);
        if (path)
            absPath = path;
    }
    else {
        // Find the path of the executable. Theoretically, there could occur a
        // race condition when using readlink, but we only use this method to
        // get the absolute path of the executable to compute the actual home
        // path. In the worst case we simply get q wrong path and FreeCAD is not
        // able to load its modules.
        char resolved[PATH_MAX];
#if defined(FC_OS_BSD)
        int mib[4];
        mib[0] = CTL_KERN;
        mib[1] = KERN_PROC;
        mib[2] = KERN_PROC_PATHNAME;
        mib[3] = -1;
        size_t cb = sizeof(resolved);
        sysctl(mib, 4, resolved, &cb, NULL, 0);
        int nchars = strlen(resolved);
#else
        int nchars = readlink("/proc/self/exe", resolved, PATH_MAX);
#endif
        if (nchars < 0 || nchars >= PATH_MAX)
            throw Base::FileSystemError("Cannot determine the absolute path of the executable");
        resolved[nchars] = '\0'; // enforce null termination
        absPath = resolved;
    }

    // should be an absolute path now
    std::string::size_type pos = absPath.find_last_of('/');
    homePath.assign(absPath,0,pos);
    pos = homePath.find_last_of('/');
    homePath.assign(homePath,0,pos+1);

    return homePath;
}

#elif defined(FC_OS_MACOSX)
#include <mach-o/dyld.h>
#include <string>
#include <cstdlib>
#include <sys/param.h>

std::string Application::FindHomePath(const char* sCall)
{
    // If Python is initialized at this point, then we're being run from
    // MainPy.cpp, which hopefully rewrote argv[0] to point at the
    // FreeCAD shared library.
    if (!Py_IsInitialized()) {
        uint32_t sz = 0;

        // function only returns "sz" if first arg is to small to hold value
        _NSGetExecutablePath(nullptr, &sz);

        if (const auto buf = new char[++sz]; _NSGetExecutablePath(buf, &sz) == 0) {
            char resolved[PATH_MAX];
            const char* path = realpath(buf, resolved);
            delete [] buf;

            if (path) {
                const std::string Call(resolved);
                std::string TempHomePath;
                std::string::size_type pos = Call.find_last_of(PATHSEP);
                TempHomePath.assign(Call,0,pos);
                pos = TempHomePath.find_last_of(PATHSEP);
                TempHomePath.assign(TempHomePath,0,pos+1);
                return TempHomePath;
            }
        } else {
            delete [] buf;
        }
    }

    return sCall;
}

#elif defined (FC_OS_WIN32)
std::string Application::FindHomePath(const char* sCall)
{
    // We have several ways to start this application:
    // * use one of the two executables
    // * import the FreeCAD.pyd module from a running Python session. In this case the
    //   Python interpreter is already initialized.
    // * use a custom dll that links FreeCAD core dlls and that is loaded by its host application
    //   In this case the calling name should be set to FreeCADBase.dll or FreeCADApp.dll in order
    //   to locate the correct home directory
    wchar_t szFileName [MAX_PATH];
    QString dll(QString::fromUtf8(sCall));
    if (Py_IsInitialized() || dll.endsWith(QLatin1String(".dll"))) {
        GetModuleFileNameW(GetModuleHandleA(sCall),szFileName, MAX_PATH-1);
    }
    else {
        GetModuleFileNameW(0, szFileName, MAX_PATH-1);
    }

    std::wstring Call(szFileName), homePath;
    std::wstring::size_type pos = Call.find_last_of(PATHSEP);
    homePath.assign(Call,0,pos);
    pos = homePath.find_last_of(PATHSEP);
    homePath.assign(homePath,0,pos+1);

    // fixes #0001638 to avoid to load DLLs from Windows' system directories before FreeCAD's bin folder
    std::wstring binPath = homePath;
    binPath += L"bin";
    SetDllDirectoryW(binPath.c_str());

    // https://stackoverflow.com/questions/5625884/conversion-of-stdwstring-to-qstring-throws-linker-error
#ifdef _MSC_VER
    QString str = QString::fromUtf16(reinterpret_cast<const ushort *>(homePath.c_str()));
#else
    QString str = QString::fromStdWString(homePath);
#endif
    // convert to utf-8
    return str.toUtf8().data();
}

#else
# error "std::string Application::FindHomePath(const char*) not implemented"
#endif
