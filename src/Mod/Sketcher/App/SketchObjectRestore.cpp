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

FC_LOG_LEVEL_INIT("Sketch", true, true)

// clang-format off
void SketchObject::Save(Writer& writer) const
{
    int index = -1;
    auto &geos = const_cast<Part::PropertyGeometryList&>(ExternalGeo).getValues();
    for(auto geo : geos)
        ExternalGeometryFacade::getFacade(geo)->setRefIndex(-1);

    if(isExporting()) {
        // We cannot export shape with the new topological naming, because it
        // uses hasher indices that are unique only within its owner document.
        // Therefore, we cannot rely on Geometry::Ref as key to map geometry to
        // external object reference. So, before exporting, we pre-calculate
        // the mapping and store them in Geometry::RefIndex. When importing,
        // inside updateGeometryRefs() (called by onDocumentRestore()), we shall
        // regenerate Geometry::Ref based on RefIndex.
        //
        // Note that the regenerated Ref will not be using the new topological
        // naming either, because we didn't export them.  This is exactly the
        // same as if we are opening a legacy file without new names.
        // updateGeometryRefs() will know how to handle the name change thanks
        // to a flag setup in onUpdateElementReference().
        for(auto &key : externalGeoRef) {
            ++index;
            auto iter = externalGeoRefMap.find(key);
            if(iter == externalGeoRefMap.end())
                continue;
            for(auto id : iter->second) {
                auto it = externalGeoMap.find(id);
                if(it != externalGeoMap.end())
                    ExternalGeometryFacade::getFacade(geos[it->second])->setRefIndex(index);
            }
        }
    }

    // save the father classes
    Part::Part2DObject::Save(writer);
}

void SketchObject::Restore(XMLReader& reader)
{
    // read the father classes
    Part::Part2DObject::Restore(reader);
}

void SketchObject::handleChangedPropertyType(Base::XMLReader &reader,
        const char *TypeName, App::Property *prop)
{
    if (prop == &Exports) {
        if(strcmp(TypeName, "App::PropertyLinkList") == 0)
            Exports.Restore(reader);
    }
}

// clang-format on

// clang-format off
void SketchObject::onDocumentRestored()
{
    try {
        onSketchRestore();
        Part::Part2DObject::onDocumentRestored();
    }
    catch (...) {
    }
}

void SketchObject::restoreFinished()
{
    App::DocumentObject::restoreFinished();
    onSketchRestore();
}

void SketchObject::mintDurableIdentity()
{
    // What the geometry is called before it is reborn. Read in list order, which is the only
    // thing the two sides have in common: the re-mint replaces each tag in place.
    std::vector<boost::uuids::uuid> before;
    const std::vector<Part::Geometry*>& geometry = getInternalGeometry();
    before.reserve(geometry.size());
    for (const Part::Geometry* geo : geometry) {
        before.push_back(geo->getTag());
    }

    Part::Part2DObject::mintDurableIdentity();

    std::map<boost::uuids::uuid, boost::uuids::uuid> renamed;
    const std::vector<Part::Geometry*>& reborn = getInternalGeometry();
    for (std::size_t i = 0; i < before.size() && i < reborn.size(); ++i) {
        if (before[i] != reborn[i]->getTag()) {
            renamed[before[i]] = reborn[i]->getTag();
        }
    }
    if (renamed.empty()) {
        return;
    }

    // No signal, for the same reason the property's own re-mint sends none: this runs mid-import
    // on constraints nothing can see yet, and what changes is identity rather than the constraint.
    for (Constraint* constraint : Constraints.getValues()) {
        if (constraint != nullptr) {
            constraint->carryElementReferencesTo(renamed);
        }
    }
}

void SketchObject::rebindConstraintsToDurableGeometry()
{
    // Geometry carries the durable identity a constraint reference resolves through. Build a
    // tag -> current-GeoId map, then let each constraint rewrite its positional GeoIds from
    // the durable tags it read on Restore. Only internal geometry (GeoId == list index) has
    // a tag; axes and external geometry keep the GeoId loaded from the file.
    const std::vector<Part::Geometry*>& geometry = getInternalGeometry();
    std::map<boost::uuids::uuid, int> geoIdByTag;
    for (int geoId = 0; geoId < static_cast<int>(geometry.size()); ++geoId) {
        geoIdByTag[geometry[geoId]->getTag()] = geoId;
    }

    auto tagToGeoId = [&geoIdByTag](const boost::uuids::uuid& tag) -> std::optional<int> {
        const auto it = geoIdByTag.find(tag);
        if (it == geoIdByTag.end()) {
            return std::nullopt;
        }
        return it->second;
    };

    int danglingCount = 0;
    for (Constraint* constraint : Constraints.getValues()) {
        if (constraint->bindElementsToDurableGeometry(tagToGeoId)) {
            ++danglingCount;
        }
    }

    // §10.1 / Amendment 15 (honest retirement, loud-drop): a reference whose durable target
    // is gone is disclosed, never silently patched. The dangling elements were marked
    // GeoUndef above, so validateConstraints/the solver will treat them as invalid rather
    // than let a stale index alias a surviving element.
    if (danglingCount > 0) {
        FC_WARN(getFullName() << ": " << danglingCount
                              << " constraint(s) reference geometry that no longer exists; "
                                 "their references were dropped rather than rebound to a "
                                 "different element");
    }
}

void SketchObject::onSketchRestore()
{
    try {
        // First, before anything reads a constraint's references. Resolving a reference is part
        // of reading it, not a repair afterwards: the file states the durable identity of the
        // geometry a constraint holds and not the index it currently occupies, so until this has
        // run every reference reads as GeoUndef. A pass that runs ahead of it is not looking at
        // the sketch the file describes.
        rebindConstraintsToDurableGeometry();

        migrateSketch();

        updateGeometryRefs();

        fixMissingAxisInExternalGeo();

        if(ExternalGeo.getSize()<=2) {
            for(auto &key : externalGeoRef) {
                long id = getDocument()->getStringHasher()->getID(key.c_str()).value();
                if(geoLastId < id)
                    geoLastId = id;
                externalGeoRefMap[key].push_back(id);
            }
            rebuildExternalGeometry();
            if(ExternalGeometry.getSize()+2!=ExternalGeo.getSize())
                FC_WARN("Failed to restore some external geometry in " << getFullName());
        }else
            acceptGeometry();

        synchroniseGeometryState();
        // this may happen when saving a sketch directly in edit mode
        // but never performed a recompute before
        if (Shape.getValue().IsNull() && hasConflicts() == 0) {
            if (this->solve(true) == 0) {
                // Setting the shape sets the placement from it, and an unattached sketch's
                // placement is the person's: the shape carries it rather than overwrite it.
                Part::TopoShape solved = solvedSketch.toShape();
                solved.setTransform(Placement.getValue().toMatrix());
                Shape.setValue(solved);
            }
        }

        // Sanity check on constraints with expression. It is added because the
        // way SketchObject syncs expression and constraints heavily relies on
        // proper setup of undo/redo transactions. The missing transaction in
        // EditDatumDialog may cause stray or worse wrongly bound expressions.
        for (auto &v : ExpressionEngine.getExpressions()) {
            if (v.first.getProperty() != &Constraints)
                continue;
            const Constraint * cstr = nullptr;
            try {
                cstr = Constraints.getConstraint(v.first);
            } catch (Base::Exception &) {
            }
            if (!cstr || !cstr->isDimensional()) {
                FC_WARN((cstr ? "Invalid" : "Orphan")
                        << " constraint expression in "
                        << getFullName() << "."
                        << v.first.toString()
                        << ": " << v.second->toString());
                ExpressionEngine.setValue(v.first, nullptr);
            }
        }
    } catch (Base::Exception &e) {
        e.reportException();
        FC_ERR("Error while restoring " << getFullName());
    } catch (...) {
    }
}

// clang-format on
void SketchObject::migrateSketch()
{

    const auto& allGeoms = getInternalGeometry();
    bool noextensions = std::ranges::any_of(allGeoms, [](const auto& geo) {
        return !geo->hasExtension(SketchGeometryExtension::getClassTypeId());
    });

    if (noextensions) {
        for (const auto& c : Constraints.getValues()) {
            addGeometryState(c);

            // Convert B-Spline controlpoints radius/diameter constraints to Weight constraints
            if (c->Type != InternalAlignment || c->AlignmentType != BSplineControlPoint) {
                continue;
            }

            int circleGeoId = c->First;
            int bSplineGeoId = c->Second;

            auto bsp = static_cast<const Part::GeomBSplineCurve*>(getGeometry(bSplineGeoId));

            std::vector<double> weights = bsp->getWeights();

            if (!(c->InternalAlignmentIndex < int(weights.size()))) {
                continue;
            }

            for (auto& ccp : Constraints.getValues()) {
                if ((ccp->Type == Radius || ccp->Type == Diameter) && ccp->First == circleGeoId) {
                    ccp->Type = Weight;
                    ccp->setValue(weights[c->InternalAlignmentIndex]);
                }
            }
        }

        // Construction migration to extension
        for (auto& g : Geometry.getValues()) {
            if (!g->hasExtension(Part::GeometryMigrationExtension::getClassTypeId())) {
                continue;
            }

            auto ext = std::static_pointer_cast<Part::GeometryMigrationExtension>(
                g->getExtension(Part::GeometryMigrationExtension::getClassTypeId()).lock()
            );

            if (!ext->testMigrationType(Part::GeometryMigrationExtension::Construction)) {
                continue;
            }
            // at this point IA geometry is already migrated
            auto gf = GeometryFacade::getFacade(g);

            bool oldConstr = ext->getConstruction()
                || (g->is<Part::GeomPoint>() && !gf->isInternalAligned());

            GeometryFacade::setConstruction(g, oldConstr);

            g->deleteExtension(Part::GeometryMigrationExtension::getClassTypeId());
        }
    }

    {
        // Migrate point-line, circle-circle and circle-line distance from abs to signed
        auto constraints = Constraints.getValues();
        for (auto& constr : constraints) {
            setOrientation(constr, false);
        }

        Constraints.setValues(std::move(constraints));
    }

    /* parabola axis as internal geometry */
    auto constraints = Constraints.getValues();
    auto geometries = getInternalGeometry();

    bool parabolaFound = std::ranges::any_of(geometries, &Part::Geometry::is<Part::GeomArcOfParabola>);

    if (!parabolaFound) {
        return;
    }

    auto focalAxisFound = std::ranges::any_of(constraints, [](auto c) {
        return c->Type == InternalAlignment && c->AlignmentType == ParabolaFocalAxis;
    });

    if (focalAxisFound) {
        return;
    }

    // There are parabolas and there isn't an IA axis. (1) there are no axis or (2) there is a
    // legacy construction line

    // maps parabola geoid to focusGeoId
    std::map<int, int> parabolaGeoId2FocusGeoId;

    // populate parabola and focus geoids
    for (const auto& c : constraints) {
        if (c->Type == InternalAlignment && c->AlignmentType == ParabolaFocus) {
            parabolaGeoId2FocusGeoId[c->Second] = {c->First};
        }
    }

    // maps axis geoid to parabolaGeoId
    std::map<int, int> axisGeoId2ParabolaGeoId;

    // populate axis geoid
    for (const auto& [parabolaGeoId, focusGeoId] : parabolaGeoId2FocusGeoId) {
        // look for a line from focusGeoId:start to Geoid:mid_external
        std::vector<int> focusGeoIdListGeoIdList;
        std::vector<PointPos> focusPosIdList;
        getDirectlyCoincidentPoints(
            focusGeoId,
            Sketcher::PointPos::start,
            focusGeoIdListGeoIdList,
            focusPosIdList
        );

        std::vector<int> parabGeoIdListGeoIdList;
        std::vector<PointPos> parabposidlist;
        getDirectlyCoincidentPoints(
            parabolaGeoId,
            Sketcher::PointPos::mid,
            parabGeoIdListGeoIdList,
            parabposidlist
        );

        for (const auto& parabGeoIdListGeoId : parabGeoIdListGeoIdList) {
            auto iterParabolaGeoId = std::ranges::find(focusGeoIdListGeoIdList, parabGeoIdListGeoId);
            if (iterParabolaGeoId != focusGeoIdListGeoIdList.end()) {
                axisGeoId2ParabolaGeoId[*iterParabolaGeoId] = parabolaGeoId;
            }
        }
    }

    std::vector<Constraint*> newConstraints;
    newConstraints.reserve(constraints.size());

    for (const auto& c : constraints) {
        if (c->Type != Coincident) {
            newConstraints.push_back(c);
            continue;
        }

        auto axisMajorCoincidentFound
            = std::ranges::any_of(axisGeoId2ParabolaGeoId, [&](const auto& pair) {
                  auto parabolaGeoId = pair.second;
                  auto axisgeoid = pair.first;
                  return (c->First == axisgeoid && c->Second == parabolaGeoId
                          && c->SecondPos == PointPos::mid)
                      || (c->Second == axisgeoid && c->First == parabolaGeoId
                          && c->FirstPos == PointPos::mid);
              });

        if (axisMajorCoincidentFound) {
            // we skip this coincident, the other coincident on axis will be substituted
            // by internal geometry constraint
            continue;
        }

        auto focusCoincidentFound = std::ranges::find_if(axisGeoId2ParabolaGeoId, [&](const auto& pair) {
            auto parabolaGeoId = pair.second;
            auto axisgeoid = pair.first;
            auto focusGeoId = parabolaGeoId2FocusGeoId[parabolaGeoId];
            return (c->First == axisgeoid && c->Second == focusGeoId
                    && c->SecondPos == PointPos::start)
                || (c->Second == axisgeoid && c->First == focusGeoId
                    && c->FirstPos == PointPos::start);
        });

        if (focusCoincidentFound != axisGeoId2ParabolaGeoId.end()) {
            auto* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::ParabolaFocalAxis;
            newConstr->First = focusCoincidentFound->first;  // axis geoid
            newConstr->FirstPos = Sketcher::PointPos::none;
            newConstr->Second = focusCoincidentFound->second;  // parabola geoid
            newConstr->SecondPos = Sketcher::PointPos::none;
            newConstraints.push_back(newConstr);

            addGeometryState(newConstr);

            // we skip the coincident, as we have substituted it by internal geometry
            // constraint
            continue;
        }

        newConstraints.push_back(c);
    }

    Constraints.setValues(std::move(newConstraints));

    Base::Console().critical(
        this->getFullName(),
        QT_TRANSLATE_NOOP(
            "Notifications",
            "Parabolas were migrated. Migrated files won't open in previous "
            "versions of FreeCAD!!\n"
        )
    );
}
