/***************************************************************************
 *   Copyright (c) 2004 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/
// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

// The tree's dock panel and its search box.

#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>


#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Sequencer.h>
#include <Base/Tools.h>
#include <Base/Writer.h>

#include <Base/Color.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Datums.h>
#include <App/DocumentObjectGroup.h>
#include <App/AutoTransaction.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Link.h>
#include <App/Project.h>
#include <App/Origin.h>
#include <App/SuppressibleExtension.h>

#include "Tree.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "Document.h"
#include "ExpressionCompleter.h"
#include "Macro.h"
#include "MainWindow.h"
#include "MenuManager.h"
#include "ProjectPanel.h"
#include "TreeHistory.h"
#include "TreeLineage.h"
#include "TreeShared.h"
#include "TreeSwatches.h"
#include "TreeParams.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"
#include "Widgets.h"
#include "Workbench.h"

using namespace Gui;
using namespace Gui::TreeHistory;
using namespace Gui::TreeSwatches;
using namespace Gui::TreeShared;
namespace sp = std::placeholders;

TreePanel::TreePanel(const char* name, QWidget* parent)
    : QWidget(parent)
{
    this->treeWidget = new TreeWidget(name, this);
    int indent = TreeParams::getIndentation();
    if (indent) {
        this->treeWidget->setIndentation(indent);
    }

    auto pLayout = new QVBoxLayout(this);
    pLayout->setSpacing(0);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(this->treeWidget);
    connect(this->treeWidget, &TreeWidget::emitSearchObjects, this, &TreePanel::showEditor);

    this->searchBox = new Gui::ExpressionLineEdit(this, true);
    static_cast<ExpressionLineEdit*>(this->searchBox)
        ->setExactMatch(Gui::ExpressionParameter::instance()->isExactMatch());
    pLayout->addWidget(this->searchBox);
    this->searchBox->hide();
    this->searchBox->installEventFilter(this);
    this->searchBox->setPlaceholderText(tr("Search"));
    connect(this->searchBox, &QLineEdit::returnPressed, this, &TreePanel::accept);
    connect(this->searchBox, &QLineEdit::textChanged, this, &TreePanel::itemSearch);
}

TreePanel::~TreePanel() = default;

void TreePanel::accept()
{
    QString text = this->searchBox->text();
    hideEditor();
    this->treeWidget->setFocus();
    this->treeWidget->itemSearch(text, true);
}

bool TreePanel::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj != this->searchBox) {
        return false;
    }

    if (ev->type() == QEvent::KeyPress) {
        bool consumed = false;
        int key = static_cast<QKeyEvent*>(ev)->key();
        switch (key) {
            case Qt::Key_Escape:
                hideEditor();
                consumed = true;
                treeWidget->setFocus();
                break;

            default:
                break;
        }

        return consumed;
    }

    return false;
}

void TreePanel::showEditor()
{
    this->searchBox->show();
    this->searchBox->setFocus();
    this->treeWidget->startItemSearch(searchBox);
}

void TreePanel::hideEditor()
{
    static_cast<ExpressionLineEdit*>(this->searchBox)->setDocumentObject(nullptr);
    this->searchBox->clear();
    this->searchBox->hide();
    this->treeWidget->resetItemSearch();
    auto sels = this->treeWidget->selectedItems();
    if (!sels.empty()) {
        this->treeWidget->scrollToItem(sels.front());
    }
}

void TreePanel::itemSearch(const QString& text)
{
    this->treeWidget->itemSearch(text, false);
}

TreeDockWidget::TreeDockWidget(Gui::Document* pcDocument, QWidget* parent)
    : DockWindow(pcDocument, parent)
{
    setWindowTitle(tr("Tree View"));
    auto panel = new TreePanel("TreeView", this);
    auto pLayout = new QGridLayout(this);
    pLayout->setSpacing(0);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(panel, 0, 0);
}

TreeDockWidget::~TreeDockWidget() = default;
