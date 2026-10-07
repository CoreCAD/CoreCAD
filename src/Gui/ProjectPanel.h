// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

#include <fastsignals/signal.h>

#include <FCGlobal.h>

class QDockWidget;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace Gui
{

/// The Project side panel (UI_GUIDE §2.2.2): the open project's documents, those in its folder
/// and those it uses from outside, each outside one held at a version and offered Update when a
/// newer one exists. Only one project is open at a time; opening another replaces it.
class GuiExport ProjectPanel: public QWidget
{
    Q_OBJECT

public:
    /// Open the project whose file is `projectFile` in the panel, and show it.
    static void openProject(const QString& projectFile);
    /// The open project's file, or empty when none is open.
    static QString openProjectFile();

private:
    explicit ProjectPanel(QWidget* parent = nullptr);

    void refresh();
    void openDocument(QTreeWidgetItem* item);
    void addFromOutside();
    void update(const QString& path);
    void showProblem(const QString& text);

    QString projectFile;
    QDockWidget* dock = nullptr;
    QTreeWidget* documentList;
    QLabel* problem;
    fastsignals::scoped_connection saved;

    static QPointer<ProjectPanel> instance;
};

}  // namespace Gui
