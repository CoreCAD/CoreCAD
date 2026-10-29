// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <limits>

#include <Precision.hxx>
#include <gp_Vec.hxx>

#include <App/Expression.h>
#include <App/ExpressionParser.h>
#include <App/ObjectIdentifier.h>
#include <Base/Exception.h>

#include "FeatureFormulaPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE(PartDesign::FormulaPattern, PartDesign::Transformed)

const App::PropertyIntegerConstraint::Constraints FormulaPattern::intOccurrences
    = {1, std::numeric_limits<int>::max(), 1};

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
        n,
        (1),
        "FormulaPattern",
        App::PropertyType(App::Prop_Hidden | App::Prop_ReadOnly | App::Prop_Output),
        "The number of the copy the formulas are being worked out for"
    );
}

void FormulaPattern::setupObject()
{
    Transformed::setupObject();
    setExpression(App::ObjectIdentifier(X), App::Expression::parse(this, "n * 10 mm"));
}

short FormulaPattern::mustExecute() const
{
    if (Occurrences.isTouched() || X.isTouched() || Y.isTouched() || Z.isTouched()) {
        return 1;
    }
    return Transformed::mustExecute();
}

double FormulaPattern::offsetOf(App::PropertyDistance& axis) const
{
    const App::ObjectIdentifier path(axis);
    const std::shared_ptr<App::Expression> formula = getExpression(path).expression;
    if (!formula) {
        return axis.getValue();
    }
    const App::ExpressionPtr result = formula->eval();
    const auto* number = freecad_cast<App::NumberExpression*>(result.get());
    if (!number || number->getUnit() != Base::Unit::Length) {
        throw Base::ValueError(
            "The formula for " + path.toString() + " does not give a length for copy "
            + std::to_string(n.getValue()) + "; write it with a unit, such as n * 10 mm"
        );
    }
    return number->getValue();
}

std::list<gp_Trsf> FormulaPattern::getTransformations()
{
    std::list<gp_Trsf> transformations {gp_Trsf()};
    try {
        for (int copy = 1; copy < Occurrences.getValue(); ++copy) {
            n.setValue(copy);
            const gp_Vec move(offsetOf(X), offsetOf(Y), offsetOf(Z));
            if (move.Magnitude() < Precision::Confusion()) {
                continue;
            }
            gp_Trsf trsf;
            trsf.SetTranslation(move);
            transformations.push_back(trsf);
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
