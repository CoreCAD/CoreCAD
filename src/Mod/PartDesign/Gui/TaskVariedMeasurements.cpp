// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/Document.h>
#include <App/Expression.h>
#include <App/Transactions.h>
#include <App/ObjectIdentifier.h>
#include <Base/Quantity.h>
#include <Base/Tools.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/QuantitySpinBox.h>
#include <Mod/PartDesign/App/FeatureTransformed.h>

#include "TaskVariedMeasurements.h"
#include "ViewProviderTransformed.h"

using namespace PartDesignGui;

namespace
{
/// "Radius (Hole)" for Hole.Constraints.Radius: the measurement, then the step it belongs to.
QString shownName(const App::ObjectIdentifier& target)
{
    const App::DocumentObject* owner = target.getDocumentObject();
    const auto& components = target.getPropertyComponents();
    const QString measurement = QString::fromStdString(
        components.empty() ? target.getPropertyName() : components.back().getName()
    );
    return owner
        ? QStringLiteral("%1 (%2)").arg(measurement, QString::fromUtf8(owner->Label.getValue()))
        : measurement;
}

/// The panel's own changes are undone by Cancel only inside the edit's transaction, which the
/// panel opens at its first change.
void openEditTransaction(App::DocumentObject* pattern)
{
    App::Document* document = pattern->getDocument();
    if (document->getBookedTransactionID() == App::NullTransaction) {
        document->openTransaction((std::string("Edit ") + pattern->Label.getValue()).c_str());
    }
}

/// The formula a new row starts with: one more of the measurement's unit for each copy.
std::string startingFormula(const std::string& target, PartDesign::Transformed* pattern)
{
    const App::ObjectIdentifier path = App::ObjectIdentifier::parse(pattern, target);
    const App::Property* prop = path.getProperty();
    const App::any value = prop ? prop->getPathValue(path) : App::any();
    std::string step = "1";
    if (value.type() == typeid(Base::Quantity)) {
        const Base::Unit unit = App::any_cast<Base::Quantity>(value).getUnit();
        if (unit == Base::Unit::Length) {
            step = "1 mm";
        }
        else if (unit == Base::Unit::Angle) {
            step = "1 deg";
        }
    }
    return target + " + n * " + step;
}
}  // namespace

TaskVariedMeasurements::TaskVariedMeasurements(ViewProviderTransformed* view, QWidget* parent)
    : TaskBox(tr("Changes per copy"), true, parent)
    , view(view)
    , rows(new QGridLayout())
    , emptyLabel(new QLabel(tr("Every copy is the same as the original."), this))
    , choices(new QComboBox(this))
{
    choices->setObjectName(QStringLiteral("comboVaryMeasurement"));
    choices->setPlaceholderText(tr("Change a measurement for each copy"));
    connect(choices, qOverload<int>(&QComboBox::activated), this, &TaskVariedMeasurements::onChosen);

    auto* proxy = new QWidget(this);
    auto* layout = new QVBoxLayout(proxy);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(emptyLabel);
    layout->addLayout(rows);
    layout->addWidget(choices);
    groupLayout()->addWidget(proxy);

    if (PartDesign::Transformed* pattern = this->pattern()) {
        formulaChanged = pattern->ExpressionEngine.expressionChanged.connect(
            [this](const App::ObjectIdentifier&) {
                QTimer::singleShot(0, this, [this]() {
                    if (PartDesign::Transformed* pattern = this->pattern()) {
                        pattern->getDocument()->recompute();
                    }
                });
            }
        );
    }
    showRows();
}

PartDesign::Transformed* TaskVariedMeasurements::pattern() const
{
    return view ? view->getObject<PartDesign::Transformed>() : nullptr;
}

void TaskVariedMeasurements::showRows()
{
    while (QLayoutItem* item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    choices->clear();

    PartDesign::Transformed* pattern = this->pattern();
    if (!pattern) {
        return;
    }
    std::vector<PartDesign::Transformed::VariedMeasurement> varied;
    try {
        varied = pattern->variedMeasurements();
    }
    catch (const Base::Exception&) {
        varied.clear();
    }

    std::vector<std::string> taken;
    for (int index = 0; index < static_cast<int>(varied.size()); ++index) {
        const auto& measurement = varied[index];
        taken.push_back(measurement.target.toString());

        auto* name = new QLabel(shownName(measurement.target), this);
        auto* formula = new Gui::QuantitySpinBox(this);
        formula->setObjectName(QStringLiteral("spinVaried%1").arg(index + 1));
        formula->bind(App::ObjectIdentifier(*measurement.perCopy));
        if (auto* quantity = freecad_cast<App::PropertyQuantity*>(measurement.perCopy)) {
            formula->setValue(quantity->getQuantityValue());
        }
        connect(
            formula,
            qOverload<const Base::Quantity&>(&Gui::QuantitySpinBox::valueChanged),
            this,
            [this, formula, perCopy = measurement.perCopy](const Base::Quantity& value) {
                const App::ObjectIdentifier path(*perCopy);
                if (formula->hasExpression()
                    || App::anyToQuantity(perCopy->getPathValue(path)) == value) {
                    return;
                }
                openEditTransaction(this->pattern());
                perCopy->setPathValue(path, value);
                rebuild();
            }
        );
        auto* remove = new QToolButton(this);
        remove->setObjectName(QStringLiteral("buttonStopVarying%1").arg(index + 1));
        remove->setIcon(Gui::BitmapFactory().iconFromTheme("list-remove"));
        remove->setToolTip(tr("Stop changing this measurement"));
        connect(remove, &QToolButton::clicked, this, [this, index]() { stopVarying(index); });

        rows->addWidget(name, index, 0);
        rows->addWidget(formula, index, 1);
        rows->addWidget(remove, index, 2);
    }
    emptyLabel->setVisible(varied.empty());

    for (const std::string& target : pattern->measurementsToVary()) {
        if (std::ranges::find(taken, target) != taken.end()) {
            continue;
        }
        choices->addItem(
            shownName(App::ObjectIdentifier::parse(pattern, target)),
            QString::fromStdString(target)
        );
    }
    choices->setCurrentIndex(-1);
    choices->setEnabled(choices->count() > 0);
}

void TaskVariedMeasurements::onChosen(int index)
{
    PartDesign::Transformed* pattern = this->pattern();
    if (!pattern || index < 0) {
        return;
    }
    const std::string target = choices->itemData(index).toString().toStdString();
    openEditTransaction(pattern);
    Gui::Command::doCommand(Gui::Command::Doc, "import PartDesign");
    Gui::Command::doCommand(
        Gui::Command::Doc,
        "PartDesign.varyMeasurement(App.getDocument('%s').getObject('%s'), '%s', '%s')",
        pattern->getDocument()->getName(),
        pattern->getNameInDocument(),
        Base::Tools::escapeQuotesFromString(target).c_str(),
        Base::Tools::escapeQuotesFromString(startingFormula(target, pattern)).c_str()
    );
    rebuild();
}

void TaskVariedMeasurements::stopVarying(int index)
{
    PartDesign::Transformed* pattern = this->pattern();
    if (!pattern) {
        return;
    }
    openEditTransaction(pattern);
    Gui::Command::doCommand(Gui::Command::Doc, "import PartDesign");
    Gui::Command::doCommand(
        Gui::Command::Doc,
        "PartDesign.stopVarying(App.getDocument('%s').getObject('%s'), %d)",
        pattern->getDocument()->getName(),
        pattern->getNameInDocument(),
        index
    );
    rebuild();
}

void TaskVariedMeasurements::rebuild()
{
    if (PartDesign::Transformed* pattern = this->pattern()) {
        pattern->getDocument()->recompute();
    }
    // After the click that asked for it has finished, since the row holding it may go.
    QTimer::singleShot(0, this, &TaskVariedMeasurements::showRows);
}

#include "moc_TaskVariedMeasurements.cpp"
