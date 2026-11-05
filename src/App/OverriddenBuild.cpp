// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>
#include <memory>
#include <set>

#include <Base/Console.h>
#include <Base/Exception.h>

#include "DocumentObject.h"
#include "OverriddenBuild.h"
#include "Property.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace
{

void throwIfFailed(DocumentObject* step, DocumentObjectExecReturn* ret)
{
    if (ret == DocumentObject::StdReturn) {
        return;
    }
    const std::unique_ptr<DocumentObjectExecReturn> failed(ret);
    throw Base::RuntimeError(std::string(step->Label.getValue()) + ": " + failed->Why);
}

/// @p within in build order: every step after the steps it is built from.
std::vector<DocumentObject*> inBuildOrder(const std::set<DocumentObject*>& within)
{
    std::vector<DocumentObject*> order;
    std::set<DocumentObject*> placed;
    const auto place = [&](const auto& self, DocumentObject* step) -> void {
        if (!placed.insert(step).second) {
            return;
        }
        for (DocumentObject* input : step->getOutList()) {
            if (within.contains(input)) {
                self(self, input);
            }
        }
        order.push_back(step);
    };
    for (DocumentObject* step : within) {
        place(place, step);
    }
    return order;
}

}  // namespace

OverriddenBuild::OverriddenBuild(DocumentObject* result, std::vector<ObjectIdentifier> targets)
    : result(result)
    , targets(std::move(targets))
{
    std::set<DocumentObject*> builtFrom;
    for (DocumentObject* input : result->getOutListRecursive()) {
        builtFrom.insert(input);
    }
    builtFrom.insert(result);

    std::set<DocumentObject*> toRebuild;
    for (const ObjectIdentifier& target : this->targets) {
        DocumentObject* owner = target.getDocumentObject();
        Property* prop = target.getProperty();
        if (!owner || !prop || !builtFrom.contains(owner)) {
            throw Base::ValueError(
                target.toString() + " is not used to build " + result->Label.getValue()
            );
        }
        asStated.push_back(prop->getPathValue(target));
        toRebuild.insert(owner);
        for (DocumentObject* reader : owner->getInListRecursive()) {
            if (builtFrom.contains(reader)) {
                toRebuild.insert(reader);
            }
        }
    }
    steps = inBuildOrder(toRebuild);
    for (DocumentObject* step : steps) {
        if (step->isTouched()) {
            alreadyTouched.insert(step);
        }
    }
}

OverriddenBuild::~OverriddenBuild()
{
    if (!overridden) {
        return;
    }
    try {
        rebuild(asStated);
    }
    catch (const Base::Exception& e) {
        FC_ERR("Could not rebuild " << result->getFullName() << " as stated: " << e.what());
    }
    for (DocumentObject* step : steps) {
        if (!alreadyTouched.contains(step)) {
            step->purgeTouched();
        }
    }
}

void OverriddenBuild::build(const std::vector<App::any>& values)
{
    if (values.size() != targets.size()) {
        throw Base::ValueError("One value is needed for each replaced value");
    }
    overridden = true;
    rebuild(values);
}

void OverriddenBuild::rebuild(const std::vector<App::any>& values)
{
    for (DocumentObject* step : steps) {
        throwIfFailed(step, step->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteNonOutput));
        // After the step's own formulas, so a formula on a replaced value does not undo it.
        for (std::size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].getDocumentObject() == step) {
                targets[i].getProperty()->setPathValue(targets[i], values[i]);
            }
        }
        throwIfFailed(step, step->recompute());
        throwIfFailed(step, step->ExpressionEngine.execute(PropertyExpressionEngine::ExecuteOutput));
    }
}
