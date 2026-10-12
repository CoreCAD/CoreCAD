// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QApplication>
# include <QDir>
# include <QDockWidget>
# include <QFileInfo>
# include <QHBoxLayout>
# include <QHeaderView>
# include <QLabel>
# include <QPushButton>
# include <QTreeWidget>
# include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/Project.h>
#include <App/VersionHistory.h>
#include <Base/Exception.h>

#include "ProjectPanel.h"

#include "BitmapFactory.h"
#include "DockWindowManager.h"
#include "FileDialog.h"
#include "HistoryPanel.h"
#include "MainWindow.h"
#include "ModuleIO.h"
#include "OverlayManager.h"
#include "WaitCursor.h"

using namespace Gui;

QPointer<ProjectPanel> ProjectPanel::instance;

namespace
{
constexpr int PathRole = Qt::UserRole;

enum Column
{
    NameColumn,
    UpdateColumn,
};

QString displayName(const std::string& path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

/// The version an outside document is held at, as the History panel speaks of a version.
QString versionText(const App::OutsideDocument& outside)
{
    if (outside.version.empty()) {
        return ProjectPanel::tr("not versioned");
    }
    for (const App::FileVersion& version : App::fileVersions(outside.path)) {
        if (version.commit == outside.version) {
            return ProjectPanel::tr("%1 · %2").arg(
                QString::fromStdString(version.summary),
                timeAgo(version.time)
            );
        }
    }
    return QString::fromStdString(outside.version.substr(0, 8));
}

QTreeWidgetItem* heading(QTreeWidget* list, const QString& text)
{
    auto* item = new QTreeWidgetItem(list, {text});
    item->setFlags(Qt::ItemIsEnabled);
    item->setFirstColumnSpanned(true);
    QFont font = item->font(NameColumn);
    font.setBold(true);
    item->setFont(NameColumn, font);
    return item;
}

QTreeWidgetItem* documentRow(QTreeWidgetItem* group, const std::string& path)
{
    auto* item = new QTreeWidgetItem(group, {displayName(path)});
    item->setIcon(NameColumn, BitmapFactory().iconFromTheme("Document"));
    item->setData(NameColumn, PathRole, QString::fromStdString(path));
    item->setToolTip(NameColumn, QDir::toNativeSeparators(QString::fromStdString(path)));
    return item;
}
}  // namespace

ProjectPanel::ProjectPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    documentList = new QTreeWidget(this);
    documentList->setObjectName(QStringLiteral("ProjectDocuments"));
    documentList->setColumnCount(2);
    documentList->setHeaderHidden(true);
    documentList->header()->setStretchLastSection(false);
    // A side panel is narrow: an outside row says where it comes from and its version on a second
    // line, which gives way before the name and Update do.
    documentList->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    documentList->header()->setSectionResizeMode(UpdateColumn, QHeaderView::ResizeToContents);
    documentList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    documentList->setTextElideMode(Qt::ElideRight);
    documentList->header()->setSectionResizeMode(UpdateColumn, QHeaderView::ResizeToContents);
    layout->addWidget(documentList);
    connect(documentList, &QTreeWidget::itemDoubleClicked, this, &ProjectPanel::openDocument);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(6, 0, 6, 6);
    auto* add = new QPushButton(tr("Add from outside…"), this);
    add->setObjectName(QStringLiteral("ProjectAddFromOutside"));
    add->setToolTip(tr("List a document from outside this project where it is. It is not moved."));
    buttons->addWidget(add);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(add, &QPushButton::clicked, this, &ProjectPanel::addFromOutside);

    problem = new QLabel(this);
    problem->setObjectName(QStringLiteral("ProjectProblem"));
    problem->setWordWrap(true);
    problem->setContentsMargins(6, 0, 6, 6);
    problem->hide();
    layout->addWidget(problem);

    // A version committed elsewhere shows when the user comes back to the app.
    connect(qApp, &QApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && isVisible()) {
            refresh();
        }
    });
    saved = App::GetApplication().signalFinishSaveDocument.connect(
        [this](const App::Document&, const std::string&) { refresh(); }
    );
}

void ProjectPanel::openProject(const QString& projectFile)
{
    if (!instance) {
        instance = new ProjectPanel();
        auto* manager = DockWindowManager::instance();
        instance->dock = manager->addDockWindow("Project", instance, Qt::LeftDockWidgetArea);
        instance->dock->setWindowTitle(tr("Project"));
        if (QDockWidget* model = manager->getDockContainer("Model")) {
            getMainWindow()->tabifyDockWidget(model, instance->dock);
        }
        instance->dock->setFeatures(
            QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
            | QDockWidget::DockWidgetFloatable
        );
        OverlayManager::instance()->setupTitleBar(instance->dock);
    }
    instance->projectFile = QFileInfo(projectFile).absoluteFilePath();
    instance->problem->hide();
    instance->refresh();
    instance->dock->show();
    instance->dock->raise();
}

QString ProjectPanel::openProjectFile()
{
    return instance ? instance->projectFile : QString();
}

fastsignals::signal<void()>& ProjectPanel::projectChanged()
{
    static fastsignals::signal<void()> changed;
    return changed;
}

void ProjectPanel::refresh()
{
    fillList();
    projectChanged()();
}

void ProjectPanel::fillList()
{
    documentList->clear();
    if (projectFile.isEmpty()) {
        return;
    }
    const std::string file = projectFile.toStdString();
    std::vector<std::string> inside;
    std::vector<App::OutsideDocument> outside;
    try {
        inside = App::projectDocuments(file);
        outside = App::outsideDocuments(file);
    }
    catch (const Base::Exception&) {
        showProblem(
            tr("The project file %1 can no longer be read.").arg(QDir::toNativeSeparators(projectFile))
        );
        return;
    }

    if (inside.empty() && outside.empty()) {
        auto* item = new QTreeWidgetItem(documentList, {tr("This project has no documents yet.")});
        item->setFlags(Qt::NoItemFlags);
        item->setFirstColumnSpanned(true);
        return;
    }

    auto* insideGroup
        = heading(documentList, tr("In this project (%1)").arg(static_cast<int>(inside.size())));
    for (const std::string& path : inside) {
        documentRow(insideGroup, path);
    }

    auto* outsideGroup
        = heading(documentList, tr("From outside (%1)").arg(static_cast<int>(outside.size())));
    const QDir projectFolder = QFileInfo(projectFile).absoluteDir();
    for (const App::OutsideDocument& entry : outside) {
        QTreeWidgetItem* item = documentRow(outsideGroup, entry.path);
        const QString folder = QFileInfo(QString::fromStdString(entry.path)).absolutePath();
        const QString from = QDir::toNativeSeparators(projectFolder.relativeFilePath(folder));
        QString version;
        if (!QFileInfo::exists(QString::fromStdString(entry.path))) {
            version = tr("missing");
        }
        else {
            version = versionText(entry);
            if (!App::newerVersionOf(entry).empty()) {
                version = tr("%1 · newer version available").arg(version);
                auto* holder = new QWidget(documentList);
                auto* holderLayout = new QVBoxLayout(holder);
                holderLayout->setContentsMargins(0, 0, 4, 0);
                auto* updateButton = new QPushButton(tr("Update"), holder);
                holderLayout->addWidget(updateButton, 0, Qt::AlignVCenter);
                updateButton->setObjectName(QStringLiteral("ProjectUpdate"));
                updateButton->setToolTip(tr("Use the newest version of this document in the project."));
                const QString path = QString::fromStdString(entry.path);
                connect(updateButton, &QPushButton::clicked, this, [this, path]() { update(path); });
                documentList->setItemWidget(item, UpdateColumn, holder);
            }
        }
        item->setText(NameColumn, tr("%1\n%2 · %3").arg(item->text(NameColumn), from, version));
        item->setToolTip(
            NameColumn,
            tr("%1\nHeld at: %2").arg(QDir::toNativeSeparators(QString::fromStdString(entry.path)), version)
        );
    }
    documentList->expandAll();
}

void ProjectPanel::openDocument(QTreeWidgetItem* item)
{
    const QString path = item ? item->data(NameColumn, PathRole).toString() : QString();
    if (!path.isEmpty()) {
        ModuleIO::verifyAndOpenFile(path);
    }
}

void ProjectPanel::addFromOutside()
{
    QStringList patterns;
    for (const std::string& ext : App::Document::nativeFormatExtensions()) {
        patterns << QStringLiteral("*.") + QString::fromStdString(ext);
    }
    const QString chosen = FileDialog::getOpenFileName(
        getMainWindow(),
        tr("Add from outside"),
        QFileInfo(projectFile).absolutePath(),
        {FileDialog::Filter(tr("Documents"), patterns)}
    );
    if (chosen.isEmpty()) {
        return;
    }
    problem->hide();
    try {
        WaitCursor wait;
        App::addOutsideDocument(projectFile.toStdString(), chosen.toStdString());
    }
    catch (const Base::Exception& e) {
        showProblem(tr("%1 was not added. %2.")
                        .arg(QFileInfo(chosen).fileName(), QString::fromStdString(e.getMessage())));
        return;
    }
    refresh();
}

void ProjectPanel::update(const QString& path)
{
    problem->hide();
    try {
        App::updateOutsideDocument(projectFile.toStdString(), path.toStdString());
    }
    catch (const Base::Exception& e) {
        showProblem(tr("%1 was not updated. %2.")
                        .arg(QFileInfo(path).fileName(), QString::fromStdString(e.getMessage())));
        return;
    }
    refresh();
}

void ProjectPanel::showProblem(const QString& text)
{
    problem->setText(text);
    problem->show();
}

#include "moc_ProjectPanel.cpp"
