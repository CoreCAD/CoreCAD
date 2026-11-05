// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// Internal to PartDesign: reading a Body chain forward.

#ifndef PARTDESIGN_BODYCHAIN_H
#define PARTDESIGN_BODYCHAIN_H

#include <vector>

#include <App/Document.h>

#include "Feature.h"

namespace PartDesign::chain
{

constexpr long WholeOutput = -1;
constexpr long AnyCopy = -2;

// The steps built on copy `copy` of `base`. A feature has one next step per pattern copy at most,
// so callers must not assume there is only one.
inline std::vector<Feature*> nextSteps(const App::DocumentObject* base, long copy = AnyCopy)
{
    std::vector<Feature*> steps;
    if (!base || !base->getDocument()) {
        return steps;
    }
    for (auto* step : base->getDocument()->getObjectsOfType<Feature>()) {
        if (step->BaseFeature.getValue() == base
            && (copy == AnyCopy || step->BaseInstance.getValue() == copy)) {
            steps.push_back(step);
        }
    }
    return steps;
}

}  // namespace PartDesign::chain

#endif  // PARTDESIGN_BODYCHAIN_H
