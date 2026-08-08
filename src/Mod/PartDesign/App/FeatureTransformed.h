// SPDX-License-Identifier: LGPL-2.1-or-later

/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#pragma once

#include <gp_Trsf.hxx>

#include <App/PropertyStandard.h>
#include "FeatureRefine.h"


namespace PartDesign
{

/**
 * Abstract superclass of all features that are created by transformation of another feature
 * Transformations are translation, rotation and mirroring
 */
class PartDesignExport Transformed: public PartDesign::FeatureRefine
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Transformed);

public:
    enum class Mode
    {
        Features,
        WholeShape
    };

    Transformed();

    /** The features to be transformed
     */
    App::PropertyLinkList Originals;
    App::PropertyEnumeration TransformMode;
    App::PropertyBool Refine;

    /// Cruth §5.5: when true, the pattern emits its N copies as disconnected solids
    /// (a compound) instead of fusing them into one. The multi-output reconciler then
    /// spawns one Body per copy — "each copy is its own Body". Default false keeps the
    /// classic instance-fusing behaviour.
    App::PropertyBool MultiBody;

    /// Cruth §5.6 skip-list: original ordinals of the instances broken out of the MultiBody
    /// output. An instance's ordinal is its position in the transform sequence
    /// (getTransformedCompShape order) — stable across recompute and independent of other
    /// skips — so the pattern continues with one fewer member and never silently re-merges it.
    /// Index-based per ARCHITECTURE §5.6/§11.2 ("marking that index as broken-out"). An earlier
    /// design keyed this on Body::TipComponentId to "speak the identity language the user
    /// selects", but the element-map component-id is context-dependent: its map name shifts
    /// with the surrounding compound, so a skip keyed on it silently failed to match at execute
    /// time. Break-out translates the selected Body's component-id to its ordinal once, against
    /// the pattern's stored shape where the ids are self-consistent, then stores the ordinal.
    App::PropertyIntegerList SkipInstances;

    /**
     * Returns the BaseFeature property's object(if any) otherwise return first original,
     *         which serves as "Support" for old style workflows
     * @param silent if couldn't determine the base feature and silent == true,
     *               silently return a nullptr, otherwise throw Base::Exception.
     *               Default is false.
     */
    App::DocumentObject* getBaseObject(bool silent = false) const override;

    virtual std::vector<App::DocumentObject*> getOriginals() const;

    /// Return the sketch of the first original
    App::DocumentObject* getSketchObject() const;

    /// Return true if this feature is a child of a MultiTransform
    bool isMultiTransformChild() const;

    /// Get the list of transformations describing the members of the pattern
    // Note: Only the Scaled feature requires the originals
    virtual const std::list<gp_Trsf> getTransformations(const std::vector<App::DocumentObject*> /*originals*/)
    {
        return std::list<gp_Trsf>();  // Default method
    }

    /** @name methods override feature */
    //@{
    /** Recalculate the feature
     * Gets the transformations from the virtual getTransformations() method of the sub class
     * and applies them to every member of Originals. The total number of copies including
     * the untransformed Originals will be sizeof(Originals) times sizeof(getTransformations())
     * If Originals is empty, execute() returns immediately without doing anything as
     * the actual processing will happen in the MultiTransform feature
     */
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    //@}

    App::DocumentObjectExecReturn* recomputePreview() override;

    void onChanged(const App::Property* prop) override;

protected:
    void Restore(Base::XMLReader& reader) override;
    void handleChangedPropertyType(
        Base::XMLReader& reader,
        const char* TypeName,
        App::Property* prop
    ) override;

    /// Hook run before gathering transformations. The base implementation does
    /// nothing; MultiTransform overrides it to purge the touched state of its
    /// linked sub-transformations during a recompute.
    virtual void purgeTouchedTransformations();

private:
};

}  // namespace PartDesign
