// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/FeatureFillPattern.h>

#include "ReferenceSelection.h"
#include "ui_TaskFillPatternParameters.h"
#include "TaskVariedMeasurements.h"
#include "TaskFillPatternParameters.h"

using namespace PartDesignGui;

/* TRANSLATOR PartDesignGui::TaskFillPatternParameters */

TaskFillPatternParameters::TaskFillPatternParameters(
    ViewProviderTransformed* TransformedView,
    QWidget* parent
)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskFillPatternParameters)
{
    setupUI();
}

TaskFillPatternParameters::~TaskFillPatternParameters() = default;

void TaskFillPatternParameters::bindSpin(Gui::QuantitySpinBox* spin, App::PropertyQuantity& prop)
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

void TaskFillPatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    auto* pattern = getObject<PartDesign::FillPattern>();
    bindSpin(ui->spinSpacing, pattern->Spacing);
    bindSpin(ui->spinRowSpacing, pattern->RowSpacing);
    bindSpin(ui->spinMargin, pattern->Margin);
    bindSpin(ui->spinAngle, pattern->Angle);

    connect(
        ui->comboBoundary,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskFillPatternParameters::onBoundaryChosen
    );
    connect(
        ui->comboLayout,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskFillPatternParameters::onLayoutChosen
    );

    fillBoundaries();
    updateUI();
}

void TaskFillPatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
    fillBoundaries();
    updateUI();
}

// Every sketch that outlines a flat region, in timeline order. A sketch built on this
// pattern is left out: the pattern cannot be placed by something made from it.
void TaskFillPatternParameters::fillBoundaries()
{
    auto* pattern = getObject();
    boundaries.clear();
    ui->comboBoundary->clear();
    if (!pattern) {
        return;
    }

    const std::vector<App::DocumentObject*> builtOnPattern = pattern->getInListRecursive();
    for (auto* obj : pattern->getDocument()->getObjects()) {
        if (!obj->isDerivedFrom<Part::Part2DObject>() || !PartDesign::FillPattern::isBoundary(obj)
            || std::ranges::find(builtOnPattern, obj) != builtOnPattern.end()) {
            continue;
        }
        boundaries.push_back(obj);
        ui->comboBoundary->addItem(QString::fromUtf8(obj->Label.getValue()));
    }
    boundaries.push_back(nullptr);
    ui->comboBoundary->addItem(tr("Select faces…"));
}

void TaskFillPatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto* pattern = getObject<PartDesign::FillPattern>();
    fillBoundaries();
    App::DocumentObject* chosen = pattern->Boundary.getValue();
    const std::vector<std::string> faces = pattern->Boundary.getSubValues();
    const bool wholeObject = std::ranges::all_of(faces, &std::string::empty);
    auto it = std::ranges::find(boundaries, chosen);
    if (chosen && (!wholeObject || it == boundaries.end())) {
        // Faces picked in the view, or a boundary chosen by a script.
        it = boundaries.insert(boundaries.end() - 1, chosen);
        ui->comboBoundary->insertItem(
            static_cast<int>(it - boundaries.begin()),
            wholeObject ? QString::fromUtf8(chosen->Label.getValue())
                : faces.size() == 1
                ? tr("%1, 1 face").arg(QString::fromUtf8(chosen->Label.getValue()))
                : tr("%1, %2 faces").arg(QString::fromUtf8(chosen->Label.getValue())).arg(faces.size())
        );
    }
    ui->comboBoundary->setCurrentIndex(chosen ? static_cast<int>(it - boundaries.begin()) : -1);

    ui->comboLayout->setCurrentIndex(pattern->Layout.getValue());
    ui->spinSpacing->setValue(pattern->Spacing.getValue());
    ui->spinRowSpacing->setValue(pattern->RowSpacing.getValue());
    ui->spinMargin->setValue(pattern->Margin.getValue());
    ui->spinAngle->setValue(pattern->Angle.getValue());

    blockUpdate = false;
}

void TaskFillPatternParameters::changed()
{
    recomputeFeature();
    updateUI();
}

void TaskFillPatternParameters::useBoundary(
    App::DocumentObject* obj,
    const std::vector<std::string>& faces
)
{
    setupTransaction();
    getObject<PartDesign::FillPattern>()->Boundary.setValue(obj, faces);
    changed();
}

void TaskFillPatternParameters::onBoundaryChosen(int index)
{
    if (blockUpdate || index < 0 || index >= static_cast<int>(boundaries.size())) {
        return;
    }
    auto* pattern = getObject<PartDesign::FillPattern>();
    if (App::DocumentObject* obj = boundaries[index]) {
        exitSelectionMode();
        if (obj != pattern->Boundary.getValue()
            || !std::ranges::all_of(pattern->Boundary.getSubValues(), &std::string::empty)) {
            useBoundary(obj, {});
        }
        return;
    }
    hideObject();
    showBase();
    selectionMode = SelectionMode::Reference;
    addReferenceSelectionGate(AllowSelection::FACE);
    Gui::Selection().clearSelection();
    Gui::getMainWindow()->showMessage(tr("Select the flat faces to fill"));
}

void TaskFillPatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
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
    auto* pattern = getObject<PartDesign::FillPattern>();
    App::DocumentObject* obj = nullptr;
    std::vector<std::string> picked;
    if (!getReferencedSelection(pattern, msg, obj, picked) || !obj || picked.empty()
        || picked.front().compare(0, 4, "Face") != 0) {
        return;
    }

    // Each face picked on the same object joins the boundary; picking it again leaves it out.
    std::vector<std::string> faces;
    if (obj == pattern->Boundary.getValue()) {
        faces = pattern->Boundary.getSubValues();
        std::erase(faces, std::string());
    }
    if (auto it = std::ranges::find(faces, picked.front()); it != faces.end()) {
        faces.erase(it);
    }
    else {
        faces.push_back(picked.front());
    }
    useBoundary(faces.empty() ? nullptr : obj, faces);
}

void TaskFillPatternParameters::onLayoutChosen(int index)
{
    if (blockUpdate) {
        return;
    }
    setupTransaction();
    getObject<PartDesign::FillPattern>()->Layout.setValue(index);
    changed();
}

void TaskFillPatternParameters::apply()
{
    auto* pattern = getObject<PartDesign::FillPattern>();
    App::DocumentObject* boundary = pattern->Boundary.getValue();
    FCMD_OBJ_CMD(
        pattern,
        "Boundary = "
            << (boundary ? "(" + Gui::Command::getObjectCmd(boundary) + ", "
                        + buildLinkSubPythonStr(boundary, pattern->Boundary.getSubValues()) + ")"
                         : std::string("None"))
    );
    FCMD_OBJ_CMD(pattern, "Layout = " << pattern->Layout.getValue());
    ui->spinSpacing->apply();
    ui->spinRowSpacing->apply();
    ui->spinMargin->apply();
    ui->spinAngle->apply();
}


TaskDlgFillPatternParameters::TaskDlgFillPatternParameters(ViewProviderFillPattern* FillPatternView)
    : TaskDlgTransformedParameters(FillPatternView)
{
    parameter = new TaskFillPatternParameters(FillPatternView);

    Content.push_back(parameter);
    Content.push_back(new TaskVariedMeasurements(FillPatternView));
    Content.push_back(new TaskMergeResultParameters(FillPatternView));
    Content.push_back(preview);
}

#include "moc_TaskFillPatternParameters.cpp"
