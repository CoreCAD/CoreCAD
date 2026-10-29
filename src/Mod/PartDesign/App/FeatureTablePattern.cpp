// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <optional>

#include <BRepAdaptor_Surface.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>

#include <App/Datums.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyUnits.h>
#include <App/Range.h>
#include <Base/Exception.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/PartFeature.h>

#include "FeatureTablePattern.h"

using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::TablePattern, PartDesign::Transformed)

const char* TablePattern::MeasuredFromEnums[] = {"Original", "Plane", nullptr};

TablePattern::TablePattern()
{
    ADD_PROPERTY_TYPE(
        MeasuredFrom,
        (0L),
        "TablePattern",
        App::Prop_None,
        "Original: each row is an offset along X, Y and Z. Plane: each row is a position on the "
        "plane"
    );
    MeasuredFrom.setEnums(MeasuredFromEnums);
    ADD_PROPERTY_TYPE(
        Plane,
        (nullptr),
        "TablePattern",
        App::Prop_None,
        "The sketch, datum plane or flat face the positions are measured on"
    );
    ADD_PROPERTY_TYPE(Rows, (), "TablePattern", App::Prop_None, "One row for each copy");
    ADD_PROPERTY_TYPE(
        Spreadsheet,
        (nullptr),
        "TablePattern",
        App::Prop_None,
        "A spreadsheet whose cells give the rows instead of the typed ones"
    );
    ADD_PROPERTY_TYPE(
        Cells,
        (""),
        "TablePattern",
        App::Prop_None,
        "The spreadsheet's cells holding the rows, such as A2:C10"
    );
}

short TablePattern::mustExecute() const
{
    if (MeasuredFrom.isTouched() || Plane.isTouched() || Rows.isTouched() || Spreadsheet.isTouched()
        || Cells.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

static gp_Ax3 frameOfPlacement(const Base::Placement& placement)
{
    const Base::Vector3d& base = placement.getPosition();
    Base::Vector3d normal;
    Base::Vector3d x;
    placement.getRotation().multVec(Base::Vector3d(0, 0, 1), normal);
    placement.getRotation().multVec(Base::Vector3d(1, 0, 0), x);
    return {gp_Pnt(base.x, base.y, base.z), gp_Dir(normal.x, normal.y, normal.z), gp_Dir(x.x, x.y, x.z)};
}

gp_Ax3 TablePattern::frameOf(const App::DocumentObject* obj, const std::vector<std::string>& subs)
{
    if (!obj) {
        throw Base::ValueError("No plane specified to measure the positions on");
    }
    const bool whole = subs.empty() || subs.front().empty();
    if (whole) {
        if (const auto* sketch = freecad_cast<const Part::Part2DObject*>(obj)) {
            return frameOfPlacement(sketch->Placement.getValue());
        }
        if (const auto* plane = freecad_cast<const App::Plane*>(obj)) {
            Base::Placement placement = plane->Placement.getValue();
            const App::LocalCoordinateSystem* lcs = plane->getLCS();
            if (lcs && !lcs->isOrigin()) {
                placement = lcs->Placement.getValue() * placement;
            }
            return frameOfPlacement(placement);
        }
        throw Base::ValueError("The positions must be measured on a sketch, a datum plane or a face");
    }

    constexpr auto options = Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform;
    const TopoDS_Shape shape
        = Part::Feature::getTopoShape(obj, options, subs.front().c_str()).getShape();
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
        throw Base::ValueError("The positions must be measured on a sketch, a datum plane or a face");
    }
    BRepAdaptor_Surface surface(TopoDS::Face(shape));
    if (surface.GetType() != GeomAbs_Plane) {
        throw Base::ValueError("The face the positions are measured on must be flat");
    }
    // A face has no axes of its own a person can see, so positions are measured from the
    // world's origin and X laid onto it (the world's Y where X stands upright on the face),
    // as seen from the face's outer side.
    const gp_Pln plane = surface.Plane();
    gp_Dir normal = plane.Axis().Direction();
    if (shape.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    gp_Vec along(1, 0, 0);
    if (gp_Dir(along).IsParallel(normal, Precision::Angular())) {
        along = gp_Vec(0, 1, 0);
    }
    along -= gp_Vec(normal) * along.Dot(gp_Vec(normal));
    const gp_Pnt origin(0, 0, 0);
    const gp_Vec toPlane = gp_Vec(normal) * gp_Vec(origin, plane.Location()).Dot(gp_Vec(normal));
    return {origin.Translated(toPlane), normal, gp_Dir(along)};
}

// Each position is read on the plane; the original's centre slides there across the plane
// it is level with, so a copy never leaves the original's face.
std::vector<gp_Vec> TablePattern::movesFromPlane(const std::vector<Base::Vector3d>& rows)
{
    const gp_Ax3 frame = frameOf(Plane.getValue(), Plane.getSubValues());
    const gp_Vec normal(frame.Direction());
    const Base::Vector3d centre = centreOfOriginals();
    const gp_Pnt from(centre.x, centre.y, centre.z);

    std::vector<gp_Vec> moves;
    for (const Base::Vector3d& row : rows) {
        const gp_Pnt to = frame.Location().Translated(
            gp_Vec(frame.XDirection()) * row.x + gp_Vec(frame.YDirection()) * row.y
        );
        gp_Vec move(from, to);
        moves.push_back(move - normal * move.Dot(normal));
    }
    return moves;
}

static App::Range rangeOf(const std::string& cells)
{
    try {
        return App::Range(cells.c_str(), true);
    }
    catch (const Base::Exception&) {
        throw Base::ValueError(
            "The cells \"" + cells + "\" are not a cell or a range of cells such as A2:C10"
        );
    }
}

std::vector<Base::Vector3d> TablePattern::rowsFromSpreadsheet() const
{
    const App::DocumentObject* sheet = Spreadsheet.getValue();
    if (!sheet->getTypeId().isDerivedFrom(Base::Type::fromName("Spreadsheet::Sheet"))) {
        throw Base::ValueError("The rows must be read from a spreadsheet");
    }
    const App::Range range = rangeOf(Cells.getStrValue());
    if (range.colCount() > 3) {
        throw Base::ValueError("The cells span more than three columns; a row is X, Y and Z");
    }

    std::vector<Base::Vector3d> rows;
    for (int row = range.from().row(); row <= range.to().row(); ++row) {
        double values[3] = {0, 0, 0};
        bool used = false;
        for (int column = 0; column < range.colCount(); ++column) {
            const std::string address = App::CellAddress(row, range.from().col() + column).toString();
            const App::Property* cell = sheet->getPropertyByName(address.c_str());
            if (!cell) {
                continue;
            }
            const auto* quantity = freecad_cast<const App::PropertyQuantity*>(cell);
            if (!quantity || quantity->getQuantityValue().getUnit() != Base::Unit::Length) {
                throw Base::ValueError(
                    "Cell " + address + " does not hold a length; write it with a unit, such as 10 mm"
                );
            }
            values[column] = quantity->getValue();
            used = true;
        }
        if (used) {
            rows.emplace_back(values[0], values[1], values[2]);
        }
    }
    return rows;
}

// Rows or columns inserted in the spreadsheet move the cells; the range moves with them.
void TablePattern::renameObjectIdentifiers(
    const std::map<App::ObjectIdentifier, App::ObjectIdentifier>& paths
)
{
    Transformed::renameObjectIdentifiers(paths);
    App::DocumentObject* sheet = Spreadsheet.getValue();
    if (!sheet) {
        return;
    }
    std::optional<App::Range> range;
    try {
        range = rangeOf(Cells.getStrValue());
    }
    catch (const Base::Exception&) {
        return;
    }
    bool changed = false;
    auto moved = [&](const App::CellAddress& cell) {
        auto it = paths.find(App::ObjectIdentifier(sheet, cell.toString()));
        if (it == paths.end()) {
            return cell.toString();
        }
        changed = true;
        return it->second.getPropertyName();
    };
    const std::string from = moved(range->from());
    const std::string to = moved(range->to());
    if (changed) {
        Cells.setValue(range->size() == 1 ? from : from + ":" + to);
    }
}

std::list<gp_Trsf> TablePattern::getTransformations()
{
    const std::vector<Base::Vector3d> rows = Spreadsheet.getValue() ? rowsFromSpreadsheet()
                                                                    : Rows.getValues();
    if (rows.empty()) {
        throw Base::ValueError("The table has no rows to place copies with");
    }

    std::vector<gp_Vec> moves;
    if (MeasuredFrom.getValue() == 1) {
        moves = movesFromPlane(rows);
    }
    else {
        for (const Base::Vector3d& row : rows) {
            moves.emplace_back(row.x, row.y, row.z);
        }
    }

    std::list<gp_Trsf> transformations {gp_Trsf()};
    for (const gp_Vec& move : moves) {
        if (move.Magnitude() < Precision::Confusion()) {
            continue;
        }
        gp_Trsf trsf;
        trsf.SetTranslation(move);
        transformations.push_back(trsf);
    }
    if (transformations.size() == 1) {
        throw Base::ValueError("Every row leaves the copy on the original, so no copy is placed");
    }
    return transformations;
}
