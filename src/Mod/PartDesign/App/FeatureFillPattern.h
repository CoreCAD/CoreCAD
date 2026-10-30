// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <App/PropertyLinks.h>
#include <App/PropertyUnits.h>
#include "FeatureTransformed.h"


namespace PartDesign
{

/// Copies packed into a flat region, in a grid or in staggered rows. The grid passes through
/// the original, and a copy is placed wherever its centre lies inside the region, at least
/// Margin from its edge. Copies slide across the region's plane and keep the original's turn.
class PartDesignExport FillPattern: public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::FillPattern);

public:
    FillPattern();

    App::PropertyLinkSub Boundary;
    App::PropertyEnumeration Layout;
    App::PropertyLength Spacing;
    App::PropertyLength RowSpacing;
    App::PropertyLength Margin;
    App::PropertyAngle Angle;

    short mustExecute() const override;

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderFillPattern";
    }

    std::list<gp_Trsf> getTransformations() override;

    /// Whether the object can serve as a whole boundary: it outlines one or more flat regions.
    static bool isBoundary(const App::DocumentObject* obj);

private:
    static const char* LayoutEnums[];
};

}  // namespace PartDesign
