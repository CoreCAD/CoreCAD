// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <App/PropertyLinks.h>
#include <App/PropertyUnits.h>
#include "FeatureTransformed.h"


namespace PartDesign
{

/// Copies spaced along a path of connected edges. The pattern starts where the path passes
/// closest to the original, and each copy keeps the original's offset from the path. By default
/// a copy turns with the path; KeepOrientation leaves every copy facing the original's way.
class PartDesignExport CurvePattern: public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::CurvePattern);

public:
    CurvePattern();

    App::PropertyLinkSub Path;
    App::PropertyBool Reversed;
    App::PropertyEnumeration Mode;
    App::PropertyLength Length;
    App::PropertyLength Offset;
    App::PropertyIntegerConstraint Occurrences;
    App::PropertyBool KeepOrientation;

    short mustExecute() const override;

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderCurvePattern";
    }

    std::list<gp_Trsf> getTransformations() override;

    /// The length of the path from where it passes closest to the original to its end, in the
    /// pattern's direction. For a closed path, the whole length.
    double lengthAhead();

    /// Whether the object can serve as a whole path: it has edges and they form one curve.
    static bool isPath(const App::DocumentObject* obj);

protected:
    void onChanged(const App::Property* prop) override;

private:
    static const char* ModeEnums[];
    static const App::PropertyIntegerConstraint::Constraints intOccurrences;
};

}  // namespace PartDesign
