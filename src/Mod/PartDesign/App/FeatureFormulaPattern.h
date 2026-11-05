// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include "FeatureTransformed.h"


namespace PartDesign
{

/// Copies placed by formulas of the copy number. X, Y and Z are the copy's offset from the
/// original along the world's axes; a formula on one is worked out again for each copy with
/// n set to that copy's number, 1 for the first copy. Without a formula the value is the same
/// for every copy. Angle turns the copy about a line through the original's centre, parallel
/// to the world axis Axis, before it is moved. Occurrences counts the original, as in the other
/// patterns.
class PartDesignExport FormulaPattern: public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::FormulaPattern);

public:
    FormulaPattern();

    App::PropertyIntegerConstraint Occurrences;
    App::PropertyDistance X;
    App::PropertyDistance Y;
    App::PropertyDistance Z;
    App::PropertyAngle Angle;
    App::PropertyEnumeration Axis;

    short mustExecute() const override;

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderFormulaPattern";
    }

    std::list<gp_Trsf> getTransformations() override;

protected:
    void setupObject() override;

private:
    double valueOf(App::PropertyQuantity& prop, const Base::Unit& unit, const char* example) const;

    static const App::PropertyIntegerConstraint::Constraints intOccurrences;
    static const char* AxisEnums[];
};

}  // namespace PartDesign
