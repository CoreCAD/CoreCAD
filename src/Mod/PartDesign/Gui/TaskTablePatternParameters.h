// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <Base/Vector3D.h>

#include "TaskTransformedParameters.h"
#include "ViewProviderTablePattern.h"


class Ui_TaskTablePatternParameters;

namespace PartDesignGui
{

class TaskTablePatternParameters: public TaskTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskTablePatternParameters(
        ViewProviderTransformed* TransformedView,
        QWidget* parent = nullptr
    );
    ~TaskTablePatternParameters() override;

    void apply() override;

    /// Rows read from text copied out of a spreadsheet: one line per row, cells split by tabs,
    /// commas or semicolons. A bare number is in the document's length unit; a line without
    /// a number is skipped.
    static std::vector<std::vector<double>> parseCells(const QString& text);

protected:
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void onMeasuredFromChosen(int index);
    void onCellEdited(int row, int column);
    void onAddRow();
    void onRemoveRow();
    void onUpdateView(bool on) override;

private:
    void setupParameterUI(QWidget* widget) override;
    void retranslateParameterUI(QWidget* widget) override;
    void fillPlanes();
    void updateUI();
    void useRows(const std::vector<Base::Vector3d>& rows);
    void usePlane(App::DocumentObject* obj, const std::vector<std::string>& subs);
    void paste();

    /// The combo's entries in order: first null for the original, last null for
    /// "Select face…".
    std::vector<App::DocumentObject*> planes;
    std::unique_ptr<Ui_TaskTablePatternParameters> ui;
};


class TaskDlgTablePatternParameters: public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgTablePatternParameters(ViewProviderTablePattern* TablePatternView);
};

}  // namespace PartDesignGui
