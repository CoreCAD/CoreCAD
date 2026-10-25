// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include <algorithm>
#include <map>
#include <optional>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/geometry/geometries/register/point.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/geometry.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <App/Expression.h>
#include <App/FeaturePythonPyImp.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <App/ObjectIdentifier.h>
#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/TimeInfo.h>
#include <Base/Tools.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Part/App/GeometryMigrationExtension.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/WireJoiner.h>

#include <iomanip>
#include <memory>

#include "GeoEnum.h"
#include "SketchObject.h"
#include "Constraint.h"
#include "SketchObjectPy.h"
#include "ExternalGeometryFacade.h"


#undef DEBUG
// #define DEBUG

using namespace Sketcher;
using namespace Base;
namespace sp = std::placeholders;
namespace bio = boost::iostreams;

// clang-format off
int SketchObject::autoConstraint(double precision, double angleprecision, bool includeconstruction)
{
    return analyser->autoconstraint(precision, angleprecision, includeconstruction);
}

int SketchObject::detectMissingPointOnPointConstraints(double precision, bool includeconstruction)
{
    return analyser->detectMissingPointOnPointConstraints(precision, includeconstruction);
}

void SketchObject::analyseMissingPointOnPointCoincident(double angleprecision)
{
    analyser->analyseMissingPointOnPointCoincident(angleprecision);
}

int SketchObject::detectMissingVerticalHorizontalConstraints(double angleprecision)
{
    return analyser->detectMissingVerticalHorizontalConstraints(angleprecision);
}

int SketchObject::detectMissingEqualityConstraints(double precision)
{
    return analyser->detectMissingEqualityConstraints(precision);
}

std::vector<ConstraintIds>& SketchObject::getMissingPointOnPointConstraints()
{
    return analyser->getMissingPointOnPointConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingVerticalHorizontalConstraints()
{
    return analyser->getMissingVerticalHorizontalConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingLineEqualityConstraints()
{
    return analyser->getMissingLineEqualityConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingRadiusConstraints()
{
    return analyser->getMissingRadiusConstraints();
}

void SketchObject::setMissingRadiusConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingRadiusConstraints(cl);
}

void SketchObject::setMissingLineEqualityConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingLineEqualityConstraints(cl);
}

void SketchObject::setMissingVerticalHorizontalConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingVerticalHorizontalConstraints(cl);
}

void SketchObject::setMissingPointOnPointConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingPointOnPointConstraints(cl);
}

void SketchObject::makeMissingPointOnPointCoincident(bool onebyone)
{
    if (analyser) {
        onebyone ? analyser->makeMissingPointOnPointCoincidentOneByOne()
                 : analyser->makeMissingPointOnPointCoincident();
    }
}

void SketchObject::makeMissingVerticalHorizontal(bool onebyone)
{
    if (analyser) {
        onebyone ? analyser->makeMissingVerticalHorizontalOneByOne()
                 : analyser->makeMissingVerticalHorizontal();
    }
}

void SketchObject::makeMissingEquality(bool onebyone)
{
    if (analyser) {
        onebyone ? analyser->makeMissingEqualityOneByOne()
                 : analyser->makeMissingEquality();
    }
}

int SketchObject::detectDegeneratedGeometries(double tolerance)
{
    return analyser->detectDegeneratedGeometries(tolerance);
}

int SketchObject::removeDegeneratedGeometries(double tolerance)
{
    return analyser->removeDegeneratedGeometries(tolerance);
}

std::vector<Base::Vector3d> SketchObject::getOpenVertices() const
{
    std::vector<Base::Vector3d> points;

    if (analyser)
        points = analyser->getOpenVertices();

    return points;
}

// clang-format on
