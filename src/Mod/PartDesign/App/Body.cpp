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


#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Datums.h>
#include <App/Document.h>
#include <App/MappedName.h>
#include <App/Origin.h>
#include <App/PropertyLinks.h>
#include <App/VarSet.h>
#include <Base/Color.h>
#include <Base/Uuid.h>

#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>

#include "Body.h"
#include "BodyChain.h"
#include "BodyPy.h"
#include "FeatureBase.h"
#include "FeatureTransformed.h"

using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Body, Part::BodyBase)

namespace
{
// Blue, orange, green, purple, teal, magenta, gold, slate.
constexpr std::array<std::array<float, 3>, 8> bodyPalette = {{
    {0.30F, 0.55F, 0.90F},
    {0.95F, 0.60F, 0.20F},
    {0.40F, 0.75F, 0.40F},
    {0.65F, 0.45F, 0.85F},
    {0.25F, 0.70F, 0.70F},
    {0.90F, 0.40F, 0.70F},
    {0.85F, 0.75F, 0.25F},
    {0.50F, 0.55F, 0.65F},
}};

Base::Color paletteColorFor(std::size_t index)
{
    const auto& rgb = bodyPalette[index % bodyPalette.size()];
    return Base::Color(rgb[0], rgb[1], rgb[2], 1.0F);
}

// Counted by colour, not by number of Bodies: Bodies retire and respawn, so a count repeats.
Base::Color leastUsedPaletteColor(const App::Document* doc, const PartDesign::Body* self)
{
    std::array<int, bodyPalette.size()> uses {};
    for (auto* other : doc->getObjectsOfType<PartDesign::Body>()) {
        if (other == self) {
            continue;
        }
        const Base::Color color = other->Color.getValue();
        for (std::size_t i = 0; i < bodyPalette.size(); ++i) {
            if (color == paletteColorFor(i)) {
                ++uses[i];
                break;
            }
        }
    }
    const auto least = std::ranges::min_element(uses);
    return paletteColorFor(static_cast<std::size_t>(least - uses.begin()));
}

// Points every link from @p obj to an origin plane, axis or point at the same one on @p origin.
void relinkToOrigin(App::DocumentObject* obj, App::Origin* origin)
{
    if (!origin) {
        return;
    }
    const auto onOrigin = [origin](App::DocumentObject* linked) -> App::DocumentObject* {
        auto* datum = dynamic_cast<App::DatumElement*>(linked);
        return datum && datum->isOriginFeature() ? origin->getDatumElement(datum->Role.getValue())
                                                 : linked;
    };

    std::vector<App::Property*> props;
    obj->getPropertyList(props);
    for (App::Property* prop : props) {
        if (auto* link = dynamic_cast<App::PropertyLink*>(prop)) {
            if (auto* to = onOrigin(link->getValue()); to != link->getValue()) {
                link->setValue(to);
            }
        }
        else if (auto* list = dynamic_cast<App::PropertyLinkList*>(prop)) {
            auto values = list->getValues();
            std::ranges::transform(values, values.begin(), onOrigin);
            if (values != list->getValues()) {
                list->setValues(values);
            }
        }
        else if (auto* sub = dynamic_cast<App::PropertyLinkSub*>(prop)) {
            if (auto* to = onOrigin(sub->getValue()); to != sub->getValue()) {
                sub->setValue(to, sub->getSubValues());
            }
        }
        else if (auto* subList = dynamic_cast<App::PropertyLinkSubList*>(prop)) {
            auto values = subList->getSubListValues();
            bool changed = false;
            for (auto& [linked, subs] : values) {
                auto* to = onOrigin(linked);
                changed = changed || to != linked;
                linked = to;
            }
            if (changed) {
                subList->setSubListValues(values);
            }
        }
    }
}

Part::TopoShape solidWithComponentKey(
    const App::DocumentObject* tip,
    const Part::TopoShape& shape,
    const std::string& key
)
{
    const auto count = static_cast<int>(shape.countSubShapes(TopAbs_SOLID));
    for (int i = 1; i <= count; ++i) {
        if (Body::componentKeyOfSolid(tip, shape, i) == key) {
            return shape.getSubTopoShape(TopAbs_SOLID, i, /*silent*/ true);
        }
    }
    return {};
}

// Builds @p feature on copy @p copy of @p base; the steps that were there now build on it.
// Returns how many moved.
std::size_t spliceAfter(PartDesign::Feature* feature, App::DocumentObject* base, long copy)
{
    auto steps = chain::nextSteps(base, copy);
    std::erase(steps, feature);
    feature->BaseFeature.setValue(base);
    feature->BaseInstance.setValue(copy);
    for (auto* step : steps) {
        step->BaseFeature.setValue(feature);
        step->BaseInstance.setValue(chain::WholeOutput);
    }
    return steps.size();
}

// Steps on the whole output move to the feature's base. A step on one copy stays: the copy
// goes with the feature, so that step fails and says why.
void unsplice(App::DocumentObject* feature)
{
    auto* pd = freecad_cast<PartDesign::Feature*>(feature);
    App::DocumentObject* base = pd ? pd->BaseFeature.getValue() : nullptr;
    const long copy = pd ? pd->BaseInstance.getValue() : chain::WholeOutput;
    for (auto* step : chain::nextSteps(feature, chain::WholeOutput)) {
        step->BaseInstance.setValue(copy);
        step->BaseFeature.setValue(base);
        step->onBaseFeatureRerouted(feature, base);
    }
}
}  // namespace

Body::Body()
{
    ADD_PROPERTY_TYPE(Color, (paletteColorFor(0)), "Base", App::Prop_None, "Body identity colour");
    ADD_PROPERTY_TYPE(
        TipComponentId,
        (""),
        "Base",
        App::Prop_None,
        "The solid of the Tip this Body stands for; empty for the whole Tip"
    );
    // Restored over on load, so a Body keeps its identity for life.
    Base::Uuid bodyId;
    ADD_PROPERTY_TYPE(Uid, (bodyId), "Base", App::Prop_ReadOnly, "Durable body identity");
    ADD_PROPERTY_TYPE(
        AcknowledgedOverlaps,
        (),
        "Base",
        static_cast<App::PropertyType>(App::Prop_Hidden | App::Prop_NoRecompute),
        "Uids of Bodies whose overlap with this one the user acknowledged"
    );

    // Derived from the Tip on every recompute and on load, never authored or saved.
    Shape.setStatus(App::Property::Transient, true);
    Shape.setStatus(App::Property::ReadOnly, true);
}

short Body::mustExecute() const
{
    if (Tip.isTouched()) {
        return 1;
    }
    return Part::BodyBase::mustExecute();
}

App::DocumentObject* Body::getPrevSolidFeature(App::DocumentObject* start)
{
    if (!start) {
        start = Tip.getValue();
    }
    if (!start) {
        return nullptr;
    }
    std::set<App::DocumentObject*> seen {start};
    for (auto* pd = freecad_cast<PartDesign::Feature*>(start); pd;) {
        App::DocumentObject* prev = pd->BaseFeature.getValue();
        if (!prev || !seen.insert(prev).second) {
            return nullptr;
        }
        if (isSolidFeature(prev)) {
            return prev;
        }
        pd = freecad_cast<PartDesign::Feature*>(prev);
    }
    return nullptr;
}

bool Body::isSolidFeature(const App::DocumentObject* obj)
{
    if (!obj) {
        return false;
    }
    if (obj->isDerivedFrom<PartDesign::Feature>()) {
        if (PartDesign::Feature::isDatum(obj)) {
            return false;
        }
        if (auto* pattern = freecad_cast<const PartDesign::Transformed*>(obj)) {
            return !pattern->isMultiTransformChild();
        }
        return true;
    }
    // An import: it stands as a part of its own and can only start a chain.
    if (const auto* shapeFeature = freecad_cast<const Part::ShapeFeature*>(obj)) {
        return shapeFeature->spawnsBodyForOutput();
    }
    return false;
}

bool Body::isAllowed(const App::DocumentObject* obj)
{
    if (!obj) {
        return false;
    }
    return isSolidFeature(obj) || obj->isDerivedFrom<PartDesign::Feature>()
        || obj->isDerivedFrom<Part::Part2DObject>() || obj->isDerivedFrom<App::DatumElement>()
        || obj->isDerivedFrom<App::LocalCoordinateSystem>() || obj->isDerivedFrom<App::VarSet>();
}

std::vector<App::DocumentObject*> Body::addFeature(App::DocumentObject* feature)
{
    if (!isAllowed(feature)) {
        throw Base::ValueError("Body: object is not allowed");
    }
    relinkToOrigin(feature, getOrigin());
    const long copy = tipCopy();

    if (isSolidFeature(feature) && !feature->isDerivedFrom<PartDesign::Feature>()) {
        // It has no base to point at the old Tip, so splicing it in would drop the chain.
        if (Tip.getValue()) {
            throw Base::ValueError(
                "Body: this feature has no base of its own, so it can only start a body's chain"
            );
        }
        Tip.setValue(feature);
    }
    else if (isSolidFeature(feature)) {
        appendAtTip(static_cast<PartDesign::Feature*>(feature), copy);
    }
    return {feature};
}

void Body::takeOffChain(PartDesign::Feature* feature)
{
    App::DocumentObject* base = feature->BaseFeature.getValue();
    unsplice(feature);
    retreatTippedBodies(feature, base);
    feature->BaseFeature.setValue(nullptr);
    feature->BaseInstance.setValue(chain::WholeOutput);
}

long Body::tipCopy() const
{
    auto* pattern = freecad_cast<PartDesign::Transformed*>(Tip.getValue());
    const std::string cid = TipComponentId.getStrValue();
    return pattern && !cid.empty() ? pattern->ordinalOfComponent(cid) : chain::WholeOutput;
}

void Body::appendAtTip(PartDesign::Feature* feature, long copy)
{
    App::DocumentObject* prevTip = Tip.getValue();
    spliceAfter(feature, prevTip, copy);
    if (copy >= 0) {
        TipComponentId.setValue("");  // the new Tip's whole output is that one copy
    }
    Tip.setValue(feature);
    if (prevTip && prevTip->isDerivedFrom<PartDesign::Feature>() && prevTip->Visibility.getValue()) {
        prevTip->Visibility.setValue(false);
    }
}

std::vector<App::DocumentObject*> Body::addFeatures(std::vector<App::DocumentObject*> features)
{
    for (auto* feature : features) {
        addFeature(feature);
    }
    return features;
}

void Body::insertObject(App::DocumentObject* feature, App::DocumentObject* target, bool after)
{
    if (target && (!target->isDerivedFrom<PartDesign::Feature>() || !backsBody(target, this))) {
        throw Base::ValueError(
            "Body: the feature we should insert relative to is not part of that body"
        );
    }
    relinkToOrigin(feature, getOrigin());
    if (!isSolidFeature(feature)) {
        return;
    }

    auto* pd = static_cast<PartDesign::Feature*>(feature);
    auto* targetPd = freecad_cast<PartDesign::Feature*>(target);
    if (target && after) {
        if (spliceAfter(pd, target, chain::WholeOutput) == 0) {
            Tip.setValue(feature);
        }
    }
    else if (targetPd && targetPd->BaseFeature.getValue()) {
        // On the target's own base and copy, so the target builds on it.
        spliceAfter(pd, targetPd->BaseFeature.getValue(), targetPd->BaseInstance.getValue());
    }
    else if (target || after) {
        // At the start: the feature becomes the root the old root builds on.
        App::DocumentObject* root = target ? target : Tip.getValue();
        std::set<App::DocumentObject*> seen;
        for (auto* cur = freecad_cast<PartDesign::Feature*>(root);
             cur && cur->BaseFeature.getValue() && seen.insert(cur).second;
             cur = freecad_cast<PartDesign::Feature*>(root)) {
            root = cur->BaseFeature.getValue();
        }
        pd->BaseFeature.setValue(nullptr);
        pd->BaseInstance.setValue(chain::WholeOutput);
        if (auto* rootPd = freecad_cast<PartDesign::Feature*>(root)) {
            rootPd->BaseFeature.setValue(feature);
        }
        else {
            Tip.setValue(feature);
        }
    }
    else {
        appendAtTip(pd, tipCopy());
    }
}

// Call before the feature leaves the document.
std::vector<App::DocumentObject*> Body::removeFeature(App::DocumentObject* feature)
{
    auto* pd = freecad_cast<PartDesign::Feature*>(feature);
    const auto steps = chain::nextSteps(feature, chain::WholeOutput);
    App::DocumentObject* base = pd ? pd->BaseFeature.getValue() : nullptr;
    App::DocumentObject* const retreatTo = base ? base : (steps.empty() ? nullptr : steps.front());
    unsplice(feature);

    retreatTippedBodies(feature, retreatTo);

    // Retire an emptied Body. removeObject can destroy `this`, so it must be the last action.
    App::Document* doc = getDocument();
    const char* name = getNameInDocument();
    if (!Tip.getValue() && doc && name) {
        const std::string bodyName = name;
        doc->removeObject(bodyName.c_str());
    }
    return {feature};
}

void Body::removeFeatures(const std::vector<App::DocumentObject*>& features)
{
    for (auto* feature : features) {
        removeFeature(feature);
    }
}

App::DocumentObjectExecReturn* Body::execute()
{
    Part::BodyBase::execute();

    App::DocumentObject* tip = Tip.getValue();
    if (!tip) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
            "Exception",
            "A Body must have at least one feature; empty bodies are not allowed"
        ));
    }
    if (!isSolidFeature(tip)) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Linked object is not a solid feature")
        );
    }
    if (static_cast<Part::ShapeFeature*>(tip)->Shape.getShape().isNull()) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Tip shape is empty"));
    }
    // Empty here means this Body's copy is gone: the reconciler retires it after this
    // recompute, so keep the last good shape rather than report an error.
    Part::TopoShape shape = derivedTipShape();
    if (!shape.isNull()) {
        Shape.setValue(shape);
    }
    return App::DocumentObject::StdReturn;
}

void Body::onChanged(const App::Property* prop)
{
    const bool userEdit = !isRestoring() && getDocument()
        && !getDocument()->isPerformingTransaction();
    if (userEdit && prop == &BaseFeature) {
        // The chain starts with a FeatureBase that carries this Body's BaseFeature.
        const auto solids = ownSolids();
        App::DocumentObject* first = solids.empty()
                || !solids.front()->isDerivedFrom<PartDesign::Feature>()
            ? nullptr
            : solids.front();

        FeatureBase* featureBase = nullptr;
        if (BaseFeature.getValue()) {
            featureBase = freecad_cast<FeatureBase*>(first);
            if (!featureBase) {
                featureBase = getDocument()->addObject<FeatureBase>("BaseFeature");
                insertObject(featureBase, first, false);
                if (!Tip.getValue()) {
                    Tip.setValue(featureBase);
                }
            }
        }
        if (featureBase && featureBase->BaseFeature.getValue() != BaseFeature.getValue()) {
            featureBase->BaseFeature.setValue(BaseFeature.getValue());
        }
    }
    Part::BodyBase::onChanged(prop);
}

App::Origin* Body::findDocumentOrigin(App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }
    const auto origins = doc->getObjectsOfType<App::Origin>();
    return origins.empty() ? nullptr : origins.front();
}

App::Origin* Body::requireDocumentOrigin(App::Document* doc)
{
    if (App::Origin* origin = findDocumentOrigin(doc)) {
        return origin;
    }
    // The document owns the world frame; a Body must never create one.
    throw Base::RuntimeError(
        "PartDesign Body requires a document-level world frame (App::Origin), but the "
        "document has none. Create the Body in a Part document "
        "(App.newDocument(type='Part')); a Body must not create the coordinate frame."
    );
}

void Body::setupObject()
{
    Part::BodyBase::setupObject();
    getOrigin();  // refuse a document without a world frame
    if (auto* doc = getDocument()) {
        Color.setValue(leastUsedPaletteColor(doc, this));
    }
}

PyObject* Body::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        PythonObject = Py::Object(new BodyPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

std::vector<std::string> Body::getSubObjects(int reason) const
{
    if (reason == GS_SELECT && !showTip) {
        return Part::BodyBase::getSubObjects(reason);
    }
    return {};
}

PartDesign::Feature* Body::findOwnedFeature(const std::string& name) const
{
    App::Document* doc = getDocument();
    if (!doc || name.empty()) {
        return nullptr;
    }
    const bool byLabel = name[0] == '$';
    const std::string key = byLabel ? name.substr(1) : name;
    for (auto* feat : doc->getObjectsOfType<PartDesign::Feature>()) {
        const char* fname = feat->getNameInDocument();
        if ((byLabel ? (key == feat->Label.getStrValue()) : (fname && key == fname))
            && backsBody(feat, this)) {
            return feat;
        }
    }
    return nullptr;
}

Part::TopoShape Body::derivedTipShape() const
{
    App::DocumentObject* tip = Tip.getValue();
    if (!tip || !isSolidFeature(tip)) {
        return {};
    }
    Part::TopoShape tipShape = static_cast<Part::ShapeFeature*>(tip)->Shape.getShape();
    if (tipShape.isNull()) {
        return {};
    }
    const std::string cid = TipComponentId.getStrValue();
    if (!cid.empty()) {
        Part::TopoShape solid = solidWithComponentKey(tip, tipShape, cid);
        if (solid.isNull()) {
            return {};
        }
        // A pattern keeps each copy's offset in its placement; bake it into the geometry.
        solid.transformShape(Base::Matrix4D(), true);
        tipShape = solid;
    }
    tipShape.transformShape(tipShape.getTransform(), true);
    return tipShape;
}

std::string Body::tipSubElement(const char* bodySub) const
{
    App::DocumentObject* tip = Tip.getValue();
    if (!tip || !isSolidFeature(tip) || !bodySub || !*bodySub) {
        return {};
    }
    const Part::TopoShape tipShape = static_cast<Part::ShapeFeature*>(tip)->Shape.getShape();
    if (tipShape.isNull()) {
        return {};
    }
    const std::string cid = TipComponentId.getStrValue();
    if (cid.empty()) {
        return tipShape.findShape(bodySub).IsNull() ? std::string() : std::string(bodySub);
    }
    // The copy's solid is placed but not renumbered, so find the picked element on it and read
    // back its number in the whole shape.
    const Part::TopoShape solid = solidWithComponentKey(tip, tipShape, cid);
    if (solid.isNull()) {
        return {};
    }
    const TopoDS_Shape element = solid.findShape(bodySub);
    if (element.IsNull()) {
        return {};
    }
    const int index = tipShape.findShape(element);
    if (index <= 0) {
        return {};
    }
    return Part::TopoShape::shapeName(element.ShapeType()) + std::to_string(index);
}

App::DocumentObject* Body::getSubObject(
    const char* subname,
    PyObject** pyObj,
    Base::Matrix4D* pmat,
    bool transform,
    int depth
) const
{
    while (subname && *subname == '.') {
        ++subname;
    }
    // "Pad.Edge3" resolves through the feature that makes the edge, and "Origin.XY_Plane"
    // through the document's origin: neither is stored in the Body.
    if (subname && *subname && !Data::isMappedElement(subname)) {
        if (const char* dot = std::strchr(subname, '.')) {
            const std::string first(subname, dot);
            if (auto* feat = findOwnedFeature(first)) {
                return feat->getSubObject(dot + 1, pyObj, pmat, transform, depth + 1);
            }
            if (App::Origin* origin = findDocumentOrigin(getDocument())) {
                const char* originName = origin->getNameInDocument();
                if (originName && first == originName) {
                    return origin->getSubObject(dot + 1, pyObj, pmat, transform, depth + 1);
                }
            }
        }
    }
    return App::DocumentObject::getSubObject(subname, pyObj, pmat, transform, depth);
}

void Body::onDocumentRestored()
{
    // The Tip's shape is already loaded, so the unsaved Shape can be filled without a recompute.
    Part::TopoShape restoredShape = derivedTipShape();
    if (!restoredShape.isNull()) {
        Shape.setValue(restoredShape);
    }
    // Lets the view provider copy the Tip's colours.
    if (Tip.getValue()) {
        Tip.touch();
    }
    DocumentObject::onDocumentRestored();
}

bool Body::isSolid()
{
    return std::ranges::any_of(getFullModel(), &Body::isSolidFeature);
}
