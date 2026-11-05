// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>
#include <limits>

#include <App/Document.h>
#include <App/DocumentObject.h>

#include "TreeHistory.h"

namespace Gui::TreeHistory
{

std::map<const App::DocumentObject*, int> historyPositions(const App::Document* doc)
{
    std::map<const App::DocumentObject*, int> position;
    int index = 0;
    for (auto* obj : doc->getObjects()) {
        position[obj] = index++;
    }
    return position;
}

int positionIn(const std::map<const App::DocumentObject*, int>& position, const App::DocumentObject* obj)
{
    auto found = position.find(obj);
    return found == position.end() ? std::numeric_limits<int>::max() : found->second;
}

std::vector<App::DocumentObject*> placeDropped(
    const std::vector<App::DocumentObject*>& objects,
    const std::vector<App::DocumentObject*>& dragged,
    const App::DocumentObject* underMouse,
    bool inBottomHalf
)
{
    std::vector<App::DocumentObject*> placed;
    for (auto* obj : objects) {
        if (obj == underMouse) {
            if (inBottomHalf) {
                placed.push_back(obj);
            }
            placed.insert(placed.end(), dragged.begin(), dragged.end());
            if (!inBottomHalf) {
                placed.push_back(obj);
            }
        }
        else if (std::ranges::find(dragged, obj) == dragged.end()) {
            placed.push_back(obj);
        }
    }
    return placed;
}

bool keepsInputsFirst(const std::vector<App::DocumentObject*>& order)
{
    std::map<const App::DocumentObject*, std::size_t> position;
    for (std::size_t i = 0; i < order.size(); ++i) {
        position[order[i]] = i;
    }
    bool stepSeen = false;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (!order[i]->isTimelineStep()) {
            if (stepSeen && order[i]->isRollbackStop()) {
                return false;
            }
            continue;
        }
        stepSeen = true;
        for (auto* input : order[i]->getOutList()) {
            auto found = position.find(input);
            if (found != position.end() && input->isTimelineStep() && found->second > i) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace Gui::TreeHistory
