// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/FeatureCurvePattern.h>

#include "ReferenceSelection.h"
#include "ui_TaskCurvePatternParameters.h"
#include "TaskVariedMeasurements.h"
#include "TaskCurvePatternParameters.h"

using namespace PartDesignGui;

/* TRANSLATOR PartDesignGui::TaskCurvePatternParameters */

TaskCurvePatternParameters::TaskCurvePatternParameters(
    ViewProviderTransformed* TransformedView,
    QWidget* parent
)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskCurvePatternParameters)
{
    setupUI();
}

TaskCurvePatternParameters::~TaskCurvePatternParameters() = default;

void TaskCurvePatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    auto* pattern = getObject<PartDesign::CurvePattern>();
    ui->spinLength->bind(pattern->Length);
    ui->spinOffset->bind(pattern->Offset);

    connect(
        ui->comboPath,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskCurvePatternParameters::onPathChosen
    );
    connect(
        ui->comboMode,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskCurvePatternParameters::onModeChosen
    );
    connect(
        ui->spinLength,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        [this](double value) {
            if (!blockUpdate) {
                setupTransaction();
                getObject<PartDesign::CurvePattern>()->Length.setValue(value);
                changed();
            }
        }
    );
    connect(
        ui->spinOffset,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this,
        [this](double value) {
            if (!blockUpdate) {
                setupTransaction();
                getObject<PartDesign::CurvePattern>()->Offset.setValue(value);
                changed();
            }
        }
    );
    connect(
        ui->spinOccurrences,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        &TaskCurvePatternParameters::onOccurrencesChanged
    );
    connect(ui->checkReversed, &QCheckBox::toggled, this, &TaskCurvePatternParameters::onReversedChanged);
    connect(
        ui->checkKeepOrientation,
        &QCheckBox::toggled,
        this,
        &TaskCurvePatternParameters::onKeepOrientationChanged
    );

    fillPaths();
    updateUI();
}

void TaskCurvePatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
    fillPaths();
    updateUI();
}

// Every sketch that is one connected curve, in timeline order. A sketch built on this
// pattern is left out: the pattern cannot be placed by something made from it.
void TaskCurvePatternParameters::fillPaths()
{
    auto* pattern = getObject();
    paths.clear();
    ui->comboPath->clear();
    if (!pattern) {
        return;
    }

    const std::vector<App::DocumentObject*> builtOnPattern = pattern->getInListRecursive();
    for (auto* obj : pattern->getDocument()->getObjects()) {
        if (!obj->isDerivedFrom<Part::Part2DObject>() || !PartDesign::CurvePattern::isPath(obj)
            || std::ranges::find(builtOnPattern, obj) != builtOnPattern.end()) {
            continue;
        }
        paths.push_back(obj);
        ui->comboPath->addItem(QString::fromUtf8(obj->Label.getValue()));
    }
    paths.push_back(nullptr);
    ui->comboPath->addItem(tr("Select edges…"));
}

void TaskCurvePatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto* pattern = getObject<PartDesign::CurvePattern>();
    fillPaths();
    App::DocumentObject* chosen = pattern->Path.getValue();
    const std::vector<std::string> edges = pattern->Path.getSubValues();
    const bool wholeObject = std::ranges::all_of(edges, &std::string::empty);
    auto it = std::ranges::find(paths, chosen);
    if (chosen && (!wholeObject || it == paths.end())) {
        // Edges picked in the view, or a path chosen by a script.
        it = paths.insert(paths.end() - 1, chosen);
        ui->comboPath->insertItem(
            static_cast<int>(it - paths.begin()),
            wholeObject ? QString::fromUtf8(chosen->Label.getValue())
                : edges.size() == 1
                ? tr("%1, 1 edge").arg(QString::fromUtf8(chosen->Label.getValue()))
                : tr("%1, %2 edges").arg(QString::fromUtf8(chosen->Label.getValue())).arg(edges.size())
        );
    }
    ui->comboPath->setCurrentIndex(chosen ? static_cast<int>(it - paths.begin()) : -1);

    const bool extent = pattern->Mode.getValue() == 0;
    ui->comboMode->setCurrentIndex(extent ? 0 : 1);
    ui->labelLength->setVisible(extent);
    ui->spinLength->setVisible(extent);
    ui->labelOffset->setVisible(!extent);
    ui->spinOffset->setVisible(!extent);
    ui->spinLength->setValue(pattern->Length.getValue());
    ui->spinOffset->setValue(pattern->Offset.getValue());
    ui->spinOccurrences->setValue(pattern->Occurrences.getValue());
    ui->checkReversed->setChecked(pattern->Reversed.getValue());
    ui->checkKeepOrientation->setChecked(pattern->KeepOrientation.getValue());

    blockUpdate = false;
}

void TaskCurvePatternParameters::changed()
{
    recomputeFeature();
    updateUI();
}

void TaskCurvePatternParameters::usePath(App::DocumentObject* obj, const std::vector<std::string>& edges)
{
    setupTransaction();
    getObject<PartDesign::CurvePattern>()->Path.setValue(obj, edges);
    changed();
}

void TaskCurvePatternParameters::onPathChosen(int index)
{
    if (blockUpdate || index < 0 || index >= static_cast<int>(paths.size())) {
        return;
    }
    auto* pattern = getObject<PartDesign::CurvePattern>();
    if (App::DocumentObject* obj = paths[index]) {
        exitSelectionMode();
        if (obj != pattern->Path.getValue()
            || !std::ranges::all_of(pattern->Path.getSubValues(), &std::string::empty)) {
            usePath(obj, {});
        }
        return;
    }
    hideObject();
    showBase();
    selectionMode = SelectionMode::Reference;
    addReferenceSelectionGate(AllowSelection::EDGE);
    Gui::Selection().clearSelection();
    Gui::getMainWindow()->showMessage(tr("Select the edges of the path"));
}

void TaskCurvePatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (selectionMode == SelectionMode::None || msg.Type != Gui::SelectionChanges::AddSelection) {
        return;
    }

    if (originalSelected(msg)) {
        exitSelectionMode();
        return;
    }

    if (selectionMode != SelectionMode::Reference) {
        return;
    }
    auto* pattern = getObject<PartDesign::CurvePattern>();
    App::DocumentObject* obj = nullptr;
    std::vector<std::string> picked;
    if (!getReferencedSelection(pattern, msg, obj, picked) || !obj || picked.empty()
        || picked.front().compare(0, 4, "Edge") != 0) {
        return;
    }

    // Each edge picked on the same object joins the path; picking it again leaves it out.
    std::vector<std::string> edges;
    if (obj == pattern->Path.getValue()) {
        edges = pattern->Path.getSubValues();
        std::erase(edges, std::string());
    }
    if (auto it = std::ranges::find(edges, picked.front()); it != edges.end()) {
        edges.erase(it);
    }
    else {
        edges.push_back(picked.front());
    }
    usePath(edges.empty() ? nullptr : obj, edges);
}

void TaskCurvePatternParameters::onModeChosen(int index)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::CurvePattern>()->Mode.setValue(index);
    changed();
}

void TaskCurvePatternParameters::onOccurrencesChanged(int value)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::CurvePattern>()->Occurrences.setValue(value);
    changed();
}

void TaskCurvePatternParameters::onReversedChanged(bool on)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::CurvePattern>()->Reversed.setValue(on);
    changed();
}

void TaskCurvePatternParameters::onKeepOrientationChanged(bool on)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::CurvePattern>()->KeepOrientation.setValue(on);
    changed();
}

void TaskCurvePatternParameters::apply()
{
    auto* pattern = getObject<PartDesign::CurvePattern>();
    App::DocumentObject* path = pattern->Path.getValue();
    FCMD_OBJ_CMD(
        pattern,
        "Path = "
            << (path ? "(" + Gui::Command::getObjectCmd(path) + ", "
                        + buildLinkSubPythonStr(path, pattern->Path.getSubValues()) + ")"
                     : std::string("None"))
    );
    FCMD_OBJ_CMD(pattern, "Mode = " << pattern->Mode.getValue());
    ui->spinLength->apply();
    ui->spinOffset->apply();
    FCMD_OBJ_CMD(pattern, "Occurrences = " << pattern->Occurrences.getValue());
    FCMD_OBJ_CMD(pattern, "Reversed = " << (pattern->Reversed.getValue() ? "True" : "False"));
    FCMD_OBJ_CMD(
        pattern,
        "KeepOrientation = " << (pattern->KeepOrientation.getValue() ? "True" : "False")
    );
}


TaskDlgCurvePatternParameters::TaskDlgCurvePatternParameters(ViewProviderCurvePattern* CurvePatternView)
    : TaskDlgTransformedParameters(CurvePatternView)
{
    parameter = new TaskCurvePatternParameters(CurvePatternView);

    Content.push_back(parameter);
    Content.push_back(new TaskVariedMeasurements(CurvePatternView));
    Content.push_back(new TaskMergeResultParameters(CurvePatternView));
    Content.push_back(preview);
}

#include "moc_TaskCurvePatternParameters.cpp"
