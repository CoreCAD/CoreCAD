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

#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <Mod/Part/App/FCBRepAlgoAPI_Cut.h>
#include <Mod/Part/App/FCBRepAlgoAPI_Fuse.h>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Precision.hxx>
#include <TopExp_Explorer.hxx>


#include <algorithm>
#include <array>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Sequencer.h>
#include <Mod/Part/App/modelRefine.h>

#include <Mod/Part/App/SpatialInterference.h>

#include "FeatureTransformed.h"
#include "Body.h"
#include "FeatureAddSub.h"
#include "FeatureMirrored.h"
#include "FeatureLinearPattern.h"
#include "FeaturePolarPattern.h"
#include "FeatureSketchBased.h"
#include "Mod/Part/App/TopoShapeOpCode.h"


using namespace PartDesign;

namespace PartDesign
{
extern bool getPDRefineModelParameter();

PROPERTY_SOURCE(PartDesign::Transformed, PartDesign::FeatureAddSub)

std::array<char const*, 3> transformModeEnums = {"Features", "Whole shape", nullptr};

Transformed::Transformed()
{
    ADD_PROPERTY(Originals, (nullptr));
    Originals.setSize(0);

    ADD_PROPERTY(TransformMode, (static_cast<long>(Mode::Features)));
    TransformMode.setEnums(transformModeEnums.data());

    AddSubShape.setStatus(App::Property::Output, true);

    ADD_PROPERTY_TYPE(
        MultiBody,
        (false),
        "Base",
        App::Prop_None,
        "Cruth §5.5: emit the pattern's N copies as disconnected solids (one Body each) "
        "instead of fusing them"
    );
    ADD_PROPERTY_TYPE(
        SkipInstances,
        (),
        "Base",
        App::Prop_None,
        "Cruth §5.6 skip-list: original ordinals of instances broken out of the MultiBody output"
    );

    // Derived facts, not settings: read-only so nobody edits an answer, Output so recording
    // one does not re-touch the feature that just computed it, Transient because a recompute
    // is the only thing entitled to say what they are.
    constexpr auto derivedCount = static_cast<App::PropertyType>(
        App::Prop_ReadOnly | App::Prop_Output | App::Prop_Transient
    );
    ADD_PROPERTY_TYPE(
        InstancesRequested,
        (0),
        "Base",
        derivedCount,
        "Cruth P7 (#34): how many copies the pattern was asked for, last recompute"
    );
    ADD_PROPERTY_TYPE(
        InstancePieces,
        (0),
        "Base",
        derivedCount,
        "Cruth P7 (#34): how many connected pieces those copies formed among themselves"
    );
}

int Transformed::solidIndexOfInstance(long ordinal) const
{
    const std::vector<long>& skips = SkipInstances.getValues();
    if (ordinal < 0 || std::ranges::find(skips, ordinal) != skips.end()) {
        return 0;
    }
    const auto skippedBefore = std::ranges::count_if(skips, [ordinal](long s) { return s < ordinal; });
    const long index = ordinal - skippedBefore + 1;
    const auto solidCount = static_cast<long>(Shape.getShape().countSubShapes(TopAbs_SOLID));
    return index <= solidCount ? static_cast<int>(index) : 0;
}

int Transformed::solidIndexOfOriginal() const
{
    // Copies kept apart are emitted after the original.
    if (MultiBody.getValue()) {
        return solidIndexOfInstance(0);
    }
    const Part::TopoShape shape = Shape.getShape();
    const auto solidCount = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
    if (solidCount <= 1) {
        return solidCount;
    }
    // Fused copies: the original is the one solid still touching the shape the pattern began
    // with. A copy that touched it would have been fused into it.
    App::DocumentObject* base = getBaseObject(/*silent*/ true);
    const Part::TopoShape original = narrowToBaseInstance(base, Part::getShape(base), true);
    if (original.isNull()) {
        return 0;
    }
    int found = 0;
    for (int s = 1; s <= solidCount; ++s) {
        const Part::TopoShape solid = shape.getSubTopoShape(TopAbs_SOLID, s, /*silent*/ true);
        BRepExtrema_DistShapeShape distance(original.getShape(), solid.getShape());
        if (distance.IsDone() && distance.Value() <= Precision::Confusion()) {
            if (found) {
                return 0;
            }
            found = s;
        }
    }
    return found;
}

bool Transformed::leavesOriginalAsIs() const
{
    if (!originalAsIs) {
        originalAsIs = checkOriginalAsIs();
    }
    return *originalAsIs;
}

bool Transformed::checkOriginalAsIs() const
{
    if (AddSubShape.getShape().isNull()) {
        return false;
    }
    Part::TopoShape added;
    Part::TopoShape cut;
    splitAddSubHalves(AddSubShape.getShape(), added, cut);
    // Cut copies are cut from the original's own solid.
    if (!cut.isNull() || added.isNull()) {
        return false;
    }
    // Copies kept apart are held back from the fuse.
    if (MultiBody.getValue()) {
        return true;
    }
    // Fused, a copy joins the original only where it touches it. The untransformed copy is the
    // original itself, so one touching copy is that one.
    App::DocumentObject* base = getBaseObject(/*silent*/ true);
    const Part::TopoShape original = narrowToBaseInstance(base, Part::getShape(base), true);
    if (original.isNull()) {
        return false;
    }
    int touching = 0;
    for (TopExp_Explorer it(added.getShape(), TopAbs_SOLID); it.More(); it.Next()) {
        BRepExtrema_DistShapeShape distance(original.getShape(), it.Current());
        if (distance.IsDone() && distance.Value() <= Precision::Confusion() && ++touching > 1) {
            return false;
        }
    }
    return true;
}

long Transformed::ordinalOfComponent(const std::string& cid) const
{
    const Part::TopoShape shape = Shape.getShape();
    const auto solidCount = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
    for (long ordinal = 0, index = 1; index <= solidCount; ++ordinal) {
        if (std::ranges::find(SkipInstances.getValues(), ordinal) != SkipInstances.getValues().end()) {
            continue;
        }
        if (Body::componentIdOfSolid(shape, static_cast<int>(index)) == cid) {
            return ordinal;
        }
        ++index;
    }
    return -1;
}

App::DocumentObject* Transformed::getBaseObject(bool silent) const
{
    App::DocumentObject* rv = Feature::getBaseObject(/* silent = */ true);
    if (rv) {
        return rv;
    }

    const char* err = nullptr;
    const std::vector<App::DocumentObject*>& originals = Originals.getValues();
    // NOTE: may be here supposed to be last origin but in order to keep the old behaviour keep here
    // first
    App::DocumentObject* firstOriginal = originals.empty() ? nullptr : originals.front();
    if (firstOriginal) {
        rv = Part::hasShape(firstOriginal) ? firstOriginal : nullptr;
        if (!rv) {
            err = QT_TRANSLATE_NOOP(
                "Exception",
                "Transformation feature Linked object is not a Part object"
            );
        }
    }
    else {
        if (freecad_cast<const Mirrored*>(this)) {
            err = QT_TRANSLATE_NOOP("Exception", "No features selected to be mirrored.");
        }
        else if (freecad_cast<const LinearPattern*>(this) || freecad_cast<const PolarPattern*>(this)) {
            err = QT_TRANSLATE_NOOP("Exception", "No features selected to be patterned.");
        }
        else {
            err = QT_TRANSLATE_NOOP("Exception", "No features selected to be transformed.");
        }
    }

    if (!silent && err) {
        throw Base::RuntimeError(err);
    }

    return rv;
}

std::vector<App::DocumentObject*> Transformed::getOriginals() const
{
    auto const mode = static_cast<Mode>(TransformMode.getValue());

    if (mode == Mode::WholeShape) {
        return {};
    }

    std::vector<DocumentObject*> originals = Originals.getValues();

    const auto isSuppressed = [](const DocumentObject* obj) {
        auto feature = freecad_cast<Feature*>(obj);

        return feature != nullptr && feature->Suppressed.getValue();
    };

    // Remove suppressed features from the list so the transformations behave as if they are not
    // there
    auto [first, last] = std::ranges::remove_if(originals, isSuppressed);
    originals.erase(first, last);

    return originals;
}

Base::Vector3d Transformed::centreOfOriginals()
{
    std::vector<Part::TopoShape> pieces;
    if (static_cast<Mode>(TransformMode.getValue()) == Mode::WholeShape) {
        pieces.push_back(getBaseTopoShape());
    }
    else {
        for (auto* original : getOriginals()) {
            auto* feature = freecad_cast<PartDesign::FeatureAddSub*>(original);
            if (!feature) {
                continue;
            }
            Part::TopoShape added;
            Part::TopoShape cut;
            feature->getAddSubShape(added, cut);
            const gp_Trsf location = feature->getLocation().Transformation();
            for (Part::TopoShape* half : {&added, &cut}) {
                if (!half->isNull()) {
                    pieces.push_back(half->makeElementTransform(location));
                }
            }
        }
    }

    Base::Vector3d centre;
    if (pieces.empty() || !Part::TopoShape().makeElementCompound(pieces).getCenterOfGravity(centre)) {
        throw Base::ValueError("Cannot find the centre of the features to copy");
    }
    return centre;
}

App::DocumentObject* Transformed::getSketchObject() const
{
    std::vector<DocumentObject*> originals = getOriginals();
    DocumentObject const* firstOriginal = !originals.empty() ? originals.front() : nullptr;

    if (auto feature = freecad_cast<PartDesign::ProfileBased*>(firstOriginal)) {
        return feature->getVerifiedSketch(true);
    }
    if (freecad_cast<PartDesign::FeatureAddSub*>(firstOriginal)) {
        return nullptr;
    }
    if (auto pattern = freecad_cast<LinearPattern*>(this)) {
        return pattern->Direction.getValue();
    }
    if (auto pattern = freecad_cast<PolarPattern*>(this)) {
        return pattern->Axis.getValue();
    }
    if (auto pattern = freecad_cast<Mirrored*>(this)) {
        return pattern->MirrorPlane.getValue();
    }

    return nullptr;
}

void Transformed::Restore(Base::XMLReader& reader)
{
    PartDesign::Feature::Restore(reader);
}

void Transformed::handleChangedPropertyType(
    Base::XMLReader& reader,
    const char* TypeName,
    App::Property* prop
)
{
    // The property 'Angle' of PolarPattern has changed from PropertyFloat
    // to PropertyAngle and the property 'Length' has changed to PropertyLength.
    Base::Type inputType = Base::Type::fromName(TypeName);
    if (auto property = freecad_cast<App::PropertyFloat*>(prop);
        property != nullptr && inputType.isDerivedFrom(App::PropertyFloat::getClassTypeId())) {
        // Do not directly call the property's Restore method in case the implementation
        // has changed. So, create a temporary PropertyFloat object and assign the value.
        App::PropertyFloat floatProp;
        floatProp.Restore(reader);
        property->setValue(floatProp.getValue());
    }
    else {
        PartDesign::Feature::handleChangedPropertyType(reader, TypeName, prop);
    }
}

short Transformed::mustExecute() const
{
    if (Originals.isTouched() || TransformMode.isTouched() || MultiBody.isTouched()
        || SkipInstances.isTouched()) {
        return 1;
    }
    return FeatureAddSub::mustExecute();
}

void Transformed::getAddSubShape(Part::TopoShape& addShape, Part::TopoShape& subShape)
{
    if (AddSubShape.getShape().isNull()) {
        return;
    }
    splitAddSubHalves(AddSubShape.getShape(), addShape, subShape);
}

App::DocumentObjectExecReturn* Transformed::recomputePreview()
{
    const auto mode = static_cast<Mode>(TransformMode.getValue());

    App::DocumentObject* supportFeature = getBaseObject();
    const Part::TopoShape supportShape
        = narrowToBaseInstance(supportFeature, Part::getShape(supportFeature), true);

    if (supportShape.isNull()) {
        return App::DocumentObject::StdReturn;
    }

    gp_Trsf supportTransform = supportShape.getShape().Location().Transformation();

    const auto makeCompoundOfToolShapes = [this, &supportTransform]() {
        BRep_Builder builder;
        TopoDS_Compound compound;

        builder.MakeCompound(compound);
        for (const auto& original : getOriginals()) {
            if (auto* feature = freecad_cast<FeatureAddSub*>(original)) {
                auto shape = feature->AddSubShape.getShape();

                gp_Trsf trsf = feature->getLocation().Transformation().Multiplied(
                    supportTransform.Inverted()
                );

                if (shape.isNull()) {
                    continue;
                }

                shape = shape.makeElementTransform(trsf);

                builder.Add(compound, shape.getShape());
            }
        }

        return compound;
    };

    switch (mode) {
        case Mode::Features:
            PreviewShape.setValue(makeCompoundOfToolShapes());
            return StdReturn;

        case Mode::WholeShape: {
            auto shape = getBaseTopoShape();
            shape = shape.makeElementTransform(supportTransform.Inverted());

            PreviewShape.setValue(shape.getShape());

            return StdReturn;
        }

        default:
            return FeatureAddSub::recomputePreview();
    }
}

void Transformed::onChanged(const App::Property* prop)
{
    if (prop == &Shape || prop == &AddSubShape || prop == &BaseFeature) {
        originalAsIs.reset();
    }
    if (prop == &TransformMode) {
        auto const mode = static_cast<Mode>(TransformMode.getValue());
        Originals.setStatus(App::Property::Status::Hidden, mode == Mode::WholeShape);
    }

    FeatureAddSub::onChanged(prop);
}

App::DocumentObjectExecReturn* Transformed::execute()
{
    AddSubShape.setValue(Part::TopoShape());

    auto const mode = static_cast<Mode>(TransformMode.getValue());

    std::vector<DocumentObject*> originals = getOriginals();

    if (mode == Mode::Features && originals.empty()) {
        Shape.setValue(getBaseTopoShape(true));
        return App::DocumentObject::StdReturn;
    }

    // The counts answer for THIS recompute only; a stale pair would have the dialog offering
    // a choice about an overlap that is no longer there.
    InstancesRequested.setValue(0);
    InstancePieces.setValue(0);

    // get transformations from subclass by calling virtual method
    std::vector<gp_Trsf> transformations;
    try {
        std::list<gp_Trsf> t_list = getTransformations();
        transformations.insert(transformations.end(), t_list.begin(), t_list.end());
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    catch (const Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }

    if (transformations.empty()) {
        Shape.setValue(getBaseTopoShape(true));
        return App::DocumentObject::StdReturn;
    }

    // Get the support
    App::DocumentObject* supportFeature = nullptr;

    try {
        supportFeature = getBaseObject();
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    Part::TopoShape supportTopShape;
    try {
        supportTopShape = narrowToBaseInstance(supportFeature, Part::getShape(supportFeature), false);
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    if (supportTopShape.getShape().IsNull()) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Cannot transform invalid support shape")
        );
    }

    // create an untransformed copy of the support shape
    Part::TopoShape supportShape(supportTopShape);

    gp_Trsf trsfInv = supportShape.getShape().Location().Transformation().Inverted();

    supportShape.setTransform(Base::Matrix4D());

    auto getTransformedCompShape = [&](const auto& supportShape, const auto& origShape) {
        std::vector<TopoShape> shapes = {supportShape};
        TopoShape shape(origShape);
        int idx = 1;
        auto transformIter = transformations.cbegin();
        transformIter++;
        for (; transformIter != transformations.end(); transformIter++) {
            if (Base::Sequencer().wasCanceled()) {
                return std::vector<TopoShape>();
            }
            auto opName = Data::indexSuffix(idx++);
            shapes.emplace_back(shape.makeElementTransform(*transformIter, opName.c_str()));
        }
        return shapes;
    };

    // Cruth §5.5, the second merge decision: when the copies are to stay apart, the
    // transformed ones are held back from the fuse and emitted beside the support instead of
    // welded into it. Collected across every original, because instance k of the pattern is
    // copy k of each of them.
    const bool keepCopiesApart = MultiBody.getValue();
    std::vector<Part::TopoShape> looseCopies;

    // This pattern's own piece: every copy it added or cut, the untransformed ones included.
    std::vector<Part::TopoShape> addedCopies;
    std::vector<Part::TopoShape> cutCopies;
    const auto recordPiece = [&]() {
        const auto compoundOf = [](const std::vector<Part::TopoShape>& copies) {
            return copies.empty() ? Part::TopoShape() : Part::TopoShape().makeElementCompound(copies);
        };
        AddSubShape.setValue(makeAddSubHalves(compoundOf(addedCopies), compoundOf(cutCopies)));
    };

    switch (mode) {
        case Mode::Features:
            // NOTE: It would be possible to build a compound from all original addShapes/subShapes
            // and then transform the compounds as a whole. But we choose to apply the
            // transformations to each Original separately. This way it is easier to discover what
            // feature causes a fuse/cut to fail. The downside is that performance suffers when
            // there are many originals. But it seems safe to assume that in most cases there are
            // few originals and many transformations
            for (auto original : originals) {
                // Extract the original shape and determine whether to cut or to fuse
                Part::TopoShape fuseShape;
                Part::TopoShape cutShape;

                auto feature = freecad_cast<PartDesign::FeatureAddSub*>(original);
                if (!feature) {
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                        "Exception",
                        "Only additive and subtractive features can be transformed"
                    ));
                }

                feature->getAddSubShape(fuseShape, cutShape);
                if (fuseShape.isNull() && cutShape.isNull()) {
                    return new App::DocumentObjectExecReturn(
                        QT_TRANSLATE_NOOP("Exception", "Shape of additive/subtractive feature is empty")
                    );
                }
                gp_Trsf trsf = feature->getLocation().Transformation().Multiplied(trsfInv);
                if (!fuseShape.isNull()) {
                    fuseShape = fuseShape.makeElementTransform(trsf);
                }
                if (!cutShape.isNull()) {
                    cutShape = cutShape.makeElementTransform(trsf);
                }
                if (!fuseShape.isNull()) {
                    auto shapes = getTransformedCompShape(supportShape, fuseShape);
                    if (Base::Sequencer().wasCanceled()) {
                        return new App::DocumentObjectExecReturn("User aborted");
                    }
                    addedCopies.push_back(fuseShape);
                    if (keepCopiesApart) {
                        // Hold the transformed copies back. shapes[0] is the support and is
                        // not a copy: the untransformed original was spliced into the chain
                        // before the pattern existed and cannot be peeled back out, so it
                        // stays where it is and the rest come out beside it.
                        //
                        // §5.6: an instance broken out of the pattern is dropped by its
                        // ordinal in the transform sequence. The ordinal spans originals --
                        // instance k of the pattern is copy k of every original -- so the
                        // skip applies inside this loop and drops the whole instance.
                        const std::vector<long>& skip = SkipInstances.getValues();
                        for (std::size_t i = 1; i < shapes.size(); ++i) {
                            if (std::ranges::find(skip, static_cast<long>(i)) == skip.end()) {
                                looseCopies.push_back(shapes[i]);
                                addedCopies.push_back(shapes[i]);
                            }
                        }
                    }
                    else {
                        // Cruth §8.6/P7 (#34): ask, before the fuse hides the answer, whether
                        // the copies ran into each other. shapes[0] is the support; the
                        // instances are the untransformed original plus every copy after it.
                        std::vector<Part::TopoShape> instances {fuseShape};
                        instances.insert(instances.end(), shapes.begin() + 1, shapes.end());
                        reportCollapsedInstances(instances);

                        addedCopies.insert(addedCopies.end(), shapes.begin() + 1, shapes.end());
                        supportShape.makeElementFuse(shapes);
                    }
                }
                if (!cutShape.isNull()) {
                    auto shapes = getTransformedCompShape(supportShape, cutShape);
                    if (Base::Sequencer().wasCanceled()) {
                        return new App::DocumentObjectExecReturn("User aborted");
                    }
                    cutCopies.push_back(cutShape);
                    cutCopies.insert(cutCopies.end(), shapes.begin() + 1, shapes.end());
                    supportShape.makeElementCut(shapes);
                }
            }
            break;
        case Mode::WholeShape: {
            auto shapes = getTransformedCompShape(supportShape, supportShape);
            if (Base::Sequencer().wasCanceled()) {
                return new App::DocumentObjectExecReturn("User aborted");
            }
            if (MultiBody.getValue()) {
                // Cruth §5.5/§5.6: emit the copies as disconnected solids (one Body each via
                // the multi-output reconciler), omitting any instance the break-out skip-list
                // records. Bypass the single-solid fuse/rule.
                //
                // The skip-list keys on each instance's ORIGINAL ordinal — its index in the
                // transform sequence `shapes` (ARCHITECTURE §5.6/§11.2). That ordinal is stable
                // across recompute and independent of other skips, so the drop always matches.
                // (An earlier design keyed the skip on the element-map component-id; that id is
                // context-dependent — its map name shifts with the surrounding compound — so the
                // recorded key silently failed to match here and the instance was never dropped.)
                const std::vector<long>& skip = SkipInstances.getValues();
                std::vector<Part::TopoShape> kept;
                for (std::size_t i = 0; i < shapes.size(); ++i) {
                    if (std::ranges::find(skip, static_cast<long>(i)) == skip.end()) {
                        kept.push_back(shapes[i]);
                    }
                }
                if (kept.empty()) {
                    return new App::DocumentObjectExecReturn(
                        QT_TRANSLATE_NOOP("Exception", "Pattern skip-list removed every instance")
                    );
                }
                addedCopies = kept;
                recordPiece();
                Part::TopoShape compound;
                compound.makeElementCompound(kept);
                this->Shape.setValue(compound);
                return App::DocumentObject::StdReturn;
            }
            addedCopies = shapes;
            reportCollapsedInstances(shapes);
            supportShape.makeElementFuse(shapes);
            break;
        }
    }

    recordPiece();

    if (!looseCopies.empty()) {
        // The copies were kept apart, so the result is several solids: the support, and one
        // per surviving copy. The multi-output reconciler turns that into one Body each
        // (§4.6/§5.5). No refine pass -- refining fuses coplanar faces across the compound
        // and would undo the separation just asked for, which is why the Whole shape path
        // returns early for the same reason.
        std::vector<Part::TopoShape> pieces {supportShape};
        pieces.insert(pieces.end(), looseCopies.begin(), looseCopies.end());
        Part::TopoShape compound;
        compound.makeElementCompound(pieces);
        this->Shape.setValue(compound);
        return App::DocumentObject::StdReturn;
    }

    supportShape = refineShapeIfActive((supportShape));

    this->Shape.setValue(supportShape);

    return App::DocumentObject::StdReturn;
}

void Transformed::reportCollapsedInstances(const std::vector<Part::TopoShape>& instances)
{
    const std::size_t requested = instances.size();
    if (requested < 2) {
        return;
    }

    const std::size_t produced = Part::connectedComponentCount(instances);

    // Record before deciding whether to warn: "they all stayed apart" is an answer the dialog
    // needs as much as "they collapsed", and a pair left at zero reads as "never asked". With
    // several originals the worst run wins -- that is the one the user has to hear about.
    const long shortfall = static_cast<long>(requested) - static_cast<long>(produced);
    const long recorded = InstancesRequested.getValue() - InstancePieces.getValue();
    if (InstancesRequested.getValue() == 0 || shortfall > recorded) {
        InstancesRequested.setValue(static_cast<long>(requested));
        InstancePieces.setValue(static_cast<long>(produced));
    }

    if (produced >= requested) {
        return;
    }

    // Local-first and concrete per §3.6: name this feature, state both numbers, and say what
    // happened to the difference. No advice — what to do about it is the user's call.
    Base::Console().warning(
        "%s: %zu instances were requested but they form %zu connected %s. The copies overlap "
        "or touch and were fused.\n",
        Label.getValue(),
        requested,
        produced,
        produced == 1 ? "piece" : "pieces"
    );
}

}  // namespace PartDesign
