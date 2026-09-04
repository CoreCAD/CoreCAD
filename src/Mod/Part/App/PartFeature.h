// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#pragma once

#include <App/FeaturePython.h>
#include <App/GeoFeature.h>
#include <App/PlacementExtension.h>
#include <Mod/Material/App/PropertyMaterial.h>
#include <Mod/Part/PartGlobal.h>
#include <Base/Bitmask.h>

#include <TopoDS_Face.hxx>

#include <Mod/Part/PartGlobal.h>

#include "PropertyTopoShape.h"
#include "ShapeExtension.h"


class gp_Dir;
class BRepBuilderAPI_MakeShape;

namespace Data
{
struct HistoryItem;
}

namespace Part
{

enum class ShapeOption
{
    NoFlag = 0,
    NeedSubElement = 1,
    ResolveLink = 2,
    Transform = 4,
    NoElementMap = 8,
    DontSimplifyCompound = 16
};
using ShapeOptions = Base::Flags<ShapeOption>;


class PartFeaturePy;
class Feature;

/** Base class of all shape feature classes.
 *
 * ShapeFeature owns the geometry (Shape) and the shape machinery but authors
 * no position of its own — Amendment 4's unplaced base. The placed concrete
 * class Part::Feature (below) adds an authored placement via
 * App::PlacementExtension; the derived PartDesign feature line derives from
 * ShapeFeature directly and holds none. Everything asks getLocation() /
 * getPlacement(), which answer identity when no placement is carried.
 *
 * ShapeFeature composes Part::ShapeExtension (Amendment 17, #79): carrying a
 * shape is a capability, so the whole shape lineage gets it from the base and
 * Part::hasShape(obj) is true for every shape feature — the faithful successor
 * to the ~138 isDerivedFrom<ShapeFeature>() type tests the consumer migration
 * will retire. The extension is stateless (no properties to serialize) and its
 * getSubObject routing stays dormant here: ShapeFeature::getSubObject keeps the
 * in-line resolution; only classes that override getSubObject to delegate to the
 * App base (Box, Body) actually route the shape query through the extension.
 */
class PartExport ShapeFeature: public App::GeoFeature, public Part::ShapeExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::ShapeFeature);

public:
    /// Constructor
    ShapeFeature();
    ~ShapeFeature() override;

    PropertyPartShape Shape;
    Materials::PropertyMaterial ShapeMaterial;

    /** @name methods override feature */
    //@{
    short mustExecute() const override;
    //@}

    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override;
    const App::PropertyComplexGeoData* getPropertyOfGeometry() const override;

    /// Cruth §4.6: does this feature's output stand as a part of its own -- a Body?
    ///
    /// Bodies are not user-managed objects: the user authors features, and a Body is the
    /// system's accounting of which connected solids exist as a result. §4.6 says every
    /// connected solid a feature produces that does not extend an existing chain spawns
    /// one, which makes the honest default here `true` for anything producing a solid --
    /// there is no such thing, in the model, as a solid belonging to no Body.
    ///
    /// It is `false` while that is being built, because flipping it for every shape
    /// feature at once changes what the whole Part workbench produces. A feature opts in
    /// by overriding, and when the last one has, this predicate goes away rather than
    /// being inverted: the model has no room for a shape that opts out.
    virtual bool spawnsBodyForOutput() const
    {
        return false;
    }

    /// A shape-carrying feature is Part-scoped geometry (ARCHITECTURE §7.1, Amendment 8):
    /// only a Part document admits it; Assembly, Drawing, and Spreadsheet documents refuse
    /// it at the admission door. Bodies and sketches narrow this to their own kind.
    App::DocumentObject::ContentScope getContentScope() const override
    {
        return App::DocumentObject::ContentScope::Feature;
    }

    PyObject* getPyObject() override;

    App::ElementNamePair getElementName(const char* name, ElementNameType type = Normal) const override;

    static std::list<Data::HistoryItem> getElementHistory(
        App::DocumentObject* obj,
        const char* name,
        bool recursive = true,
        bool sameType = false
    );

    static QVector<Data::MappedElement> getRelatedElements(
        App::DocumentObject* obj,
        const char* name,
        HistoryTraceType sameType = HistoryTraceType::followTypeChange,
        bool withCache = true
    );

    /** Obtain the element name from a feature based of the element name of its source feature
     *
     * @param obj: current feature
     * @param subname: sub-object/element reference
     * @param src: source feature
     * @param srcSub: sub-object/element reference of the source
     * @param single: if true, then return upon first match is found, or else
     *                return all matches. Multiple matches are possible for
     *                compound of multiple instances of the same source shape.
     *
     * @return Return a vector of pair of new style and old style element names.
     */
    static QVector<Data::MappedElement> getElementFromSource(
        App::DocumentObject* obj,
        const char* subname,
        App::DocumentObject* src,
        const char* srcSub,
        bool single = false
    );

    TopLoc_Location getLocation() const;

    DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* mat,
        bool transform,
        int depth
    ) const override;

    App::Material getMaterialAppearance() const override;
    void setMaterialAppearance(const App::Material& material) override;

    /** Convenience function to extract shape from fully qualified subname
     *
     * @param obj: the parent object
     *
     * @param subname: dot separated full qualified subname
     *
     * @param needSubElement: whether to ignore the non-object subelement
     * reference inside \c subname
     *
     * @param pmat: used as current transformation on input, and return the
     * accumulated transformation on output
     *
     * @param owner: return the owner of the shape returned
     *
     * @param resolveLink: if true, resolve link(s) of the returned 'owner'
     * by calling its getLinkedObject(true) function
     *
     * @param transform: if true, apply obj's transformation. Set to false
     * if pmat already include obj's transformation matrix.
     */
    static TopoDS_Shape getShape(
        const App::DocumentObject* obj,
        ShapeOptions options,
        const char* subname = nullptr,
        Base::Matrix4D* pmat = nullptr,
        App::DocumentObject** owner = nullptr
    );

    static TopoShape getTopoShape(
        const App::DocumentObject* obj,
        ShapeOptions options,
        const char* subname = nullptr,
        Base::Matrix4D* pmat = nullptr,
        App::DocumentObject** owner = nullptr
    );

    static TopoShape simplifyCompound(TopoShape compoundShape);
    static void clearShapeCache();

    static App::DocumentObject* getShapeOwner(
        const App::DocumentObject* obj,
        const char* subname = nullptr
    );

    static bool hasShapeOwner(const App::DocumentObject* obj, const char* subname = nullptr)
    {
        auto owner = getShapeOwner(obj, subname);
        return owner && owner->isDerivedFrom(getClassTypeId());
    }

    static Feature* create(
        const TopoShape& shape,
        const char* name = nullptr,
        App::Document* document = nullptr
    );

    static bool isElementMappingDisabled(App::PropertyContainer* container);

    bool getCameraAlignmentDirection(
        Base::Vector3d& directionZ,
        Base::Vector3d& directionX,
        const char* subname
    ) const override;
    bool getCameraAlignmentDirection(
        Base::Vector3d& directionZ,
        const std::vector<std::string>& subnames
    ) const override;

    static void guessNewLink(std::string& replacementName, DocumentObject* base, const char* oldLink);

    const std::vector<std::string>& searchElementCache(
        const std::string& element,
        Data::SearchOptions options = Data::SearchOption::CheckGeometry,
        double tol = 1e-7,
        double atol = 1e-10
    ) const override;

protected:
    /// recompute only this object
    App::DocumentObjectExecReturn* recompute() override;
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    void onBeforeChange(const App::Property* prop) override;
    void onChanged(const App::Property* prop) override;
    void onDocumentRestored() override;

    void copyMaterial(ShapeFeature* feature);
    void copyMaterial(App::DocumentObject* link);

    void registerElementCache(const std::string& prefix, PropertyPartShape* prop);

    /** Helper function to obtain mapped and indexed element name from a shape
     * @params shape: source shape
     * @param name: the input name, can be either mapped or indexed name
     * @return Returns both the indexed and mapped name
     *
     * If the 'name' referencing a non-primary shape type, i.e. not
     * Vertex/Edge/Face, this function will auto generate a name from primary
     * sub-shapes.
     */
    App::ElementNamePair getExportElementName(TopoShape shape, const char* name) const;

    /**
     * Build a history of changes
     * MakeShape: The operation that created the changes, e.g. FCBRepAlgoAPI_Common
     * type: The type of object we are interested in, e.g. TopAbs_FACE
     * newS: The new shape that was created by the operation
     * oldS: The original shape prior to the operation
     */
    ShapeHistory buildHistory(
        BRepBuilderAPI_MakeShape&,
        TopAbs_ShapeEnum type,
        const TopoDS_Shape& newS,
        const TopoDS_Shape& oldS
    );
    ShapeHistory joinHistory(const ShapeHistory&, const ShapeHistory&);

private:
    struct ElementCache;
    std::map<std::string, ElementCache> _elementCache;
    std::vector<std::pair<std::string, PropertyPartShape*>> _elementCachePrefixMap;
};

/** A shape feature that also authors its own placement (Amendment 4).
 *
 * Kept under the historical type name "Part::Feature" so imported shapes and
 * existing documents need no migration. It mixes App::PlacementExtension into
 * the unplaced ShapeFeature base to carry an authored position; the ->Placement
 * member access resolves to the extension's property. This is the placed
 * concrete class used for primitives, imports and generic shape holders.
 */
class PartExport Feature: public ShapeFeature, public App::PlacementExtension
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Feature);

public:
    Feature();
    ~Feature() override;
};

/** Base of the dress-ups that round or cut back an edge of what they consume.
 *
 * Derived, not an anchor (Amendment 4): a fillet sits where its base sits, so
 * it holds no authored placement and derives from the unplaced ShapeFeature.
 */
class PartExport FilletBase: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::FilletBase);

public:
    FilletBase();

    App::PropertyLink Base;
    PropertyFilletEdges Edges;
    App::PropertyLinkSub EdgeLinks;

    short mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    void onUpdateElementReference(const App::Property* prop) override;

protected:
    void onDocumentRestored() override;
    void onChanged(const App::Property*) override;
    void syncEdgeLink();
};

using FeaturePython = App::FeaturePythonT<Feature>;


/** Base class of all shape feature classes in FreeCAD
 */
class PartExport FeatureExt: public Feature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::FeatureExt);

public:
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderPartExt";
    }
};

// Utility methods
/**
 * Find all faces cut by a line through the centre of gravity of a given face
 * Useful for the "up to face" options to pocket or pad
 */
struct cutTopoShapeFaces
{
    TopoShape face;
    double distsq;
};

PartExport std::vector<cutTopoShapeFaces> findAllFacesCutBy(
    const TopoShape& shape,
    const TopoShape& face,
    const gp_Dir& dir
);

PartExport std::vector<cutTopoShapeFaces> findAllFacesCutBy(
    const TopoShape& shape,
    const TopoShape& face,
    const gp_Ax1& axis
);

/**
 * Check for intersection between the two shapes. Only solids are guaranteed to work properly
 * There are two modes:
 * 1. Bounding box check only - quick but inaccurate
 * 2. Bounding box check plus (if necessary) boolean operation - costly but accurate
 * Return true if the shapes intersect, false if they don't
 * The flag touch_is_intersection decides whether shapes touching at distance zero are regarded
 * as intersecting or not
 * 1. If set to true, a true check result means that a boolean fuse operation between the two shapes
 *    will return a single solid
 * 2. If set to false, a true check result means that a boolean common operation will return a
 *    valid solid
 * If there is any error in the boolean operations, the check always returns false
 */
PartExport bool checkIntersection(
    const TopoDS_Shape& first,
    const TopoDS_Shape& second,
    const bool quick,
    const bool touch_is_intersection
);

/** Rebuild a shape's geometry at the position its location holds.
 *
 * OCC records a rigid move as a location beside the geometry rather than in it.
 * A feature that authors no placement stores its shape at identity (Amendment 4),
 * so such a location is dropped on the way into the Shape property and the result
 * snaps back to the origin. A derived feature whose operation may leave the
 * position in the location -- rather than baking it, as a boolean or a real scale
 * does -- passes its result through here first. A shape already at identity is
 * returned untouched.
 */
PartExport TopoDS_Shape bakeLocationIntoGeometry(const TopoDS_Shape& shape);

}  // namespace Part

ENABLE_BITMASK_OPERATORS(Part::ShapeOption)
