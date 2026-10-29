// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <App/Datums.h>

#include "FeatureSketchBased.h"

using namespace PartDesign;

void ProfileBased::getUpToFaceFromLinkSub(TopoShape& upToFace, const App::PropertyLinkSub& refFace)
{
    App::DocumentObject* ref = refFace.getValue();

    if (!ref) {
        throw Base::ValueError("SketchBased: No face selected");
    }

    if (ref->isDerivedFrom<App::Plane>()) {
        upToFace = makeShapeFromPlane(ref);
        return;
    }

    const auto& subs = refFace.getSubValues();
    upToFace = Part::Feature::getTopoShape(
        ref,
        Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
            | Part::ShapeOption::Transform,
        subs.empty() ? nullptr : subs[0].c_str()
    );

    if (!upToFace.hasSubShape(TopAbs_FACE)) {
        throw Base::ValueError("SketchBased: Up to face: Failed to extract face");
    }
}

int ProfileBased::getUpToShapeFromLinkSubList(
    TopoShape& upToShape,
    const App::PropertyLinkSubList& refShape
)
{
    int ret = 0;

    auto subSets = refShape.getSubListValues();

    std::vector<TopoShape> faceList;
    for (auto& subSet : subSets) {
        auto ref = subSet.first;
        if (ref->isDerivedFrom<App::Plane>()) {
            faceList.push_back(makeTopoShapeFromPlane(ref));
            ret++;
        }
        else {
            if (!Part::hasShape(ref)) {
                throw Base::TypeError("SketchBased: Must be face of a feature");
            }

            auto subStrings = subSet.second;
            if (subStrings.empty() || subStrings[0].empty()) {
                TopoShape baseShape = Part::Feature::getTopoShape(
                    ref,
                    Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                        | Part::ShapeOption::Transform
                );


                for (auto face : baseShape.getSubTopoShapes(TopAbs_FACE)) {
                    faceList.push_back(face);
                    ret++;
                }
            }
            else {
                for (auto& subString : subStrings) {
                    auto shape = Part::Feature::getShape(
                        ref,
                        Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                            | Part::ShapeOption::Transform,
                        subString.c_str()
                    );

                    TopoShape face = shape;
                    face = face.makeElementFace();
                    if (face.isNull()) {
                        throw Base::ValueError("SketchBased: Failed to extract face");
                    }
                    faceList.push_back(face);
                    ret++;
                }
            }
        }
    }
    if (ret == 0) {
        return 0;
    }
    if (ret == 1) {
        upToShape = faceList[0];
        return 1;
    }

    // create a unique shell with all selected faces
    upToShape = upToShape.makeElementCompound(faceList);
    return ret;
}

void ProfileBased::getFaceFromLinkSub(TopoDS_Face& upToFace, const App::PropertyLinkSub& refFace)
{
    App::DocumentObject* ref = refFace.getValue();
    std::vector<std::string> subStrings = refFace.getSubValues();

    if (!ref) {
        throw Base::ValueError("SketchBased: No face selected");
    }

    if (ref->isDerivedFrom<App::Plane>()) {
        upToFace = TopoDS::Face(makeShapeFromPlane(ref));
        return;
    }

    if (!Part::hasShape(ref)) {
        throw Base::TypeError("SketchBased: Must be face of a feature");
    }
    Part::TopoShape baseShape = Part::getShape(ref);

    // Allow an empty sub here - example is a sketch reference (no sub) that creates a face.
    if (subStrings.empty()) {
        throw Base::ValueError("SketchBased: No face selected");
    }
    // TODO: Check for multiple UpToFaces?

    upToFace = TopoDS::Face(baseShape.getSubShape(subStrings[0].c_str()));
    if (upToFace.IsNull()) {
        throw Base::ValueError("SketchBased: Failed to extract face");
    }
}


void ProfileBased::getUpToFace(
    TopoShape& upToFace,
    const TopoShape& support,
    const TopoShape& sketchshape,
    const std::string& method,
    gp_Dir& dir
)
{
    if ((method == "UpToLast") || (method == "UpToFirst")) {
        std::vector<Part::cutTopoShapeFaces> cfaces
            = Part::findAllFacesCutBy(support, sketchshape, dir);
        if (cfaces.empty()) {
            throw Base::ValueError("SketchBased: No faces found in this direction");
        }

        // Find nearest/furthest face
        std::vector<Part::cutTopoShapeFaces>::const_iterator it, it_near, it_far;
        it_near = it_far = cfaces.begin();
        for (it = cfaces.begin(); it != cfaces.end(); it++) {
            if (it->distsq > it_far->distsq) {
                it_far = it;
            }
            else if (it->distsq < it_near->distsq) {
                it_near = it;
            }
        }
        upToFace = (method == "UpToLast" ? it_far->face : it_near->face);
    }
    else if (Part::findAllFacesCutBy(upToFace, sketchshape, dir).empty()) {
        dir = -dir;
    }

    if (upToFace.shapeType(true) != TopAbs_FACE) {
        if (!upToFace.hasSubShape(TopAbs_FACE)) {
            throw Base::ValueError("SketchBased: Up to face: No face found");
        }
        upToFace = upToFace.getSubTopoShape(TopAbs_FACE, 1);
    }

    TopoDS_Face face = TopoDS::Face(upToFace.getShape());

    // Check that the upToFace does not intersect the sketch face and
    // is not parallel to the extrusion direction
    BRepAdaptor_Surface adapt(face);

    if (adapt.GetType() == GeomAbs_Plane) {
        if (dir.IsNormal(adapt.Plane().Axis().Direction(), Precision::Confusion())) {
            throw Base::ValueError(
                "SketchBased: Up to face: Must not be parallel to extrusion direction!"
            );
        }
    }

    // We must measure from sketchshape, not supportface, here
    BRepExtrema_DistShapeShape distSS(sketchshape.getShape(), face);
    if (distSS.Value() < Precision::Confusion()) {
        throw Base::ValueError("SketchBased: Up to face: Must not intersect sketch!");
    }
}

void ProfileBased::getUpToFace(
    TopoShape& upToFace,
    const TopoShape& support,
    const TopoShape& sketchshape,
    const std::string& method,
    const gp_Ax1& axis
)
{
    if ((method == "UpToLast") || (method == "UpToFirst")) {
        std::vector<Part::cutTopoShapeFaces> cfaces
            = Part::findAllFacesCutBy(support, sketchshape, axis);
        if (cfaces.empty()) {
            throw Base::ValueError("SketchBased: No faces found in this direction");
        }

        // Find nearest/furthest face
        std::sort(cfaces.begin(), cfaces.end(), [](const auto& it1, const auto& it2) {
            return it1.distsq < it2.distsq;
        });
        upToFace = (method == "UpToLast" ? cfaces.back().face : cfaces.front().face);
    }

    if (upToFace.shapeType(true) != TopAbs_FACE) {
        if (!upToFace.hasSubShape(TopAbs_FACE)) {
            throw Base::ValueError("SketchBased: Up to face: No face found");
        }
        upToFace = upToFace.getSubTopoShape(TopAbs_FACE, 1);
    }

    TopoDS_Face face = TopoDS::Face(upToFace.getShape());

    // Check that the upToFace does not intersect the sketch face and
    // is not normal to the rotation axis
    BRepAdaptor_Surface adapt(face);
    if (adapt.GetType() == GeomAbs_Plane) {
        if (axis.Direction().IsParallel(adapt.Plane().Axis().Direction(), Precision::Confusion())) {
            throw Base::ValueError("SketchBased: Up to face: Must not be normal to rotation axis!");
        }
    }
}

void ProfileBased::addOffsetToFace(TopoShape& upToFace, const gp_Dir& dir, double offset)
{
    // Move the face in the extrusion direction
    // TODO: For non-planar faces, we could consider offsetting the surface
    if (fabs(offset) > Precision::Confusion()) {
        gp_Trsf mov;
        mov.SetTranslation(offset * gp_Vec(dir));
        TopLoc_Location loc(mov);
        upToFace.move(loc);
    }
}

double ProfileBased::getThroughAllLength() const
{
    TopoShape profileshape;
    TopoShape base;
    profileshape = getTopoShapeVerifiedFace(true);
    base = getBaseTopoShape();
    Bnd_Box box;
    BRepBndLib::Add(base.getShape(), box);

    if (!profileshape.isNull()) {
        BRepBndLib::Add(profileshape.getShape(), box);
    }
    box.SetGap(0.0);
    // The diagonal of the bounding box, plus 1%  extra to eliminate risk of
    // co-planar issues, gives a length that is guaranteed to go through all.
    // The result is multiplied by 2 for the guarantee to work also for the midplane option.
    return 2.02 * sqrt(box.SquareExtent());
}
