// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "ViewProviderFormulaPattern.h"
#include "TaskFormulaPatternParameters.h"

using namespace PartDesignGui;

PROPERTY_SOURCE(PartDesignGui::ViewProviderFormulaPattern, PartDesignGui::ViewProviderTransformed)

TaskDlgFeatureParameters* ViewProviderFormulaPattern::getEditDialog()
{
    return new TaskDlgFormulaPatternParameters(this);
}

void ViewProviderFormulaPattern::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    addDefaultAction(menu, QObject::tr("Edit Formula Pattern"));
    PartDesignGui::ViewProvider::setupContextMenu(menu, receiver, member);
}

const std::string& ViewProviderFormulaPattern::featureName() const
{
    static const std::string name = "FormulaPattern";
    return name;
}
