// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
#include <App/PropertyUnits.h>

#include <Mod/Part/PartGlobal.h>

#include "PartFeature.h"


namespace Part
{

/** The surface ruled between two curves.
 *
 * Derived, not an anchor (Amendment 4): it is stretched between the two curves
 * it consumes, so it holds no authored placement and derives from the unplaced
 * ShapeFeature.
 */
class PartExport RuledSurface: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::RuledSurface);

public:
    RuledSurface();

    App::PropertyEnumeration Orientation;
    App::PropertyLinkSub Curve1;
    App::PropertyLinkSub Curve2;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderRuledSurface";
    }
    //@}

protected:
    void onChanged(const App::Property* prop) override;

private:
    App::DocumentObjectExecReturn* getShape(const App::PropertyLinkSub& link, TopoDS_Shape&) const;

private:
    static const char* OrientationEnums[];
};

/** A shape lofted through a series of section profiles.
 *
 * Derived, not an anchor (Amendment 4): it passes through the sections it
 * consumes and sits where they sit, so it holds no authored placement and
 * derives from the unplaced ShapeFeature.
 */
class PartExport Loft: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Loft);

public:
    Loft();

    App::PropertyLinkList Sections;
    App::PropertyBool Solid;
    App::PropertyBool Ruled;
    App::PropertyBool Closed;
    App::PropertyBool Linearize;
    App::PropertyIntegerConstraint MaxDegree;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderLoft";
    }
    //@}

protected:
    void onChanged(const App::Property* prop) override;

private:
    static App::PropertyIntegerConstraint::Constraints Degrees;
};

/** A profile swept along a spine.
 *
 * Derived, not an anchor (Amendment 4): it follows the spine it consumes, so it
 * holds no authored placement and derives from the unplaced ShapeFeature.
 */
class PartExport Sweep: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Sweep);

public:
    Sweep();

    App::PropertyLinkList Sections;
    App::PropertyLinkSub Spine;
    App::PropertyBool Solid;
    App::PropertyBool Frenet;
    App::PropertyBool Linearize;
    App::PropertyEnumeration Transition;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderSweep";
    }
    //@}

protected:
    void onChanged(const App::Property* prop) override;

private:
    static const char* TransitionEnums[];
};

/** A solid hollowed out to a wall thickness, opened at the chosen faces.
 *
 * Derived, not an anchor (Amendment 4): it sits where the solid it hollows
 * sits, so it holds no authored placement and derives from the unplaced
 * ShapeFeature.
 */
class PartExport Thickness: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Thickness);

public:
    Thickness();

    App::PropertyLinkSub Faces;
    App::PropertyQuantity Value;
    App::PropertyEnumeration Mode;
    App::PropertyEnumeration Join;
    App::PropertyBool Intersection;
    App::PropertyBool SelfIntersection;
    /// Durable neutral references (Part::NRef) for the selected faces, one per sub of
    /// @c Faces, captured on execute. Carried alongside the positional subs so the
    /// selection can be re-bound after the base is rebuilt with different face
    /// ordinals (a merge). Hidden: an implementation detail, not user-facing.
    App::PropertyStringList FaceRefs;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;

    /**
     * Rewrite the positional sub-names in @c Faces from the durable @c FaceRefs.
     *
     * Resolves each stored NRef against the current base solid and replaces the
     * matching @c Faces sub with the sub-name the reference now denotes -- the step
     * that heals a selection gone stale because the base was rebuilt with a different
     * face numbering. A ref that no longer resolves leaves its sub untouched (a lost
     * face is not silently rebound to a survivor). Returns the number of subs changed.
     */
    int rebindFacesFromRefs();
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderThickness";
    }
    //@}

protected:
    void handleChangedPropertyType(
        Base::XMLReader& reader,
        const char* TypeName,
        App::Property* prop
    ) override;

private:
    static const char* ModeEnums[];
    static const char* JoinEnums[];
};

/** The shape it consumes with its redundant seams removed.
 *
 * Derived, not an anchor (Amendment 4): refining changes no position, so it
 * holds no authored placement and derives from the unplaced ShapeFeature.
 */
class Refine: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Refine);

public:
    Refine();

    App::PropertyLink Source;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderRefine";
    }
    //@}
};

/** The shape it consumes with its orientation flipped.
 *
 * Derived, not an anchor (Amendment 4): reversing changes no position, so it
 * holds no authored placement and derives from the unplaced ShapeFeature.
 */
class Reverse: public Part::ShapeFeature
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Reverse);

public:
    Reverse();

    App::PropertyLink Source;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderReverse";
    }
    //@}
};

}  // namespace Part
