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

class QAction;
class QCheckBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace PartDesignGui
{

/// The Body information panel (DESIGN_ui-surfaces §1, UI_GUIDE §2.2), opened by double-clicking a
/// body's swatch: its name, colour, material, whether it is shown, and the steps in other bodies
/// that use it. It follows the selection unless pinned, and refreshes after every change.
class BodyPanel: public QWidget, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    static void showFor(App::DocumentObject* body);

protected:
    void leaveEvent(QEvent* event) override;

private:
    explicit BodyPanel(QWidget* parent = nullptr);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    void setBody(App::DocumentObject* newBody);
    void scheduleRefresh();
    void refresh();
    void rename();
    void setShown(bool shown);
    App::DocumentObject* objectAt(QTreeWidgetItem* item) const;
    void selectEntry(QTreeWidgetItem* item);
    void preselectEntry(QTreeWidgetItem* item);
    void editEntry(QTreeWidgetItem* item);

    App::DocumentObjectWeakPtrT body {nullptr};
    std::vector<std::unique_ptr<App::DocumentObjectWeakPtrT>> entries;
    QDockWidget* dock = nullptr;
    QAction* pin;
    QWidget* details;
    QLabel* emptyState;
    QLineEdit* name;
    QLabel* colour;
    QLabel* material;
    QCheckBox* shown;
    QLabel* usedByHeading;
    QTreeWidget* usedBy;
    bool refreshPending = false;
    bool selecting = false;
    fastsignals::scoped_connection recomputed, changed, undone, redone, deleted, documentClosed;

    static QPointer<BodyPanel> instance;
};

}  // namespace PartDesignGui
