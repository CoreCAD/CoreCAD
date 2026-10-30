// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QRegularExpression>

#include <App/Datums.h>
#include <App/PropertyUnits.h>
#include <App/Range.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Interpreter.h>
#include <Base/Quantity.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/ViewProvider.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/PartDesign/App/FeatureTablePattern.h>

#include "ReferenceSelection.h"
#include "ui_TaskTablePatternParameters.h"
#include "TaskVariedMeasurements.h"
#include "TaskTablePatternParameters.h"

using namespace PartDesignGui;

/* TRANSLATOR PartDesignGui::TaskTablePatternParameters */

namespace
{

constexpr int fromPlane = 1;

QString lengthText(double value)
{
    return QString::fromStdString(Base::Quantity(value, Base::Unit::Length).getUserString());
}

// A bare number is in the unit lengths are shown in, as in the panel's number boxes.
std::optional<double> parseLength(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return std::nullopt;
    }
    try {
        const Base::Quantity quantity = Base::Quantity::parse(text.trimmed().toStdString());
        if (!quantity.isValid()) {
            return std::nullopt;
        }
        if (quantity.getUnit() == Base::Unit::Length) {
            return quantity.getValue();
        }
        if (quantity.getUnit() == Base::Unit::One) {
            double factor = 1.0;
            std::string unit;
            Base::Quantity(1.0, Base::Unit::Length).getUserString(factor, unit);
            return quantity.getValue() * factor;
        }
    }
    catch (const Base::Exception&) {
    }
    return std::nullopt;
}

double& component(Base::Vector3d& row, int column)
{
    return column == 0 ? row.x : column == 1 ? row.y : row.z;
}

bool isSpreadsheet(const App::DocumentObject* obj)
{
    return obj->getTypeId().isDerivedFrom(Base::Type::fromName("Spreadsheet::Sheet"));
}

// The smallest range holding the sheet's lengths, at most three columns wide from the first.
std::string cellsHoldingLengths(const App::DocumentObject* sheet)
{
    std::vector<std::pair<const char*, App::Property*>> properties;
    sheet->getPropertyNamedList(properties);
    std::optional<App::Range> found;
    for (const auto& [name, property] : properties) {
        App::CellAddress cell = App::stringToAddress(name, true);
        const auto* quantity = freecad_cast<App::PropertyQuantity*>(property);
        if (!cell.isValid() || !quantity
            || quantity->getQuantityValue().getUnit() != Base::Unit::Length) {
            continue;
        }
        found = found ? App::Range(
                            std::min(found->from().row(), cell.row()),
                            std::min(found->from().col(), cell.col()),
                            std::max(found->to().row(), cell.row()),
                            std::max(found->to().col(), cell.col())
                        )
                      : App::Range(cell.row(), cell.col(), cell.row(), cell.col());
    }
    if (!found) {
        return "A1:C1";
    }
    const int lastColumn = std::min(found->to().col(), found->from().col() + 2);
    return App::Range(found->from().row(), found->from().col(), found->to().row(), lastColumn)
        .rangeString();
}

}  // namespace

TaskTablePatternParameters::TaskTablePatternParameters(
    ViewProviderTransformed* TransformedView,
    QWidget* parent
)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskTablePatternParameters)
{
    setupUI();
}

TaskTablePatternParameters::~TaskTablePatternParameters() = default;

void TaskTablePatternParameters::setupParameterUI(QWidget* widget)
{
    ui->setupUi(widget);
    QMetaObject::connectSlotsByName(this);

    connect(
        ui->comboMeasuredFrom,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskTablePatternParameters::onMeasuredFromChosen
    );
    connect(
        ui->comboRowsFrom,
        qOverload<int>(&QComboBox::activated),
        this,
        &TaskTablePatternParameters::onRowsFromChosen
    );
    connect(ui->editCells, &QLineEdit::editingFinished, this, &TaskTablePatternParameters::onCellsEdited);
    connect(ui->tableRows, &QTableWidget::cellChanged, this, &TaskTablePatternParameters::onCellEdited);
    connect(ui->buttonAddRow, &QPushButton::clicked, this, &TaskTablePatternParameters::onAddRow);
    connect(ui->buttonRemoveRow, &QPushButton::clicked, this, &TaskTablePatternParameters::onRemoveRow);
    ui->tableRows->installEventFilter(this);

    fillPlanes();
    updateUI();
}

void TaskTablePatternParameters::retranslateParameterUI(QWidget* widget)
{
    ui->retranslateUi(widget);
    fillPlanes();
    updateUI();
}

// The original first; then the world's planes, datum planes and sketches in timeline order.
// Something built on this pattern is left out: the pattern cannot be placed by it.
void TaskTablePatternParameters::fillPlanes()
{
    auto* pattern = getObject();
    planes.clear();
    ui->comboMeasuredFrom->clear();
    if (!pattern) {
        return;
    }

    planes.push_back(nullptr);
    ui->comboMeasuredFrom->addItem(tr("The original"));

    const std::vector<App::DocumentObject*> builtOnPattern = pattern->getInListRecursive();
    auto add = [this, &builtOnPattern](App::DocumentObject* obj) {
        if (std::ranges::find(builtOnPattern, obj) != builtOnPattern.end()) {
            return;
        }
        planes.push_back(obj);
        QIcon icon;
        if (auto* vp = Gui::Application::Instance->getViewProvider(obj)) {
            icon = vp->getIcon();
        }
        ui->comboMeasuredFrom->addItem(icon, QString::fromUtf8(obj->Label.getValue()));
    };
    const std::vector<App::DocumentObject*> objects = pattern->getDocument()->getObjects();
    for (auto* obj : objects) {
        auto* plane = freecad_cast<App::Plane*>(obj);
        if (plane && plane->isOriginFeature()) {
            add(obj);
        }
    }
    for (auto* obj : objects) {
        auto* plane = freecad_cast<App::Plane*>(obj);
        if ((plane && !plane->isOriginFeature()) || obj->isDerivedFrom<Part::Part2DObject>()) {
            add(obj);
        }
    }

    planes.push_back(nullptr);
    ui->comboMeasuredFrom->addItem(tr("Select face…"));
}

void TaskTablePatternParameters::fillSpreadsheets()
{
    auto* pattern = getObject();
    spreadsheets.clear();
    ui->comboRowsFrom->clear();
    if (!pattern) {
        return;
    }
    spreadsheets.push_back(nullptr);
    ui->comboRowsFrom->addItem(tr("Typed rows"));
    for (auto* obj : pattern->getDocument()->getObjects()) {
        if (!isSpreadsheet(obj)) {
            continue;
        }
        spreadsheets.push_back(obj);
        QIcon icon;
        if (auto* vp = Gui::Application::Instance->getViewProvider(obj)) {
            icon = vp->getIcon();
        }
        ui->comboRowsFrom->addItem(icon, QString::fromUtf8(obj->Label.getValue()));
    }
}

void TaskTablePatternParameters::updateUI()
{
    if (blockUpdate) {
        return;
    }
    blockUpdate = true;

    auto* pattern = getObject<PartDesign::TablePattern>();
    fillPlanes();
    const bool onPlane = pattern->MeasuredFrom.getValue() == fromPlane;
    App::DocumentObject* chosen = onPlane ? pattern->Plane.getValue() : nullptr;
    int index = 0;
    if (chosen) {
        const std::vector<std::string> subs = pattern->Plane.getSubValues();
        const bool whole = subs.empty() || subs.front().empty();
        auto it = std::find(planes.begin() + 1, planes.end() - 1, chosen);
        if (!whole || it == planes.end() - 1) {
            // A face picked in the view, or a plane chosen by a script.
            it = planes.insert(planes.end() - 1, chosen);
            const QString label = QString::fromUtf8(chosen->Label.getValue());
            ui->comboMeasuredFrom->insertItem(
                static_cast<int>(it - planes.begin()),
                whole ? label : tr("%1, %2").arg(label, QString::fromStdString(subs.front()))
            );
        }
        index = static_cast<int>(it - planes.begin());
    }
    else if (onPlane) {
        index = -1;
    }
    ui->comboMeasuredFrom->setCurrentIndex(index);

    fillSpreadsheets();
    App::DocumentObject* sheet = pattern->Spreadsheet.getValue();
    const auto source = std::ranges::find(spreadsheets, sheet);
    ui->comboRowsFrom->setCurrentIndex(
        source == spreadsheets.end() ? -1 : static_cast<int>(source - spreadsheets.begin())
    );
    ui->labelCells->setVisible(sheet);
    ui->editCells->setVisible(sheet);
    ui->editCells->setText(QString::fromStdString(pattern->Cells.getStrValue()));
    ui->buttonAddRow->setVisible(!sheet);
    ui->buttonRemoveRow->setVisible(!sheet);
    ui->tableRows->setEditTriggers(
        sheet ? QAbstractItemView::NoEditTriggers : QAbstractItemView::AllEditTriggers
    );

    std::vector<Base::Vector3d> rows;
    if (sheet) {
        try {
            rows = pattern->rowsFromSpreadsheet();
        }
        catch (const Base::Exception&) {
        }
    }
    else {
        rows = pattern->Rows.getValues();
    }
    ui->tableRows->setColumnHidden(2, onPlane);
    ui->tableRows->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        Base::Vector3d values = rows[row];
        for (int column = 0; column < 3; ++column) {
            ui->tableRows
                ->setItem(row, column, new QTableWidgetItem(lengthText(component(values, column))));
        }
    }
    ui->buttonRemoveRow->setEnabled(!rows.empty());

    blockUpdate = false;
}

void TaskTablePatternParameters::useRows(const std::vector<Base::Vector3d>& rows)
{
    setupTransaction();
    getObject<PartDesign::TablePattern>()->Rows.setValues(rows);
    recomputeFeature();
    updateUI();
}

void TaskTablePatternParameters::usePlane(App::DocumentObject* obj, const std::vector<std::string>& subs)
{
    setupTransaction();
    auto* pattern = getObject<PartDesign::TablePattern>();
    pattern->Plane.setValue(obj, subs);
    pattern->MeasuredFrom.setValue(obj ? fromPlane : 0);
    recomputeFeature();
    updateUI();
}

void TaskTablePatternParameters::onMeasuredFromChosen(int index)
{
    if (blockUpdate || index < 0 || index >= static_cast<int>(planes.size())) {
        return;
    }
    if (index == 0 || planes[index]) {
        exitSelectionMode();
        auto* pattern = getObject<PartDesign::TablePattern>();
        const bool onPlane = pattern->MeasuredFrom.getValue() == fromPlane;
        if ((onPlane ? pattern->Plane.getValue() : nullptr) != planes[index]) {
            usePlane(planes[index], {});
        }
        return;
    }
    hideObject();
    showBase();
    selectionMode = SelectionMode::Reference;
    addReferenceSelectionGate(AllowSelection::FACE | AllowSelection::PLANAR);
    Gui::Selection().clearSelection();
    Gui::getMainWindow()->showMessage(tr("Select a flat face to measure the positions on"));
}

void TaskTablePatternParameters::onRowsFromChosen(int index)
{
    if (blockUpdate || index < 0 || index >= static_cast<int>(spreadsheets.size())) {
        return;
    }
    auto* pattern = getObject<PartDesign::TablePattern>();
    App::DocumentObject* sheet = spreadsheets[index];
    if (pattern->Spreadsheet.getValue() == sheet) {
        return;
    }
    setupTransaction();
    pattern->Spreadsheet.setValue(sheet);
    if (sheet && pattern->Cells.getStrValue().empty()) {
        pattern->Cells.setValue(cellsHoldingLengths(sheet));
    }
    recomputeFeature();
    updateUI();
}

void TaskTablePatternParameters::onCellsEdited()
{
    auto* pattern = getObject<PartDesign::TablePattern>();
    const std::string cells = ui->editCells->text().trimmed().toUpper().toStdString();
    if (blockUpdate || cells == pattern->Cells.getStrValue()) {
        return;
    }
    setupTransaction();
    pattern->Cells.setValue(cells);
    recomputeFeature();
    updateUI();
}

void TaskTablePatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
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
    App::DocumentObject* obj = nullptr;
    std::vector<std::string> picked;
    if (!getReferencedSelection(getObject(), msg, obj, picked) || !obj || picked.empty()
        || picked.front().compare(0, 4, "Face") != 0) {
        return;
    }
    exitSelectionMode();
    Gui::getMainWindow()->showMessage(QString());
    usePlane(obj, {picked.front()});
}

void TaskTablePatternParameters::onCellEdited(int row, int column)
{
    if (blockUpdate) {
        return;
    }
    std::vector<Base::Vector3d> rows = getObject<PartDesign::TablePattern>()->Rows.getValues();
    if (row >= static_cast<int>(rows.size())) {
        return;
    }
    const std::optional<double> value = parseLength(ui->tableRows->item(row, column)->text());
    if (!value) {
        updateUI();
        return;
    }
    component(rows[row], column) = *value;
    useRows(rows);
}

// A new row continues the step between the last two, so a run of copies grows by repeating
// Add row.
void TaskTablePatternParameters::onAddRow()
{
    std::vector<Base::Vector3d> rows = getObject<PartDesign::TablePattern>()->Rows.getValues();
    const Base::Vector3d step = rows.size() >= 2 ? rows.back() - rows[rows.size() - 2]
                                                 : Base::Vector3d(10, 0, 0);
    rows.push_back(rows.empty() ? step : rows.back() + step);
    useRows(rows);
    ui->tableRows->setCurrentCell(static_cast<int>(rows.size()) - 1, 0);
}

void TaskTablePatternParameters::onRemoveRow()
{
    std::vector<Base::Vector3d> rows = getObject<PartDesign::TablePattern>()->Rows.getValues();
    if (rows.empty()) {
        return;
    }
    const int current = ui->tableRows->currentRow();
    rows.erase(
        rows.begin()
        + (current >= 0 && current < static_cast<int>(rows.size()) ? current : rows.size() - 1)
    );
    useRows(rows);
}

// Spreadsheets copy cells split by tabs; text files use semicolons where a comma is the
// decimal mark, else commas.
std::vector<std::vector<std::optional<double>>> TaskTablePatternParameters::parseCells(
    const QString& text
)
{
    std::vector<std::vector<std::optional<double>>> cells;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QChar cellBreak = line.contains(QLatin1Char('\t')) ? QLatin1Char('\t')
            : line.contains(QLatin1Char(';'))                    ? QLatin1Char(';')
                                                                 : QLatin1Char(',');
        std::vector<std::optional<double>> values;
        for (const QString& cell : line.split(cellBreak)) {
            values.push_back(parseLength(cell));
        }
        if (std::ranges::any_of(values, [](const auto& value) { return value.has_value(); })) {
            cells.push_back(values);
        }
    }
    return cells;
}

// Pasted cells overwrite from the current cell rightward and down, adding rows as needed; a
// cell that is not a length leaves its place unchanged.
void TaskTablePatternParameters::paste()
{
    const auto cells = parseCells(QApplication::clipboard()->text());
    if (cells.empty()) {
        return;
    }
    auto* pattern = getObject<PartDesign::TablePattern>();
    std::vector<Base::Vector3d> rows = pattern->Rows.getValues();
    const int columns = pattern->MeasuredFrom.getValue() == fromPlane ? 2 : 3;
    const int firstRow = std::max(ui->tableRows->currentRow(), 0);
    const int firstColumn = std::clamp(ui->tableRows->currentColumn(), 0, columns - 1);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const std::size_t row = firstRow + i;
        if (row >= rows.size()) {
            rows.resize(row + 1);
        }
        for (std::size_t j = 0; j < cells[i].size() && firstColumn + j < std::size_t(columns); ++j) {
            if (cells[i][j]) {
                component(rows[row], firstColumn + static_cast<int>(j)) = *cells[i][j];
            }
        }
    }
    useRows(rows);
}

bool TaskTablePatternParameters::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->tableRows && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Paste)) {
        paste();
        return true;
    }
    return TaskTransformedParameters::eventFilter(watched, event);
}

void TaskTablePatternParameters::apply()
{
    auto* pattern = getObject<PartDesign::TablePattern>();
    App::DocumentObject* plane = pattern->Plane.getValue();
    FCMD_OBJ_CMD(
        pattern,
        "Plane = "
            << (plane ? "(" + Gui::Command::getObjectCmd(plane) + ", "
                        + buildLinkSubPythonStr(plane, pattern->Plane.getSubValues()) + ")"
                      : std::string("None"))
    );
    FCMD_OBJ_CMD(pattern, "MeasuredFrom = " << pattern->MeasuredFrom.getValue());
    std::ostringstream rows;
    rows.precision(std::numeric_limits<double>::max_digits10);
    rows << "[";
    for (const Base::Vector3d& row : pattern->Rows.getValues()) {
        rows << "FreeCAD.Vector(" << row.x << ", " << row.y << ", " << row.z << "), ";
    }
    rows << "]";
    FCMD_OBJ_CMD(pattern, "Rows = " << rows.str());
    App::DocumentObject* sheet = pattern->Spreadsheet.getValue();
    FCMD_OBJ_CMD(
        pattern,
        "Spreadsheet = " << (sheet ? Gui::Command::getObjectCmd(sheet) : std::string("None"))
    );
    FCMD_OBJ_CMD(
        pattern,
        "Cells = \"" << Base::InterpreterSingleton::strToPython(pattern->Cells.getValue()) << "\""
    );
}


TaskDlgTablePatternParameters::TaskDlgTablePatternParameters(ViewProviderTablePattern* TablePatternView)
    : TaskDlgTransformedParameters(TablePatternView)
{
    parameter = new TaskTablePatternParameters(TablePatternView);

    Content.push_back(parameter);
    Content.push_back(new TaskVariedMeasurements(TablePatternView));
    Content.push_back(new TaskMergeResultParameters(TablePatternView));
    Content.push_back(preview);
}

#include "moc_TaskTablePatternParameters.cpp"
