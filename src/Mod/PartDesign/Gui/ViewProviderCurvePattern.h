// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "ViewProviderTransformed.h"

namespace PartDesignGui
{

class PartDesignGuiExport ViewProviderCurvePattern: public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderCurvePattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderCurvePattern);

public:
    ViewProviderCurvePattern()
    {
        menuName = tr("Curve Pattern Parameters");
        sPixmap = "PartDesign_CurvePattern.svg";
    }

    const std::string& featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    /// Returns a newly created dialog for the part to be placed in the task view
    TaskDlgFeatureParameters* getEditDialog() override;
};


}  // namespace PartDesignGui
