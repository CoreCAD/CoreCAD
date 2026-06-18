// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
 *   Copyright (c) 2026 Cruth contributors                                 *
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

#include <App/PropertyStandard.h>
#include <Mod/Part/App/BodyBase.h>
#include <Mod/PartDesign/PartDesignGlobal.h>

namespace App
{
class Origin;
}

namespace Part
{
class Part2DObject;
}

namespace PartDesign
{

class Feature;

class PartDesignExport Body: public Part::BodyBase
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Body);

public:
    App::PropertyBool AllowCompound;

    /// CoreCAD §4.6 visual identity — auto-assigned from a deterministic palette at spawn.
    App::PropertyColor Color;

    /// True if this body feature is active or was active when the document was last closed
    // App::PropertyBool IsActive;

    Body();

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;

    /// returns the type name of the view provider
    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderBody";
    }
    //@}

    /**
     * Add the feature into the body at the current insert point.
     * The insertion point is the before next solid after the Tip feature
     */
    std::vector<App::DocumentObject*> addObject(App::DocumentObject*) override;
    std::vector<DocumentObject*> addObjects(std::vector<DocumentObject*> obj) override;

    /**
     * CoreCAD intra-body de-ownership: wire a new feature into the pipeline by
     * reference (BaseFeature chain + Tip) without adding it to Body.Group.
     * The implementation behind addObject(); handles the tip-append gesture and
     * mid-chain insert. See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<App::DocumentObject*> addObjectDeowned(App::DocumentObject* feature);

    /**
     * Insert the feature into the body after the given feature.
     *
     * @param feature  The feature to insert into the body
     * @param target   The feature relative which one should be inserted the given.
     *                 If target is NULL than insert into the end if where is InsertBefore
     *                 and into the begin if where is InsertAfter.
     * @param after    if true insert the feature after the target. Default is false.
     *
     * @note the method doesn't modify the Tip unlike addObject()
     */
    void insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after = false);

    void setBaseProperty(App::DocumentObject* feature);

    /// Remove the feature from the body
    std::vector<DocumentObject*> removeObject(DocumentObject* obj) override;

    /**
     * Cruth intra-body de-ownership: remove a feature by rewiring the
     * BaseFeature chain (and retreating the Tip) without consulting Group order.
     * The implementation behind removeObject(). See ARCHITECTURE §3.2/§3.3.
     */
    std::vector<App::DocumentObject*> removeObjectDeowned(App::DocumentObject* feature);

    /// Cruth: chain successor of a feature (the solid whose BaseFeature links to it).
    App::DocumentObject* getNextSolidFeatureByChain(App::DocumentObject* feature) const;

    /**
     * Checks if the given document object lays after the current insert point
     * (place before next solid after the Tip)
     */
    bool isAfterInsertPoint(App::DocumentObject* feature);

    /**
     * Return true if the given feature is a solid feature allowed in a Body. Currently this is only
     * valid for features derived from PartDesign::Feature Return false if the given feature is a
     * Sketch or a Part::Datum feature
     */
    static bool isSolidFeature(const App::DocumentObject* obj);

    /**
     * Return true if the given feature is allowed in a Body. Currently allowed are
     * all features derived from PartDesign::Feature and Part::Datum and sketches
     */
    static bool isAllowed(const App::DocumentObject* obj);
    bool allowObject(DocumentObject* obj) override
    {
        return isAllowed(obj);
    }

    /**
     * Return the body which this feature belongs too, or NULL
     * The only difference to BodyBase::findBodyOf() is that this one casts value to Body*
     */
    static Body* findBodyOf(const App::DocumentObject* feature);

    /**
     * Cruth §8.5/§4.6: resolve the base Body for a new sketch-based feature by
     * walking the sketch's anchor chain (AttachmentSupport through datums/reference
     * geometry).
     *  - chain ends at a global plane or independent geometry → auto-spawn a new
     *    Body at document level (§4.6) and return it;
     *  - chain terminates on exactly one Body → return that Body (extend);
     *  - chain reaches more than one Body → return nullptr and set @p ambiguous
     *    (the caller surfaces the §8.3 ambiguity prompt).
     *
     * Lives in the App layer so the Gui command and the Python API share one code
     * path — this is the P8 (Programmatic Equivalence) guarantee for auto-spawn.
     */
    static Body* resolveBaseBody(Part::Part2DObject* sketch, App::Document* doc, bool& ambiguous);

    /**
     * Cruth §4.6: auto-spawn a new Body at document level (no active-Part
     * containment). Color is assigned in setupObject() from the per-document
     * palette index. Returns nullptr if @p doc is null.
     */
    static Body* spawnAutoBody(App::Document* doc);

    PyObject* getPyObject() override;

    std::vector<std::string> getSubObjects(int reason = 0) const override;
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* pmat,
        bool transform,
        int depth
    ) const override;

    void setShowTip(bool enable)
    {
        showTip = enable;
    }

    /**
     * Return the solid feature before the given feature, or before the Tip feature
     * That is, sketches and datum features are skipped
     */
    App::DocumentObject* getPrevSolidFeature(App::DocumentObject* start = nullptr);

    /**
     * Return the next solid feature after the given feature, or after the Tip feature
     * That is, sketches and datum features are skipped
     */
    App::DocumentObject* getNextSolidFeature(App::DocumentObject* start = nullptr);

    // a body is solid if it has features that are solid according to member isSolidFeature.
    bool isSolid();

    /// Cruth substrate flip (Stage 3a): returns the single document-level Origin,
    /// lazily creating it if absent. This is the shared coordinate root that de-owned
    /// features resolve against, replacing the per-body Origin (which stays created and
    /// persisted but dormant until Stage 3b removes it). The document-level Origin is a
    /// free-standing App::Origin not owned by any OriginGroup.
    App::Origin* ensureDocumentOrigin();

protected:
    void onSettingDocument() override;

    /// Adjusts the first solid's feature's base on BaseFeature getting set
    void onChanged(const App::Property* prop) override;

    /// Creates the corresponding Origin object
    void setupObject() override;
    /// Removes all planes and axis if they are still linked to the document
    void unsetupObject() override;

    void onDocumentRestored() override;

private:
    fastsignals::scoped_connection connection;
    bool showTip = false;
};

}  // namespace PartDesign
