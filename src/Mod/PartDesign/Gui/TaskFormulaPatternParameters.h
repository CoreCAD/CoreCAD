// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "TaskTransformedParameters.h"
#include "ViewProviderFormulaPattern.h"


class Ui_TaskFormulaPatternParameters;

namespace App
{
class PropertyQuantity;
}

namespace Gui
{
class QuantitySpinBox;
}

namespace PartDesignGui
{

class TaskFormulaPatternParameters: public TaskTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskFormulaPatternParameters(
        ViewProviderTransformed* TransformedView,
        QWidget* parent = nullptr
    );
    ~TaskFormulaPatternParameters() override;

    void apply() override;

private Q_SLOTS:
    void onOccurrencesChanged(int occurrences);
    void onUpdateView(bool on) override;

private:
    void setupParameterUI(QWidget* widget) override;
    void retranslateParameterUI(QWidget* widget) override;
    void bindSpin(Gui::QuantitySpinBox* spin, App::PropertyQuantity& prop);
    void changed();
    void updateUI();

    std::unique_ptr<Ui_TaskFormulaPatternParameters> ui;
};


class TaskDlgFormulaPatternParameters: public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgFormulaPatternParameters(ViewProviderFormulaPattern* FormulaPatternView);
};

}  // namespace PartDesignGui
