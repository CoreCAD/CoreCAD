// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

namespace App
{
class Document;
}

namespace PartGui
{

/** How History draws a version in the model view (UI_GUIDE §2.2.1): a hidden document, with no tab,
 *  holding `after`'s material. With `showChanges` the material `after` added over `before` is blue
 *  and the material it removed is red and see-through; without, `after` is drawn plainly. Both
 *  documents must hold built shapes. The caller closes the returned document.
 */
App::Document* versionLayers(App::Document& before, App::Document& after, bool showChanges);

/// Offer versionLayers to the History panel. Called once, at module initialisation.
void registerVersionLayers();

}  // namespace PartGui
