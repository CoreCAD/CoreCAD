// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "ViewProviderFillPattern.h"
#include "TaskFillPatternParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderFillPattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters* ViewProviderFillPattern::getEditDialog()
{
    return new TaskDlgFillPatternParameters(this);
}

void ViewProviderFillPattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit Fill Pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string& ViewProviderFillPattern::featureName() const
{
    static const std::string name = "FillPattern";
    return name;
}
