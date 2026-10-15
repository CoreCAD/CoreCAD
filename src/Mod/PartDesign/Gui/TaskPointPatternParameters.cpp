// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/FeaturePointPattern.h>

#include "ui_TaskPointPatternParameters.h"
#include "TaskPointPatternParameters.h"

using namespace PartDesignGui;

/* TRANSLATOR PartDesignGui::TaskPointPatternParameters */

TaskPointPatternParameters::TaskPointPatternParameters(
    ViewProviderTransformed* TransformedView,
    QWidget* parent
)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskPointPatternParameters)
{
    setupUI();
}

TaskPointPatternParameters::~TaskPointPatternParameters() = default;

void TaskPointPatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    connect(
        ui->comboPoints,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskPointPatternParameters::onSketchChosen
    );

    fillSketches();
    updateUI();
}

void TaskPointPatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
    fillSketches();
    updateUI();
}

// Every sketch with a point to copy onto, in timeline order. A sketch built on this pattern
// is left out: the pattern cannot be placed by something made from it.
void TaskPointPatternParameters::fillSketches()
{
    auto* pattern = getObject();
    sketches.clear();
    ui->comboPoints->clear();
    if (!pattern) {
        return;
    }

    const std::vector<App::DocumentObject*> builtOnPattern = pattern->getInListRecursive();
    for (auto* obj : pattern->getDocument()->getObjects()) {
        auto* sketch = freecad_cast<Part::Part2DObject*>(obj);
        if (!sketch || PartDesign::PointPattern::pointsOf(sketch).empty()
            || std::ranges::find(builtOnPattern, obj) != builtOnPattern.end()) {
            continue;
        }
        sketches.push_back(obj);
        ui->comboPoints->addItem(QString::fromUtf8(obj->Label.getValue()));
    }
    sketches.push_back(nullptr);
    ui->comboPoints->addItem(tr("Select sketch…"));
}

void TaskPointPatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto* chosen = getObject<PartDesign::PointPattern>()->Points.getValue();
    auto it = std::ranges::find(sketches, chosen);
    if (chosen && it == sketches.end()) {
        // Chosen elsewhere, by a script or before its points were drawn.
        it = sketches.insert(sketches.end() - 1, chosen);
        ui->comboPoints->insertItem(
            static_cast<int>(it - sketches.begin()),
            QString::fromUtf8(chosen->Label.getValue())
        );
    }
    ui->comboPoints->setCurrentIndex(chosen ? static_cast<int>(it - sketches.begin()) : -1);

    blockUpdate = false;
}

void TaskPointPatternParameters::usePoints(App::DocumentObject* sketch)
{
    setupTransaction();
    getObject<PartDesign::PointPattern>()->Points.setValue(sketch);
    recomputeFeature();
    updateUI();
}

void TaskPointPatternParameters::onSketchChosen(int index)
{
    if (blockUpdate || index < 0 || index >= static_cast<int>(sketches.size())) {
        return;
    }
    if (App::DocumentObject* sketch = sketches[index]) {
        exitSelectionMode();
        usePoints(sketch);
        return;
    }
    hideObject();
    showBase();
    selectionMode = SelectionMode::Reference;
    Gui::Selection().clearSelection();
    Gui::getMainWindow()->showMessage(tr("Select a sketch with points"));
}

void TaskPointPatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
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
    auto* sketch = freecad_cast<Part::Part2DObject*>(msg.Object.getSubObject());
    if (!sketch || PartDesign::PointPattern::pointsOf(sketch).empty()) {
        Gui::getMainWindow()->showMessage(
            tr("That is not a sketch with points. Select a sketch with points")
        );
        return;
    }
    exitSelectionMode();
    Gui::getMainWindow()->showMessage(QString());
    usePoints(sketch);
}

void TaskPointPatternParameters::onUpdateView(bool on)
{
    blockUpdate = !on;
    if (on) {
        setupTransaction();
        recomputeFeature();
    }
}

void TaskPointPatternParameters::apply()
{
    auto* sketch = getObject<PartDesign::PointPattern>()->Points.getValue();
    FCMD_OBJ_CMD(
        getObject(),
        "Points = " << (sketch ? Gui::Command::getObjectCmd(sketch) : std::string("None"))
    );
}


TaskDlgPointPatternParameters::TaskDlgPointPatternParameters(ViewProviderPointPattern* PointPatternView)
    : TaskDlgTransformedParameters(PointPatternView)
{
    parameter = new TaskPointPatternParameters(PointPatternView);

    Content.push_back(parameter);
    Content.push_back(new TaskMergeResultParameters(PointPatternView));
    Content.push_back(preview);
}

#include "moc_TaskPointPatternParameters.cpp"
