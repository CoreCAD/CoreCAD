// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <cmath>
#include <limits>

#include <Precision.hxx>
#include <gp.hxx>
#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <App/Expression.h>
#include <App/ExpressionParser.h>
#include <App/ObjectIdentifier.h>
#include <Base/Exception.h>
#include <Base/Tools.h>

#include "FeatureFormulaPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::FormulaPattern, PartDesign::Transformed)

const App::PropertyIntegerConstraint::Constraints FormulaPattern::intOccurrences
    = {1, std::numeric_limits<int>::max(), 1};

const char* FormulaPattern::AxisEnums[] = {"X", "Y", "Z", nullptr};

FormulaPattern::FormulaPattern()
{
    ADD_PROPERTY_TYPE(
        Occurrences,
        (2),
        "FormulaPattern",
        App::Prop_None,
        "The total number of instances, including the original feature"
    );
    Occurrences.setConstraints(&intOccurrences);
    ADD_PROPERTY_TYPE(
        X,
        (0.0),
        "FormulaPattern",
        App::Prop_None,
        "The copy's offset along X; a formula may use n, the copy's number"
    );
    ADD_PROPERTY_TYPE(
        Y,
        (0.0),
        "FormulaPattern",
        App::Prop_None,
        "The copy's offset along Y; a formula may use n, the copy's number"
    );
    ADD_PROPERTY_TYPE(
        Z,
        (0.0),
        "FormulaPattern",
        App::Prop_None,
        "The copy's offset along Z; a formula may use n, the copy's number"
    );
    ADD_PROPERTY_TYPE(
        Angle,
        (0.0),
        "FormulaPattern",
        App::Prop_None,
        "The copy's turn about its own centre; a formula may use n, the copy's number"
    );
    ADD_PROPERTY_TYPE(Axis, (2L), "FormulaPattern", App::Prop_None, "The world axis the turn is parallel to");
    Axis.setEnums(AxisEnums);
}

void FormulaPattern::setupObject()
{
    Transformed::setupObject();
    setExpression(App::ObjectIdentifier(X), App::Expression::parse(this, "n * 10 mm"));
}

short FormulaPattern::mustExecute() const
{
    if (Occurrences.isTouched() || X.isTouched() || Y.isTouched() || Z.isTouched()
        || Angle.isTouched() || Axis.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

double FormulaPattern::valueOf(App::PropertyQuantity& prop, const Base::Unit& unit, const char* example) const
{
    const App::ObjectIdentifier path(prop);
    const std::shared_ptr<App::Expression> formula = getExpression(path).expression;
    if (!formula) {
        return prop.getValue();
    }
    const App::ExpressionPtr result = formula->eval();
    const auto* number = freecad_cast<App::NumberExpression*>(result.get());
    if (!number || number->getUnit() != unit) {
        throw Base::ValueError(
            "The formula for " + path.toString() + " does not give "
            + (unit == Base::Unit::Angle ? "an angle" : "a length") + " for copy "
            + std::to_string(n.getValue()) + "; write it with a unit, such as " + example
        );
    }
    return number->getValue();
}

std::list<gp_Trsf> FormulaPattern::getTransformations()
{
    static const gp_Dir axisDirections[] = {gp::DX(), gp::DY(), gp::DZ()};
    const bool turns = Angle.getValue() != 0.0
        || getExpression(App::ObjectIdentifier(Angle)).expression;
    gp_Ax1 turnAxis;
    if (turns) {
        const Base::Vector3d centre = centreOfOriginals();
        turnAxis = gp_Ax1(gp_Pnt(centre.x, centre.y, centre.z), axisDirections[Axis.getValue()]);
    }

    std::list<gp_Trsf> transformations {gp_Trsf()};
    try {
        for (int copy = 1; copy < Occurrences.getValue(); ++copy) {
            n.setValue(copy);
            const gp_Vec move(
                valueOf(X, Base::Unit::Length, "n * 10 mm"),
                valueOf(Y, Base::Unit::Length, "n * 10 mm"),
                valueOf(Z, Base::Unit::Length, "n * 10 mm")
            );
            const double degrees = valueOf(Angle, Base::Unit::Angle, "n * 30 deg");
            const double turnLeft = std::remainder(degrees, 360.0);
            // A copy left on the original adds something only when it is built differently.
            if (move.Magnitude() < Precision::Confusion()
                && std::abs(turnLeft) < Precision::Angular() && !variesMeasurements()) {
                continue;
            }
            gp_Trsf turn;
            if (turns) {
                turn.SetRotation(turnAxis, Base::toRadians(degrees));
            }
            gp_Trsf trsf;
            trsf.SetTranslation(move);
            transformations.push_back(trsf * turn);
        }
    }
    catch (...) {
        n.setValue(1);
        throw;
    }
    n.setValue(1);
    if (transformations.size() == 1) {
        throw Base::ValueError("Every copy is left on the original, so no copy is placed");
    }
    return transformations;
}
