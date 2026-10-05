// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

#include <fastsignals/signal.h>

#include <App/VersionHistory.h>
#include <FCGlobal.h>

class QDockWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace App
{
class Document;
}

namespace Gui
{

/// Shows how two versions of a document differ as shapes, under `title`. The module that can
/// build shapes registers one; History offers Compare in 3D only when one is registered.
using VersionComparer
    = std::function<void(App::Document& before, App::Document& after, const std::string& title)>;
GuiExport void registerVersionComparer(VersionComparer comparer);

/// The History side panel (UI_GUIDE §2.2.1): the active document's committed versions, newest
/// first, under a "Not yet committed" entry, and what the chosen one changed. It follows the
/// active document.
class GuiExport HistoryPanel: public QWidget
{
    Q_OBJECT

public:
    static void showPanel();
    ~HistoryPanel() override;

protected:
    void leaveEvent(QEvent* event) override;

private:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void setDocument(App::Document* doc);
    void scheduleRefresh(bool rereadVersions);
    void readVersions();
    void showChanges();
    void compareIn3D();

    /// The two documents the chosen entry compares, and how to speak of it.
    struct Comparison
    {
        App::Document* before = nullptr;
        App::Document* after = nullptr;
        QString heading;
        QString emptyText;
        QString title;
    };
    /// Empty when nothing comparable is chosen. Throws when a version cannot be read.
    std::optional<Comparison> chosenComparison();
    App::Document* versionDocument(const std::string& commit);
    App::Document* emptyDocument();
    void closeVersionDocuments();
    App::DocumentObject* objectAt(QTreeWidgetItem* item) const;
    void selectChange(QTreeWidgetItem* item);
    void preselectChange(QTreeWidgetItem* item);

    App::Document* document = nullptr;
    std::string documentFile;
    std::vector<App::FileVersion> versions;
    std::map<std::string, App::Document*> versionDocuments;
    App::Document* empty = nullptr;
    QDockWidget* dock = nullptr;
    QTreeWidget* versionList;
    QTreeWidget* changeList;
    QPushButton* compareButton;
    std::set<App::Document*> builtVersions;
    bool refreshPending = false;
    bool rereadPending = false;
    bool closingVersions = false;
    fastsignals::scoped_connection activeDocument, recomputed, undone, redone, relabelled, saved,
        documentClosed;

    static QPointer<HistoryPanel> instance;
};

}  // namespace Gui
