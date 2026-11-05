// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <vector>

#include <gp_Pnt.hxx>

#include <App/PropertyLinks.h>
#include "FeatureTransformed.h"


namespace Part
{
class Part2DObject;
}

namespace PartDesign
{

/// One copy on each point of a sketch. Each copy is the original slid across the sketch's
/// plane until the original's centre sits over the point; a point already under the
/// original adds nothing. Construction points are helpers and are not used.
class PartDesignExport PointPattern: public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::PointPattern);

public:
    PointPattern();

    App::PropertyLink Points;

    short mustExecute() const override;

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderPointPattern";
    }

    std::list<gp_Trsf> getTransformations() override;

    /// The points a copy can be placed on, in the world.
    static std::vector<gp_Pnt> pointsOf(const Part::Part2DObject* sketch);
};

}  // namespace PartDesign
