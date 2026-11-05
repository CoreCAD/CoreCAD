// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#include <algorithm>
#include <filesystem>

#ifndef _PreComp_
# include <QApplication>
# include <QButtonGroup>
# include <QCheckBox>
# include <QDateTime>
# include <QDockWidget>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QLabel>
# include <QLocale>
# include <QMenu>
# include <QPlainTextEdit>
# include <QPushButton>
# include <QRadioButton>
# include <QSplitter>
# include <QTimer>
# include <QTreeWidget>
# include <QVBoxLayout>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoSeparator.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentChanges.h>
#include <App/DocumentObject.h>
#include <App/ObjectRecipe.h>
#include <App/Project.h>
#include <Base/Exception.h>
#include <Base/Uuid.h>

#include "HistoryPanel.h"

#include "Application.h"
#include "Command.h"
#include "DockWindowManager.h"
#include "Document.h"
#include "MainWindow.h"
#include "NaviCube.h"
#include "OverlayManager.h"
#include "ProjectDialogs.h"
#include "Selection/Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"

using namespace Gui;

QPointer<HistoryPanel> HistoryPanel::instance;

struct HistoryPanel::Merge
{
    App::FileBranch branch;
    /// Every change both sides made that the merge has met, in the order met; answered ones stay.
    std::vector<App::MergeConflict> conflicts;
    std::map<std::string, bool> takesTheirs;  ///< each answer, by conflictKey
    App::Document* merged = nullptr;          ///< the result, once nothing is left open
    App::DocumentMergeReport report;
    std::vector<std::string> newlyFailing;
    QString problem;  ///< why the merge cannot go on
};

namespace
{
VersionLayers& versionLayers()
{
    static VersionLayers layers;
    return layers;
}
}  // namespace

void Gui::registerVersionLayers(VersionLayers layers)
{
    versionLayers() = std::move(layers);
}

QString Gui::timeAgo(std::int64_t time)
{
    const QDateTime then = QDateTime::fromSecsSinceEpoch(time);
    const qint64 seconds = then.secsTo(QDateTime::currentDateTime());
    const auto count = [](qint64 n, const char* one, const char* many) {
        return n == 1 ? HistoryPanel::tr(one) : HistoryPanel::tr(many).arg(n);
    };
    if (seconds < 60) {
        return HistoryPanel::tr("just now");
    }
    if (seconds < 3600) {
        return count(seconds / 60, "1 minute ago", "%1 minutes ago");
    }
    if (seconds < 86400) {
        return count(seconds / 3600, "1 hour ago", "%1 hours ago");
    }
    if (seconds < 7 * 86400) {
        return count(seconds / 86400, "1 day ago", "%1 days ago");
    }
    return QLocale().toString(then.date(), QLocale::ShortFormat);
}

namespace
{
constexpr int VersionRole = Qt::UserRole;
constexpr int ObjectRole = Qt::UserRole + 1;
constexpr int NotYetCommitted = -1;

QTreeWidgetItem* emptyLine(QTreeWidget* list, const QString& text)
{
    auto* item = new QTreeWidgetItem(list, {text});
    item->setFlags(Qt::NoItemFlags);
    item->setFirstColumnSpanned(true);
    return item;
}

QTreeWidgetItem* headingLine(QTreeWidget* list, const QString& text)
{
    auto* header = new QTreeWidgetItem(list, {text});
    header->setFlags(Qt::ItemIsEnabled);
    header->setFirstColumnSpanned(true);
    QFont bold = header->font(0);
    bold.setBold(true);
    header->setFont(0, bold);
    return header;
}

std::string conflictKey(const App::MergeConflict& conflict)
{
    return conflict.id + '\n' + conflict.value;
}

App::DocumentObject* objectWithId(const App::Document& doc, const std::string& id)
{
    Base::Uuid uid;
    try {
        uid.setValue(id);
    }
    catch (const std::exception&) {
        return nullptr;  // an element inside an object, not an object
    }
    return doc.getObjectByUuid(uid);
}

/// The conflict as a person reads it, led by the step it is in.
QString conflictText(const App::Document& doc, const App::MergeConflict& conflict)
{
    QString text = QString::fromStdString(conflict.detail);
    if (const App::DocumentObject* obj = objectWithId(doc, conflict.id)) {
        const QString label = QString::fromUtf8(obj->Label.getValue());
        if (!text.startsWith(label + QStringLiteral(": "))
            && !text.startsWith(label + QStringLiteral(", "))) {
            text = label + QStringLiteral(": ") + text;
        }
    }
    return text;
}
}  // namespace

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter);

    auto* versionsPane = new QWidget(splitter);
    auto* versionsLayout = new QVBoxLayout(versionsPane);
    versionsLayout->setContentsMargins(0, 0, 0, 0);
    versionList = new QTreeWidget(versionsPane);
    versionList->setObjectName(QStringLiteral("HistoryVersions"));
    versionsLayout->addWidget(versionList);
    // Outside the list, because a list row cannot wrap and the offer's sentence is long.
    projectOffer = new QWidget(versionsPane);
    auto* offerLayout = new QVBoxLayout(projectOffer);
    auto* offerText
        = new QLabel(tr("This part is not in a project, so it has no history."), projectOffer);
    offerText->setWordWrap(true);
    offerLayout->addWidget(offerText);
    auto* offerButtons = new QHBoxLayout();
    auto* create = new QPushButton(tr("New project"), projectOffer);
    create->setObjectName(QStringLiteral("HistoryNewProjectButton"));
    create->setToolTip(tr("Make a project for this part and move the part into its folder."));
    auto* add = new QPushButton(tr("Add to project"), projectOffer);
    add->setObjectName(QStringLiteral("HistoryAddToProjectButton"));
    add->setToolTip(tr("Move this part into an existing project."));
    offerButtons->addWidget(create);
    offerButtons->addWidget(add);
    offerButtons->addStretch();
    offerLayout->addLayout(offerButtons);
    offerLayout->addStretch();
    versionsLayout->addWidget(projectOffer);
    projectOffer->hide();
    connect(create, &QPushButton::clicked, this, &HistoryPanel::newProject);
    connect(add, &QPushButton::clicked, this, &HistoryPanel::addToProject);
    versionList->setColumnCount(2);
    versionList->setHeaderHidden(true);
    versionList->setRootIsDecorated(false);
    versionList->header()->setStretchLastSection(false);
    versionList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    versionList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* changesPane = new QWidget(splitter);
    auto* changesLayout = new QVBoxLayout(changesPane);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changeList = new QTreeWidget(changesPane);
    changesLayout->addWidget(changeList);

    commitArea = new QWidget(changesPane);
    auto* commitLayout = new QVBoxLayout(commitArea);
    commitLayout->setContentsMargins(0, 0, 0, 0);
    messageBox = new QPlainTextEdit(commitArea);
    messageBox->setObjectName(QStringLiteral("HistoryCommitMessage"));
    messageBox->setPlaceholderText(tr("Say what changed"));
    messageBox->setTabChangesFocus(true);
    messageBox->setFixedHeight(messageBox->fontMetrics().lineSpacing() * 4);
    commitLayout->addWidget(messageBox);
    commitButton = new QPushButton(tr("Commit"), commitArea);
    commitButton->setObjectName(QStringLiteral("HistoryCommitButton"));
    commitButton->setToolTip(tr("Save the part and keep it as a new version."));
    commitButton->setEnabled(false);
    commitLayout->addWidget(commitButton, 0, Qt::AlignRight);
    commitProblem = new QLabel(commitArea);
    commitProblem->setObjectName(QStringLiteral("HistoryCommitProblem"));
    commitProblem->setWordWrap(true);
    commitProblem->hide();
    commitLayout->addWidget(commitProblem);
    changesLayout->addWidget(commitArea);
    commitArea->hide();
    connect(messageBox, &QPlainTextEdit::textChanged, this, [this]() {
        commitButton->setEnabled(!messageBox->toPlainText().trimmed().isEmpty());
        commitProblem->hide();
    });
    connect(commitButton, &QPushButton::clicked, this, &HistoryPanel::commit);

    mergeArea = new QWidget(changesPane);
    auto* mergeLayout = new QVBoxLayout(mergeArea);
    mergeLayout->setContentsMargins(0, 0, 0, 0);
    mergeList = new QTreeWidget(mergeArea);
    mergeList->setObjectName(QStringLiteral("HistoryMergeList"));
    mergeList->setColumnCount(2);
    mergeList->setHeaderHidden(true);
    mergeList->setRootIsDecorated(false);
    mergeList->header()->setStretchLastSection(false);
    mergeList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mergeList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mergeLayout->addWidget(mergeList);
    mergeProblem = new QLabel(mergeArea);
    mergeProblem->setObjectName(QStringLiteral("HistoryMergeProblem"));
    mergeProblem->setWordWrap(true);
    mergeProblem->hide();
    mergeLayout->addWidget(mergeProblem);
    mergeMessage = new QPlainTextEdit(mergeArea);
    mergeMessage->setObjectName(QStringLiteral("HistoryMergeMessage"));
    mergeMessage->setPlaceholderText(tr("Say what changed"));
    mergeMessage->setTabChangesFocus(true);
    mergeMessage->setFixedHeight(mergeMessage->fontMetrics().lineSpacing() * 4);
    mergeLayout->addWidget(mergeMessage);
    auto* mergeButtons = new QHBoxLayout();
    mergeButtons->addStretch();
    auto* cancelMerge = new QPushButton(tr("Cancel"), mergeArea);
    cancelMerge->setObjectName(QStringLiteral("HistoryCancelMergeButton"));
    cancelMerge->setToolTip(tr("Leave the part as it is."));
    mergeButtons->addWidget(cancelMerge);
    mergeButton = new QPushButton(tr("Commit merge"), mergeArea);
    mergeButton->setObjectName(QStringLiteral("HistoryCommitMergeButton"));
    mergeButton->setToolTip(tr("Keep the merged part as a new version."));
    mergeButton->setEnabled(false);
    mergeButtons->addWidget(mergeButton);
    mergeLayout->addLayout(mergeButtons);
    changesLayout->addWidget(mergeArea);
    mergeArea->hide();
    connect(mergeMessage, &QPlainTextEdit::textChanged, this, [this]() {
        mergeButton->setEnabled(
            merge && merge->merged && merge->report.complete()
            && !mergeMessage->toPlainText().trimmed().isEmpty()
        );
    });
    connect(mergeButton, &QPushButton::clicked, this, &HistoryPanel::commitMerge);
    connect(cancelMerge, &QPushButton::clicked, this, &HistoryPanel::endMerge);

    versionList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(versionList, &QWidget::customContextMenuRequested, this, &HistoryPanel::showVersionMenu);

    changeList->setHeaderHidden(true);
    changeList->setRootIsDecorated(false);
    changeList->setMouseTracking(true);

    // Moving quickly through the list builds only the version it stops on.
    versionViewTimer = new QTimer(this);
    versionViewTimer->setSingleShot(true);
    versionViewTimer->setInterval(150);
    connect(versionViewTimer, &QTimer::timeout, this, &HistoryPanel::showChosenVersion);

    connect(versionList, &QTreeWidget::currentItemChanged, this, [this]() {
        showChanges();
        scheduleVersionView();
    });
    connect(changeList, &QTreeWidget::itemClicked, this, &HistoryPanel::selectChange);
    connect(changeList, &QTreeWidget::itemEntered, this, &HistoryPanel::preselectChange);
    mergeList->setMouseTracking(true);
    connect(mergeList, &QTreeWidget::itemClicked, this, &HistoryPanel::selectChange);
    connect(mergeList, &QTreeWidget::itemEntered, this, &HistoryPanel::preselectChange);
    // A commit made outside the app shows when the user comes back to it.
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && isVisible()) {
            scheduleRefresh(true);
        }
    });

    activeDocument = Application::Instance->signalActiveDocument.connect(
        [this](const Gui::Document& doc) {
            if (!doc.getDocument()->testStatus(App::Document::TempDoc)) {
                setDocument(doc.getDocument());
            }
        }
    );
    auto& app = App::GetApplication();
    recomputed = app.signalRecomputed.connect([this](const App::Document& doc) {
        if (&doc == document) {
            scheduleRefresh(false);
        }
    });
    undone = app.signalUndo.connect([this]() { scheduleRefresh(false); });
    redone = app.signalRedo.connect([this]() { scheduleRefresh(false); });
    relabelled = app.signalRelabelObject.connect([this](const App::DocumentObject& obj) {
        if (obj.getDocument() == document) {
            scheduleRefresh(false);
        }
    });
    saved = app.signalFinishSaveDocument.connect([this](const App::Document& doc, const std::string&) {
        if (&doc == document) {
            scheduleRefresh(true);
        }
    });
    documentClosed = app.signalDeleteDocument.connect([this](const App::Document& doc) {
        if (closingVersions) {
            return;
        }
        builtVersions.erase(const_cast<App::Document*>(&doc));
        for (auto it = layerDocuments.begin(); it != layerDocuments.end();) {
            it = (it->second == &doc || it->first.first == &doc) ? layerDocuments.erase(it)
                                                                 : std::next(it);
        }
        if (&doc == empty) {
            empty = nullptr;
        }
        if (merge && &doc == merge->merged) {
            merge->merged = nullptr;
        }
        for (auto it = versionDocuments.begin(); it != versionDocuments.end(); ++it) {
            if (it->second == &doc) {
                versionDocuments.erase(it);
                break;
            }
        }
        if (&doc == document) {
            setDocument(nullptr);
        }
    });

    // Anything that still changes the part while an older version is shown returns the view to
    // now before it is next drawn (UI_GUIDE §2.2.1).
    objectChanged = app.signalChangedObject.connect(
        [this](const App::DocumentObject& obj, const App::Property&) {
            returnIfShowingPast(obj.getDocument());
        }
    );
    objectAdded = app.signalNewObject.connect([this](const App::DocumentObject& obj) {
        returnIfShowingPast(obj.getDocument());
    });
    objectDeleted = app.signalDeletedObject.connect([this](const App::DocumentObject& obj) {
        returnIfShowingPast(obj.getDocument());
    });
    editStarted = Application::Instance->signalInEdit.connect(
        [this](const ViewProviderDocumentObject& view) {
            returnIfShowingPast(view.getDocument()->getDocument());
        }
    );
}

HistoryPanel::~HistoryPanel()
{
    closeMerged();
    closeVersionDocuments();
}

bool HistoryPanel::isShowingPastVersion(const App::Document* doc)
{
    return instance && instance->showingPast && doc && instance->document == doc;
}

void HistoryPanel::showPanel()
{
    if (!instance) {
        instance = new HistoryPanel();
        instance->dock = DockWindowManager::instance()
                             ->addDockWindow("History", instance, Qt::RightDockWidgetArea);
        instance->dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable
        );
        OverlayManager::instance()->setupTitleBar(instance->dock);
    }
    App::Document* active = App::GetApplication().getActiveDocument();
    if (active && active->testStatus(App::Document::TempDoc)) {
        active = nullptr;
    }
    instance->setDocument(active);
    instance->scheduleRefresh(true);
    instance->dock->show();
    instance->dock->raise();
}

void HistoryPanel::setDocument(App::Document* doc)
{
    if (doc == document) {
        return;
    }
    if (merge) {
        endMerge();
    }
    closeVersionDocuments();
    document = doc;
    versions.clear();
    versionList->clear();
    scheduleRefresh(true);
}

void HistoryPanel::scheduleRefresh(bool rereadVersions)
{
    rereadPending = rereadPending || rereadVersions;
    if (refreshPending) {
        return;
    }
    refreshPending = true;
    QTimer::singleShot(0, this, [this]() {
        refreshPending = false;
        if (rereadPending) {
            rereadPending = false;
            readVersions();
        }
        showChanges();
    });
}

void HistoryPanel::readVersions()
{
    std::string chosen = "uncommitted";
    if (auto* item = versionList->currentItem()) {
        const QVariant index = item->data(0, VersionRole);
        if (index.isValid() && index.toInt() >= 0
            && static_cast<std::size_t>(index.toInt()) < versions.size()) {
            chosen = versions[index.toInt()].commit;
        }
    }

    documentFile = document ? document->FileName.getStrValue() : std::string();
    std::vector<App::FileVersion> fresh;
    if (!documentFile.empty()) {
        try {
            fresh = App::fileVersions(documentFile);
        }
        catch (const Base::Exception& e) {
            e.reportException();
        }
    }
    const auto commitsOf = [](const std::vector<App::FileVersion>& list) {
        std::vector<std::string> ids;
        for (const auto& version : list) {
            ids.push_back(version.commit);
        }
        return ids;
    };
    if (commitsOf(fresh) != commitsOf(versions)) {
        closeVersionDocuments();
    }
    versions = fresh;

    const QSignalBlocker blocker(versionList);
    versionList->clear();
    projectOffer->hide();
    versionList->show();
    if (dock) {
        dock->setWindowTitle(
            document ? tr("History — %1").arg(QString::fromUtf8(document->Label.getValue()))
                     : tr("History")
        );
    }
    if (!document) {
        emptyLine(versionList, tr("Open a part to see its history."));
        return;
    }
    if (documentFile.empty()) {
        emptyLine(versionList, tr("Save this part to start its history."));
        return;
    }
    if (App::projectFileOf(documentFile).empty()) {
        versionList->hide();
        projectOffer->show();
        return;
    }

    auto* pending = new QTreeWidgetItem(versionList, {tr("Not yet committed")});
    pending->setData(0, VersionRole, NotYetCommitted);
    QFont italic = pending->font(0);
    italic.setItalic(true);
    pending->setFont(0, italic);
    QTreeWidgetItem* current = pending;

    for (std::size_t i = 0; i < versions.size(); ++i) {
        const App::FileVersion& version = versions[i];
        auto* item = new QTreeWidgetItem(
            versionList,
            {QString::fromStdString(version.summary), timeAgo(version.time)}
        );
        item->setData(0, VersionRole, static_cast<int>(i));
        const QString when
            = QLocale().toString(QDateTime::fromSecsSinceEpoch(version.time), QLocale::LongFormat);
        item->setToolTip(
            0,
            tr("%1, %2\n%3")
                .arg(QString::fromStdString(version.author), when, QString::fromStdString(version.commit))
        );
        item->setToolTip(1, item->toolTip(0));
        if (version.commit == chosen) {
            current = item;
        }
    }
    versionList->setCurrentItem(current);
}

std::optional<HistoryPanel::Comparison> HistoryPanel::chosenComparison()
{
    auto* item = versionList->currentItem();
    const QVariant index = item ? item->data(0, VersionRole) : QVariant();
    if (!document || !index.isValid()
        || (index.toInt() != NotYetCommitted
            && static_cast<std::size_t>(index.toInt()) >= versions.size())) {
        return std::nullopt;
    }
    Comparison comparison;
    if (index.toInt() == NotYetCommitted) {
        comparison.heading = tr("Not yet committed");
        comparison.emptyText = versions.empty() ? tr("The part is empty.")
                                                : tr("Nothing has changed since the last commit.");
        comparison.before = versions.empty() ? emptyDocument()
                                             : versionDocument(versions.front().commit);
        comparison.after = document;
    }
    else {
        const auto at = static_cast<std::size_t>(index.toInt());
        const QString summary = QString::fromStdString(versions[at].summary);
        comparison.heading = tr("Changed in “%1”").arg(summary);
        comparison.emptyText = tr("This version changed nothing in the part.");
        comparison.before = at + 1 < versions.size() ? versionDocument(versions[at + 1].commit)
                                                     : emptyDocument();
        comparison.after = versionDocument(versions[at].commit);
        comparison.barText = tr("Viewing “%1”, %2").arg(summary, timeAgo(versions[at].time));
    }
    return comparison;
}

void HistoryPanel::showChanges()
{
    if (merge) {
        return;
    }
    changeList->clear();
    showCommitArea(false);
    auto* item = versionList->currentItem();
    const QVariant index = item ? item->data(0, VersionRole) : QVariant();

    std::optional<Comparison> chosen;
    std::vector<App::DocumentChange> changes;
    try {
        chosen = chosenComparison();
        if (!chosen) {
            return;
        }
        changes = App::diffDocuments(*chosen->before, *chosen->after);
    }
    catch (const Base::Exception& e) {
        emptyLine(changeList, tr("This version could not be read: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    const QString heading = chosen->heading;
    const QString emptyText = chosen->emptyText;

    if (index.toInt() == NotYetCommitted) {
        item->setText(0, tr("Not yet committed (%1)").arg(changes.size()));
        showCommitArea(!changes.empty());
    }

    auto* header = new QTreeWidgetItem(changeList, {tr("%1 (%2)").arg(heading).arg(changes.size())});
    header->setFlags(Qt::ItemIsEnabled);
    QFont bold = header->font(0);
    bold.setBold(true);
    header->setFont(0, bold);

    if (changes.empty()) {
        auto* line = new QTreeWidgetItem(header, {emptyText});
        line->setFlags(Qt::NoItemFlags);
    }
    for (const App::DocumentChange& change : changes) {
        auto* line = new QTreeWidgetItem(header, {QString::fromStdString(change.text)});
        line->setData(0, ObjectRole, QString::fromStdString(change.objectId));
    }
    changeList->expandAll();
}

void HistoryPanel::showCommitArea(bool shown)
{
    commitArea->setVisible(shown);
    if (!shown) {
        commitProblem->hide();
    }
}

void HistoryPanel::commit()
{
    const std::string message = messageBox->toPlainText().trimmed().toStdString();
    if (!document || documentFile.empty() || message.empty()) {
        return;
    }
    auto* guiDocument = Application::Instance->getDocument(document);
    if (guiDocument && guiDocument->isModified() && !guiDocument->save()) {
        return;
    }
    try {
        App::commitFile(documentFile, message);
    }
    catch (const Base::Exception& e) {
        e.reportException();
        commitProblem->setText(tr("The version was not kept: %1").arg(QString::fromUtf8(e.what())));
        commitProblem->show();
        return;
    }
    messageBox->clear();
    scheduleRefresh(true);
}

void HistoryPanel::newProject()
{
    if (!document) {
        return;
    }
    NewProjectDialog dialog(document, getMainWindow());
    if (dialog.exec() == QDialog::Accepted) {
        scheduleRefresh(true);
    }
}

void HistoryPanel::addToProject()
{
    if (document && Gui::addToProject(document, getMainWindow())) {
        scheduleRefresh(true);
    }
}

void HistoryPanel::showVersionMenu(const QPoint& pos)
{
    // A merge goes into the current version, so only its row offers one.
    auto* item = versionList->itemAt(pos);
    if (!item || merge || item->data(0, VersionRole) != QVariant(0)) {
        return;
    }
    std::vector<App::FileBranch> branches;
    try {
        branches = App::branchesToMerge(documentFile);
    }
    catch (const Base::Exception& e) {
        e.reportException();
        return;
    }
    if (branches.empty()) {
        return;  // hidden, not greyed out (UI_GUIDE §2.4)
    }

    QMenu menu(this);
    QMenu* mergeWith = menu.addMenu(tr("Merge with"));
    mergeWith->setToolTipsVisible(true);
    for (const App::FileBranch& branch : branches) {
        QAction* action = mergeWith->addAction(tr("%1 — %2").arg(
            QString::fromStdString(branch.name),
            QString::fromStdString(branch.latest.summary)
        ));
        action->setToolTip(
            tr("%1, %2").arg(QString::fromStdString(branch.latest.author), timeAgo(branch.latest.time))
        );
        connect(action, &QAction::triggered, this, [this, branch]() { startMerge(branch); });
    }
    menu.exec(versionList->viewport()->mapToGlobal(pos));
}

void HistoryPanel::startMerge(const App::FileBranch& branch)
{
    if (!document || merge) {
        return;
    }
    restoreModelView();
    merge = std::make_unique<Merge>();
    merge->branch = branch;
    versionList->setEnabled(false);
    changeList->hide();
    showCommitArea(false);
    mergeArea->show();
    mergeMessage->setPlainText(tr("Merge %1").arg(QString::fromStdString(branch.name)));

    if (changedSinceLastVersion()) {
        merge->problem = tr("The part has changes not yet committed. Commit them before merging.");
        showMerge();
        return;
    }
    runMerge();
}

void HistoryPanel::runMerge()
{
    if (!merge || !document) {
        return;
    }
    closeMerged();
    merge->problem.clear();
    std::vector<App::MergeChoice> choices;
    for (const App::MergeConflict& conflict : merge->conflicts) {
        const auto answered = merge->takesTheirs.find(conflictKey(conflict));
        if (answered != merge->takesTheirs.end()) {
            choices.push_back({conflict.id, conflict.value, answered->second});
        }
    }
    try {
        WaitCursor wait;
        App::MergedDocument made
            = App::mergeFileRevision(*document, merge->branch.latest.commit, choices);
        merge->merged = made.document;
        merge->report = std::move(made.report);
        merge->newlyFailing = std::move(made.newlyFailing);
        // A choice can open a change the first merge did not show, as a step kept on what the
        // other side deleted.
        for (const App::MergeConflict& conflict : merge->report.conflicts) {
            const std::string key = conflictKey(conflict);
            if (std::none_of(merge->conflicts.begin(), merge->conflicts.end(), [&](const auto& met) {
                    return conflictKey(met) == key;
                })) {
                merge->conflicts.push_back(conflict);
            }
        }
        if (!merge->merged && merge->report.conflicts.empty()) {
            merge->problem = QString::fromStdString(made.refusal);
        }
    }
    catch (const Base::Exception& e) {
        merge->problem = QString::fromUtf8(e.what());
    }
    showMerge();
}

void HistoryPanel::showMerge()
{
    mergeList->clear();
    auto* title
        = headingLine(mergeList, tr("Merging “%1”").arg(QString::fromStdString(merge->branch.name)));
    title->setToolTip(
        0,
        tr("%1\n%2, %3")
            .arg(
                QString::fromStdString(merge->branch.latest.summary),
                QString::fromStdString(merge->branch.latest.author),
                timeAgo(merge->branch.latest.time)
            )
    );

    if (!merge->conflicts.empty()) {
        headingLine(mergeList, tr("Changed on both sides (%1)").arg(merge->conflicts.size()));
    }
    for (const App::MergeConflict& conflict : merge->conflicts) {
        const std::string key = conflictKey(conflict);
        auto* line = new QTreeWidgetItem(mergeList, {conflictText(*document, conflict)});
        line->setToolTip(0, line->text(0));
        if (const App::DocumentObject* obj = objectWithId(*document, conflict.id)) {
            line->setData(0, ObjectRole, QString::fromStdString(obj->Uid.getValueStr()));
        }

        // No preselected answer (UI_GUIDE §4.3).
        auto* sides = new QWidget(mergeList);
        auto* sidesLayout = new QHBoxLayout(sides);
        sidesLayout->setContentsMargins(4, 0, 4, 0);
        auto* group = new QButtonGroup(sides);
        auto* mine = new QRadioButton(tr("Keep mine"), sides);
        auto* theirs = new QRadioButton(tr("Take theirs"), sides);
        group->addButton(mine);
        group->addButton(theirs);
        sidesLayout->addWidget(mine);
        sidesLayout->addWidget(theirs);
        const auto answered = merge->takesTheirs.find(key);
        if (answered != merge->takesTheirs.end()) {
            (answered->second ? theirs : mine)->setChecked(true);
        }
        connect(mine, &QRadioButton::clicked, this, [this, key]() { answer(key, false); });
        connect(theirs, &QRadioButton::clicked, this, [this, key]() { answer(key, true); });
        mergeList->setItemWidget(line, 1, sides);
    }

    if (merge->merged) {
        const App::DocumentMergeReport& report = merge->report;
        if (!report.complete()) {
            const std::size_t open = report.overridden.size() + report.undetermined.size();
            headingLine(mergeList, tr("Cannot be settled here yet (%1)").arg(open));
            for (const auto* list : {&report.overridden, &report.undetermined}) {
                for (const std::string& text : *list) {
                    emptyLine(mergeList, QString::fromStdString(text));
                }
            }
            merge->problem = tr("This merge cannot be finished in the app yet.");
        }
        else {
            std::vector<App::DocumentChange> changes;
            try {
                changes = App::diffDocuments(*document, *merge->merged);
            }
            catch (const Base::Exception& e) {
                e.reportException();
            }
            headingLine(mergeList, tr("What the merge changes (%1)").arg(changes.size()));
            for (const App::DocumentChange& change : changes) {
                auto* line = new QTreeWidgetItem(mergeList, {QString::fromStdString(change.text)});
                line->setFirstColumnSpanned(true);
                line->setData(0, ObjectRole, QString::fromStdString(change.objectId));
            }
            for (const std::string& name : merge->newlyFailing) {
                emptyLine(mergeList, tr("%1 fails after the merge").arg(QString::fromStdString(name)));
            }
        }
    }
    mergeList->resizeColumnToContents(1);

    mergeProblem->setText(merge->problem);
    mergeProblem->setVisible(!merge->problem.isEmpty());
    mergeButton->setEnabled(
        merge->merged && merge->report.complete() && !mergeMessage->toPlainText().trimmed().isEmpty()
    );
}

void HistoryPanel::answer(const std::string& conflict, bool takeTheirs)
{
    if (!merge) {
        return;
    }
    merge->takesTheirs[conflict] = takeTheirs;
    const bool allAnswered
        = std::all_of(merge->conflicts.begin(), merge->conflicts.end(), [this](const auto& met) {
              return merge->takesTheirs.count(conflictKey(met)) != 0;
          });
    if (allAnswered) {
        // Showing the new result rebuilds the list, deleting the button that sent this.
        QTimer::singleShot(0, this, &HistoryPanel::runMerge);
    }
}

void HistoryPanel::commitMerge()
{
    const std::string message = mergeMessage->toPlainText().trimmed().toStdString();
    if (!merge || !merge->merged || !document || message.empty()) {
        return;
    }
    if (changedSinceLastVersion()) {
        mergeProblem->setText(tr("The part changed during the merge. Cancel and merge again."));
        mergeProblem->show();
        return;
    }
    try {
        App::recordMerge(*document, *merge->merged, merge->branch.latest.commit, message);
    }
    catch (const Base::Exception& e) {
        e.reportException();
        mergeProblem->setText(tr("The merge was not kept: %1").arg(QString::fromUtf8(e.what())));
        mergeProblem->show();
        return;
    }
    endMerge();
    // The file now holds the merge; the open part is read from it again.
    try {
        WaitCursor wait;
        document->restore();
    }
    catch (const Base::Exception& e) {
        e.reportException();
    }
    scheduleRefresh(true);
}

// Asks what the "Not yet committed" row shows, not whether the document is marked modified:
// opening a part can mark it modified without changing anything a version records.
bool HistoryPanel::changedSinceLastVersion()
{
    try {
        App::Document* last = versions.empty() ? emptyDocument()
                                               : versionDocument(versions.front().commit);
        return !App::diffDocuments(*last, *document).empty();
    }
    catch (const Base::Exception& e) {
        e.reportException();
        return true;
    }
}

void HistoryPanel::endMerge()
{
    closeMerged();
    merge.reset();
    mergeArea->hide();
    mergeList->clear();
    changeList->show();
    versionList->setEnabled(true);
    scheduleRefresh(false);
}

void HistoryPanel::closeMerged()
{
    if (merge && merge->merged) {
        App::Document* merged = merge->merged;
        merge->merged = nullptr;
        App::GetApplication().closeDocument(merged->getName());
    }
}

void HistoryPanel::scheduleVersionView()
{
    auto* item = versionList->currentItem();
    const QVariant index = item ? item->data(0, VersionRole) : QVariant();
    if (!index.isValid() || index.toInt() == NotYetCommitted || !versionLayers()) {
        versionViewTimer->stop();
        restoreModelView();
        return;
    }
    versionViewTimer->start();
}

void HistoryPanel::showChosenVersion()
{
    std::optional<Comparison> chosen;
    App::Document* layers = nullptr;
    try {
        chosen = chosenComparison();
        if (!chosen || chosen->after == document) {
            restoreModelView();
            return;
        }
        WaitCursor wait;
        layers = layersFor(*chosen->before, *chosen->after);
    }
    catch (const Base::Exception& e) {
        e.reportException();
        restoreModelView();
        return;
    }

    auto* standIn = new SoSeparator();
    standIn->ref();
    auto* unpickable = new SoPickStyle();
    unpickable->style = SoPickStyle::UNPICKABLE;
    standIn->addChild(unpickable);
    for (App::DocumentObject* layer : layers->getObjects()) {
        if (auto* view = Application::Instance->getViewProvider(layer)) {
            standIn->addChild(view->getRoot());
        }
    }

    restoreModelView();
    if (Gui::Document* shown = Application::Instance->getDocument(document)) {
        for (MDIView* mdi : shown->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
            auto* view = static_cast<View3DInventor*>(mdi);
            view->getViewer()->setStandIn(standIn);
            standInViews.emplace_back(view);
            addBar(view, chosen->barText);
        }
    }
    standIn->unref();
    showingPast = !standInViews.empty();
    Application::Instance->commandManager().testActive();
}

App::Document* HistoryPanel::layersFor(App::Document& before, App::Document& after)
{
    const auto key = std::make_pair(&after, drawChanges);
    auto found = layerDocuments.find(key);
    if (found != layerDocuments.end()) {
        return found->second;
    }
    // A version read from its file carries no shapes until it is built; build each once.
    for (App::Document* side : {&before, &after}) {
        if (side != document && builtVersions.insert(side).second) {
            side->recompute();
        }
    }
    App::Document* layers = versionLayers()(before, after, drawChanges);
    layerDocuments[key] = layers;
    return layers;
}

namespace
{
/// The bar across the top of a model view showing an older version (UI_GUIDE §2.2.1). It follows
/// the view's width, tinted with the theme's accent so it reads as a mode, and stops short of the
/// navigation cube.
class PastVersionBar: public QFrame
{
public:
    explicit PastVersionBar(View3DInventor* view)
        : QFrame(view)
        , viewer(view->getViewer())
    {
        view->installEventFilter(this);
        // Releasing the mouse may end a drag of the navigation cube.
        viewer->viewport()->installEventFilter(this);
        setObjectName(QStringLiteral("PastVersionBar"));
        const QColor base = palette().color(QPalette::Window);
        const QColor accent = palette().color(QPalette::Highlight);
        constexpr double accentShare = 0.35;
        auto blend = [&](int from, int to) {
            return static_cast<int>(from + (to - from) * accentShare);
        };
        const QColor tint(
            blend(base.red(), accent.red()),
            blend(base.green(), accent.green()),
            blend(base.blue(), accent.blue())
        );
        // A style rule, because the theme's style sheet overrides a palette background.
        setStyleSheet(QStringLiteral(
                          "#PastVersionBar { background-color: %1; border: none; "
                          "border-bottom: 1px solid %2; }"
        )
                          .arg(tint.name(), accent.name()));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ((watched == parentWidget() && event->type() == QEvent::Resize)
            || (watched == viewer->viewport() && event->type() == QEvent::MouseButtonRelease)) {
            fitToView();
        }
        return QFrame::eventFilter(watched, event);
    }

public:
    void fitToView()
    {
        QRect band(0, 0, parentWidget()->width(), sizeHint().height());
        if (viewer->isEnabledNaviCube() && viewer->getNaviCube()) {
            QRect cube = viewer->getNaviCube()->area();
            cube.moveTopLeft(viewer->viewport()->mapTo(parentWidget(), cube.topLeft()));
            if (cube.intersects(band)) {
                constexpr int gap = 4;
                if (cube.center().x() > band.center().x()) {
                    band.setRight(cube.left() - gap);
                }
                else {
                    band.setLeft(cube.right() + gap);
                }
            }
        }
        setGeometry(band);
        raise();
    }

private:
    View3DInventorViewer* viewer;
};
}  // namespace

void HistoryPanel::addBar(View3DInventor* view, const QString& text)
{
    auto* bar = new PastVersionBar(view);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(new QLabel(text, bar), 1);

    auto* changes = new QCheckBox(tr("Show changes"), bar);
    changes->setChecked(drawChanges);
    changes->setToolTip(tr("Draws what this version added in blue and what it removed in red"));
    connect(changes, &QCheckBox::toggled, this, [this](bool on) {
        drawChanges = on;
        // Rebuilding here would delete the bar that sent the signal.
        QTimer::singleShot(0, this, &HistoryPanel::showChosenVersion);
    });
    layout->addWidget(changes);

    auto* now = new QPushButton(tr("Return to now"), bar);
    now->setToolTip(tr("Shows the part as it is now, which can be edited"));
    connect(now, &QPushButton::clicked, this, [this]() {
        QTimer::singleShot(0, this, &HistoryPanel::returnToNow);
    });
    layout->addWidget(now);

    bar->fitToView();
    bar->show();
}

void HistoryPanel::restoreModelView()
{
    for (const QPointer<View3DInventor>& view : standInViews) {
        if (!view) {
            continue;
        }
        view->getViewer()->setStandIn(nullptr);
        for (auto* bar : view->findChildren<QFrame*>(QStringLiteral("PastVersionBar"))) {
            bar->hide();
            bar->deleteLater();
        }
    }
    standInViews.clear();
    if (showingPast) {
        showingPast = false;
        Application::Instance->commandManager().testActive();
    }
}

void HistoryPanel::returnToNow()
{
    restoreModelView();
    auto* top = versionList->topLevelItem(0);
    if (top && top->data(0, VersionRole).toInt() == NotYetCommitted
        && versionList->currentItem() != top) {
        versionList->setCurrentItem(top);
    }
}

void HistoryPanel::returnIfShowingPast(const App::Document* changed)
{
    if (!showingPast || changed != document) {
        return;
    }
    versionViewTimer->stop();
    restoreModelView();
    QTimer::singleShot(0, this, &HistoryPanel::returnToNow);
}

App::Document* HistoryPanel::versionDocument(const std::string& commit)
{
    auto found = versionDocuments.find(commit);
    if (found != versionDocuments.end()) {
        return found->second;
    }
    App::Document* version = App::openFileVersion(documentFile, commit);
    versionDocuments[commit] = version;
    return version;
}

App::Document* HistoryPanel::emptyDocument()
{
    if (!empty) {
        App::DocumentInitFlags flags;
        flags.createView = false;
        flags.temporary = true;
        auto& app = App::GetApplication();
        empty = app.newDocument(app.getUniqueDocumentName("Version").c_str(), "Empty", flags);
    }
    return empty;
}

void HistoryPanel::closeVersionDocuments()
{
    restoreModelView();
    closingVersions = true;
    for (const auto& [key, layers] : layerDocuments) {
        App::GetApplication().closeDocument(layers->getName());
    }
    layerDocuments.clear();
    for (const auto& [commit, version] : versionDocuments) {
        App::GetApplication().closeDocument(version->getName());
    }
    versionDocuments.clear();
    builtVersions.clear();
    if (empty) {
        App::GetApplication().closeDocument(empty->getName());
        empty = nullptr;
    }
    closingVersions = false;
}

App::DocumentObject* HistoryPanel::objectAt(QTreeWidgetItem* item) const
{
    const QVariant id = item ? item->data(0, ObjectRole) : QVariant();
    if (!document || !id.isValid()) {
        return nullptr;
    }
    Base::Uuid uid;
    uid.setValue(id.toString().toStdString());
    return document->getObjectByUuid(uid);
}

void HistoryPanel::selectChange(QTreeWidgetItem* item)
{
    if (auto* obj = objectAt(item)) {
        Selection().clearSelection();
        Selection().addSelection(document->getName(), obj->getNameInDocument());
    }
}

void HistoryPanel::preselectChange(QTreeWidgetItem* item)
{
    if (auto* obj = objectAt(item)) {
        Selection().setPreselect(document->getName(), obj->getNameInDocument(), "");
    }
    else {
        Selection().rmvPreselect();
    }
}

void HistoryPanel::leaveEvent(QEvent* event)
{
    Selection().rmvPreselect();
    QWidget::leaveEvent(event);
}

#include "moc_HistoryPanel.cpp"
