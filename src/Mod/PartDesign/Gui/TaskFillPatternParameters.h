// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "TaskTransformedParameters.h"
#include "ViewProviderFillPattern.h"


class Ui_TaskFillPatternParameters;

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

class TaskFillPatternParameters: public TaskTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskFillPatternParameters(
        ViewProviderTransformed* TransformedView,
        QWidget* parent = nullptr
    );
    ~TaskFillPatternParameters() override;

    void apply() override;

protected:
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

private Q_SLOTS:
    void onBoundaryChosen(int index);
    void onLayoutChosen(int index);

private:
    void setupParameterUI(QWidget* widget) override;
    void retranslateParameterUI(QWidget* widget) override;
    void fillBoundaries();
    void updateUI();
    void changed();
    void useBoundary(App::DocumentObject* obj, const std::vector<std::string>& faces);
    void bindSpin(Gui::QuantitySpinBox* spin, App::PropertyQuantity& prop);

    /// The combo's entries in order; the last, null, is "Select faces…".
    std::vector<App::DocumentObject*> boundaries;
    std::unique_ptr<Ui_TaskFillPatternParameters> ui;
};


class TaskDlgFillPatternParameters: public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgFillPatternParameters(ViewProviderFillPattern* FillPatternView);
};

}  // namespace PartDesignGui
