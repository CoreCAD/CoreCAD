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
class QTimer;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace App
{
class Document;
}

namespace Gui
{

class View3DInventor;

/// How History draws a version in the model view: a hidden document, with no tab, holding `after`'s
/// material, with what it changed over `before` coloured when `showChanges`. The module that can
/// build shapes registers one; without one, choosing a version only lists what it changed.
using VersionLayers
    = std::function<App::Document*(App::Document& before, App::Document& after, bool showChanges)>;
GuiExport void registerVersionLayers(VersionLayers layers);

/// The History side panel (UI_GUIDE §2.2.1): the active document's committed versions, newest
/// first, under a "Not yet committed" entry, and what the chosen one changed. Choosing an older
/// entry shows that version in the model view, which cannot be edited until the view returns to
/// now. "Not yet committed" offers to commit the document; a document with no history is offered
/// one. It follows the active document.
class GuiExport HistoryPanel: public QWidget
{
    Q_OBJECT

public:
    static void showPanel();
    /// Whether `doc`'s model view shows an older version, so nothing may change `doc`.
    static bool isShowingPastVersion(const App::Document* doc);
    ~HistoryPanel() override;

protected:
    void leaveEvent(QEvent* event) override;

private:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void setDocument(App::Document* doc);
    void scheduleRefresh(bool rereadVersions);
    void readVersions();
    void showChanges();
    void scheduleVersionView();
    void showChosenVersion();
    App::Document* layersFor(App::Document& before, App::Document& after);
    void addBar(View3DInventor* view, const QString& text);
    void restoreModelView();
    void returnToNow();
    void returnIfShowingPast(const App::Document* changed);
    void showCommitArea(bool shown);
    void commit();
    void startHistoryHere();

    /// The two documents the chosen entry compares, and how to speak of it.
    struct Comparison
    {
        App::Document* before = nullptr;
        App::Document* after = nullptr;
        QString heading;
        QString emptyText;
        /// What the model view's bar says while this entry is shown there.
        QString barText;
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
    QWidget* commitArea;
    QPlainTextEdit* messageBox;
    QPushButton* commitButton;
    QLabel* commitProblem;
    QTimer* versionViewTimer;
    std::map<std::pair<App::Document*, bool>, App::Document*> layerDocuments;
    std::vector<QPointer<View3DInventor>> standInViews;
    bool showingPast = false;
    bool drawChanges = true;
    std::set<App::Document*> builtVersions;
    bool refreshPending = false;
    bool rereadPending = false;
    bool closingVersions = false;
    fastsignals::scoped_connection activeDocument, recomputed, undone, redone, relabelled, saved,
        documentClosed, objectChanged, objectAdded, objectDeleted, editStarted;

    static QPointer<HistoryPanel> instance;
};

}  // namespace Gui
