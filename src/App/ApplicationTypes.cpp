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


// clang-format off
void Application::initTypes()
{
    // Base types
    Base::Type                      ::init();
    Base::BaseClass                 ::init();
    Base::Exception                 ::init();
    Base::AbortException            ::init();
    Base::Persistence               ::init();

    // Complex data classes
    Data::ComplexGeoData            ::init();
    Data::Segment                   ::init();

    // Properties
    // Note: the order matters
    App::Property                   ::init();
    App::PropertyContainer          ::init();
    App::PropertyLists              ::init();
    App::PropertyBool               ::init();
    App::PropertyBoolList           ::init();
    App::PropertyFloat              ::init();
    App::PropertyFloatList          ::init();
    App::PropertyFloatConstraint    ::init();
    App::PropertyPrecision          ::init();
    App::PropertyQuantity           ::init();
    App::PropertyQuantityConstraint ::init();
    App::PropertyInteger            ::init();
    App::PropertyIntegerConstraint  ::init();
    App::PropertyPercent            ::init();
    App::PropertyEnumeration        ::init();
    App::PropertyIntegerList        ::init();
    App::PropertyIntegerSet         ::init();
    App::PropertyMap                ::init();
    App::PropertyOverrideTable      ::init();
    App::PropertyString             ::init();
    App::PropertyPersistentObject   ::init();
    App::PropertyUUID               ::init();
    App::PropertyFont               ::init();
    App::PropertyStringList         ::init();
    App::PropertyLinkBase           ::init();
    App::PropertyLinkListBase       ::init();
    App::PropertyLink               ::init();
    App::PropertyLinkChild          ::init();
    App::PropertyLinkGlobal         ::init();
    App::PropertyLinkHidden         ::init();
    App::PropertyLinkSub            ::init();
    App::PropertyLinkSubChild       ::init();
    App::PropertyLinkSubGlobal      ::init();
    App::PropertyLinkSubHidden      ::init();
    App::PropertyLinkList           ::init();
    App::PropertyLinkListChild      ::init();
    App::PropertyLinkListGlobal     ::init();
    App::PropertyLinkListHidden     ::init();
    App::PropertyLinkSubList        ::init();
    App::PropertyLinkSubListChild   ::init();
    App::PropertyLinkSubListGlobal  ::init();
    App::PropertyLinkSubListHidden  ::init();
    App::PropertyXLink              ::init();
    App::PropertyXLinkSub           ::init();
    App::PropertyXLinkSubHidden     ::init();
    App::PropertyXLinkSubList       ::init();
    App::PropertyXLinkList          ::init();
    App::PropertyXLinkContainer     ::init();
    App::PropertyMatrix             ::init();
    App::PropertyVector             ::init();
    App::PropertyVectorDistance     ::init();
    App::PropertyPosition           ::init();
    App::PropertyDirection          ::init();
    App::PropertyVectorList         ::init();
    App::PropertyPlacement          ::init();
    App::PropertyPlacementList      ::init();
    App::PropertyPlacementLink      ::init();
    App::PropertyRotation           ::init();
    App::PropertyGeometry           ::init();
    App::PropertyComplexGeoData     ::init();
    App::PropertyColor              ::init();
    App::PropertyColorList          ::init();
    App::PropertyMaterial           ::init();
    App::PropertyMaterialList       ::init();
    App::PropertyPath               ::init();
    App::PropertyFile               ::init();
    App::PropertyFileIncluded       ::init();
    App::PropertyPythonObject       ::init();
    App::PropertyExpressionContainer::init();
    App::PropertyExpressionEngine   ::init();
    // all know unit properties
    App::PropertyAcceleration               ::init();
    App::PropertyAmountOfSubstance          ::init();
    App::PropertyAngle                      ::init();
    App::PropertyArea                       ::init();
    App::PropertyCompressiveStrength        ::init();
    App::PropertyCurrentDensity             ::init();
    App::PropertyDensity                    ::init();
    App::PropertyDissipationRate            ::init();
    App::PropertyDistance                   ::init();
    App::PropertyDynamicViscosity           ::init();
    App::PropertyElectricalCapacitance      ::init();
    App::PropertyElectricalConductance      ::init();
    App::PropertyElectricalConductivity     ::init();
    App::PropertyElectricalInductance       ::init();
    App::PropertyElectricalResistance       ::init();
    App::PropertyElectricCharge             ::init();
    App::PropertySurfaceChargeDensity       ::init();
    App::PropertyVolumeChargeDensity        ::init();
    App::PropertyElectricCurrent            ::init();
    App::PropertyElectricPotential          ::init();
    App::PropertyElectromagneticPotential   ::init();
    App::PropertyFrequency                  ::init();
    App::PropertyForce                      ::init();
    App::PropertyHeatFlux                   ::init();
    App::PropertyInverseArea                ::init();
    App::PropertyInverseLength              ::init();
    App::PropertyInverseVolume              ::init();
    App::PropertyKinematicViscosity         ::init();
    App::PropertyLength                     ::init();
    App::PropertyLuminousIntensity          ::init();
    App::PropertyMagneticFieldStrength      ::init();
    App::PropertyMagneticFlux               ::init();
    App::PropertyMagneticFluxDensity        ::init();
    App::PropertyMagnetization              ::init();
    App::PropertyMass                       ::init();
    App::PropertyMoment                     ::init();
    App::PropertyPressure                   ::init();
    App::PropertyPower                      ::init();
    App::PropertyShearModulus               ::init();
    App::PropertySpecificEnergy             ::init();
    App::PropertySpecificHeat               ::init();
    App::PropertySpeed                      ::init();
    App::PropertyStiffness                  ::init();
    App::PropertyStiffnessDensity           ::init();
    App::PropertyStress                     ::init();
    App::PropertyTemperature                ::init();
    App::PropertyThermalConductivity        ::init();
    App::PropertyThermalExpansionCoefficient::init();
    App::PropertyThermalTransferCoefficient ::init();
    App::PropertyTime                       ::init();
    App::PropertyUltimateTensileStrength    ::init();
    App::PropertyVacuumPermittivity         ::init();
    App::PropertyVelocity                   ::init();
    App::PropertyVolume                     ::init();
    App::PropertyVolumeFlowRate             ::init();
    App::PropertyVolumetricThermalExpansionCoefficient::init();
    App::PropertyWork                       ::init();
    App::PropertyYieldStrength              ::init();
    App::PropertyYoungsModulus              ::init();

    // Extension classes
    App::Extension                     ::init();
    App::ExtensionContainer            ::init();
    App::DocumentObjectExtension       ::init();
    App::GroupExtension                ::init();
    App::GroupExtensionPython          ::init();
    App::GeoFeatureGroupExtension      ::init();
    App::GeoFeatureGroupExtensionPython::init();
    App::LinkBaseExtension             ::init();
    App::LinkBaseExtensionPython       ::init();
    App::LinkExtension                 ::init();
    App::LinkExtensionPython           ::init();
    App::SuppressibleExtension         ::init();
    App::SuppressibleExtensionPython   ::init();
    App::PlacementExtension            ::init();
    App::PlacementExtensionPython      ::init();

    // Document classes
    App::TransactionalObject       ::init();
    App::DocumentObject            ::init();
    App::GeoFeature                ::init();

    // Test features
    App::FeatureTest               ::init();
    App::FeatureTestException      ::init();
    App::FeatureTestColumn         ::init();
    App::FeatureTestRow            ::init();
    App::FeatureTestAbsAddress     ::init();
    App::FeatureTestPlacement      ::init();
    App::FeatureTestAttribute      ::init();
    App::FeatureTestAsyncBlocker   ::init();

    // Feature class
    App::FeaturePython             ::init();
    App::GeometryPython            ::init();
    App::Document                  ::init();
    App::DocumentObjectGroup       ::init();
    App::DocumentObjectGroupPython ::init();
    App::DocumentObjectFileIncluded::init();
    Image::ImagePlane              ::init();
    App::InventorObject            ::init();
    App::VRMLObject                ::init();
    App::Annotation                ::init();
    App::AnnotationLabel           ::init();
    App::MaterialObject            ::init();
    App::MaterialObjectPython      ::init();
    App::TextDocument              ::init();
    App::Placement                 ::init();
    App::PlacementPython           ::init();
    App::DatumElement              ::init();
    App::Plane                     ::init();
    App::Line                      ::init();
    App::Point                     ::init();
    App::LocalCoordinateSystem     ::init();
    App::Origin                    ::init();
    App::Link                      ::init();
    App::LinkPython                ::init();
    App::LinkElement               ::init();
    App::LinkElementPython         ::init();
    App::LinkGroup                 ::init();
    App::LinkGroupPython           ::init();
    App::VarSet                    ::init();
    App::Configuration             ::init();

    // Expression classes
    App::Expression                ::init();
    App::UnitExpression            ::init();
    App::NumberExpression          ::init();
    App::ConstantExpression        ::init();
    App::OperatorExpression        ::init();
    App::VariableExpression        ::init();
    App::ConditionalExpression     ::init();
    App::StringExpression          ::init();
    App::FunctionExpression        ::init();
    App::RangeExpression           ::init();
    App::PyObjectExpression        ::init();

    // Topological naming classes
    App::StringHasher              ::init();
    App::StringID                  ::init();

    // register transaction type
    new App::TransactionProducer<TransactionDocumentObject>
            (DocumentObject::getClassTypeId());

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

    Base::registerServiceImplementation<CenterOfMassProvider>(new NullCenterOfMass);
}
