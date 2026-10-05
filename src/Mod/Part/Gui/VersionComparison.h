// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <string>

namespace App
{
class Document;
}

namespace PartGui
{

/** Compare in 3D (UI_GUIDE §2.2.1): a tab of its own showing the material `after` shares with
 *  `before` faint, the material it added blue and the material it removed red. Both documents must
 *  hold built shapes. The tab belongs to a hidden document of its own, so the model and what is
 *  shown in it are never touched; comparing the same entry again replaces the tab. Returns that
 *  hidden document.
 */
App::Document* showVersionComparison(
    App::Document& before,
    App::Document& after,
    const std::string& title
);

/// Offer showVersionComparison to the History panel. Called once, at module initialisation.
void registerVersionComparison();

}  // namespace PartGui
