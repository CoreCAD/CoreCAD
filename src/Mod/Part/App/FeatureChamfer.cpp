// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include <BRepFilletAPI_MakeChamfer.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>


#include <Base/Tools.h>

#include <SignalException.h>
#include "FeatureChamfer.h"
#include "TopoShapeOpCode.h"


using namespace Part;

PROPERTY_SOURCE(Part::Chamfer, Part::FilletBase)

Chamfer::Chamfer()
{
    // Declared here rather than on the shared base: the two numbers per edge are
    // distances, and the file says so in this operation's own words.
    ADD_PROPERTY(Edges, (0, 0, 0));
    Edges.setSize(0);
}

App::DocumentObjectExecReturn* Chamfer::execute()
{
    App::DocumentObject* link = Base.getValue();
    if (!link) {
        return new App::DocumentObjectExecReturn("No object linked");
    }

    try {
        TopoShape baseTopoShape
            = Feature::getTopoShape(link, ShapeOption::ResolveLink | ShapeOption::Transform);
        const auto& baseShape = baseTopoShape.getShape();
        BRepFilletAPI_MakeChamfer mkChamfer(baseShape);
        TopTools_IndexedDataMapOfShapeListOfShape mapEdgeFace;
        TopExp::MapShapesAndAncestors(baseShape, TopAbs_EDGE, TopAbs_FACE, mapEdgeFace);
        TopTools_IndexedMapOfShape mapOfEdges;
        std::vector<Part::FilletElement> edges = Edges.getValues();
        TopExp::MapShapes(baseShape, TopAbs_EDGE, mapOfEdges);
        std::string fullErrMsg;

        const auto& vals = EdgeLinks.getSubValues();
        const auto& subs = EdgeLinks.getShadowSubs();
        if (subs.size() != (size_t)Edges.getSize()) {
            return new App::DocumentObjectExecReturn("Edge link size mismatch");
        }
        size_t i = 0;
        for (const auto& info : edges) {
            auto& sub = subs[i];
            auto& ref = sub.newName.empty() ? vals[i] : sub.newName;
            auto& oldName = sub.oldName.empty() ? "" : sub.oldName;
            ++i;

            if (Data::hasMissingElement(ref.c_str()) || Data::hasMissingElement(oldName.c_str())) {
                fullErrMsg.append("Missing edge link: ");
                fullErrMsg.append(ref);
                fullErrMsg.append("\n");

                auto removeIt = std::remove(edges.begin(), edges.end(), info);
                edges.erase(removeIt, edges.end());

                continue;
            }
            // Toponaming project March 2024:  Replaced this code because it wouldn't work:
            //            TopoDS_Shape edge;
            //            try {
            //                edge = baseTopoShape.getSubShape(ref.c_str());
            //            }catch(...){}
            auto id = Data::MappedName(ref.c_str()).toIndexedName().getIndex();
            const TopoDS_Edge& edge = TopoDS::Edge(mapOfEdges.FindKey(id));
            if (edge.IsNull()) {
                return new App::DocumentObjectExecReturn("Invalid edge link");
            }
            // A chamfer takes a distance along each of the two faces the edge joins, or a
            // distance and an angle. Which of the three kinds this edge takes travels with its
            // measurements, so the numbers below are read the way the person who set them meant.
            const double size = info.radius1;
            const TopoDS_Face& face = TopoDS::Face(mapEdgeFace.FindFromKey(edge).First());
            switch (info.kind) {
                case ChamferType::equalDistance:
                    mkChamfer.Add(size, size, TopoDS::Edge(edge), face);
                    break;
                case ChamferType::twoDistances:
                    mkChamfer.Add(size, info.radius2, TopoDS::Edge(edge), face);
                    break;
                case ChamferType::distanceAngle:
                    if (info.angle <= 0.0 || info.angle >= 180.0) {
                        // Said here rather than left to the kernel, which answers a nonsensical
                        // angle with a failure that names neither the edge nor the number.
                        return new App::DocumentObjectExecReturn(
                            "Chamfer angle must be greater than 0 and less than 180 degrees"
                        );
                    }
                    mkChamfer.AddDA(size, Base::toRadians(info.angle), TopoDS::Edge(edge), face);
                    break;
            }
        }

        if (!fullErrMsg.empty()) {
            return new App::DocumentObjectExecReturn(fullErrMsg);
        }
        Edges.setValues(edges);

        Part::SignalException sig;
        TopoDS_Shape shape = mkChamfer.Shape();
        if (shape.IsNull()) {
            return new App::DocumentObjectExecReturn("Resulting shape is null");
        }

        TopoShape res(0);
        this->Shape.setValue(res.makeElementShape(mkChamfer, baseTopoShape, Part::OpCodes::Chamfer));
        return Part::FilletBase::execute();
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (...) {
        return new App::DocumentObjectExecReturn(
            "Chamfer failed: OCC kernel error in chamfer computation"
        );
    }
}
