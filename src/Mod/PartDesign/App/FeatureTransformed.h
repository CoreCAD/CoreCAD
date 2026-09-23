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
    ///
    /// This is the second of the two merge decisions a pattern makes, and the only one it
    /// owns. The first — whether the pattern's result joins the Body it was added to — is
    /// the ordinary §8.5 Merge Result gesture, asked of every solid feature alike and
    /// answered by the BaseFeature chain. This one asks whether the COPIES join EACH OTHER.
    /// They are independent: a pattern can extend its Body while keeping its copies apart.
    ///
    /// Honoured in both transform modes. In Whole shape the copies are the whole output. In
    /// Features mode the support keeps the untransformed original (it was already in the
    /// chain before the pattern existed) and every transformed copy is emitted beside it
    /// rather than welded on, so N copies still yield N pieces.
    App::PropertyBool MultiBody;

    /// Cruth P7 (#34): how many copies the pattern was asked for, and how many connected
    /// pieces they actually formed among themselves, as of the last recompute. Equal when
    /// nothing overlapped; the second is smaller when copies ran into each other and were
    /// fused. Both zero when the question does not arise (fewer than two copies, or the
    /// copies were kept apart and never fused).
    ///
    /// Derived, never authored: read-only, output (recording them does not re-touch the
    /// feature) and transient (recomputed, never stored). They exist so the answer is
    /// readable — by the dialog that offers the merge choice, and by a script — instead of
    /// living only in a console line nobody is obliged to read. With several originals the
    /// pair records the worst run, which is the one the user needs to hear about.
    App::PropertyInteger InstancesRequested;
    App::PropertyInteger InstancePieces;

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

    /**
     * Cruth P7 (#34): say so when the requested instances collapse into fewer pieces.
     *
     * A pattern whose copies overlap fuses them into fewer connected pieces than were asked
     * for, reports Up-to-date, and says nothing — the user asked for N and silently received
     * fewer. This compares the instances against EACH OTHER (never against the finished
     * result, which is legitimately one solid whenever a pattern is meant to join its base)
     * and reports the shortfall.
     *
     * A notice, not a failure: overlap is valid geometry and can be deliberate, so the
     * feature stays valid and the recompute is untouched. Deciding what to DO about it is
     * the MultiBody choice above, offered by the dialog and not decided here.
     *
     * Records the counts in InstancesRequested/InstancePieces as well as writing the
     * console line, so the answer can be read rather than only watched for. Called only on
     * the fusing path: copies that were deliberately kept apart did not collapse, so there
     * is nothing to report about them.
     */
    void reportCollapsedInstances(const std::vector<Part::TopoShape>& instances);

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
