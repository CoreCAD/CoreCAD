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
#include "ProgramOptionsUtilities.h"
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


// scriptings (scripts are built-in but can be overridden by command line option)
#include <App/InitScript.h>
#include <App/TestScript.h>
#include <App/CMakeScript.h>

#include "SafeMode.h"

#ifdef FC_OS_WIN32
#include <windows.h>
#endif


FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;
namespace sp = std::placeholders;
namespace fs = std::filesystem;


std::optional<std::string> getenvUTF8(const char* name) {
#ifdef FC_OS_WIN32
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    std::wstring wideName(wideLength ? wideLength - 1 : 0, L'\0');
    if (wideLength) {
        MultiByteToWideChar(CP_UTF8, 0, name, -1, wideName.data(), wideLength);
    }

    DWORD needed = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (needed == 0) {
        return std::nullopt;
    }

    std::wstring wideValue(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(wideName.c_str(), wideValue.data(), needed);
    if (written == 0) {
        return std::nullopt;
    }
    wideValue.resize(written);
    return Base::Tools::wstringToString(wideValue);
#else
    if (const char* v = std::getenv(name)) {
        return std::string(v);
    }
    return std::nullopt;
#endif
}

// clang-format off
PyDoc_STRVAR(FreeCAD_doc,
     "The functions in the FreeCAD module allow working with documents.\n"
     "The FreeCAD instance provides a list of references of documents which\n"
     "can be addressed by a string. Hence the document name must be unique.\n"
     "\n"
     "The document has the read-only attribute FileName which points to the\n"
     "file the document should be stored to.\n"
    );

PyDoc_STRVAR(Console_doc,
    "FreeCAD Console module.\n\n"
    "The Console module contains functions to manage log entries, messages,\n"
    "warnings and errors.\n"
    "There are also functions to get/set the status of the observers used as\n"
    "logging interfaces."
    );

PyDoc_STRVAR(Base_doc,
    "The Base module contains the classes for the geometric basics\n"
    "like vector, matrix, bounding box, placement, rotation, axis, ...\n"
    );

// This is called via the PyImport_AppendInittab mechanism called
// during initialization, to make the built-in __FreeCADBase__
// module known to Python.
PyMODINIT_FUNC
init_freecad_base_module(void)
{
    static struct PyModuleDef BaseModuleDef = {
        PyModuleDef_HEAD_INIT,
        "__FreeCADBase__", Base_doc, -1,
        nullptr, nullptr, nullptr, nullptr, nullptr
    };
    return PyModule_Create(&BaseModuleDef);
}

// Set in inside Application
static PyMethodDef* ApplicationMethods = nullptr;

PyMODINIT_FUNC
init_freecad_module(void)
{
    static struct PyModuleDef FreeCADModuleDef = {
        PyModuleDef_HEAD_INIT,
        "FreeCAD", FreeCAD_doc, -1,
        ApplicationMethods,
        nullptr, nullptr, nullptr, nullptr
    };
    return PyModule_Create(&FreeCADModuleDef);
}

PyMODINIT_FUNC
init_image_module()
{
    static struct PyModuleDef ImageModuleDef = {
        PyModuleDef_HEAD_INIT,
        "Image", "", -1,
        nullptr,
        nullptr, nullptr, nullptr, nullptr
    };
    return PyModule_Create(&ImageModuleDef);
}
// clang-format on

void Application::setupPythonTypes()
{
    // setting up Python binding
    Base::PyGILStateLocker lock;
    PyObject* modules = PyImport_GetModuleDict();

    ApplicationMethods = ApplicationPy::Methods;
    PyObject* pAppModule = PyImport_ImportModule ("FreeCAD");
    if (!pAppModule) {
        PyErr_Clear();
        pAppModule = init_freecad_module();
        PyDict_SetItemString(modules, "FreeCAD", pAppModule);
    }
    Py::Module(pAppModule).setAttr(std::string("ActiveDocument"),Py::None());

    // clang-format off
    static struct PyModuleDef ConsoleModuleDef = {
        PyModuleDef_HEAD_INIT,
        "__FreeCADConsole__", Console_doc, -1,
        Base::ConsoleSingleton::Methods,
        nullptr, nullptr, nullptr, nullptr
    };
    PyObject* pConsoleModule = PyModule_Create(&ConsoleModuleDef);

    // fake Image module
    PyObject* imageModule = init_image_module();
    PyDict_SetItemString(modules, "Image", imageModule);

    // introducing additional classes

    // NOTE: To finish the initialization of our own type objects we must
    // call PyType_Ready, otherwise we run into a segmentation fault, later on.
    // This function is responsible for adding inherited slots from a type's base class.
    Base::InterpreterSingleton::addType(&Base::VectorPy::Type, pAppModule, "Vector");
    Base::InterpreterSingleton::addType(&Base::MatrixPy::Type, pAppModule, "Matrix");
    Base::InterpreterSingleton::addType(&Base::BoundBoxPy::Type, pAppModule, "BoundBox");
    Base::InterpreterSingleton::addType(&Base::PlacementPy::Type, pAppModule, "Placement");
    Base::InterpreterSingleton::addType(&Base::RotationPy::Type, pAppModule, "Rotation");
    Base::InterpreterSingleton::addType(&Base::AxisPy::Type, pAppModule, "Axis");

    // Note: Create an own module 'Base' which should provide the python
    // binding classes from the base module. At a later stage we should
    // remove these types from the FreeCAD module.

    PyObject* pBaseModule = PyImport_ImportModule ("__FreeCADBase__");
    if (!pBaseModule) {
        PyErr_Clear();
        pBaseModule = init_freecad_base_module();
        PyDict_SetItemString(modules, "__FreeCADBase__", pBaseModule);
    }

    setupPythonException(pBaseModule);


    // Python types
    Base::InterpreterSingleton::addType(&Base::VectorPy          ::Type,pBaseModule,"Vector");
    Base::InterpreterSingleton::addType(&Base::MatrixPy          ::Type,pBaseModule,"Matrix");
    Base::InterpreterSingleton::addType(&Base::BoundBoxPy        ::Type,pBaseModule,"BoundBox");
    Base::InterpreterSingleton::addType(&Base::PlacementPy       ::Type,pBaseModule,"Placement");
    Base::InterpreterSingleton::addType(&Base::RotationPy        ::Type,pBaseModule,"Rotation");
    Base::InterpreterSingleton::addType(&Base::AxisPy            ::Type,pBaseModule,"Axis");
    Base::InterpreterSingleton::addType(&Base::CoordinateSystemPy::Type,pBaseModule,"CoordinateSystem");
    Base::InterpreterSingleton::addType(&Base::TypePy            ::Type,pBaseModule,"TypeId");
    Base::InterpreterSingleton::addType(&Base::PrecisionPy       ::Type,pBaseModule,"Precision");

    Base::InterpreterSingleton::addType(&ApplicationDirectoriesPy::Type, pAppModule, "ApplicationDirectories");
    Base::InterpreterSingleton::addType(&MaterialPy::Type, pAppModule, "Material");
    Base::InterpreterSingleton::addType(&MetadataPy::Type, pAppModule, "Metadata");

    Base::InterpreterSingleton::addType(&MeasureManagerPy::Type, pAppModule, "MeasureManager");

    Base::InterpreterSingleton::addType(&StringHasherPy::Type, pAppModule, "StringHasher");
    Base::InterpreterSingleton::addType(&StringIDPy::Type, pAppModule, "StringID");

    // Add document types
    Base::InterpreterSingleton::addType(&PropertyContainerPy::Type, pAppModule, "PropertyContainer");
    Base::InterpreterSingleton::addType(&ExtensionContainerPy::Type, pAppModule, "ExtensionContainer");
    Base::InterpreterSingleton::addType(&DocumentPy::Type, pAppModule, "Document");
    Base::InterpreterSingleton::addType(&DocumentObjectPy::Type, pAppModule, "DocumentObject");
    Base::InterpreterSingleton::addType(&DocumentObjectGroupPy::Type, pAppModule, "DocumentObjectGroup");
    Base::InterpreterSingleton::addType(&GeoFeaturePy::Type, pAppModule, "GeoFeature");

    // Add extension types
    Base::InterpreterSingleton::addType(&ExtensionPy::Type, pAppModule, "Extension");
    Base::InterpreterSingleton::addType(&DocumentObjectExtensionPy::Type, pAppModule, "DocumentObjectExtension");
    Base::InterpreterSingleton::addType(&GroupExtensionPy::Type, pAppModule, "GroupExtension");
    Base::InterpreterSingleton::addType(&GeoFeatureGroupExtensionPy::Type, pAppModule, "GeoFeatureGroupExtension");
    Base::InterpreterSingleton::addType(&LinkBaseExtensionPy::Type, pAppModule, "LinkBaseExtension");

    //insert Base and Console
    Py_INCREF(pBaseModule);
    PyModule_AddObject(pAppModule, "Base", pBaseModule);
    Py_INCREF(pConsoleModule);
    PyModule_AddObject(pAppModule, "Console", pConsoleModule);

    // Translate module
    PyObject* pTranslateModule = Base::Interpreter().addModule(new Base::Translate);
    Py_INCREF(pTranslateModule);
    PyModule_AddObject(pAppModule, "Qt", pTranslateModule);

    //insert Units module
    static struct PyModuleDef UnitsModuleDef = {
        PyModuleDef_HEAD_INIT,
        "Units", "The Unit API", -1,
        Base::UnitsApi::Methods,
        nullptr, nullptr, nullptr, nullptr
    };
    PyObject* pUnitsModule = PyModule_Create(&UnitsModuleDef);
    Base::InterpreterSingleton::addType(&Base::QuantityPy  ::Type,pUnitsModule,"Quantity");
    // make sure to set the 'nb_true_divide' slot
    Base::InterpreterSingleton::addType(&Base::UnitPy      ::Type,pUnitsModule,"Unit");

    Py_INCREF(pUnitsModule);
    PyModule_AddObject(pAppModule, "Units", pUnitsModule);

    // Document-type markers (§7.1 four document types) exposed for Python scripting, so a
    // creation site writes App.newDocument(type=App.DocTypeAssembly) rather than a bare
    // "Assembly" literal. The values stay single-sourced from Document's constexpr constants.
    PyModule_AddStringConstant(pAppModule, "DocTypePart", Document::DocTypePart);
    PyModule_AddStringConstant(pAppModule, "DocTypeAssembly", Document::DocTypeAssembly);
    PyModule_AddStringConstant(pAppModule, "DocTypeDrawing", Document::DocTypeDrawing);
    PyModule_AddStringConstant(pAppModule, "DocTypeSpreadsheet", Document::DocTypeSpreadsheet);

    Base::ProgressIndicatorPy::init_type();
    Base::InterpreterSingleton::addType(Base::ProgressIndicatorPy::type_object(),
        pBaseModule,"ProgressIndicator");

    Base::Vector2dPy::init_type();
    Base::InterpreterSingleton::addType(Base::Vector2dPy::type_object(),
        pBaseModule,"Vector2d");
    // clang-format on
}

/*
 * Define custom Python exception types
 */
void Application::setupPythonException(PyObject* module)
{
    auto setup = [&module, str {"Base."}](const std::string& ename, auto pyExcType) {
        auto exception = PyErr_NewException((str + ename).c_str(), pyExcType, nullptr);
        Py_INCREF(exception);
        PyModule_AddObject(module, ename.c_str(), exception);
        return exception;
    };

    Base::PyExc_FC_GeneralError = setup("FreeCADError", PyExc_RuntimeError);
    Base::PyExc_FC_FreeCADAbort = setup("FreeCADAbort", PyExc_BaseException);
    Base::PyExc_FC_XMLBaseException = setup("XMLBaseException", PyExc_Exception);
    Base::PyExc_FC_XMLParseException = setup("XMLParseException", Base::PyExc_FC_XMLBaseException);
    Base::PyExc_FC_XMLAttributeError = setup("XMLAttributeError", Base::PyExc_FC_XMLBaseException);
    Base::PyExc_FC_UnknownProgramOption = setup("UnknownProgramOption", PyExc_BaseException);
    Base::PyExc_FC_BadFormatError = setup("BadFormatError", Base::PyExc_FC_GeneralError);
    Base::PyExc_FC_BadGraphError = setup("BadGraphError", Base::PyExc_FC_GeneralError);
    Base::PyExc_FC_ExpressionError = setup("ExpressionError", Base::PyExc_FC_GeneralError);
    Base::PyExc_FC_ParserError = setup("ParserError", Base::PyExc_FC_GeneralError);
    Base::PyExc_FC_CADKernelError = setup("CADKernelError", Base::PyExc_FC_GeneralError);
    Base::PyExc_FC_PropertyError = setup("PropertyError", PyExc_AttributeError);
    Base::PyExc_FC_AbortIOException = setup("AbortIOException", PyExc_BaseException);
}

namespace
{
void initExceptions()
{
    // register exception producer types
    // NOLINTBEGIN
    new Base::ExceptionProducer<Base::AbortException>;
    new Base::ExceptionProducer<Base::XMLBaseException>;
    new Base::ExceptionProducer<Base::XMLParseException>;
    new Base::ExceptionProducer<Base::XMLAttributeError>;
    new Base::ExceptionProducer<Base::FileException>;
    new Base::ExceptionProducer<Base::FileSystemError>;
    new Base::ExceptionProducer<Base::BadFormatError>;
    new Base::ExceptionProducer<Base::MemoryException>;
    new Base::ExceptionProducer<Base::AccessViolation>;
    new Base::ExceptionProducer<Base::AbnormalProgramTermination>;
    new Base::ExceptionProducer<Base::UnknownProgramOption>;
    new Base::ExceptionProducer<Base::ProgramInformation>;
    new Base::ExceptionProducer<Base::TypeError>;
    new Base::ExceptionProducer<Base::ValueError>;
    new Base::ExceptionProducer<Base::IndexError>;
    new Base::ExceptionProducer<Base::NameError>;
    new Base::ExceptionProducer<Base::ImportError>;
    new Base::ExceptionProducer<Base::AttributeError>;
    new Base::ExceptionProducer<Base::RuntimeError>;
    new Base::ExceptionProducer<Base::BadGraphError>;
    new Base::ExceptionProducer<Base::NotImplementedError>;
    new Base::ExceptionProducer<Base::ZeroDivisionError>;
    new Base::ExceptionProducer<Base::ReferenceError>;
    new Base::ExceptionProducer<Base::ExpressionError>;
    new Base::ExceptionProducer<Base::ParserError>;
    new Base::ExceptionProducer<Base::UnicodeError>;
    new Base::ExceptionProducer<Base::OverflowError>;
    new Base::ExceptionProducer<Base::UnderflowError>;
    new Base::ExceptionProducer<Base::UnitsMismatchError>;
    new Base::ExceptionProducer<Base::CADKernelError>;
    new Base::ExceptionProducer<Base::RestoreError>;
    new Base::ExceptionProducer<Base::PropertyError>;
    // NOLINTEND
}
}

void Application::init(int argc, char ** argv)
{
    try {
        Base::SystemHandler::installNewHandler();
        Base::SystemHandler::installSegfaultHandler();

        initTypes();

        initConfig(argc,argv);
        initApplication();
        initExceptions();
    }
    catch (...) {
        // force the log to flush
        destructObserver();
        throw;
    }
}

namespace {

void parseProgramOptions(int ac, char ** av, const std::string& exe, boost::program_options::variables_map& vm)
{
    // Declare a group of options that will be
    // allowed only on the command line
    boost::program_options::options_description generic("Generic options");
    generic.add_options()
    ("version,v", "Prints version string")
    ("verbose", "Prints verbose version string")
    ("help,h", "Prints help message")
    ("console,c", "Starts in console mode")
    ("response-file", boost::program_options::value<std::string>(),"Can be specified with '@name', too")
    ("dump-config", "Dumps configuration")
    ("get-config", boost::program_options::value<std::string>(), "Prints the value of the requested configuration key")
    ("set-config", boost::program_options::value< std::vector<std::string> >()->multitoken(), "Sets the value of a configuration key")
    ("keep-deprecated-paths", "If set then config files are kept on the old location")
    ;

    // Declare a group of options that will be
    // allowed both on the command line and in
    // the config file
    std::stringstream descr;
    descr << "Writes " << exe << ".log to the user directory.";
    boost::program_options::options_description config("Configuration");
    config.add_options()
    ("write-log,l", descr.str().c_str())
    ("log-file", boost::program_options::value<std::string>(), "Unlike --write-log this allows logging to an arbitrary file")
    ("user-cfg,u", boost::program_options::value<std::string>(),"User config file to load/save user settings")
    ("system-cfg,s", boost::program_options::value<std::string>(),"System config file to load/save system settings")
    ("run-test,t", boost::program_options::value<std::string>()->implicit_value(""),"Run a given test case (use 0 (zero) to run all tests). If no argument is provided then return list of all available tests.")
    ("run-open,r", boost::program_options::value<std::string>()->implicit_value(""),"Run a given test case (use 0 (zero) to run all tests). If no argument is provided then return list of all available tests.  Keeps UI open after test(s) complete.")
    ("module-path,M", boost::program_options::value< std::vector<std::string> >()->composing(),"Additional module paths")
    ("macro-path,E", boost::program_options::value< std::vector<std::string> >()->composing(),"Additional macro paths")
    ("python-path,P", boost::program_options::value< std::vector<std::string> >()->composing(),"Additional python paths")
    ("disable-addon", boost::program_options::value< std::vector<std::string> >()->composing(),"Disable a given addon.")
    ("single-instance", "Allow to run a single instance of the application")
    ("safe-mode", "Force enable safe mode")
    ("pass", boost::program_options::value< std::vector<std::string> >()->multitoken(), "Ignores the following arguments and pass them through to be used by a script")
    ;


    // Hidden options, will be allowed both on the command line and
    // in the config file, but will not be shown to the user.
    boost::program_options::options_description hidden("Hidden options");
    hidden.add_options()
    ("input-file", boost::program_options::value< std::vector<std::string> >(), "input file")
    ("output",     boost::program_options::value<std::string>(),"output file")
    ("hidden",                                             "don't show the main window")
    // this are to ignore for the window system (QApplication)
    ("style",      boost::program_options::value< std::string >(), "set the application GUI style")
    ("stylesheet", boost::program_options::value< std::string >(), "set the application stylesheet")
    ("session",    boost::program_options::value< std::string >(), "restore the application from an earlier session")
    ("reverse",                                               "set the application's layout direction from right to left")
    ("widgetcount",                                           "print debug messages about widgets")
    ("graphicssystem", boost::program_options::value< std::string >(), "backend to be used for on-screen widgets and pixmaps")
    ("display",    boost::program_options::value< std::string >(), "set the X-Server")
    ("geometry ",  boost::program_options::value< std::string >(), "set the X-Window geometry")
    ("font",       boost::program_options::value< std::string >(), "set the X-Window font")
    ("fn",         boost::program_options::value< std::string >(), "set the X-Window font")
    ("background", boost::program_options::value< std::string >(), "set the X-Window background color")
    ("bg",         boost::program_options::value< std::string >(), "set the X-Window background color")
    ("foreground", boost::program_options::value< std::string >(), "set the X-Window foreground color")
    ("fg",         boost::program_options::value< std::string >(), "set the X-Window foreground color")
    ("button",     boost::program_options::value< std::string >(), "set the X-Window button color")
    ("btn",        boost::program_options::value< std::string >(), "set the X-Window button color")
    ("name",       boost::program_options::value< std::string >(), "set the X-Window name")
    ("title",      boost::program_options::value< std::string >(), "set the X-Window title")
    ("visual",     boost::program_options::value< std::string >(), "set the X-Window to color scheme")
    ("ncols",      boost::program_options::value< int    >(), "set the X-Window to color scheme")
    ("cmap",                                                  "set the X-Window to color scheme")
#if defined(FC_OS_MACOSX)
    ("psn",        boost::program_options::value< std::string >(), "process serial number")
#endif
    ;


    //0000723: improper handling of qt specific command line arguments
    std::vector<std::string> args;
    bool merge=false;
    for (int i=1; i<ac; i++) {
        if (merge) {
            merge = false;
            args.back() += "=";
            args.back() += av[i];
        }
        else {
            args.emplace_back(av[i]);
        }
        if (strcmp(av[i],"-style") == 0) {
            merge = true;
        }
        else if (strcmp(av[i],"-stylesheet") == 0) {
            merge = true;
        }
        else if (strcmp(av[i],"-session") == 0) {
            merge = true;
        }
        else if (strcmp(av[i],"-graphicssystem") == 0) {
            merge = true;
        }
    }

    // 0000659: SIGABRT on startup in boost::program_options (Boost 1.49)
    // Add some text to the constructor
    boost::program_options::options_description cmdline_options("Command-line options");
    cmdline_options.add(generic).add(config).add(hidden);

    boost::program_options::options_description config_file_options("Config");
    config_file_options.add(config).add(hidden);

    boost::program_options::options_description visible("Allowed options");
    visible.add(generic).add(config);

    boost::program_options::positional_options_description p;
    p.add("input-file", -1);

    try {
        store( boost::program_options::command_line_parser(args).
               options(cmdline_options).positional(p).extra_parser(Util::customSyntax).run(), vm);

        std::ifstream ifs("FreeCAD.cfg");
        if (ifs)
            store(parse_config_file(ifs, config_file_options), vm);
        notify(vm);
    }
    catch (const std::exception& e) {
        std::stringstream str;
        str << e.what() << '\n' << '\n' << visible << '\n';
        throw Base::UnknownProgramOption(str.str());
    }
    catch (...) {
        std::stringstream str;
        str << "Wrong or unknown option, bailing out!" << '\n' << '\n' << visible << '\n';
        throw Base::UnknownProgramOption(str.str());
    }

    if (vm.contains("help")) {
        std::stringstream str;
        str << exe << '\n' << '\n';
        str << "For a detailed description see https://www.freecad.org/wiki/Start_up_and_Configuration" << '\n'<<'\n';
        str << "Usage: " << exe << " [options] File1 File2 ..." << '\n' << '\n';
        str << visible << '\n';
        throw Base::ProgramInformation(str.str());
    }

    if (vm.contains("response-file")) {
        // Load the file and tokenize it
        std::ifstream ifs(vm["response-file"].as<std::string>().c_str());
        if (!ifs) {
            Base::Console().error("Could no open the response file\n");
            std::stringstream str;
            str << "Could no open the response file: '"
                << vm["response-file"].as<std::string>() << "'" << '\n';
            throw Base::UnknownProgramOption(str.str());
        }
        // Read the whole file into a string
        std::stringstream ss;
        ss << ifs.rdbuf();
        // Split the file content
        boost::char_separator<char> sep(" \n\r");
        boost::tokenizer<boost::char_separator<char> > tok(ss.str(), sep);
        std::vector<std::string> args2;
        copy(tok.begin(), tok.end(), back_inserter(args2));
        // Parse the file and store the options
        store( boost::program_options::command_line_parser(args2).
               options(cmdline_options).positional(p).extra_parser(Util::customSyntax).run(), vm);
    }
}

void processProgramOptions(const boost::program_options::variables_map& vm, std::map<std::string,std::string>& mConfig)
{
    if (vm.contains("version") && !vm.contains("verbose")) {
        std::stringstream str;
        str << mConfig["ExeName"] << " " << mConfig["ExeVersion"]
            << " Revision: " << mConfig["BuildRevision"] << '\n';
        if (vm.count("verbose")) {
            App::ProgramInformation::getVerboseCommonInfo(str, mConfig);
        }
        throw Base::ProgramInformation(str.str());
    }

    if (vm.contains("module-path")) {
        auto  Mods = vm["module-path"].as< std::vector<std::string> >();
        std::string temp;
        for (const auto & It : Mods)
            temp += It + ";";
        temp.erase(temp.end()-1);
        mConfig["AdditionalModulePaths"] = temp;
    }

    if (vm.contains("macro-path")) {
        std::vector<std::string> Macros = vm["macro-path"].as< std::vector<std::string> >();
        std::string temp;
        for (const auto & It : Macros)
            temp += It + ";";
        temp.erase(temp.end()-1);
        mConfig["AdditionalMacroPaths"] = std::move(temp);
    }

    if (vm.contains("python-path")) {
        auto  Paths = vm["python-path"].as< std::vector<std::string> >();
        for (const auto & It : Paths)
            Base::Interpreter().addPythonPath(It.c_str());
    }

    if (vm.contains("disable-addon")) {
        auto Addons = vm["disable-addon"].as< std::vector<std::string> >();
        std::string temp;
        for (const auto & It : Addons) {
            temp += It + ";";
        }
        temp.erase(temp.end()-1);
        mConfig["DisabledAddons"] = temp;
    }

    if (vm.contains("input-file")) {
        auto  files(vm["input-file"].as< std::vector<std::string> >());
        int OpenFileCount=0;
        for (const auto & It : files) {

            std::ostringstream temp;
            temp << "OpenFile" << OpenFileCount;
            mConfig[temp.str()] = It;
            OpenFileCount++;
        }
        std::ostringstream buffer;
        buffer << OpenFileCount;
        mConfig["OpenFileCount"] = buffer.str();
    }

    if (vm.contains("output")) {
        mConfig["SaveFile"] = vm["output"].as<std::string>();
    }

    if (vm.contains("hidden")) {
        mConfig["StartHidden"] = "1";
    }

    if (vm.contains("write-log")) {
        mConfig["LoggingFile"] = "1";
        mConfig["LoggingFileName"] = mConfig["UserAppData"] + mConfig["ExeName"] + ".log";
    }

    if (vm.contains("log-file")) {
        mConfig["LoggingFile"] = "1";
        mConfig["LoggingFileName"] = vm["log-file"].as<std::string>();
    }

    if (vm.contains("user-cfg")) {
        mConfig["UserParameter"] = vm["user-cfg"].as<std::string>();
    }

    if (vm.contains("system-cfg")) {
        mConfig["SystemParameter"] = vm["system-cfg"].as<std::string>();
    }

    if (vm.contains("run-test") || vm.contains("run-open")) {
        std::string testCase = vm.contains("run-open") ? vm["run-open"].as<std::string>() : vm["run-test"].as<std::string>();

        if ( "0" == testCase) {
            testCase = "TestApp.All";
        }
        else if (testCase.empty()) {
            testCase = "TestApp.PrintAll";
        }
        mConfig["TestCase"] = std::move(testCase);
        mConfig["RunMode"] = "Internal";
        mConfig["ScriptFileName"] = "FreeCADTest";
        mConfig["ExitTests"] = vm.contains("run-open") ? "no" : "yes";
    }

    if (vm.contains("single-instance")) {
        mConfig["SingleInstance"] = "1";
    }

    if (vm.contains("dump-config")) {
        std::stringstream str;
        for (const auto & it : mConfig) {
            str << it.first << "=" << it.second << '\n';
        }
        throw Base::ProgramInformation(str.str());
    }

    if (vm.contains("get-config")) {
        auto configKey = vm["get-config"].as<std::string>();
        std::stringstream str;
        std::map<std::string,std::string>::iterator pos;
        pos = mConfig.find(configKey);
        if (pos != mConfig.end()) {
            str << pos->second;
        }
        str << '\n';
        throw Base::ProgramInformation(str.str());
    }

    if (vm.contains("set-config")) {
        auto  configKeyValue = vm["set-config"].as< std::vector<std::string> >();
        for (const auto& it : configKeyValue) {
            auto pos = it.find('=');
            if (pos != std::string::npos) {
                std::string key = it.substr(0, pos);
                std::string val = it.substr(pos + 1);
                mConfig[key] = std::move(val);
            }
        }
    }
}

}
// clang-format on

void Application::initConfig(int argc, char ** argv)
{
    // find the home path....
    mConfig["AppHomePath"] = Base::FileInfo::pathToString(
        ApplicationDirectories::findHomePath(argv[0])
    );

    // Version of the application extracted from SubWCRef into src/Build/Version.h
    // We only set these keys if not yet defined. Therefore it suffices to search
    // only for 'BuildVersionMajor'.
    if (Application::Config().find("BuildVersionMajor") == Application::Config().end()) {
        std::stringstream str;
        str << FCVersionMajor
            << "." << FCVersionMinor
            << "." << FCVersionPoint;
        Application::Config()["ExeVersion"         ] = str.str();
        Application::Config()["BuildVersionMajor"  ] = FCVersionMajor;
        Application::Config()["BuildVersionMinor"  ] = FCVersionMinor;
        Application::Config()["BuildVersionPoint"  ] = FCVersionPoint;
        Application::Config()["BuildVersionSuffix" ] = FCVersionSuffix;
        Application::Config()["BuildRevision"      ] = FCRevision;
        Application::Config()["BuildRepositoryURL" ] = FCRepositoryURL;
        Application::Config()["BuildRevisionDate"  ] = FCRevisionDate;
#if defined(FCRepositoryHash)
        Application::Config()["BuildRevisionHash"  ] = FCRepositoryHash;
#endif
#if defined(FCRepositoryBranch)
        Application::Config()["BuildRevisionBranch"] = FCRepositoryBranch;
#endif
    }

    _argc = argc;
    _argv = argv;

    // Now it's time to read-in the file branding.xml if it exists
    Branding brand;
    QString binDir = QString::fromUtf8((mConfig["AppHomePath"] + "bin").c_str());
    QFileInfo fi(binDir, QStringLiteral("branding.xml"));
    if (fi.exists() && brand.readFile(fi.absoluteFilePath())) {
        Branding::XmlConfig cfg = brand.getUserDefines();
        for (Branding::XmlConfig::iterator it = cfg.begin(); it != cfg.end(); ++it) {
            Application::Config()[it.key()] = it.value();
        }
    }

    boost::program_options::variables_map vm;
    {
        BOOST_SCOPE_EXIT_ALL(&) {
            // console-mode needs to be set (if possible) also in case parseProgramOptions
            // throws, as it's needed when reporting such exceptions
            if (vm.contains("console")) {
                mConfig["Console"] = "1";
                mConfig["RunMode"] = "Cmd";
            }
        };
        parseProgramOptions(argc, argv, mConfig["ExeName"], vm);
    }

    if (vm.contains("keep-deprecated-paths")) {
        mConfig["KeepDeprecatedPaths"] = "1";
    }

    if (vm.contains("safe-mode")) {
        mConfig["SafeMode"] = "1";
    }

    // extract home paths
    _appDirs = std::make_unique<ApplicationDirectories>(mConfig);

#   ifdef FC_DEBUG
    mConfig["Debug"] = "1";
#   else
    mConfig["Debug"] = "0";
#   endif

    if (!Py_IsInitialized()) {
        // init python
        PyImport_AppendInittab ("FreeCAD", init_freecad_module);
        PyImport_AppendInittab ("__FreeCADBase__", init_freecad_base_module);
    }
    else {
        // "import FreeCAD" in a normal Python 3.12 interpreter would raise
        //     Fatal Python error: PyImport_AppendInittab:
        //         PyImport_AppendInittab() may not be called after Py_Initialize()
        //  because the (external) interpreter is already initialized.
        //  Therefore we use a workaround as described in https://stackoverflow.com/a/57019607

        PyObject* sysModules = PyImport_GetModuleDict();

        auto moduleName = "FreeCAD";
        PyImport_AddModule(moduleName);
        ApplicationMethods = ApplicationPy::Methods;
        PyObject *pyModule = init_freecad_module();
        PyDict_SetItemString(sysModules, moduleName, pyModule);
        Py_DECREF(pyModule);

        moduleName = "__FreeCADBase__";
        PyImport_AddModule(moduleName);
        pyModule = init_freecad_base_module();
        PyDict_SetItemString(sysModules, moduleName, pyModule);
        Py_DECREF(pyModule);
    }

    std::string pythonpath = Base::Interpreter().init(argc,argv);
    if (!pythonpath.empty())
        mConfig["PythonSearchPath"] = pythonpath;
    else
        Base::Console().warning("Encoding of Python paths failed\n");

    // Handle the options that have impact on the init process
    processProgramOptions(vm, mConfig);

    // Init console ===========================================================
    Base::PyGILStateLocker lock;
    _pConsoleObserverStd = new Base::ConsoleObserverStd();
    Base::Console().attachObserver(_pConsoleObserverStd);
    if (mConfig["LoggingConsole"] != "1") {
        _pConsoleObserverStd->bMsg = false;
        _pConsoleObserverStd->bLog = false;
        _pConsoleObserverStd->bWrn = false;
        _pConsoleObserverStd->bErr = false;
    }

    // file logging Init ===========================================================
    if (mConfig["LoggingFile"] == "1") {
        _pConsoleObserverFile = new Base::ConsoleObserverFile(mConfig["LoggingFileName"].c_str());
        Base::Console().attachObserver(_pConsoleObserverFile);
    }
    else
        _pConsoleObserverFile = nullptr;

    App::installConsoleQtBridge();
    App::installTranslationQtBridge();

    // Banner ===========================================================
    if (mConfig["RunMode"] != "Cmd" && !(vm.contains("verbose") && vm.contains("version"))) {
        // Remove banner if FreeCAD is invoked via the -c command as regular
        // Python interpreter
        if (mConfig["Verbose"] != "Strict")
            Base::Console().message("%s %s, Libs: %s.%s.%s%sR%s\n%s",
                              mConfig["ExeName"].c_str(),
                              mConfig["ExeVersion"].c_str(),
                              mConfig["BuildVersionMajor"].c_str(),
                              mConfig["BuildVersionMinor"].c_str(),
                              mConfig["BuildVersionPoint"].c_str(),
                              mConfig["BuildVersionSuffix"].c_str(),
                              mConfig["BuildRevision"].c_str(),
                              mConfig["CopyrightInfo"].c_str());
        else
            Base::Console().message("%s %s, Libs: %s.%s.%s%sR%s\n",
                              mConfig["ExeName"].c_str(),
                              mConfig["ExeVersion"].c_str(),
                              mConfig["BuildVersionMajor"].c_str(),
                              mConfig["BuildVersionMinor"].c_str(),
                              mConfig["BuildVersionPoint"].c_str(),
                              mConfig["BuildVersionSuffix"].c_str(),
                              mConfig["BuildRevision"].c_str());

        if (SafeMode::SafeModeEnabled()) {
            Base::Console().message("FreeCAD is running in _SAFE_MODE_.\n"
                              "Safe mode temporarily disables your configurations and "
                              "addons. Restart the application to exit safe mode.\n\n");
        }
    }
    LoadParameters();

    auto loglevelParam = _pcUserParamMngr->GetGroup("BaseApp/LogLevels");
    const auto &loglevels = loglevelParam->GetIntMap();
    bool hasDefault = false;
    for (const auto &v : loglevels) {
        if (v.first == "Default") {
#ifndef FC_DEBUG
            if (v.second>=0) {
                hasDefault = true;
                Base::Console().setDefaultLogLevel(v.second);
            }
#endif
        }
        else if (v.first == "DebugDefault") {
#ifdef FC_DEBUG
            if (v.second>=0) {
                hasDefault = true;
                Base::Console().setDefaultLogLevel(static_cast<int>(v.second));
            }
#endif
        }
        else {
            *Base::Console().getLogLevel(v.first.c_str()) = static_cast<int>(v.second);
        }
    }

    if (!hasDefault) {
#ifdef FC_DEBUG
        loglevelParam->SetInt("DebugDefault", Base::Console().logLevel(-1));
#else
        loglevelParam->SetInt("Default", Base::Console().logLevel(-1));
#endif
    }

    // Change application tmp. directory
    std::string tmpPath = _pcUserParamMngr->GetGroup("BaseApp/Preferences/General")->GetASCII("TempPath");
    Base::FileInfo di(tmpPath);
    if (di.exists() && di.isDir()) {
        mConfig["AppTempPath"] = tmpPath + PATHSEP;
    }


    // capture python variables
    SaveEnv("PYTHONPATH");
    SaveEnv("PYTHONHOME");
    SaveEnv("TCL_LIBRARY");
    SaveEnv("TCLLIBPATH");

    // capture CasCade variables
    SaveEnv("CSF_MDTVFontDirectory");
    SaveEnv("CSF_MDTVTexturesDirectory");
    SaveEnv("CSF_UnitsDefinition");
    SaveEnv("CSF_UnitsLexicon");
    SaveEnv("CSF_StandardDefaults");
    SaveEnv("CSF_PluginDefaults");
    SaveEnv("CSF_LANGUAGE");
    SaveEnv("CSF_SHMessage");
    SaveEnv("CSF_XCAFDefaults");
    SaveEnv("CSF_GraphicShr");
    SaveEnv("CSF_IGESDefaults");
    SaveEnv("CSF_STEPDefaults");

    // capture path
    SaveEnv("PATH");

    // Save version numbers of the libraries
#ifdef OCC_VERSION_STRING_EXT
    mConfig["OCC_VERSION"] = OCC_VERSION_STRING_EXT;
#endif
    mConfig["BOOST_VERSION"] = BOOST_LIB_VERSION;
    mConfig["PYTHON_VERSION"] = PY_VERSION;
    mConfig["QT_VERSION"] = QT_VERSION_STR;
    mConfig["EIGEN_VERSION"] = fcEigen3Version;
    mConfig["PYSIDE_VERSION"] = fcPysideVersion;
#ifdef SMESH_VERSION_STR
    mConfig["SMESH_VERSION"] = SMESH_VERSION_STR;
#endif
    mConfig["XERCESC_VERSION"] = fcXercescVersion;


    logStatus();

    if (vm.contains("verbose") && vm.contains("version")) {
        Application::_pcSingleton = new Application(mConfig);
        throw Base::ProgramInformation(ProgramInformation::verboseVersionEmitMessage);
    }
}

void Application::SaveEnv(const char* s)
{
    if (auto c = getenvUTF8(s)) {
        mConfig[s] = c.value();
    }
}

void Application::initApplication()
{
    // interpreter and Init script ==========================================================
    // register scripts
    new Base::ScriptProducer( "CMakeVariables", CMakeVariables );
    new Base::ScriptProducer( "FreeCADInit",    FreeCADInit    );
    new Base::ScriptProducer( "FreeCADTest",    FreeCADTest    );

    // creating the application
    if (mConfig["Verbose"] != "Strict")
        Base::Console().log("Create Application\n");
    Application::_pcSingleton = new Application(mConfig);

    // set up Unit system default
    const ParameterGrp::handle hGrp = GetApplication().GetParameterGroupByPath
       ("User parameter:BaseApp/Preferences/Units");
    Base::UnitsApi::setSchema(hGrp->GetInt("UserSchema", Base::UnitsApi::getDefSchemaNum()));
    Base::UnitsApi::setDecimals(hGrp->GetInt("Decimals", Base::UnitsApi::getDecimals()));
    Base::UnitsApi::setDenominator(hGrp->GetInt("FracInch", Base::UnitsApi::getDenominator()));

#if defined (_DEBUG)
    Base::Console().log("Application is built with debug information\n");
#endif

    // starting the init script
    Base::Console().log("Run App init script\n");
    try {
        Base::Interpreter().runString(Base::ScriptFactory().ProduceScript("CMakeVariables"));
        Base::Interpreter().runString(Base::ScriptFactory().ProduceScript("FreeCADInit"));
    }
    catch (const Base::Exception& e) {
        e.reportException();
    }

    // seed randomizer
    srand(time(nullptr));
}

std::list<std::string> Application::getCmdLineFiles()
{
    std::list<std::string> files;

    // cycling through all the open files
    unsigned short count = 0;
    count = atoi(mConfig["OpenFileCount"].c_str());
    std::string File;

    for (unsigned short i=0; i<count; i++) {
        // getting file name
        std::ostringstream temp;
        temp << "OpenFile" << i;
        files.emplace_back(mConfig[temp.str()]);
    }

    return files;
}

std::list<std::string> Application::processFiles(const std::list<std::string>& files)
{
    std::list<std::string> processed;
    Base::Console().log("Init: Processing command line files\n");
    for (const auto & it : files) {
        Base::FileInfo file(it);
        // Can we safely remove the isSymlink check and directly query the canonical
        // path for every string? The reason for avoiding it currently is that
        // getCannonicalPath will log an error if the file doesn't exist
        if (file.isSymlink()) {
            if (auto cannonicalPath = file.getCannonicalPath()) {
                file = Base::FileInfo(*cannonicalPath);
            } else {
                Base::Console().error("Failed to process symlink file: %s\n", file.filePath());
            }
        }

        Base::Console().log("Init:     Processing file: %s\n",file.filePath().c_str());

        try {
            if (file.hasExtension("fcstd") || file.hasExtension("fcbak")
                || file.hasExtension("std")) {
                // try to open
                Application::_pcSingleton->openDocument(file.filePath().c_str());
                processed.push_back(it);
            }
            else if (file.hasExtension("fcscript") || file.hasExtension("fcmacro")) {
                Base::Interpreter().runFile(file.filePath().c_str(), true);
                processed.push_back(it);
            }
            else if (file.hasExtension("py")) {
                try {
                    Base::Interpreter().addPythonPath(file.dirPath().c_str());
                    Base::Interpreter().loadModule(file.fileNamePure().c_str());
                    processed.push_back(it);
                }
                catch (const Base::PyException&) {
                    // if loading the module does not work, try just running the script (run in __main__)
                    Base::Interpreter().runFile(file.filePath().c_str(),true);
                    processed.push_back(it);
                }
            }
            else {
                std::vector<std::string> mods = GetApplication().getImportModules(file.extension());
                if (!mods.empty()) {
                    std::string escapedstr = Base::Tools::escapedUnicodeFromUtf8(file.filePath().c_str());
                    escapedstr = Base::Tools::escapeEncodeFilename(escapedstr);

                    Base::Interpreter().loadModule(mods.front().c_str());
                    Base::Interpreter().runStringArg("import %s",mods.front().c_str());
                    Base::Interpreter().runStringArg("%s.open(u\"%s\")",mods.front().c_str(),
                            escapedstr.c_str());
                    processed.push_back(it);
                    Base::Console().log("Command line open: %s.open(u\"%s\")\n",mods.front().c_str(),escapedstr.c_str());
                }
                else if (file.exists()) {
                    Base::Console().warning("File format not supported: %s \n", file.filePath().c_str());
                }
            }
        }
        catch (const Base::SystemExitException&) {
            throw; // re-throw to main() function
        }
        catch (const Base::Exception& e) {
            Base::Console().error("Exception while processing file: %s [%s]\n", file.filePath().c_str(), e.what());
        }
        catch (...) {
            Base::Console().error("Unknown exception while processing file: %s \n", file.filePath().c_str());
        }
    }

    return processed; // successfully processed files
}

void Application::processCmdLineFiles()
{
    const std::list<std::string> files = getCmdLineFiles();
    const std::list<std::string> processed = processFiles(files);

    if (files.empty()) {
        if (mConfig["RunMode"] == "Exit")
            mConfig["RunMode"] = "Cmd";
    }
    else if (processed.empty() && files.size() == 1 && mConfig["RunMode"] == "Cmd") {
        // In case we are in console mode and the argument is not a file but Python code
        // then execute it. This is to behave like the standard Python executable.
        const Base::FileInfo file(files.front());
        if (!file.exists()) {
            Base::Interpreter().runString(files.front().c_str());
            mConfig["RunMode"] = "Exit";
        }
    }

    const std::map<std::string, std::string>& cfg = Application::Config();
    const auto it = cfg.find("SaveFile");
    if (it != cfg.end()) {
        std::string output = it->second;
        output = Base::Tools::escapeEncodeFilename(output);

        const Base::FileInfo fi(output);
        try {
            const std::vector<std::string> mods = GetApplication().getExportModules(fi.extension());
            if (!mods.empty()) {
                Base::Interpreter().loadModule(mods.front().c_str());
                Base::Interpreter().runStringArg("import %s",mods.front().c_str());
                Base::Interpreter().runStringArg("%s.export(App.ActiveDocument.Objects, '%s')"
                    ,mods.front().c_str(),output.c_str());
            }
            else {
                Base::Console().warning("File format not supported: %s \n", output.c_str());
            }
        }
        catch (const Base::Exception& e) {
            Base::Console().error("Exception while saving to file: %s [%s]\n", output.c_str(), e.what());
        }
        catch (...) {
            Base::Console().error("Unknown exception while saving to file: %s \n", output.c_str());
        }
    }
}

void Application::runApplication()
{
    // process all files given through command line interface
    processCmdLineFiles();

    if (mConfig["RunMode"] == "Cmd") {
        // Run the commandline interface
        Base::Interpreter().runCommandLine("FreeCAD Console mode");
    }
    else if (mConfig["RunMode"] == "Internal") {
        // run internal script
        Base::Console().log("Running internal script:\n");
        Base::Interpreter().runString(Base::ScriptFactory().ProduceScript(mConfig["ScriptFileName"].c_str()));
    }
    else if (mConfig["RunMode"] == "Exit") {
        // getting out
        Base::Console().log("Exiting on purpose\n");
    }
    else {
        Base::Console().log("Unknown Run mode (%d) in main()?!?\n\n", mConfig["RunMode"].c_str());
    }
}

void Application::logStatus()
{
    const std::string time_str = boost::posix_time::to_simple_string(
        boost::posix_time::second_clock::local_time());
    Base::Console().log("Time = %s\n", time_str.c_str());

    for (const auto & It : mConfig) {
        Base::Console().log("%s = %s\n", It.first.c_str(), It.second.c_str());
    }
}


#if defined(_MSC_VER) && BOOST_VERSION < 108200
    // fix weird error while linking boost (all versions of VC)
    // VS2010: https://forum.freecad.org/viewtopic.php?f=4&t=1886&p=12553&hilit=boost%3A%3Afilesystem%3A%3Aget#p12553
    namespace boost { namespace program_options { std::string arg="arg"; } }
    namespace boost { namespace program_options {
    const unsigned options_description::m_default_line_length = 80;
    } }
#endif

// A helper function to simplify the main part.
template<class T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v)
{
    copy(v.begin(), v.end(), std::ostream_iterator<T>(std::cout, " "));
    return os;
}
