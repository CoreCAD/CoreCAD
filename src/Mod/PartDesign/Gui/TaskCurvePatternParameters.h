// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include "TaskTransformedParameters.h"
#include "ViewProviderCurvePattern.h"


class Ui_TaskCurvePatternParameters;

namespace PartDesignGui
{

class TaskCurvePatternParameters: public TaskTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskCurvePatternParameters(
        ViewProviderTransformed* TransformedView,
        QWidget* parent = nullptr
    );
    ~TaskCurvePatternParameters() override;

    void apply() override;

protected:
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

private Q_SLOTS:
    void onPathChosen(int index);
    void onModeChosen(int index);
    void onOccurrencesChanged(int value);
    void onReversedChanged(bool on);
    void onKeepOrientationChanged(bool on);

private:
    void setupParameterUI(QWidget* widget) override;
    void retranslateParameterUI(QWidget* widget) override;
    void fillPaths();
    void updateUI();
    void changed();
    void usePath(App::DocumentObject* obj, const std::vector<std::string>& edges);

    /// The combo's entries in order; the last, null, is "Select edges…".
    std::vector<App::DocumentObject*> paths;
    std::unique_ptr<Ui_TaskCurvePatternParameters> ui;
};


class TaskDlgCurvePatternParameters: public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgCurvePatternParameters(ViewProviderCurvePattern* CurvePatternView);
};

}  // namespace PartDesignGui
