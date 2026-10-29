// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <fastsignals/signal.h>

#include <Gui/TaskView/TaskView.h>

class QComboBox;
class QGridLayout;
class QLabel;

namespace PartDesign
{
class Transformed;
}

namespace PartDesignGui
{

class ViewProviderTransformed;

/// The measurements a pattern changes for each copy, shared by every pattern's panel. Each row
/// is one measurement and the formula of n that sets it; the list below adds another.
class TaskVariedMeasurements: public Gui::TaskView::TaskBox
{
    Q_OBJECT

public:
    explicit TaskVariedMeasurements(ViewProviderTransformed* view, QWidget* parent = nullptr);

private:
    PartDesign::Transformed* pattern() const;
    void showRows();
    void onChosen(int index);
    void stopVarying(int index);
    void rebuild();

    ViewProviderTransformed* view;
    QGridLayout* rows;
    QLabel* emptyLabel;
    QComboBox* choices;
    fastsignals::scoped_connection formulaChanged;
};

}  // namespace PartDesignGui
