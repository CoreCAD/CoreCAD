// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QApplication>
# include <QDateTime>
# include <QDockWidget>
# include <QHeaderView>
# include <QLocale>
# include <QSplitter>
# include <QTimer>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentChanges.h>
#include <App/DocumentObject.h>
#include <Base/Exception.h>
#include <Base/Uuid.h>

#include "HistoryPanel.h"

#include "Application.h"
#include "DockWindowManager.h"
#include "Document.h"
#include "OverlayManager.h"
#include "Selection/Selection.h"

using namespace Gui;

QPointer<HistoryPanel> HistoryPanel::instance;

namespace
{
constexpr int VersionRole = Qt::UserRole;
constexpr int ObjectRole = Qt::UserRole + 1;
constexpr int NotYetCommitted = -1;

QString ago(std::int64_t time)
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

QTreeWidgetItem* emptyLine(QTreeWidget* list, const QString& text)
{
    auto* item = new QTreeWidgetItem(list, {text});
    item->setFlags(Qt::NoItemFlags);
    item->setFirstColumnSpanned(true);
    return item;
}
}  // namespace

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter);

    versionList = new QTreeWidget(splitter);
    versionList->setColumnCount(2);
    versionList->setHeaderHidden(true);
    versionList->setRootIsDecorated(false);
    versionList->header()->setStretchLastSection(false);
    versionList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    versionList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    changeList = new QTreeWidget(splitter);
    changeList->setHeaderHidden(true);
    changeList->setRootIsDecorated(false);
    changeList->setMouseTracking(true);

    connect(versionList, &QTreeWidget::currentItemChanged, this, [this]() { showChanges(); });
    connect(changeList, &QTreeWidget::itemClicked, this, &HistoryPanel::selectChange);
    connect(changeList, &QTreeWidget::itemEntered, this, &HistoryPanel::preselectChange);
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
        if (&doc == empty) {
            empty = nullptr;
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
}

HistoryPanel::~HistoryPanel()
{
    closeVersionDocuments();
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
    closeVersionDocuments();
    document = doc;
    versions.clear();
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
        if (index.isValid() && index.toInt() != NotYetCommitted) {
            chosen = versions.at(index.toInt()).commit;
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
    if (versions.empty()) {
        emptyLine(
            versionList,
            tr("This part has no committed versions. Commit it to a git repository to start its "
               "history.")
        );
        versionList->setWordWrap(true);
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
            {QString::fromStdString(version.summary),
             tr("%1 · %2").arg(QString::fromStdString(version.author), ago(version.time))}
        );
        item->setData(0, VersionRole, static_cast<int>(i));
        const QString when
            = QLocale().toString(QDateTime::fromSecsSinceEpoch(version.time), QLocale::LongFormat);
        item->setToolTip(0, tr("%1, %2").arg(QString::fromStdString(version.commit), when));
        item->setToolTip(1, item->toolTip(0));
        if (version.commit == chosen) {
            current = item;
        }
    }
    versionList->setCurrentItem(current);
}

void HistoryPanel::showChanges()
{
    changeList->clear();
    auto* item = versionList->currentItem();
    const QVariant index = item ? item->data(0, VersionRole) : QVariant();
    if (!document || !index.isValid()) {
        return;
    }

    QString heading;
    QString emptyText;
    std::vector<App::DocumentChange> changes;
    try {
        if (index.toInt() == NotYetCommitted) {
            heading = tr("Not yet committed");
            emptyText = tr("Nothing has changed since the last commit.");
            changes = App::diffDocuments(*versionDocument(versions.front().commit), *document);
        }
        else {
            const auto at = static_cast<std::size_t>(index.toInt());
            heading = tr("Changed in “%1”").arg(QString::fromStdString(versions[at].summary));
            emptyText = tr("This version changed nothing in the part.");
            App::Document* before = at + 1 < versions.size()
                ? versionDocument(versions[at + 1].commit)
                : emptyDocument();
            changes = App::diffDocuments(*before, *versionDocument(versions[at].commit));
        }
    }
    catch (const Base::Exception& e) {
        emptyLine(changeList, tr("This version could not be read: %1").arg(QString::fromUtf8(e.what())));
        return;
    }

    if (index.toInt() == NotYetCommitted) {
        item->setText(0, tr("Not yet committed (%1)").arg(changes.size()));
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
    closingVersions = true;
    for (const auto& [commit, version] : versionDocuments) {
        App::GetApplication().closeDocument(version->getName());
    }
    versionDocuments.clear();
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
