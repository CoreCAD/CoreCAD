// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <string>
#include <unordered_map>

#include <Base/Reader.h>

#include "Tree.h"

/// State the tree's source files share; private to the tree, not for use elsewhere.
namespace Gui::TreeShared
{
extern bool _DraggingActive;
extern bool _DragEventFilter;
extern bool _DisableCheckTopParent;
}  // namespace Gui::TreeShared

namespace Gui
{
/// Which rows of a document were expanded when it was saved, read back from its file.
class DocumentItem::ExpandInfo: public std::unordered_map<std::string, DocumentItem::ExpandInfoPtr>
{
public:
    void restore(Base::XMLReader& reader)
    {
        int level = reader.level();
        int count = reader.getAttribute<long>("count");
        for (int i = 0; i < count; ++i) {
            reader.readElement("Expand");
            auto& entry = (*this)[reader.getAttribute<const char*>("name")];
            if (!reader.hasAttribute("count")) {
                continue;
            }
            entry.reset(new ExpandInfo);
            entry->restore(reader);
        }
        reader.readEndElement("Expand", level - 1);
    }
};
}  // namespace Gui
