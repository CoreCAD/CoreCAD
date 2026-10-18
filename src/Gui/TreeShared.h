// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

/// State the tree's source files share; private to the tree, not for use elsewhere.
namespace Gui::TreeShared
{
extern bool _DraggingActive;
extern bool _DragEventFilter;
extern bool _DisableCheckTopParent;
}  // namespace Gui::TreeShared
