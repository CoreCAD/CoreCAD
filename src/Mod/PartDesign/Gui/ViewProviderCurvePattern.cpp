// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "ViewProviderCurvePattern.h"
#include "TaskCurvePatternParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderCurvePattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters* ViewProviderCurvePattern::getEditDialog()
{
    return new TaskDlgCurvePatternParameters(this);
}

void ViewProviderCurvePattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit Curve Pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string& ViewProviderCurvePattern::featureName() const
{
    static const std::string name = "CurvePattern";
    return name;
}
