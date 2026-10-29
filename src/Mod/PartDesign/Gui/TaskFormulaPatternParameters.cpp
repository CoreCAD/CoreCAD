// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <App/Document.h>
#include <Gui/Command.h>
#include <Mod/PartDesign/App/FeatureFormulaPattern.h>

#include "ui_TaskFormulaPatternParameters.h"
#include "TaskFormulaPatternParameters.h"

using namespace PartDesignGui;

/* TRANSLATOR PartDesignGui::TaskFormulaPatternParameters */

TaskFormulaPatternParameters::TaskFormulaPatternParameters(
    ViewProviderTransformed* TransformedView,
    QWidget* parent
)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskFormulaPatternParameters)
{
    setupUI();
}

TaskFormulaPatternParameters::~TaskFormulaPatternParameters() = default;

void TaskFormulaPatternParameters::bindSpin(Gui::QuantitySpinBox* spin, App::PropertyQuantity& prop)
{
    spin->bind(prop);
    connect(spin, qOverload<double>(&Gui::QuantitySpinBox::valueChanged), this, [this, &prop](double value) {
        if (!blockUpdate) {
            setupTransaction();
            prop.setValue(value);
            changed();
        }
    });
}

void TaskFormulaPatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    auto* pattern = getObject<PartDesign::FormulaPattern>();
    bindSpin(ui->spinX, pattern->X);
    bindSpin(ui->spinY, pattern->Y);
    bindSpin(ui->spinZ, pattern->Z);
    connect(
        ui->spinOccurrences,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        &TaskFormulaPatternParameters::onOccurrencesChanged
    );

    updateUI();
}

void TaskFormulaPatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
    updateUI();
}

void TaskFormulaPatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto* pattern = getObject<PartDesign::FormulaPattern>();
    ui->spinOccurrences->setValue(pattern->Occurrences.getValue());
    ui->spinX->setValue(pattern->X.getValue());
    ui->spinY->setValue(pattern->Y.getValue());
    ui->spinZ->setValue(pattern->Z.getValue());

    blockUpdate = false;
}

void TaskFormulaPatternParameters::changed()
{
    recomputeFeature();
    updateUI();
}

void TaskFormulaPatternParameters::onOccurrencesChanged(int occurrences)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::FormulaPattern>()->Occurrences.setValue(occurrences);
    changed();
}

void TaskFormulaPatternParameters::onUpdateView(bool on)
{
    blockUpdate = !on;
    if (on) {
        setupTransaction();
        recomputeFeature();
    }
}

void TaskFormulaPatternParameters::apply()
{
    auto* pattern = getObject<PartDesign::FormulaPattern>();
    FCMD_OBJ_CMD(pattern, "Occurrences = " << pattern->Occurrences.getValue());
    ui->spinX->apply();
    ui->spinY->apply();
    ui->spinZ->apply();
}


TaskDlgFormulaPatternParameters::TaskDlgFormulaPatternParameters(
    ViewProviderFormulaPattern* FormulaPatternView
)
    : TaskDlgTransformedParameters(FormulaPatternView)
{
    parameter = new TaskFormulaPatternParameters(FormulaPatternView);

    Content.push_back(parameter);
    Content.push_back(new TaskMergeResultParameters(FormulaPatternView));
    Content.push_back(preview);
}

#include "moc_TaskFormulaPatternParameters.cpp"
