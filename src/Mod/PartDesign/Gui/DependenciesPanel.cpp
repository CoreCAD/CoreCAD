// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include <algorithm>
#include <map>

#include <QDockWidget>
#include <QAction>
#include <QHeaderView>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/DockWindowManager.h>
#include <Gui/Document.h>
#include <Gui/OverlayManager.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Mod/PartDesign/App/Body.h>

#include "DependenciesPanel.h"

using namespace PartDesignGui;

QPointer<DependenciesPanel> DependenciesPanel::instance;

namespace
{
constexpr int EntryRole = Qt::UserRole;

void sortInTimelineOrder(std::vector<App::DocumentObject*>& objects)
{
    if (objects.empty()) {
        return;
    }
    std::map<const App::DocumentObject*, std::size_t> position;
    const auto all = objects.front()->getDocument()->getObjects();
    for (std::size_t i = 0; i < all.size(); ++i) {
        position[all[i]] = i;
    }
    std::stable_sort(objects.begin(), objects.end(), [&](auto* a, auto* b) {
        return position[a] < position[b];
    });
}
}  // namespace

DependenciesPanel::DependenciesPanel(QWidget* parent)
    : QWidget(parent)
    , SelectionObserver(true)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    pin = new QAction(Gui::BitmapFactory().iconFromTheme("PartDesign_PanelPin"), tr("Pin"), this);
    pin->setCheckable(true);
    pin->setToolTip(tr("Keeps the panel on this step instead of following the selection"));

    list = new QTreeWidget(this);
    list->setHeaderHidden(true);
    list->setRootIsDecorated(false);
    list->setMouseTracking(true);
    layout->addWidget(list);

    connect(list, &QTreeWidget::itemClicked, this, &DependenciesPanel::selectEntry);
    connect(list, &QTreeWidget::itemDoubleClicked, this, &DependenciesPanel::editEntry);
    connect(list, &QTreeWidget::itemEntered, this, &DependenciesPanel::preselectEntry);
    connect(pin, &QAction::toggled, this, [this](bool pinned) {
        if (!pinned) {
            const auto selection = Gui::Selection().getSelection();
            if (selection.size() == 1) {
                setStep(selection.front().pObject);
            }
        }
    });

    auto& app = App::GetApplication();
    recomputed = app.signalRecomputed.connect([this](const App::Document&) { scheduleRefresh(); });
    undone = app.signalUndo.connect([this]() { scheduleRefresh(); });
    redone = app.signalRedo.connect([this]() { scheduleRefresh(); });
    deleted = app.signalDeletedObject.connect([this](const App::DocumentObject& obj) {
        if (step.get<App::DocumentObject>() == &obj) {
            step.reset();
        }
        scheduleRefresh();
    });
    documentClosed = app.signalDeleteDocument.connect([this](const App::Document&) {
        scheduleRefresh();
    });
}

void DependenciesPanel::showFor(App::DocumentObject* step)
{
    if (!instance) {
        instance = new DependenciesPanel();
        instance->dock = Gui::DockWindowManager::instance()
                             ->addDockWindow("Dependencies", instance, Qt::RightDockWidgetArea);
        instance->dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable
        );
        instance->dock->addAction(instance->pin);
        Gui::OverlayManager::instance()->setupTitleBar(instance->dock);
    }
    instance->pin->setChecked(false);
    instance->setStep(step);
    instance->dock->show();
    instance->dock->raise();
}

void DependenciesPanel::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    using Type = Gui::SelectionChanges::MsgType;
    const bool selectionChanged = msg.Type == Type::AddSelection || msg.Type == Type::RmvSelection
        || msg.Type == Type::SetSelection || msg.Type == Type::ClrSelection;
    if (!selectionChanged || selecting || pin->isChecked() || !isVisible()) {
        return;
    }
    // A click on an entry selects it without moving the panel; any other single selection does.
    const auto selection = Gui::Selection().getSelection();
    if (selection.size() == 1 && selection.front().pObject != step.get<App::DocumentObject>()) {
        setStep(selection.front().pObject);
    }
}

void DependenciesPanel::setStep(App::DocumentObject* newStep)
{
    step = newStep;
    refresh();
}

void DependenciesPanel::scheduleRefresh()
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

void DependenciesPanel::refresh()
{
    list->clear();
    entries.clear();
    auto* current = step.get<App::DocumentObject>();
    if (dock) {
        dock->setWindowTitle(
            current ? tr("Dependencies — %1").arg(QString::fromUtf8(current->Label.getValue()))
                    : tr("Dependencies")
        );
    }
    if (!current) {
        auto* item
            = new QTreeWidgetItem(list, {tr("Select a step to see what it uses and what uses it.")});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    addSection(tr("Uses"), PartDesign::Body::stepsUsedBy(current), tr("This step uses nothing."));
    addSection(tr("Used by"), PartDesign::Body::stepsUsing(current), tr("Nothing uses this step."));
    list->expandAll();
}

void DependenciesPanel::addSection(
    const QString& heading,
    const std::vector<App::DocumentObject*>& objects,
    const QString& emptyText
)
{
    auto sorted = objects;
    sortInTimelineOrder(sorted);

    auto* header
        = new QTreeWidgetItem(list, {QStringLiteral("%1 (%2)").arg(heading).arg(sorted.size())});
    header->setFlags(Qt::ItemIsEnabled);
    QFont bold = header->font(0);
    bold.setBold(true);
    header->setFont(0, bold);

    if (sorted.empty()) {
        auto* item = new QTreeWidgetItem(header, {emptyText});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    for (auto* obj : sorted) {
        auto* item = new QTreeWidgetItem(header, {QString::fromUtf8(obj->Label.getValue())});
        if (auto* vp = Gui::Application::Instance->getViewProvider(obj)) {
            item->setIcon(0, vp->getIcon());
        }
        item->setData(0, EntryRole, static_cast<int>(entries.size()));
        entries.push_back(std::make_unique<App::DocumentObjectWeakPtrT>(obj));
    }
}

App::DocumentObject* DependenciesPanel::objectAt(QTreeWidgetItem* item) const
{
    const QVariant index = item ? item->data(0, EntryRole) : QVariant();
    if (!index.isValid()) {
        return nullptr;
    }
    return entries.at(index.toInt())->get<App::DocumentObject>();
}

void DependenciesPanel::selectEntry(QTreeWidgetItem* item)
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

void DependenciesPanel::preselectEntry(QTreeWidgetItem* item)
{
    if (auto* obj = objectAt(item)) {
        Gui::Selection().setPreselect(obj->getDocument()->getName(), obj->getNameInDocument(), "");
    }
    else {
        Gui::Selection().rmvPreselect();
    }
}

void DependenciesPanel::leaveEvent(QEvent* event)
{
    Gui::Selection().rmvPreselect();
    QWidget::leaveEvent(event);
}

void DependenciesPanel::editEntry(QTreeWidgetItem* item)
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

#include "moc_DependenciesPanel.cpp"
