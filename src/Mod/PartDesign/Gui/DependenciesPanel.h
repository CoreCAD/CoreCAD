// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <memory>
#include <vector>

#include <QPointer>
#include <QWidget>

#include <fastsignals/signal.h>

#include <App/DocumentObserver.h>
#include <Gui/Selection/Selection.h>

class QDockWidget;
class QAction;
class QTreeWidget;
class QTreeWidgetItem;

namespace PartDesignGui
{

/// The Show dependencies side panel (UI_GUIDE §2.2): what a step uses and what uses it. It follows
/// the selection unless pinned, and refreshes after every recompute, undo and redo.
class DependenciesPanel: public QWidget, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    static void showFor(App::DocumentObject* step);

protected:
    void leaveEvent(QEvent* event) override;

private:
    explicit DependenciesPanel(QWidget* parent = nullptr);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    void setStep(App::DocumentObject* step);
    void scheduleRefresh();
    void refresh();
    void addSection(
        const QString& heading,
        const std::vector<App::DocumentObject*>& objects,
        const QString& emptyText
    );
    App::DocumentObject* objectAt(QTreeWidgetItem* item) const;
    void selectEntry(QTreeWidgetItem* item);
    void preselectEntry(QTreeWidgetItem* item);
    void editEntry(QTreeWidgetItem* item);

    App::DocumentObjectWeakPtrT step {nullptr};
    std::vector<std::unique_ptr<App::DocumentObjectWeakPtrT>> entries;
    QDockWidget* dock = nullptr;
    QAction* pin;
    QTreeWidget* list;
    bool refreshPending = false;
    bool selecting = false;
    fastsignals::scoped_connection recomputed, undone, redone, deleted, documentClosed;

    static QPointer<DependenciesPanel> instance;
};

}  // namespace PartDesignGui
