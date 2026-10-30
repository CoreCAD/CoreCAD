// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "TaskTransformedParameters.h"
#include "ViewProviderPointPattern.h"


class Ui_TaskPointPatternParameters;

namespace PartDesignGui
{

class TaskPointPatternParameters: public TaskTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskPointPatternParameters(
        ViewProviderTransformed* TransformedView,
        QWidget* parent = nullptr
    );
    ~TaskPointPatternParameters() override;

    void apply() override;

protected:
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

private Q_SLOTS:
    void onSketchChosen(int index);

private:
    void setupParameterUI(QWidget* widget) override;
    void retranslateParameterUI(QWidget* widget) override;
    void fillSketches();
    void updateUI();
    void usePoints(App::DocumentObject* sketch);

    /// The combo's entries in order; the last, null, is "Select sketch…".
    std::vector<App::DocumentObject*> sketches;
    std::unique_ptr<Ui_TaskPointPatternParameters> ui;
};


class TaskDlgPointPatternParameters: public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgPointPatternParameters(ViewProviderPointPattern* PointPatternView);
};

}  // namespace PartDesignGui
