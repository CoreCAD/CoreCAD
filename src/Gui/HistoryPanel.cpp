// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#include <filesystem>

#ifndef _PreComp_
# include <QApplication>
# include <QCheckBox>
# include <QDateTime>
# include <QDockWidget>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QLabel>
# include <QLocale>
# include <QPlainTextEdit>
# include <QPushButton>
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
#include <Base/Exception.h>
#include <Base/Uuid.h>

#include "HistoryPanel.h"

#include "Application.h"
#include "Command.h"
#include "DockWindowManager.h"
#include "Document.h"
#include "NaviCube.h"
#include "OverlayManager.h"
#include "Selection/Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "WaitCursor.h"

using namespace Gui;

QPointer<HistoryPanel> HistoryPanel::instance;

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

    auto* versionsPane = new QWidget(splitter);
    auto* versionsLayout = new QVBoxLayout(versionsPane);
    versionsLayout->setContentsMargins(0, 0, 0, 0);
    versionList = new QTreeWidget(versionsPane);
    versionList->setObjectName(QStringLiteral("HistoryVersions"));
    versionsLayout->addWidget(versionList);
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
    if (versions.empty() && !App::isKeptInHistory(documentFile)) {
        emptyLine(versionList, tr("This part has no history yet."));
        // A row of the list, so the offer sits under the sentence it answers.
        auto* row = emptyLine(versionList, QString());
        auto* holder = new QWidget();
        auto* holderLayout = new QHBoxLayout(holder);
        holderLayout->setContentsMargins(0, 2, 0, 2);
        auto* start = new QPushButton(tr("Start history here"), holder);
        start->setObjectName(QStringLiteral("HistoryStartButton"));
        start->setToolTip(tr("Keep versions of the parts in this folder."));
        holderLayout->addWidget(start);
        holderLayout->addStretch();
        versionList->setItemWidget(row, 0, holder);
        connect(start, &QPushButton::clicked, this, &HistoryPanel::startHistoryHere);
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
        comparison.barText = tr("Viewing “%1”, %2").arg(summary, ago(versions[at].time));
    }
    return comparison;
}

void HistoryPanel::showChanges()
{
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

void HistoryPanel::startHistoryHere()
{
    if (documentFile.empty()) {
        return;
    }
    try {
        App::startHistory(std::filesystem::path(documentFile).parent_path().string());
    }
    catch (const Base::Exception& e) {
        e.reportException();
        emptyLine(versionList, tr("History was not started: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    scheduleRefresh(true);
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
