// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <map>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

#include <fastsignals/signal.h>

#include <App/VersionHistory.h>
#include <FCGlobal.h>

class QDockWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace App
{
class Document;
}

namespace Gui
{

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
    bool refreshPending = false;
    bool rereadPending = false;
    bool closingVersions = false;
    fastsignals::scoped_connection activeDocument, recomputed, undone, redone, relabelled, saved,
        documentClosed;

    static QPointer<HistoryPanel> instance;
};

}  // namespace Gui
