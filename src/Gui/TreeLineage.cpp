// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>
#include <optional>
#include <vector>

#include <App/Datums.h>
#include <App/DocumentObject.h>
#include <App/Origin.h>

#include "TreeLineage.h"

using namespace Gui;

TreeLineage TreeLineage::trace(
    std::set<App::DocumentObject*> selected,
    const ColumnOf& lookUpColumn,
    const QColor& highlight
)
{
    // The document's origin is left out: every step comes from it, so tinting it says nothing.
    // A datum the user made is a real input and stays.
    auto isOrigin = [](App::DocumentObject* obj) {
        if (obj->isDerivedFrom<App::Origin>()) {
            return true;
        }
        auto datum = freecad_cast<App::DatumElement*>(obj);
        return datum && datum->isOriginFeature();
    };
    // A selection asks what made it, back from where it stands, never forward: for a step, its
    // inputs; for a body (its swatch was clicked), the row its swatch stands on and that row's
    // inputs. Their sketches come too, and so does the step of another body it was built on,
    // in this body's colour, but not what went into that step: that is another body's lineage.
    //
    // A row's bodies take real work to find, so each row is asked once per selection.
    std::map<App::DocumentObject*, ViewProvider::TreeBodyColumn> columns;
    auto columnOf = [&](App::DocumentObject* obj) -> const ViewProvider::TreeBodyColumn& {
        auto [it, added] = columns.try_emplace(obj);
        if (added) {
            it->second = lookUpColumn(obj);
        }
        return it->second;
    };
    auto colorIn = [](const std::vector<ViewProvider::BodySwatch>& swatches,
                      App::DocumentObject* body) -> std::optional<QColor> {
        for (const auto& swatch : swatches) {
            for (std::size_t i = 0; i < swatch.bodies.size() && i < swatch.colors.size(); ++i) {
                if (swatch.bodies[i] == body) {
                    return swatch.colors[i];
                }
            }
        }
        return std::nullopt;
    };
    auto bodiesBuilt = [&](App::DocumentObject* obj) {
        std::set<App::DocumentObject*> bodies;
        for (const auto& swatch : columnOf(obj).partOf) {
            bodies.insert(swatch.bodies.begin(), swatch.bodies.end());
        }
        return bodies;
    };
    // The tint of a row that builds a body is that body's colour.
    auto bodyColor = [&](App::DocumentObject* obj) -> std::optional<QColor> {
        const auto& partOf = columnOf(obj).partOf;
        if (partOf.empty()) {
            return std::nullopt;
        }
        return partOf.front().colors.front();
    };
    auto swatchRowOf = [&](App::DocumentObject* body) -> App::DocumentObject* {
        App::DocumentObject* row = columnOf(body).swatchRow;
        return row && colorIn(columnOf(row).builds, body) ? row : nullptr;
    };

    std::set<App::DocumentObject*> lineage;
    // Rows tinted in the selected body's colour rather than their own: its family when its swatch
    // was clicked, and the steps of other bodies it was built on, which win.
    std::map<App::DocumentObject*, QColor> ownTint;
    std::set<App::DocumentObject*> borrowed;
    auto tintAsFamily = [&](App::DocumentObject* row, const QColor& color) {
        if (!borrowed.count(row)) {
            ownTint[row] = color;
        }
    };
    for (auto* obj : selected) {
        App::DocumentObject* start = obj;
        std::set<App::DocumentObject*> ownBodies;
        std::optional<QColor> ownColor;
        if (App::DocumentObject* row = swatchRowOf(obj)) {
            start = row;
            ownBodies.insert(obj);
            ownColor = colorIn(columnOf(row).builds, obj);
            lineage.insert(row);
            tintAsFamily(row, *ownColor);
        }
        else {
            ownBodies = bodiesBuilt(obj);
            ownColor = bodyColor(obj);
        }
        const bool bodySelected = start != obj;
        std::vector<App::DocumentObject*> pending {start};
        while (!pending.empty()) {
            auto* step = pending.back();
            pending.pop_back();
            for (auto* input : step->getOutList()) {
                if (lineage.count(input) || selected.count(input) || isOrigin(input)) {
                    continue;
                }
                lineage.insert(input);
                auto inputBodies = bodiesBuilt(input);
                bool sameFamily = inputBodies.empty()
                    || std::ranges::any_of(inputBodies, [&](auto* body) {
                                      return ownBodies.count(body) > 0;
                                  });
                if (sameFamily) {
                    pending.push_back(input);
                    if (bodySelected) {
                        tintAsFamily(input, *ownColor);
                    }
                }
                else if (ownColor) {
                    borrowed.insert(input);
                    ownTint[input] = *ownColor;
                }
            }
        }
    }

    // What went into a step of another body stays out, even when the selection also uses it
    // directly: a pattern's copy names the step it copied, not that step's sketch.
    for (auto it = lineage.begin(); it != lineage.end();) {
        const auto inList = (*it)->getInList();
        const bool othersInput = !borrowed.count(*it) && bodiesBuilt(*it).empty()
            && std::ranges::any_of(inList, [&](auto* user) { return borrowed.count(user) > 0; });
        it = othersInput ? lineage.erase(it) : std::next(it);
    }

    // A row that builds no body (a sketch) takes the colour of a step it feeds, else of the
    // selected step's body.
    std::optional<QColor> selectedColor;
    for (auto* step : selected) {
        if ((selectedColor = bodyColor(step))) {
            break;
        }
    }
    std::map<App::DocumentObject*, QColor> tints;
    auto fedColor = [&](App::DocumentObject* obj) -> std::optional<QColor> {
        for (auto* user : obj->getInList()) {
            if ((lineage.count(user) || selected.count(user)) && !isOrigin(user)) {
                if (auto color = bodyColor(user)) {
                    return color;
                }
            }
        }
        return std::nullopt;
    };
    for (auto* obj : lineage) {
        if (auto own = ownTint.find(obj); own != ownTint.end()) {
            tints[obj] = own->second;
        }
        else if (auto color = bodyColor(obj)) {
            tints[obj] = *color;
        }
        else if (auto fed = fedColor(obj)) {
            tints[obj] = *fed;
        }
        else if (selectedColor) {
            tints[obj] = *selectedColor;
        }
        else {
            tints[obj] = highlight;
        }
    }
    for (auto* obj : selected) {
        tints[obj] = bodyColor(obj).value_or(selectedColor.value_or(highlight));
    }
    return {std::move(tints), std::move(selected)};
}
