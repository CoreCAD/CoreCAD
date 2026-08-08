// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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


#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Solid.hxx>
#include <BRepCheck_Status.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <ShapeFix_Solid.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Builder.hxx>


#include "App/Datums.h"
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ElementNamingUtils.h>
#include <App/FeaturePythonPyImp.h>
#include <Base/Console.h>

#include "Feature.h"
#include "FeaturePy.h"
#include "Body.h"
#include "ShapeBinder.h"

#include <BRep_Builder.hxx>

FC_LOG_LEVEL_INIT("PartDesign", true, true)


namespace PartDesign
{

bool getPDRefineModelParameter()
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication()
                                             .GetUserParameter()
                                             .GetGroup("BaseApp")
                                             ->GetGroup("Preferences")
                                             ->GetGroup("Mod/PartDesign");
    return hGrp->GetBool("RefineModel", true);
}

// ------------------------------------------------------------------------------------------------

PROPERTY_SOURCE(PartDesign::Feature, Part::ShapeFeature)

Feature::Feature()
{
    ADD_PROPERTY(BaseFeature, (nullptr));
    BaseFeature.setScope(App::LinkScope::Global);  // CoreCAD Phase 3: allow Part::Feature as base
    ADD_PROPERTY_TYPE(
        _Body,
        (nullptr),
        "Base",
        (App::PropertyType)(App::Prop_ReadOnly | App::Prop_Hidden | App::Prop_Output
                            | App::Prop_Transient),
        0
    );
    ADD_PROPERTY(SuppressedShape, (TopoShape()));
    ADD_PROPERTY_TYPE(
        GestureId,
        (""),
        "Base",
        App::Prop_NoRecompute,
        "Shared tag for the sibling features of one multi-body cut/common gesture (Amendment 5 "
        "§5.3). Inert: changing it touches nothing for recompute and no membership derives from it."
    );
    BaseFeature.setStatus(App::Property::Hidden, true);

    App::SuppressibleExtension::initExtension(this);
    Part::PreviewExtension::initExtension(this);
}

App::DocumentObjectExecReturn* Feature::recompute()
{
    setMaterialToBodyMaterial();

    if (Suppressed.getValue()) {
        Shape.setValue(getBaseTopoShape(true));
        updateSuppressedShape();
        return App::DocumentObject::StdReturn;
    }

    SuppressedShape.setValue(TopoShape());
    return Part::ShapeFeature::recompute();
}

App::DocumentObjectExecReturn* Feature::recomputePreview()
{
    updatePreviewShape();

    return StdReturn;
}

void Feature::setMaterialToBodyMaterial()
{
    auto body = getFeatureBody();
    if (body) {
        // Ensure the part has the same material as the body
        auto feature = dynamic_cast<Part::ShapeFeature*>(body);
        if (feature) {
            copyMaterial(feature);
        }
    }
}

void Feature::updateSuppressedShape()
{
    TopoShape res(getID());
    TopoShape shape = Shape.getShape();
    shape.setPlacement(Base::Placement());
    std::vector<TopoShape> generated;
    if (!shape.isNull()) {
        unsigned count = shape.countSubShapes(TopAbs_FACE);
        for (unsigned i = 1; i <= count; ++i) {
            Data::MappedName mapped = shape.getMappedName(Data::IndexedName::fromConst("Face", i));
            if (mapped && shape.isElementGenerated(mapped)) {
                generated.push_back(shape.getSubTopoShape(TopAbs_FACE, i));
            }
        }
    }
    if (!generated.empty()) {
        res.makeElementCompound(generated);
        res.setPlacement(getPlacement());
    }
    SuppressedShape.setValue(res);
}

short Feature::mustExecute() const
{
    if (BaseFeature.isTouched()) {
        return 1;
    }
    return Part::ShapeFeature::mustExecute();
}

void Feature::onBaseFeatureRerouted(App::DocumentObject* /*oldBase*/, App::DocumentObject* /*newBase*/)
{}

bool Feature::relinkToMatchingSubelements(
    App::PropertyLinkSub& link,
    App::DocumentObject* oldBase,
    App::DocumentObject* newBase
)
{
    if (!oldBase || !newBase || link.getValue() != oldBase) {
        return false;
    }

    auto oldFeature = freecad_cast<Part::ShapeFeature*>(oldBase);
    auto newFeature = freecad_cast<Part::ShapeFeature*>(newBase);
    if (!oldFeature || !newFeature) {
        return false;
    }

    const auto& oldShape = oldFeature->Shape.getShape();
    const auto& newShape = newFeature->Shape.getShape();
    if (oldShape.isNull() || newShape.isNull()) {
        return false;
    }

    const auto& oldSubs = link.getSubValues();
    std::vector<std::string> newSubs;
    newSubs.reserve(oldSubs.size());

    for (const auto& sub : oldSubs) {
        if (sub.empty()) {
            newSubs.emplace_back();
            continue;
        }

        auto oldSubShape = oldShape.getSubTopoShape(sub.c_str(), true);
        if (oldSubShape.isNull()) {
            return false;
        }

        std::vector<std::string> names;
        auto matches = newShape.findSubShapesWithSharedVertex(
            oldSubShape,
            &names,
            Data::SearchOption::CheckGeometry
        );
        if (matches.size() != 1 || names.size() != 1) {
            return false;
        }
        newSubs.push_back(names.front());
    }

    link.setValue(newBase, std::move(newSubs));
    return true;
}

void Feature::onChanged(const App::Property* prop)
{
    if (!this->isRestoring() && this->getDocument()
        && !this->getDocument()->isPerformingTransaction()) {
        // Cruth de-ownership: feature order follows the BaseFeature chain itself, so a
        // BaseFeature change needs no Group reindexing (Group is empty). The former
        // Visibility/BaseFeature reorder branch is retired here.
        if (prop == &ShapeMaterial) {
            auto body = Body::findBodyOf(this);
            if (body) {
                if (body->ShapeMaterial.getValue().getUUID() != ShapeMaterial.getValue().getUUID()) {
                    body->ShapeMaterial.setValue(ShapeMaterial.getValue());
                }
            }
        }
        else if (prop == &Suppressed) {
            // Amendment 4: a derived feature holds no authored placement, so the
            // suppress path no longer saves/restores one — its geometry already
            // lives in the world frame.
            if (Suppressed.getValue()) {
                updateSuppressedShape();
            }
        }
    }
    Part::ShapeFeature::onChanged(prop);
}

int Feature::countSolids(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    int result = 0;
    if (shape.IsNull()) {
        return result;
    }
    TopExp_Explorer xp;
    xp.Init(shape, type);
    for (; xp.More(); xp.Next()) {
        result++;
    }
    return result;
}

TopoShape Feature::fixSolids(const TopoShape& solids)
{
    if (solids.isNull()) {
        return solids;
    }

    std::vector<TopoDS_Solid> fixSolids;

    TopExp_Explorer xp;
    xp.Init(solids.getShape(), TopAbs_SOLID);
    for (; xp.More(); xp.Next()) {
        TopoDS_Solid solid = TopoDS::Solid(xp.Current());
        BRepCheck_Solid bs(solid);
        if (bs.IsStatusOnShape(solid)) {
            const auto& listOfStatus = bs.StatusOnShape(solid);
            if (listOfStatus.Contains(BRepCheck_EnclosedRegion)) {
                fixSolids.emplace_back(solid);
            }
        }
    }

    if (fixSolids.empty()) {
        return solids;
    }

    TopoDS_Compound comp;
    TopoDS_Builder bb;
    bb.MakeCompound(comp);
    for (const TopoDS_Solid& it : fixSolids) {
        ShapeFix_Solid fix(it);
        fix.Perform();
        bb.Add(comp, fix.Solid());
    }

    TopoShape fixShape(comp);
    return fixShape;
}

const gp_Pnt Feature::getPointFromFace(const TopoDS_Face& f)
{
    if (!f.Infinite()) {
        TopExp_Explorer exp;
        exp.Init(f, TopAbs_VERTEX);
        if (exp.More()) {
            return BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
        }
        // Else try the other method
    }

    // TODO: Other method, e.g. intersect X,Y,Z axis with the (unlimited?) face?
    // Or get a "corner" point if the face is limited?
    throw Base::NotImplementedError("getPointFromFace(): Not implemented yet for this case");
}

App::DocumentObject* Feature::getBaseObject(bool silent) const
{
    App::DocumentObject* BaseLink = BaseFeature.getValue();
    App::DocumentObject* BaseObject = nullptr;
    const char* err = nullptr;

    if (BaseLink) {
        if (Part::hasShape(BaseLink)) {
            BaseObject = BaseLink;
        }
        if (!BaseObject) {
            err = "No base feature linked";
        }
    }
    else {
        err = "Base property not set";
    }

    // If the function not in silent mode throw the exception describing the error
    if (!silent && err) {
        throw Base::RuntimeError(err);
    }

    return BaseObject;
}

TopoDS_Shape Feature::getBaseShape() const
{
    App::DocumentObject* BaseObject = getBaseObject();

    if (!BaseObject) {
        throw Base::ValueError("Base feature's shape is not defined");
    }

    if (BaseObject->isDerivedFrom<PartDesign::ShapeBinder>()
        || BaseObject->isDerivedFrom<PartDesign::SubShapeBinder>()) {
        throw Base::ValueError("Base shape of shape binder cannot be used");
    }

    const TopoDS_Shape result = Part::getShape(BaseObject).getShape();
    if (result.IsNull()) {
        throw Base::ValueError("Base feature's shape is invalid");
    }
    TopExp_Explorer xp(result, TopAbs_SOLID);
    if (!xp.More()) {
        throw Base::ValueError("Base feature's shape is not a solid");
    }

    return result;
}

Part::TopoShape Feature::getBaseTopoShape(bool silent) const
{
    Part::TopoShape result;

    App::DocumentObject* BaseObject = getBaseObject(silent);
    if (!BaseObject) {
        return result;
    }

    if (BaseObject != BaseFeature.getValue()) {
        auto body = getFeatureBody();
        if (!body) {
            if (silent) {
                return result;
            }
            throw Base::RuntimeError("Missing container body");
        }
        if (BaseObject->isDerivedFrom<PartDesign::ShapeBinder>()
            || BaseObject->isDerivedFrom<PartDesign::SubShapeBinder>()) {
            if (silent) {
                return result;
            }
            throw Base::ValueError("Base shape of shape binder cannot be used");
        }
    }

    result = Part::getShape(BaseObject);
    if (!silent) {
        if (result.isNull()) {
            throw Base::ValueError("Base feature's TopoShape is invalid");
        }
        if (!result.hasSubShape(TopAbs_SOLID)) {
            throw Base::ValueError("Base feature's shape is not a solid");
        }
    }
    else if (!result.hasSubShape(TopAbs_SOLID)) {
        result.setShape(TopoDS_Shape());
    }
    return result;
}

void Feature::getGeneratedShapes(
    std::vector<int>& faces,
    std::vector<int>& edges,
    std::vector<int>& vertices
) const
{
    static const auto addAllSubShapesToSet = [](const Part::TopoShape& shape,
                                                const Part::TopoShape& face,
                                                TopAbs_ShapeEnum type,
                                                std::set<int>& set) {
        for (auto& subShape : face.getSubShapes(type)) {
            if (int subShapeId = shape.findShape(subShape); subShapeId > 0) {
                set.insert(subShapeId);
            }
        }
    };

    Part::TopoShape shape = Shape.getShape();

    std::set<int> edgeSet;
    std::set<int> vertexSet;

    int count = shape.countSubShapes(TopAbs_FACE);

    for (int faceId = 1; faceId <= count; ++faceId) {
        if (Data::MappedName mapped = shape.getMappedName(Data::IndexedName::fromConst("Face", faceId));
            shape.isElementGenerated(mapped)) {
            faces.push_back(faceId);

            Part::TopoShape face = shape.getSubTopoShape(TopAbs_FACE, faceId);

            addAllSubShapesToSet(shape, face, TopAbs_EDGE, edgeSet);
            addAllSubShapesToSet(shape, face, TopAbs_VERTEX, vertexSet);
        }
    }

    std::ranges::copy(edgeSet, std::back_inserter(edges));
    std::ranges::copy(vertexSet, std::back_inserter(vertices));
}

void Feature::updatePreviewShape()
{
    // no-op
}

PyObject* Feature::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

bool Feature::isDatum(const App::DocumentObject* feature)
{
    return feature->isDerivedFrom<App::DatumElement>() || feature->isDerivedFrom<Part::Datum>();
}

gp_Pln Feature::makePlnFromPlane(const App::DocumentObject* obj)
{
    const App::GeoFeature* plane = static_cast<const App::GeoFeature*>(obj);
    if (!plane) {
        throw Base::ValueError("Feature: Null object");
    }

    Base::Vector3d pos = plane->getPlacement().getPosition();
    Base::Rotation rot = plane->getPlacement().getRotation();
    Base::Vector3d normal(0, 0, 1);
    rot.multVec(normal, normal);
    return gp_Pln(gp_Pnt(pos.x, pos.y, pos.z), gp_Dir(normal.x, normal.y, normal.z));
}

// TODO: Toponaming April 2024 Deprecated in favor of TopoShape method.  Remove when possible.
TopoDS_Shape Feature::makeShapeFromPlane(const App::DocumentObject* obj)
{
    BRepBuilderAPI_MakeFace builder(makePlnFromPlane(obj));
    if (!builder.IsDone()) {
        throw Base::CADKernelError("Feature: Could not create shape from base plane");
    }

    return builder.Shape();
}

TopoShape Feature::makeTopoShapeFromPlane(const App::DocumentObject* obj)
{
    BRepBuilderAPI_MakeFace builder(makePlnFromPlane(obj));
    if (!builder.IsDone()) {
        throw Base::CADKernelError("Feature: Could not create shape from base plane");
    }

    return TopoShape(obj->getID(), nullptr, builder.Shape());
}

Body* Feature::getFeatureBody() const
{
    // De-owned features sit in no Group, so a Body's membership is a reverse lookup up the
    // BaseFeature chain, not a Group read (Cruth §11 step 5e). findBodyOf already honours the
    // transient _Body cache, so this simply delegates.
    return Body::findBodyOf(this);
}

App::DocumentObject* Feature::getSubObject(
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* pmat,
    bool transform,
    int depth
) const
{
    if (subname && subname != Data::findElementName(subname)) {
        const char* dot = strchr(subname, '.');
        if (dot) {
            auto body = PartDesign::Body::findBodyOf(this);
            if (body) {
                auto feat = body->findOwnedFeature(std::string(subname, dot));
                if (feat) {
                    // Amendment 4, Clause 4.4: reference resolution through a
                    // feature composes no feature transform. The feature holds
                    // no authored placement, so there is no inverse to apply for
                    // the no-transform case — resolve straight to the owned
                    // feature.
                    return feat->getSubObject(dot + 1, pyObj, pmat, true, depth + 1);
                }
            }
        }
    }
    return Part::ShapeFeature::getSubObject(subname, pyObj, pmat, transform, depth);
}


}  // namespace PartDesign

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PartDesign::FeaturePython, PartDesign::Feature)
template<>
const char* PartDesign::FeaturePython::getViewProviderName() const
{
    return "PartDesignGui::ViewProviderPython";
}
template<>
PyObject* PartDesign::FeaturePython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<PartDesign::FeaturePy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class PartDesignExport FeaturePythonT<PartDesign::Feature>;
}  // namespace App
