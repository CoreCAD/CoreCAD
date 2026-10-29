// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "ViewProviderTransformed.h"

namespace PartDesignGui
{

class PartDesignGuiExport ViewProviderTablePattern: public ViewProviderTransformed
{
    Q_DECLARE_TR_FUNCTIONS(PartDesignGui::ViewProviderTablePattern)
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesignGui::ViewProviderTablePattern);

public:
    ViewProviderTablePattern()
    {
        menuName = tr("Table Pattern Parameters");
        sPixmap = "PartDesign_TablePattern.svg";
    }

    const std::string& featureName() const override;
    void setupContextMenu(QMenu*, QObject*, const char*) override;

protected:
    /// Returns a newly created dialog for the part to be placed in the task view
    TaskDlgFeatureParameters* getEditDialog() override;
};


}  // namespace PartDesignGui
