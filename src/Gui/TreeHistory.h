// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <map>
#include <vector>

#include <FCGlobal.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

/// The rules the tree follows to list a document's root rows as its history, and to keep that
/// history in order when a person drags a row. None of them touch the screen.
namespace Gui::TreeHistory
{

/// Each object's place in @p doc's history, which is the order the tree lists its root rows in.
GuiExport std::map<const App::DocumentObject*, int> historyPositions(const App::Document* doc);

/// An object no longer in the history (being removed) sorts last.
GuiExport int positionIn(
    const std::map<const App::DocumentObject*, int>& position,
    const App::DocumentObject* obj
);

/// @p objects with @p dragged taken out and put back just above or below @p underMouse.
GuiExport std::vector<App::DocumentObject*> placeDropped(
    const std::vector<App::DocumentObject*>& objects,
    const std::vector<App::DocumentObject*>& dragged,
    const App::DocumentObject* underMouse,
    bool inBottomHalf
);

/// Whether every step in @p order comes after the steps it is built from, and after the world
/// frame, as a timeline must.
GuiExport bool keepsInputsFirst(const std::vector<App::DocumentObject*>& order);

}  // namespace Gui::TreeHistory
