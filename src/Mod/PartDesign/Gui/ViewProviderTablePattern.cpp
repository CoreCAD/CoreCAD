// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "ViewProviderTablePattern.h"
#include "TaskTablePatternParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderTablePattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters* ViewProviderTablePattern::getEditDialog()
{
    return new TaskDlgTablePatternParameters(this);
}

void ViewProviderTablePattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit Table Pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string& ViewProviderTablePattern::featureName() const
{
    static const std::string name = "TablePattern";
    return name;
}
