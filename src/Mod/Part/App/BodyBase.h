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

#pragma once

#include <App/PropertyStandard.h>

#include <Mod/Part/PartGlobal.h>

#include "PartFeature.h"


namespace App
{
class PropertyLinkSubList;
}

namespace Part
{
/** Base class of all body objects in FreeCAD
 * A body is used, e.g. in PartDesign, to aggregate
 * some modeling features to one shape. As long as not
 * in edit or active on a workbench, the body shows only the
 * resulting shape to the outside (Tip link).
 */
// Cruth §3.3/§4 (issue #12): a Body is an unplaced marker, not a placed feature. It derives
// from the *unplaced* Part::ShapeFeature — which carries the shape-source identity every
// consumer resolves a Body through (isDerivedFrom<Part::ShapeFeature>) — but NOT from
// Part::Feature, which would mix in App::PlacementExtension (Amendment 4). A Body therefore
// carries no Placement slot of its own: its features derive their frame from their own
// attachments, never from Body containment, so there is nothing to pin to identity.
class PartExport BodyBase: public Part::ShapeFeature
{
    PROPERTY_HEADER(Part::BodyBase);

public:
    BodyBase();

    /**
     * The final feature of the body it is associated with.
     * Note: tip may either point to the BaseFeature or to some feature inside the Group list.
     */
    App::PropertyLink Tip;

    /**
     * A base object of the body, serves as a base object for the first feature of the body.
     * A Part::Feature link to make bodies be able based upon non-PartDesign Features.
     */
    App::PropertyLink BaseFeature;

    /// Base member list: just the BaseFeature (if any). The OriginGroup extension and its
    /// Group container were retired (Cruth §11 step 5e); a de-owned Body derives its real
    /// member list from the feature graph — the override in PartDesign::Body.
    virtual std::vector<App::DocumentObject*> getFullModel()
    {
        std::vector<App::DocumentObject*> rv;
        if (BaseFeature.getValue()) {
            rv.push_back(BaseFeature.getValue());
        }
        return rv;
    }

    /// Return true if the feature belongs to the body and is located after the target
    bool isAfter(const App::DocumentObject* feature, const App::DocumentObject* target) const;

    /**
     * Return the body which this feature belongs too, or NULL.
     * Note: Normally each PartDesign feature belongs to a single body,
     *       But if a body is based on the feature it also will be return...
     *       But there are could be more features based on the same body.
     * TODO introduce a findBodiesOf() if needed (2015-08-04, Fat-Zer)
     */
    static BodyBase* findBodyOf(const App::DocumentObject* f);

    /**
     * Re-anchor sub-element references off a Body marker onto the Body's Tip feature.
     *
     * ARCHITECTURE §8: a sub-element reference (a picked face/edge/vertex) is
     * feature-anchored, while only a whole-object Body reference tracks the Tip. A face
     * pick on a Body's displayed solid resolves the selection to the Body, producing a
     * malformed "(Body, Face6)" reference that both tracks the Tip and carries a
     * sub-element. Left as-is it forms a dependency cycle the moment a new feature becomes
     * the Tip (the profile chases the very feature being built) and it drives the
     * getFullModel anchor-walk into recursion. This rewrites every "(Body, non-empty-sub)"
     * entry to "(Body.Tip, same-sub)" — the Tip's shape is the Body's shape, so the element
     * name is unchanged — while leaving whole-Body entries (empty sub) and non-Body targets
     * untouched. Every UI site that records an attachment/support reference from a selection
     * calls this before storing it.
     */
    static void rebaseBodySubReferencesToTip(
        std::vector<App::DocumentObject*>& objs,
        std::vector<std::string>& subs
    );
    static void rebaseBodySubReferencesToTip(App::PropertyLinkSubList& links);

    PyObject* getPyObject() override;

protected:
    /// If BaseFeature is getting changed and Tip points to it reset the Tip
    void onBeforeChange(const App::Property* prop) override;
    /// If BaseFeature is set and Tip is null set the Tip to it
    void onChanged(const App::Property* prop) override;
};

}  // namespace Part
