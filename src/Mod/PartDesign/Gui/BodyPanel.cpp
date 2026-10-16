// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/DockWindowManager.h>
#include <Gui/Document.h>
#include <Gui/OverlayManager.h>
#include <Gui/Tree.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Mod/Material/App/Materials.h>
#include <Mod/Part/App/BodyBase.h>
#include <Mod/Part/App/MaterialExtension.h>

#include "BodyPanel.h"

using namespace PartDesignGui;

QPointer<BodyPanel> BodyPanel::instance;

namespace
{
constexpr int EntryRole = Qt::UserRole;

/// The body a selected object stands for: a body itself, or the one body a step builds.
App::DocumentObject* bodyMeantBy(App::DocumentObject* obj)
{
    if (freecad_cast<Part::BodyBase*>(obj)) {
        return obj;
    }
    const auto built = Part::BodyBase::findBodiesBuiltBy(obj);
    return built.size() == 1 ? built.front() : nullptr;
}
}  // namespace

BodyPanel::BodyPanel(QWidget* parent)
    : QWidget(parent)
    , SelectionObserver(true)
{
    auto* layout = new QVBoxLayout(this);

    pin = new QAction(Gui::BitmapFactory().iconFromTheme("PartDesign_PanelPin"), tr("Pin"), this);
    pin->setCheckable(true);
    pin->setToolTip(tr("Keeps the panel on this body instead of following the selection"));

    emptyState = new QLabel(tr("Select a body to see what it is and what uses it."), this);
    emptyState->setWordWrap(true);
    layout->addWidget(emptyState);

    details = new QWidget(this);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout();
    name = new QLineEdit(details);
    form->addRow(tr("Name"), name);
    colour = new QLabel(details);
    form->addRow(tr("Colour"), colour);
    material = new QLabel(details);
    form->addRow(tr("Made of"), material);
    shown = new QCheckBox(tr("Shown"), details);
    form->addRow(QString(), shown);
    detailsLayout->addLayout(form);

    auto* appearance = new QPushButton(tr("Appearance and material…"), details);
    detailsLayout->addWidget(appearance, 0, Qt::AlignLeft);

    usedByHeading = new QLabel(details);
    QFont bold = usedByHeading->font();
    bold.setBold(true);
    usedByHeading->setFont(bold);
    detailsLayout->addWidget(usedByHeading);
    usedBy = new QTreeWidget(details);
    usedBy->setHeaderHidden(true);
    usedBy->setRootIsDecorated(false);
    usedBy->setMouseTracking(true);
    detailsLayout->addWidget(usedBy);
    layout->addWidget(details);

    connect(name, &QLineEdit::editingFinished, this, &BodyPanel::rename);
    connect(shown, &QCheckBox::toggled, this, &BodyPanel::setShown);
    connect(appearance, &QPushButton::clicked, this, [this]() {
        if (auto* current = body.get<App::DocumentObject>()) {
            selecting = true;
            Gui::Selection().clearSelection();
            Gui::Selection().addSelection(
                current->getDocument()->getName(),
                current->getNameInDocument()
            );
            selecting = false;
            Gui::Application::Instance->commandManager().runCommandByName("Part_EditAppearance");
        }
    });
    connect(usedBy, &QTreeWidget::itemClicked, this, &BodyPanel::selectEntry);
    connect(usedBy, &QTreeWidget::itemDoubleClicked, this, &BodyPanel::editEntry);
    connect(usedBy, &QTreeWidget::itemEntered, this, &BodyPanel::preselectEntry);
    connect(pin, &QAction::toggled, this, [this](bool pinned) {
        if (!pinned) {
            const auto selection = Gui::Selection().getSelection();
            if (selection.size() == 1) {
                if (auto* meant = bodyMeantBy(selection.front().pObject)) {
                    setBody(meant);
                }
            }
        }
    });

    auto& app = App::GetApplication();
    recomputed = app.signalRecomputed.connect([this](const App::Document&) { scheduleRefresh(); });
    changed = app.signalChangedObject.connect(
        [this](const App::DocumentObject& obj, const App::Property&) {
            if (body.get<App::DocumentObject>() == &obj) {
                scheduleRefresh();
            }
        }
    );
    undone = app.signalUndo.connect([this]() { scheduleRefresh(); });
    redone = app.signalRedo.connect([this]() { scheduleRefresh(); });
    deleted = app.signalDeletedObject.connect([this](const App::DocumentObject& obj) {
        if (body.get<App::DocumentObject>() == &obj) {
            body.reset();
        }
        scheduleRefresh();
    });
    documentClosed = app.signalDeleteDocument.connect([this](const App::Document&) {
        scheduleRefresh();
    });
}

void BodyPanel::showFor(App::DocumentObject* body)
{
    if (!instance) {
        instance = new BodyPanel();
        instance->dock = Gui::DockWindowManager::instance()
                             ->addDockWindow("Body", instance, Qt::RightDockWidgetArea);
        instance->dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable
        );
        instance->dock->addAction(instance->pin);
        Gui::OverlayManager::instance()->setupTitleBar(instance->dock);
    }
    instance->pin->setChecked(false);
    instance->setBody(body);
    instance->dock->show();
    instance->dock->raise();
}

void BodyPanel::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    using Type = Gui::SelectionChanges::MsgType;
    const bool selectionChanged = msg.Type == Type::AddSelection || msg.Type == Type::RmvSelection
        || msg.Type == Type::SetSelection || msg.Type == Type::ClrSelection;
    if (!selectionChanged || selecting || pin->isChecked() || !isVisible()) {
        return;
    }
    // Anything that does not stand for one body leaves the panel where it is.
    const auto selection = Gui::Selection().getSelection();
    if (selection.size() != 1) {
        return;
    }
    auto* meant = bodyMeantBy(selection.front().pObject);
    if (meant && meant != body.get<App::DocumentObject>()) {
        setBody(meant);
    }
}

void BodyPanel::setBody(App::DocumentObject* newBody)
{
    body = newBody;
    refresh();
}

void BodyPanel::scheduleRefresh()
{
    if (refreshPending) {
        return;
    }
    refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        refreshPending = false;
        refresh();
    });
}

void BodyPanel::refresh()
{
    usedBy->clear();
    entries.clear();
    auto* current = freecad_cast<Part::BodyBase*>(body.get<App::DocumentObject>());
    if (dock) {
        dock->setWindowTitle(
            current ? tr("Body — %1").arg(QString::fromUtf8(current->Label.getValue())) : tr("Body")
        );
    }
    emptyState->setVisible(!current);
    details->setVisible(current);
    if (!current) {
        return;
    }

    const QString label = QString::fromUtf8(current->Label.getValue());
    if (!name->hasFocus() && name->text() != label) {
        name->setText(label);
    }

    const Base::Color identity = current->getIdentityColor();
    const QColor swatchColour = QColor::fromRgbF(identity.r, identity.g, identity.b);
    const int side = name->fontMetrics().height();
    QPixmap swatch(side, side);
    swatch.fill(swatchColour);
    colour->setPixmap(swatch);
    colour->setToolTip(swatchColour.name());

    const Materials::Material* madeOf = Part::materialOfPart(current);
    material->setText(madeOf ? madeOf->getName() : tr("Nothing chosen"));

    {
        const QSignalBlocker block(shown);
        shown->setChecked(current->Visibility.getValue());
    }

    const auto users = Gui::stepsUsingBody(current);
    usedByHeading->setText(tr("Used by (%1)").arg(users.size()));
    if (users.empty()) {
        auto* item = new QTreeWidgetItem(usedBy, {tr("Nothing in another body uses this body.")});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    for (auto* step : users) {
        auto* item = new QTreeWidgetItem(usedBy, {QString::fromUtf8(step->Label.getValue())});
        if (auto* vp = Gui::Application::Instance->getViewProvider(step)) {
            item->setIcon(0, vp->getIcon());
        }
        item->setData(0, EntryRole, static_cast<int>(entries.size()));
        entries.push_back(std::make_unique<App::DocumentObjectWeakPtrT>(step));
    }
}

void BodyPanel::rename()
{
    auto* current = body.get<App::DocumentObject>();
    const QString label = name->text().trimmed();
    if (!current || label.isEmpty()) {
        scheduleRefresh();
        return;
    }
    if (label == QString::fromUtf8(current->Label.getValue())) {
        return;
    }
    App::AutoTransaction committer(current->getDocument(), QT_TRANSLATE_NOOP("Command", "Rename"));
    current->Label.setValue(label.toUtf8().constData());
}

void BodyPanel::setShown(bool show)
{
    if (auto* current = body.get<App::DocumentObject>()) {
        current->Visibility.setValue(show);
    }
}

App::DocumentObject* BodyPanel::objectAt(QTreeWidgetItem* item) const
{
    const QVariant index = item ? item->data(0, EntryRole) : QVariant();
    if (!index.isValid()) {
        return nullptr;
    }
    return entries.at(index.toInt())->get<App::DocumentObject>();
}

void BodyPanel::selectEntry(QTreeWidgetItem* item)
{
    auto* obj = objectAt(item);
    if (!obj) {
        return;
    }
    selecting = true;
    Gui::Selection().clearSelection();
    Gui::Selection().addSelection(obj->getDocument()->getName(), obj->getNameInDocument());
    selecting = false;
}

void BodyPanel::preselectEntry(QTreeWidgetItem* item)
{
    if (auto* obj = objectAt(item)) {
        Gui::Selection().setPreselect(obj->getDocument()->getName(), obj->getNameInDocument(), "");
    }
    else {
        Gui::Selection().rmvPreselect();
    }
}

void BodyPanel::leaveEvent(QEvent* event)
{
    Gui::Selection().rmvPreselect();
    QWidget::leaveEvent(event);
}

void BodyPanel::editEntry(QTreeWidgetItem* item)
{
    auto* obj = objectAt(item);
    auto* vp = obj ? freecad_cast<Gui::ViewProviderDocumentObject*>(
                         Gui::Application::Instance->getViewProvider(obj)
                     )
                   : nullptr;
    if (!vp) {
        return;
    }
    vp->getDocument()->setActiveView(vp);
    if (const char* text = vp->getTransactionText()) {
        obj->getDocument()->openTransaction(text);
    }
    vp->doubleClicked();
}

#include "moc_BodyPanel.cpp"
