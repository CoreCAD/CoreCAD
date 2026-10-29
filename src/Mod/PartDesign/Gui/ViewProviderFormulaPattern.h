// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "ViewProviderTransformed.h"

namespace PartDesignGui
{

class PartDesignGuiExport ViewProviderFormulaPattern: public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderFormulaPattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderFormulaPattern);

public:
    ViewProviderFormulaPattern()
    {
        menuName = tr("Formula Pattern Parameters");
        sPixmap = "PartDesign_FormulaPattern.svg";
    }

    const std::string& featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    /// Returns a newly created dialog for the part to be placed in the task view
    TaskDlgFeatureParameters* getEditDialog() override;
};


}  // namespace PartDesignGui
