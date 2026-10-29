// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <gp_Ax3.hxx>

#include <App/PropertyGeo.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include "FeatureTransformed.h"


namespace PartDesign
{

/// One copy for each row of a table. Measured from the original, a row is the copy's offset
/// along the world's X, Y and Z. Measured from a plane, a row is an X, Y position on the
/// plane, and the original's centre slides there, staying level with the original. A row
/// that would leave the copy on the original adds nothing. With a spreadsheet linked, the rows
/// are read from its cells instead of the typed ones.
class PartDesignExport TablePattern: public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::TablePattern);

public:
    TablePattern();

    App::PropertyEnumeration MeasuredFrom;
    App::PropertyLinkSub Plane;
    App::PropertyVectorList Rows;
    App::PropertyLink Spreadsheet;
    App::PropertyString Cells;

    short mustExecute() const override;

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderTablePattern";
    }

    std::list<gp_Trsf> getTransformations() override;
    void shiftCellReferences(const App::DocumentObject* sheet, const App::CellShift& shift) override;

    /// The rows the cells give, one per range row that holds a value; columns are X, Y and Z
    /// in order, and an empty cell in a used row is zero. Every value must be a length.
    std::vector<Base::Vector3d> rowsFromSpreadsheet() const;

    /// The plane's origin and its X and Y directions in the world: a sketch's own axes, a
    /// datum plane's, or a flat face's.
    static gp_Ax3 frameOf(const App::DocumentObject* obj, const std::vector<std::string>& subs);

private:
    std::vector<gp_Vec> movesFromPlane(const std::vector<Base::Vector3d>& rows);

    static const char* MeasuredFromEnums[];
};

}  // namespace PartDesign
