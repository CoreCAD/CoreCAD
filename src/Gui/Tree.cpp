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

/////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<QPixmap> TreeWidget::documentPixmap;
std::unique_ptr<QPixmap> TreeWidget::documentPartialPixmap;
QBrush TreeShared::_TreeItemBackground;
std::set<TreeWidget*> TreeWidget::Instances;
TreeWidget* TreeShared::_LastSelectedTreeWidget = nullptr;
const int TreeWidget::DocumentType = 1000;
const int TreeWidget::ObjectType = 1001;
const int TreeWidget::StopRowType = 1002;
const int TreeWidget::ProjectType = 1003;
bool TreeShared::_DraggingActive = false;
bool TreeShared::_DragEventFilter = false;


void TreeParams::onItemBackgroundChanged()
{
    if (getItemBackground()) {
        Base::Color color;
        color.setPackedValue(getItemBackground());
        QColor col;
        col.setRedF(color.r);
        col.setGreenF(color.g);
        col.setBlueF(color.b);
        col.setAlphaF(color.a);
        _TreeItemBackground = QBrush(col);
    }
    else {
        _TreeItemBackground = QBrush();
    }
    refreshTreeViews();
}

//////////////////////////////////////////////////////////////////////////////////////


// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------

TreeWidget::TreeWidget(const char* name, QWidget* parent)
    : QTreeWidget(parent)
    , SelectionObserver(true, ResolveMode::NoResolve)
    , contextItem(nullptr)
    , searchObject(nullptr)
    , searchDoc(nullptr)
    , searchContextDoc(nullptr)
    , editingItem(nullptr)
    , currentDocItem(nullptr)
    , myName(name)
{
    Instances.insert(this);
    if (!_LastSelectedTreeWidget) {
        _LastSelectedTreeWidget = this;
    }

    this->setDragEnabled(true);
    this->setAcceptDrops(true);
    this->setColumnCount(4);
    this->setItemDelegate(new TreeWidgetItemDelegate(this));
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    this->showHiddenAction = new QAction(this);
    this->showHiddenAction->setCheckable(true);
    connect(this->showHiddenAction, &QAction::triggered, this, &TreeWidget::onShowHidden);

    this->toggleVisibilityInTreeAction = new QAction(this);
    connect(
        this->toggleVisibilityInTreeAction,
        &QAction::triggered,
        this,
        &TreeWidget::onToggleVisibilityInTree
    );

    this->createGroupAction = new QAction(this);
    connect(this->createGroupAction, &QAction::triggered, this, &TreeWidget::onCreateGroup);

    this->relabelObjectAction = new QAction(this);
#ifndef Q_OS_MAC
    this->relabelObjectAction->setShortcut(Qt::Key_F2);
#else
    this->relabelObjectAction->setShortcut(QKeySequence(Qt::Key_Return));
#endif
    this->relabelObjectAction->setShortcutVisibleInContextMenu(true);
    connect(this->relabelObjectAction, &QAction::triggered, this, &TreeWidget::onRelabelObject);

    this->finishEditingAction = new QAction(this);
    connect(this->finishEditingAction, &QAction::triggered, this, &TreeWidget::onFinishEditing);

    this->selectDependentsAction = new QAction(this);
    connect(this->selectDependentsAction, &QAction::triggered, this, &TreeWidget::onSelectDependents);

    this->closeDocAction = new QAction(this);
    connect(this->closeDocAction, &QAction::triggered, this, &TreeWidget::onCloseDoc);

    this->reloadDocAction = new QAction(this);
    connect(this->reloadDocAction, &QAction::triggered, this, &TreeWidget::onReloadDoc);

    this->skipRecomputeAction = new QAction(this);
    this->skipRecomputeAction->setCheckable(true);
    connect(this->skipRecomputeAction, &QAction::toggled, this, &TreeWidget::onSkipRecompute);
    this->skipRecomputeCommand = Gui::Application::Instance->commandManager().getCommandByName(
        "Std_ToggleSkipRecompute"
    );

    this->allowPartialRecomputeAction = new QAction(this);
    this->allowPartialRecomputeAction->setCheckable(true);
    connect(
        this->allowPartialRecomputeAction,
        &QAction::toggled,
        this,
        &TreeWidget::onAllowPartialRecompute
    );

    this->markRecomputeAction = new QAction(this);
    connect(this->markRecomputeAction, &QAction::triggered, this, &TreeWidget::onMarkRecompute);

    this->recomputeObjectAction = new QAction(this);
    connect(this->recomputeObjectAction, &QAction::triggered, this, &TreeWidget::onRecomputeObject);
    if (auto recomputeCmd
        = Gui::Application::Instance->commandManager().getCommandByName("Std_Recompute")) {
        // Register command action on the tree so its shortcut can trigger from the main window.
        recomputeCmd->addTo(this);
        this->recomputeObjectAction->setShortcut(recomputeCmd->getShortcut());
        this->recomputeObjectAction->setShortcutVisibleInContextMenu(true);
    }
    this->searchObjectsAction = new QAction(this);
    this->searchObjectsAction->setText(tr("Search Objects"));
    this->searchObjectsAction->setStatusTip(tr("Searches for objects in the tree"));
    connect(this->searchObjectsAction, &QAction::triggered, this, &TreeWidget::onSearchObjects);

    this->openFileLocationAction = new QAction(this);
    connect(this->openFileLocationAction, &QAction::triggered, this, &TreeWidget::onOpenFileLocation);

    // NOLINTBEGIN
    //  Setup connections
    connectNewDocument = Application::Instance->signalNewDocument.connect(
        std::bind(&TreeWidget::slotNewDocument, this, sp::_1, sp::_2)
    );
    connectDelDocument = Application::Instance->signalDeleteDocument.connect(
        std::bind(&TreeWidget::slotDeleteDocument, this, sp::_1)
    );
    connectRenDocument = Application::Instance->signalRenameDocument.connect(
        std::bind(&TreeWidget::slotRenameDocument, this, sp::_1)
    );
    connectActDocument = Application::Instance->signalActiveDocument.connect(
        std::bind(&TreeWidget::slotActiveDocument, this, sp::_1)
    );
    connectRelDocument = Application::Instance->signalRelabelDocument.connect(
        std::bind(&TreeWidget::slotRelabelDocument, this, sp::_1)
    );
    connectShowHidden = Application::Instance->signalShowHidden.connect(
        std::bind(&TreeWidget::slotShowHidden, this, sp::_1)
    );
    connectProjectChanged = ProjectPanel::projectChanged().connect([this]() {
        placeDocumentsInProject();
    });
    // A document's file is known only once it has been read.
    connectRestoredDocument = App::GetApplication().signalFinishRestoreDocument.connect(
        [this](const App::Document&) { placeDocumentsInProject(); }
    );

    // Gui::Document::signalChangedObject informs the App::Document property
    // change, not view provider's own property, which is what the signal below
    // for
    connectChangedViewObj = Application::Instance->signalChangedObject.connect(
        std::bind(&TreeWidget::slotChangedViewObject, this, sp::_1, sp::_2)
    );
    // NOLINTEND

    setupResizableColumn(this);
    this->header()->setStretchLastSection(true);
    QObject::connect(this->header(), &QHeaderView::sectionResized, [](int idx, int, int newSize) {
        if (idx == 1) {
            TreeParams::setColumnSize2(newSize);
        }
        else if (idx == 2) {
            TreeParams::setColumnSize3(newSize);
        }
        else {
            TreeParams::setColumnSize1(newSize);
        }
    });

    // Add the first main label
    this->rootItem = invisibleRootItem();
    this->expandItem(this->rootItem);
    this->setSelectionMode(QAbstractItemView::ExtendedSelection);

    this->setMouseTracking(true);  // needed for itemEntered() to work


    this->preselectTimer = new QTimer(this);
    this->preselectTimer->setSingleShot(true);

    this->statusTimer = new QTimer(this);
    this->statusTimer->setSingleShot(false);

    this->selectTimer = new QTimer(this);
    this->selectTimer->setSingleShot(true);

    connect(this->statusTimer, &QTimer::timeout, this, &TreeWidget::onUpdateStatus);
    connect(this, &QTreeWidget::itemEntered, this, &TreeWidget::onItemEntered);
    connect(this, &QTreeWidget::itemCollapsed, this, &TreeWidget::onItemCollapsed);
    connect(this, &QTreeWidget::itemExpanded, this, &TreeWidget::onItemExpanded);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &TreeWidget::onItemSelectionChanged);
    connect(this, &QTreeWidget::itemChanged, this, &TreeWidget::onItemChanged);
    connect(this->preselectTimer, &QTimer::timeout, this, &TreeWidget::onPreSelectTimer);
    connect(this->selectTimer, &QTimer::timeout, this, &TreeWidget::onSelectTimer);
    preselectTime.start();

    visibilityIconDoubleClickTimer.setSingleShot(true);
    visibilityIconDoubleClickTimer.setInterval(QApplication::doubleClickInterval());

    setupText();
    if (!documentPixmap) {
        documentPixmap = std::make_unique<QPixmap>(Gui::BitmapFactory().pixmap("Document"));
        QIcon icon(*documentPixmap);
        documentPartialPixmap = std::make_unique<QPixmap>(
            icon.pixmap(documentPixmap->size(), QIcon::Disabled)
        );
    }
    setColumnHidden(1, TreeParams::getHideColumn());
    setColumnHidden(2, TreeParams::getHideInternalNames());
    header()->setVisible(!TreeParams::getHideColumn() || !TreeParams::getHideInternalNames());
    // The Body column (Cruth ARCHITECTURE §8.7) is always shown, right after the label.
    header()->moveSection(header()->visualIndex(3), 1);
    TreeParams::onFontSizeChanged();
}

TreeWidget::~TreeWidget()
{
    connectNewDocument.disconnect();
    connectDelDocument.disconnect();
    connectRenDocument.disconnect();
    connectActDocument.disconnect();
    connectRelDocument.disconnect();
    connectShowHidden.disconnect();
    connectChangedViewObj.disconnect();
    Instances.erase(this);
    if (_LastSelectedTreeWidget == this) {
        _LastSelectedTreeWidget = nullptr;
    }
}

const char* TreeWidget::getTreeName() const
{
    return myName.c_str();
}


bool TreeWidget::isObjectShowable(App::DocumentObject* obj)
{
    if (!obj || !obj->isAttachedToDocument()) {
        return true;
    }
    Gui::Document* doc = Application::Instance->getDocument(obj->getDocument());
    if (!doc) {
        return true;
    }
    if (Instances.empty()) {
        return true;
    }
    auto tree = *Instances.begin();
    auto it = tree->DocumentMap.find(doc);
    if (it != tree->DocumentMap.end()) {
        return it->second->isObjectShowable(obj);
    }
    return true;
}

bool TreeShared::_DisableCheckTopParent = false;

void TreeWidget::checkTopParent(App::DocumentObject*& obj, std::string& subname)
{
    if (_DisableCheckTopParent) {
        return;
    }
    if (!Instances.empty() && obj && obj->isAttachedToDocument()) {
        auto tree = *Instances.begin();
        auto it = tree->DocumentMap.find(Application::Instance->getDocument(obj->getDocument()));
        if (it != tree->DocumentMap.end()) {
            if (tree->statusTimer->isActive()) {
                bool locked = tree->blockSelection(true);
                tree->_updateStatus(false);
                tree->blockSelection(locked);
            }
            auto parent = it->second->getTopParent(obj, subname);
            if (parent) {
                obj = parent;
            }
        }
    }
}

void TreeWidget::resetItemSearch()
{
    if (!searchObject) {
        return;
    }
    auto it = ObjectTable.find(searchObject);
    if (it != ObjectTable.end()) {
        for (auto& data : it->second) {
            if (!data) {
                continue;
            }
            for (auto item : data->items) {
                static_cast<DocumentObjectItem*>(item)->restoreBackground();
            }
        }
    }
    searchObject = nullptr;
}

void TreeWidget::startItemSearch(QLineEdit* edit)
{
    resetItemSearch();
    searchDoc = nullptr;
    searchContextDoc = nullptr;
    auto sels = selectedItems();
    if (sels.size() == 1) {
        if (sels.front()->type() == DocumentType) {
            searchDoc = static_cast<DocumentItem*>(sels.front())->document();
        }
        else if (sels.front()->type() == ObjectType) {
            auto item = static_cast<DocumentObjectItem*>(sels.front());
            searchDoc = item->object()->getDocument();
            searchContextDoc = item->getOwnerDocument()->document();
        }
    }
    else {
        searchDoc = Application::Instance->activeDocument();
    }

    App::DocumentObject* obj = nullptr;
    if (searchContextDoc && !searchContextDoc->getDocument()->getObjects().empty()) {
        obj = searchContextDoc->getDocument()->getObjects().front();
    }
    else if (searchDoc && !searchDoc->getDocument()->getObjects().empty()) {
        obj = searchDoc->getDocument()->getObjects().front();
    }

    if (obj) {
        static_cast<ExpressionLineEdit*>(edit)->setDocumentObject(obj);
    }
}

void TreeWidget::itemSearch(const QString& text, bool select)
{
    resetItemSearch();

    auto docItem = getDocumentItem(searchDoc);
    if (!docItem) {
        docItem = getDocumentItem(Application::Instance->activeDocument());
        if (!docItem) {
            FC_TRACE("item search no document");
            resetItemSearch();
            return;
        }
    }

    auto doc = docItem->document()->getDocument();
    const auto& objs = doc->getObjects();
    if (objs.empty()) {
        FC_TRACE("item search no objects");
        return;
    }
    std::string txt(text.toUtf8().constData());
    try {
        if (txt.empty()) {
            return;
        }
        if (txt.find("<<") == std::string::npos) {
            auto pos = txt.find('.');
            if (pos == std::string::npos) {
                txt += '.';
            }
            else if (pos != txt.size() - 1) {
                txt.insert(pos + 1, "<<");
                if (txt.back() != '.') {
                    txt += '.';
                }
                txt += ">>.";
            }
        }
        else if (txt.back() != '.') {
            txt += '.';
        }
        txt += "_self";
        auto path = App::ObjectIdentifier::parse(objs.front(), txt);
        if (path.getPropertyName() != "_self") {
            FC_TRACE("Object " << txt << " not found in " << doc->getName());
            return;
        }
        auto obj = path.getDocumentObject();
        if (!obj) {
            FC_TRACE("Object " << txt << " not found in " << doc->getName());
            return;
        }
        std::string subname = path.getSubObjectName();
        App::DocumentObject* parent = nullptr;
        if (searchContextDoc) {
            auto it = DocumentMap.find(searchContextDoc);
            if (it != DocumentMap.end()) {
                parent = it->second->getTopParent(obj, subname);
                if (parent) {
                    obj = parent;
                    docItem = it->second;
                    doc = docItem->document()->getDocument();
                }
            }
        }
        if (!parent) {
            parent = docItem->getTopParent(obj, subname);
            while (!parent) {
                if (docItem->document()->getDocument() == obj->getDocument()) {
                    // this shouldn't happen
                    FC_LOG("Object " << txt << " not found in " << doc->getName());
                    return;
                }
                auto it = DocumentMap.find(Application::Instance->getDocument(obj->getDocument()));
                if (it == DocumentMap.end()) {
                    return;
                }
                docItem = it->second;
                parent = docItem->getTopParent(obj, subname);
            }
            obj = parent;
        }
        auto item = docItem->findItemByObject(true, obj, subname.c_str());
        if (!item) {
            FC_TRACE("item " << txt << " not found in " << doc->getName());
            return;
        }
        scrollToItem(item);
        Selection().setPreselect(
            obj->getDocument()->getName(),
            obj->getNameInDocument(),
            subname.c_str(),
            0,
            0,
            0,
            SelectionChanges::MsgSource::TreeView
        );
        if (select) {
            Gui::Selection().selStackPush();
            Gui::Selection().clearSelection();
            Gui::Selection().addSelection(
                obj->getDocument()->getName(),
                obj->getNameInDocument(),
                subname.c_str()
            );
            Gui::Selection().selStackPush();
        }
        else {
            searchObject = item->object()->getObject();
            item->setBackground(0, QColor(255, 255, 0, 100));
        }
        FC_TRACE("found item " << txt);
    }
    catch (...) {
        FC_TRACE("item " << txt << " search exception in " << doc->getName());
    }
}

Gui::Document* TreeWidget::selectedDocument()
{
    for (auto tree : Instances) {
        if (!tree->isVisible()) {
            continue;
        }
        auto sels = tree->selectedItems();
        if (sels.size() == 1 && sels[0]->type() == DocumentType) {
            return static_cast<DocumentItem*>(sels[0])->document();
        }
    }
    return nullptr;
}

void TreeWidget::updateStatus(bool delay)
{
    for (auto tree : Instances) {
        tree->_updateStatus(delay);
    }
}

void TreeWidget::_updateStatus(bool delay)
{
    // When running from a different thread Qt will raise a warning
    // when trying to start the QTimer
    if (Q_UNLIKELY(thread() != QThread::currentThread())) {
        return;
    }

    if (!delay) {
        if (!ChangedObjects.empty() || !NewObjects.empty()) {
            onUpdateStatus();
        }
        return;
    }
    int timeout = TreeParams::getStatusTimeout();
    if (timeout < 0) {
        timeout = 1;
    }
    statusTimer->start(timeout);
}

void TreeWidget::hideEvent(QHideEvent* ev)
{
    QTreeWidget::hideEvent(ev);
}

void TreeWidget::showEvent(QShowEvent* ev)
{
    QTreeWidget::showEvent(ev);
}

DocumentItem* TreeWidget::getDocumentItem(const Gui::Document* doc) const
{
    auto it = DocumentMap.find(doc);
    if (it != DocumentMap.end()) {
        return it->second;
    }
    return nullptr;
}

static int& treeIconSize()
{
    static int _treeIconSize = -1;

    if (_treeIconSize < 0) {
        _treeIconSize = TreeParams::getIconSize();
    }
    return _treeIconSize;
}

int TreeWidget::iconHeight() const
{
    return treeIconSize();
}

void TreeWidget::setIconHeight(int height)
{
    if (treeIconSize() == height) {
        return;
    }

    treeIconSize() = height;
    if (treeIconSize() <= 0) {
        treeIconSize() = std::max(10, iconSize());
    }

    for (auto tree : Instances) {
        tree->setIconSize(QSize(treeIconSize(), treeIconSize()));
    }
}

int TreeWidget::iconSize()
{
    static int defaultSize;
    if (defaultSize == 0) {
        auto tree = instance();
        if (tree) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            defaultSize = tree->viewOptions().decorationSize.width();
#else
            QStyleOptionViewItem opt;
            tree->initViewItemOption(&opt);
            defaultSize = opt.decorationSize.width();
#endif
        }
        else {
            defaultSize = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        }
    }
    if (treeIconSize() > 0) {
        return std::max(10, treeIconSize());
    }
    return defaultSize;
}

TreeWidget* TreeWidget::instance()
{
    auto res = _LastSelectedTreeWidget;
    if (res && res->isVisible()) {
        return res;
    }
    for (auto inst : Instances) {
        if (!res) {
            res = inst;
        }
        if (inst->isVisible()) {
            return inst;
        }
    }
    return res;
}

void TreeWidget::setupResizableColumn(TreeWidget* tree)
{
    auto mode = TreeParams::getResizableColumn() ? QHeaderView::Interactive
                                                 : QHeaderView::ResizeToContents;
    for (auto inst : Instances) {
        if (!tree || tree == inst) {
            inst->header()->setSectionResizeMode(0, mode);
            inst->header()->setSectionResizeMode(1, mode);
            inst->header()->setSectionResizeMode(2, mode);
            inst->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
            if (TreeParams::getResizableColumn()) {
                QSignalBlocker blocker(inst);
                if (TreeParams::getColumnSize1() > 0) {
                    inst->header()->resizeSection(0, TreeParams::getColumnSize1());
                }
                if (TreeParams::getColumnSize2() > 0) {
                    inst->header()->resizeSection(1, TreeParams::getColumnSize2());
                }
                if (TreeParams::getColumnSize3() > 0) {
                    inst->header()->resizeSection(2, TreeParams::getColumnSize3());
                }
            }
        }
    }
}

Qt::DropActions TreeWidget::supportedDropActions() const
{
    return Qt::LinkAction | Qt::CopyAction | Qt::MoveAction;
}

bool TreeWidget::event(QEvent* e)
{
    return QTreeWidget::event(e);
}

bool TreeWidget::eventFilter(QObject*, QEvent* ev)
{
    switch (ev->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            auto ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() != Qt::Key_Escape) {
                // Qt 5 only recheck key modifier on mouse move, so generate a fake
                // event to trigger drag cursor change
                auto mouseEvent = new QMouseEvent(
                    QEvent::MouseMove,
                    mapFromGlobal(QCursor::pos()),
                    QCursor::pos(),
                    Qt::NoButton,
                    QApplication::mouseButtons(),
                    QApplication::queryKeyboardModifiers()
                );
                QApplication::postEvent(this, mouseEvent);
            }
            break;
        }
        default:
            break;
    }
    return false;
}

namespace Gui
{

bool isTreeViewDragging()
{
    return _DraggingActive;
}

}  // namespace Gui

void TreeWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Find)) {
        event->accept();
        onSearchObjects();
        return;
    }
    else if (event->modifiers() == Qt::AltModifier) {
        if (event->key() == Qt::Key_Left) {
            for (auto& item : selectedItems()) {
                item->setExpanded(false);
            }
            event->accept();
            return;
        }
        else if (event->key() == Qt::Key_Right) {
            for (auto& item : selectedItems()) {
                item->setExpanded(true);
            }
            event->accept();
            return;
        }
        else if (event->key() == Qt::Key_Up) {
            for (auto& item : selectedItems()) {
                item->setExpanded(true);
                for (auto& child : childrenOfItem(*item)) {
                    child->setExpanded(false);
                }
            }
            event->accept();
            return;
        }
        else if (event->key() == Qt::Key_Down) {
            for (auto& item : selectedItems()) {
                item->setExpanded(true);
                for (auto& child : childrenOfItem(*item)) {
                    child->setExpanded(true);
                }
            }
            event->accept();
            return;
        }
    }

    QTreeWidget::keyPressEvent(event);
}

bool TreeWidget::isHeldBackRowAt(const QPoint& pos) const
{
    QTreeWidgetItem* item = itemAt(pos);
    return item && item->type() == ObjectType
        && static_cast<DocumentObjectItem*>(item)->object()->isHeldBack();
}

void TreeWidget::mousePressEvent(QMouseEvent* event)
{
    if (isHeldBackRowAt(event->pos())) {
        event->accept();
        return;
    }
    // A swatch is the handle for its body: clicking it selects the body, not the step's row.
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        auto bodies = bodiesAtSwatch(event->pos());
        if (!bodies.empty()) {
            Selection().clearSelection();
            for (auto* body : bodies) {
                Selection().addSelection(body->getDocument()->getName(), body->getNameInDocument());
            }
            event->accept();
            return;
        }
    }
    QTreeWidgetItem* pressed = itemAt(event->pos());
    // A body selected by its swatch has no row, so Qt sees nothing to deselect on a click in
    // the empty space below the rows; clear it here, as a click on empty 3D space does.
    if (!pressed && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        Selection().clearSelection();
    }
    if (pressed && pressed->type() == StopRowType && event->button() == Qt::LeftButton) {
        draggedStopRow = pressed;
        stopRowTarget = nullptr;
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (isVisibilityIconEnabled()) {
        QTreeWidgetItem* item = itemAt(event->pos());
        if (item && item->type() == TreeWidget::ObjectType && event->button() == Qt::LeftButton) {
            auto objitem = static_cast<DocumentObjectItem*>(item);

            // Mouse position relative to viewport
            auto mousePos = event->pos();

            // Rect occupied by the item relative to viewport
            auto iconRect = visualItemRect(objitem);

            auto style = this->style();

            // If the checkboxes are visible, these are displayed before the icon
            // and we have to compensate for its width.
            if (isSelectionCheckBoxesEnabled()) {
                int checkboxWidth = style->pixelMetric(QStyle::PM_IndicatorWidth)
                    + style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
                iconRect.adjust(checkboxWidth, 0, 0, 0);
            }

            int const margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin) + 1;
            iconRect.adjust(margin, 0, 0, 0);

            // We are interested in the first icon (visibility icon)
            iconRect.setWidth(iconSize());

            // If the visibility icon was clicked, toggle the DocumentObject visibility
            if (iconRect.contains(mousePos)) {
                auto obj = objitem->object()->getObject();
                char const* objname = obj->getNameInDocument();

                App::DocumentObject* parent = nullptr;
                std::ostringstream subName;
                objitem->getSubName(subName, parent);

                // Try the ElementVisible API, if that is not supported toggle the Visibility property
                int visible = -1;
                if (parent) {
                    visible = parent->isElementVisible(objname);
                }
                if (parent && visible >= 0) {
                    parent->setElementVisible(objname, !visible);
                }
                else {
                    visible = obj->Visibility.getValue();
                    obj->Visibility.setValue(!visible);
                }
                visibilityIconDoubleClickTimer.start();

                // to prevent selection of the item via QTreeWidget::mousePressEvent
                event->accept();
                return;
            }
        }
    }

    QTreeWidget::mousePressEvent(event);
}

void TreeWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (isHeldBackRowAt(event->pos())) {
        event->accept();
        return;
    }
    // A swatch's double-click opens its body's panel, not the step for editing.
    auto swatchBodies = bodiesAtSwatch(event->pos());
    if (!swatchBodies.empty()) {
        event->accept();
        if (swatchBodies.size() == 1) {
            auto* body = swatchBodies.front();
            Selection().clearSelection();
            Selection().addSelection(body->getDocument()->getName(), body->getNameInDocument());
            auto& commands = Application::Instance->commandManager();
            if (commands.getCommandByName("PartDesign_BodyInfo")) {
                commands.runCommandByName("PartDesign_BodyInfo");
            }
        }
        return;
    }
    QTreeWidgetItem* item = itemAt(event->pos());
    if (!item) {
        return;
    }

    if (visibilityIconDoubleClickTimer.isActive()) {
        TreeWidget::mousePressEvent(event);
        return;
    }

    try {
        if (item->type() == TreeWidget::DocumentType) {
            Gui::Document* doc = static_cast<DocumentItem*>(item)->document();
            if (!doc) {
                return;
            }
            if (doc->getDocument()->testStatus(App::Document::PartialDoc)) {
                contextItem = item;
                onReloadDoc();
                return;
            }
            if (!doc->setActiveView()) {
                doc->setActiveView(nullptr, View3DInventor::getClassTypeId());
            }
        }
        else if (item->type() == TreeWidget::ObjectType) {
            auto objitem = static_cast<DocumentObjectItem*>(item);
            ViewProviderDocumentObject* vp = objitem->object();

            Gui::Document* guidoc = objitem->getOwnerDocument()->document();
            App::Document* appdoc = guidoc->getDocument();

            guidoc->setActiveView(vp);
            auto manager = Application::Instance->macroManager();
            auto lines = manager->getLines();

            std::ostringstream ss;
            ss << Command::getObjectCmd(vp->getObject()) << ".ViewObject.doubleClicked()";

            const char* commandText = vp->getTransactionText();
            if (commandText) {
                appdoc->openTransaction(commandText);

                if (!vp->doubleClicked()) {
                    QTreeWidget::mouseDoubleClickEvent(event);
                }
                else if (lines == manager->getLines()) {
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }
            }
            else {
                if (!vp->doubleClicked()) {
                    QTreeWidget::mouseDoubleClickEvent(event);
                }
                else if (lines == manager->getLines()) {
                    manager->addLine(MacroManager::Gui, ss.str().c_str());
                }
            }
        }
    }
    catch (Base::Exception& e) {
        e.reportException();
    }
    catch (std::exception& e) {
        FC_ERR("C++ exception: " << e.what());
    }
    catch (...) {
        FC_ERR("Unknown exception");
    }
}

QTreeWidgetItem* TreeWidget::stopRowTargetAt(const QPoint& pos) const
{
    QTreeWidgetItem* item = itemAt(pos);
    if (!draggedStopRow || !item || item->type() != ObjectType
        || item->parent() != draggedStopRow->parent()
        || !static_cast<DocumentObjectItem*>(item)->object()->getObject()->isRollbackStop()) {
        return nullptr;
    }
    return item;
}

void TreeWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!draggedStopRow) {
        QTreeWidget::mouseMoveEvent(event);
        return;
    }
    QTreeWidgetItem* target = stopRowTargetAt(event->pos());
    if (target != stopRowTarget) {
        stopRowTarget = target;
        viewport()->update();
    }
    event->accept();
}

void TreeWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!draggedStopRow) {
        QTreeWidget::mouseReleaseEvent(event);
        return;
    }
    QTreeWidgetItem* row = draggedStopRow;
    QTreeWidgetItem* target = stopRowTargetAt(event->pos());
    draggedStopRow = nullptr;
    stopRowTarget = nullptr;
    viewport()->unsetCursor();
    viewport()->update();
    event->accept();

    auto* docItem = dynamic_cast<DocumentItem*>(row->parent());
    if (!docItem || !target) {
        return;
    }
    App::DocumentObject* step = static_cast<DocumentObjectItem*>(target)->object()->getObject();
    App::Document* doc = docItem->document()->getDocument();
    // Moving the marker is not an undo step, as in most CAD tools.
    Command::doCommand(
        Command::Doc,
        "App.getDocument('%s').rollBackTo(%s)",
        doc->getName(),
        Command::getObjectCmd(step).c_str()
    );
    Command::updateActive();
}

void TreeWidget::drawRow(
    QPainter* painter,
    const QStyleOptionViewItem& options,
    const QModelIndex& index
) const
{
    drawRowBody(painter, options, index);
    // Where a dragged stop row would land: a line under the step it would follow.
    if (stopRowTarget && itemFromIndex(index) == stopRowTarget) {
        const int bottom = options.rect.bottom();
        painter->fillRect(
            QRect(0, bottom - 1, viewport()->width(), 2),
            palette().color(QPalette::Highlight)
        );
    }
}

void TreeWidget::drawRowBody(
    QPainter* painter,
    const QStyleOptionViewItem& options,
    const QModelIndex& index
) const
{
    // Cruth #3 (ARCHITECTURE §8.2): the rows a selected step came from are tinted with their
    // body's colour across the whole row, and the selected row gets the same tint in place of the
    // default selection highlight, so they read as one family.
    auto item = itemFromIndex(index);
    if (!item) {
        QTreeWidget::drawRow(painter, options, index);
        return;
    }
    bool selected = item->isSelected();
    auto found = lineageTints.end();
    if (item->type() == ObjectType) {
        auto* obj = static_cast<DocumentObjectItem*>(item)->object()->getObject();
        found = lineageTints.find(obj);
        // The list's own row selection can lag the real one; the real one decides.
        selected = lineageSelected.count(obj) > 0;
    }
    if (found == lineageTints.end() && !selected) {
        QTreeWidget::drawRow(painter, options, index);
        return;
    }

    // The body colour is laid over the row as a see-through wash, so it reads on any theme; the
    // palette cannot be trusted for the background, which the stylesheet paints.
    QColor tint = found != lineageTints.end() ? found->second : palette().color(QPalette::Highlight);
    tint.setAlpha(60);
    // The whole width, margin included: the row rectangle starts after the indentation.
    painter->fillRect(QRect(0, options.rect.top(), viewport()->width(), options.rect.height()), tint);

    // The style would paint its own selection colour in the indentation beside a selected row.
    QStyleOptionViewItem opt = options;
    opt.palette.setColor(QPalette::Highlight, Qt::transparent);
    QTreeWidget::drawRow(painter, opt, index);
}

void TreeWidget::slotNewDocument(const Gui::Document& Doc, bool isMainDoc)
{
    if (Doc.getDocument()->testStatus(App::Document::TempDoc)) {
        return;
    }
    auto item = new DocumentItem(&Doc, this->rootItem);
    if (isMainDoc) {
        this->expandItem(item);
    }
    item->setIcon(0, *documentPixmap);
    item->setText(0, QString::fromUtf8(Doc.getDocument()->Label.getValue()));
    DocumentMap[&Doc] = item;
    placeDocumentsInProject();
}


void TreeWidget::slotRenameDocument(const Gui::Document& Doc)
{
    // do nothing here
    Q_UNUSED(Doc);
}

void TreeWidget::slotChangedViewObject(const Gui::ViewProvider& vp, const App::Property& prop)
{
    if (!App::GetApplication().isRestoring() && vp.isDerivedFrom<ViewProviderDocumentObject>()) {
        const auto& vpd = static_cast<const ViewProviderDocumentObject&>(vp);
        if (&prop == &vpd.ShowInTree) {
            ChangedObjects.emplace(vpd.getObject(), 0);
            _updateStatus();
        }
    }
}

void TreeWidget::slotTouchedObject(const App::DocumentObject& obj)
{
    ChangedObjects.emplace(const_cast<App::DocumentObject*>(&obj), 0);
    _updateStatus();
}

void TreeWidget::slotShowHidden(const Gui::Document& Doc)
{
    auto it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end()) {
        it->second->updateItemsVisibility(it->second, it->second->showHidden());
    }
}

void TreeWidget::slotRelabelDocument(const Gui::Document& Doc)
{
    auto it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end()) {
        it->second->setText(0, QString::fromUtf8(Doc.getDocument()->Label.getValue()));
        placeDocumentsInProject();
    }
}

namespace
{
std::string canonicalPath(const std::string& path)
{
    const QString canonical = QFileInfo(QString::fromStdString(path)).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical.toStdString();
}

/// Where a document from outside the project comes from: its own project, else its folder.
QString outsideSource(const std::string& path)
{
    try {
        const std::string ownProject = App::projectFileOf(path);
        if (!ownProject.empty()) {
            return QString::fromStdString(App::projectName(ownProject));
        }
    }
    catch (const Base::Exception&) {
    }
    return QFileInfo(QString::fromStdString(path)).absoluteDir().dirName();
}

void saveExpansion(QTreeWidgetItem* item, std::vector<QTreeWidgetItem*>& expanded)
{
    if (item->isExpanded()) {
        expanded.push_back(item);
    }
    for (int i = 0; i < item->childCount(); ++i) {
        saveExpansion(item->child(i), expanded);
    }
}
}  // namespace

std::vector<DocumentItem*> TreeWidget::documentItemsInOrder() const
{
    std::vector<DocumentItem*> items;
    for (int i = 0; i < rootItem->childCount(); ++i) {
        QTreeWidgetItem* child = rootItem->child(i);
        if (child == projectItem) {
            for (int j = 0; j < child->childCount(); ++j) {
                if (auto* docItem = dynamic_cast<DocumentItem*>(child->child(j))) {
                    items.push_back(docItem);
                }
            }
        }
        else if (auto* docItem = dynamic_cast<DocumentItem*>(child)) {
            items.push_back(docItem);
        }
    }
    return items;
}

/// UI_GUIDE §2.2.2: the open documents the open project lists sit under its row, those from
/// outside marked with where they come from; any other document sits beside it.
void TreeWidget::placeDocumentsInProject()
{
    const std::string projectFile = ProjectPanel::openProjectFile().toStdString();
    std::map<std::string, QString> listed;  // canonical path -> where it comes from, if outside
    QString projectName;
    if (!projectFile.empty()) {
        try {
            projectName = QString::fromStdString(App::projectName(projectFile));
            for (const std::string& path : App::projectDocuments(projectFile)) {
                listed[canonicalPath(path)];
            }
            for (const App::OutsideDocument& outside : App::outsideDocuments(projectFile)) {
                listed[canonicalPath(outside.path)] = outsideSource(outside.path);
            }
        }
        catch (const Base::Exception&) {
            // An unreadable project file is reported by the Project panel; the row stays.
        }
    }

    if (!projectFile.empty() && !projectItem) {
        projectItem = new QTreeWidgetItem(ProjectType);
        projectItem->setFlags(Qt::ItemIsEnabled);
        projectItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
        rootItem->insertChild(0, projectItem);
        projectItem->setExpanded(true);
    }
    if (projectItem) {
        projectItem->setText(0, projectName);
        projectItem->setToolTip(0, QDir::toNativeSeparators(QString::fromStdString(projectFile)));
    }

    for (DocumentItem* docItem : documentItemsInOrder()) {
        App::Document* doc = docItem->document()->getDocument();
        const char* fileName = doc->FileName.getValue();
        const auto found = Base::Tools::isNullOrEmpty(fileName)
            ? listed.end()
            : listed.find(canonicalPath(fileName));
        QTreeWidgetItem* wanted = found != listed.end() ? projectItem : rootItem;

        QString text = QString::fromUtf8(doc->Label.getValue());
        QString tip;
        if (found != listed.end() && !found->second.isEmpty()) {
            text = tr("%1 (external)").arg(text);
            tip = tr("From %1\n%2")
                      .arg(found->second, QDir::toNativeSeparators(QString::fromUtf8(fileName)));
        }
        docItem->setText(0, text);
        docItem->setToolTip(0, tip);

        QTreeWidgetItem* current = docItem->parent() ? docItem->parent() : rootItem;
        if (current == wanted) {
            continue;
        }
        std::vector<QTreeWidgetItem*> expanded;
        saveExpansion(docItem, expanded);
        current->takeChild(current->indexOfChild(docItem));
        wanted->addChild(docItem);
        for (QTreeWidgetItem* item : expanded) {
            item->setExpanded(true);
        }
    }
}

void TreeWidget::slotActiveDocument(const Gui::Document& Doc)
{
    auto jt = DocumentMap.find(&Doc);
    if (jt == DocumentMap.end()) {
        return;  // signal is emitted before the item gets created
    }
    int displayMode = TreeParams::getDocumentMode();
    for (auto it = DocumentMap.begin(); it != DocumentMap.end(); ++it) {
        QFont f = it->second->font(0);
        f.setBold(it == jt);
        it->second->setHidden(0 == displayMode && it != jt);
        if (2 == displayMode) {
            it->second->setExpanded(it == jt);
        }
        // this must be done as last step
        it->second->setFont(0, f);
    }
}

struct UpdateDisabler
{
    QWidget& widget;
    int& blocked;
    bool visible {false};
    bool focus {false};

    // Note! DO NOT block signal here, or else
    // QTreeWidgetItem::setChildIndicatorPolicy() does not work
    UpdateDisabler(QWidget& w, int& blocked)
        : widget(w)
        , blocked(blocked)
    {
        if (++blocked > 1) {
            return;
        }
        focus = widget.hasFocus();
        visible = widget.isVisible();
        if (visible) {
            // setUpdatesEnabled(false) does not seem to speed up anything.
            // setVisible(false) on the other hand makes QTreeWidget::setData
            // (i.e. any change to QTreeWidgetItem) faster by 10+ times.
            //
            // widget.setUpdatesEnabled(false);

            widget.setVisible(false);
        }
    }
    ~UpdateDisabler()
    {
        if (blocked <= 0 || --blocked != 0) {
            return;
        }

        if (visible) {
            widget.setVisible(true);
            if (focus) {
                widget.setFocus();
            }
        }
    }
};

void TreeWidget::onUpdateStatus()
{
    if (this->state() == DraggingState || App::GetApplication().isRestoring()) {
        _updateStatus();
        return;
    }

    for (auto& v : DocumentMap) {
        if (v.first->isPerformingTransaction()) {
            // We have to delay item creation until undo/redo is done, because the
            // object re-creation while in transaction may break tree view item
            // update logic. For example, a parent object re-created before its
            // children, but the parent's link property already contains all the
            // (detached) children.
            _updateStatus();
            return;
        }
    }

    FC_LOG("begin update status");

    UpdateDisabler disabler(*this, updateBlocked);

    std::vector<App::DocumentObject*> errors;

    // Use a local copy in case of nested calls
    auto localNewObjects = NewObjects;
    NewObjects.clear();

    // Checking for new objects
    for (auto& v : localNewObjects) {
        auto doc = App::GetApplication().getDocument(v.first.c_str());
        if (!doc) {
            continue;
        }
        auto gdoc = Application::Instance->getDocument(doc);
        if (!gdoc) {
            continue;
        }
        auto docItem = getDocumentItem(gdoc);
        if (!docItem) {
            continue;
        }
        for (auto id : v.second) {
            auto obj = doc->getObjectByID(id);
            if (!obj) {
                continue;
            }
            if (obj->isError()) {
                errors.push_back(obj);
            }
            if (docItem->ObjectMap.contains(obj)) {
                continue;
            }
            auto vpd = freecad_cast<ViewProviderDocumentObject*>(gdoc->getViewProvider(obj));
            if (vpd) {
                docItem->createNewItem(*vpd);
            }
        }
    }

    // Use a local copy in case of nested calls
    auto localChangedObjects = ChangedObjects;
    ChangedObjects.clear();
    const bool bodiesMayHaveMoved = !localChangedObjects.empty() || !localNewObjects.empty();

    // Update children of changed objects
    for (auto& v : localChangedObjects) {
        auto obj = v.first;

        auto iter = ObjectTable.find(obj);
        if (iter == ObjectTable.end()) {
            continue;
        }

        if (v.second.test(CS_Error) && obj->isError()) {
            errors.push_back(obj);
        }

        if (!iter->second.empty()) {
            auto data = *iter->second.begin();
            bool itemHidden = !data->viewObject->showInTree();
            if (data->itemHidden != itemHidden) {
                for (auto& data : iter->second) {
                    data->itemHidden = itemHidden;
                    if (data->docItem->showHidden()) {
                        continue;
                    }
                    for (auto item : data->items) {
                        item->setHidden(itemHidden);
                    }
                }
            }
        }

        updateChildren(iter->first, iter->second, v.second.test(CS_Output), false);
    }

    FC_LOG("update item status");
    for (auto pos = DocumentMap.begin(); pos != DocumentMap.end(); ++pos) {
        pos->second->testStatus();
        if (bodiesMayHaveMoved) {
            pos->second->updateBodyColumn();
        }
        pos->second->updateStopRows();
    }

    // Checking for just restored documents
    for (auto& v : DocumentMap) {
        auto docItem = v.second;

        for (auto obj : docItem->PopulateObjects) {
            docItem->populateObject(obj);
        }
        docItem->PopulateObjects.clear();

        auto doc = v.first->getDocument();

        if (!docItem->connectChgObject.connected()) {
            // NOLINTBEGIN
            docItem->connectChgObject = docItem->document()->signalChangedObject.connect(
                std::bind(&TreeWidget::slotChangeObject, this, sp::_1, sp::_2)
            );
            docItem->connectTouchedObject = doc->signalTouchedObject.connect(
                std::bind(&TreeWidget::slotTouchedObject, this, sp::_1)
            );
            // NOLINTEND
        }

        if (doc->testStatus(App::Document::PartialDoc)) {
            docItem->setIcon(0, *documentPartialPixmap);
        }
        else if (docItem->_ExpandInfo) {
            for (auto& entry : *docItem->_ExpandInfo) {
                const char* name = entry.first.c_str();
                bool legacy = name[0] == '*';
                if (legacy) {
                    ++name;
                }
                auto obj = doc->getObject(name);
                if (!obj) {
                    continue;
                }
                auto iter = docItem->ObjectMap.find(obj);
                if (iter == docItem->ObjectMap.end()) {
                    continue;
                }
                if (iter->second->rootItem) {
                    docItem->restoreItemExpansion(entry.second, iter->second->rootItem);
                }
                else if (legacy && !iter->second->items.empty()) {
                    auto item = *iter->second->items.begin();
                    item->setExpanded(true);
                }
            }
        }
        docItem->_ExpandInfo.reset();
    }

    if (Selection().hasSelection() && !selectTimer->isActive() && !this->isSelectionBlocked()) {
        this->blockSelection(true);
        currentDocItem = nullptr;
        for (auto& v : DocumentMap) {
            v.second->setSelected(false);
            v.second->selectItems();
        }
        this->blockSelection(false);
    }

    auto activeDocItem = getDocumentItem(Application::Instance->activeDocument());

    QTreeWidgetItem* errItem = nullptr;
    for (auto obj : errors) {
        DocumentObjectDataPtr data;
        if (activeDocItem) {
            auto it = activeDocItem->ObjectMap.find(obj);
            if (it != activeDocItem->ObjectMap.end()) {
                data = it->second;
            }
        }
        if (!data) {
            auto docItem = getDocumentItem(Application::Instance->getDocument(obj->getDocument()));
            if (docItem) {
                auto it = docItem->ObjectMap.find(obj);
                if (it != docItem->ObjectMap.end()) {
                    data = it->second;
                }
            }
        }
        if (data) {
            auto item = data->rootItem;
            if (!item && !data->items.empty()) {
                item = *data->items.begin();
                data->docItem->showItem(item, false, true);
            }
            if (!errItem) {
                errItem = item;
            }
        }
    }
    if (errItem) {
        scrollToItem(errItem);
    }

    updateGeometries();
    statusTimer->stop();

    FC_LOG("done update status");
}

void TreeWidget::onItemCollapsed(QTreeWidgetItem* item)
{
    // object item collapsed
    if (item && item->type() == TreeWidget::ObjectType) {
        static_cast<DocumentObjectItem*>(item)->setExpandedStatus(false);
    }
}

void TreeWidget::onItemExpanded(QTreeWidgetItem* item)
{
    // object item expanded
    if (item && item->type() == TreeWidget::ObjectType) {
        auto objItem = static_cast<DocumentObjectItem*>(item);
        objItem->setExpandedStatus(true);
        objItem->getOwnerDocument()->populateItem(objItem, false, false);
    }
}

void TreeWidget::scrollItemToTop()
{
    auto doc = Application::Instance->activeDocument();
    for (auto tree : Instances) {
        if (!tree->isSelectionAttached() || tree->isSelectionBlocked()) {
            continue;
        }

        tree->_updateStatus(false);

        if (doc
            && Gui::Selection().hasSelection(doc->getDocument()->getName(), ResolveMode::NoResolve)) {
            auto it = tree->DocumentMap.find(doc);
            if (it != tree->DocumentMap.end()) {
                bool lock = tree->blockSelection(true);
                it->second->selectItems(DocumentItem::SR_FORCE_EXPAND);
                tree->blockSelection(lock);
            }
        }
        else {
            tree->blockSelection(true);
            for (DocumentItem* docItem : tree->documentItemsInOrder()) {
                auto doc = docItem->document()->getDocument();
                if (Gui::Selection().hasSelection(doc->getName(), ResolveMode::OldStyleElement)) {
                    tree->currentDocItem = docItem;
                    docItem->selectItems(DocumentItem::SR_FORCE_EXPAND);
                    tree->currentDocItem = nullptr;
                    break;
                }
            }
            tree->blockSelection(false);
        }
        tree->selectTimer->stop();
        tree->_updateStatus(false);
    }
}

void TreeWidget::expandSelectedItems(TreeItemMode mode)
{
    if (!isSelectionAttached()) {
        return;
    }

    const auto items = selectedItems();
    for (auto item : items) {
        switch (mode) {
            case TreeItemMode::ExpandPath: {
                QTreeWidgetItem* parentItem = item->parent();
                while (parentItem) {
                    parentItem->setExpanded(true);
                    parentItem = parentItem->parent();
                }
                item->setExpanded(true);
                break;
            }
            case TreeItemMode::ExpandItem:
                item->setExpanded(true);
                break;
            case TreeItemMode::CollapseItem:
                item->setExpanded(false);
                break;
            case TreeItemMode::ToggleItem:
                if (item->isExpanded()) {
                    item->setExpanded(false);
                }
                else {
                    item->setExpanded(true);
                }
                break;
        }
    }
}

void TreeWidget::setupText()
{
    this->headerItem()->setText(0, tr("Labels & Attributes"));
    this->headerItem()->setText(1, tr("Description"));
    this->headerItem()->setText(2, tr("Internal name"));
    this->headerItem()->setText(3, tr("Bodies"));

    this->showHiddenAction->setText(tr("Show Items Hidden in Tree View"));
    this->showHiddenAction->setStatusTip(
        tr("Shows items that are marked as 'hidden' in the tree view")
    );

    this->toggleVisibilityInTreeAction->setText(tr("Toggle Visibility in Tree View"));
    this->toggleVisibilityInTreeAction->setStatusTip(
        tr("Toggles the visibility of selected items in the tree view")
    );

    this->createGroupAction->setText(tr("Create Group"));
    this->createGroupAction->setStatusTip(tr("Creates a group"));

    this->relabelObjectAction->setText(tr("Rename"));
    this->relabelObjectAction->setStatusTip(tr("Renames object"));

    this->finishEditingAction->setText(tr("Finish Editing"));
    this->finishEditingAction->setStatusTip(tr("Finishes editing object"));

    this->selectDependentsAction->setText(tr("Add Dependent Objects to Selection"));
    this->selectDependentsAction->setStatusTip(tr("Adds all dependent objects to the selection"));

    this->closeDocAction->setText(tr("Close Document"));
    this->closeDocAction->setStatusTip(tr("Closes the document"));

#ifdef Q_OS_MAC
    this->openFileLocationAction->setText(tr("Reveal in Finder"));
    this->openFileLocationAction->setStatusTip(tr("Reveals the current file location in Finder"));
#else
    this->openFileLocationAction->setText(tr("Open File Location"));
    this->openFileLocationAction->setStatusTip(tr("Opens the current file location"));
#endif

    this->reloadDocAction->setText(tr("Reload Document"));
    this->reloadDocAction->setStatusTip(tr("Reloads a partially loaded document"));

    this->skipRecomputeAction->setText(tr("Skip Recomputes"));
    this->skipRecomputeAction->setStatusTip(tr("Enables or disables the recomputations of document"));

    this->allowPartialRecomputeAction->setText(tr("Allow Partial Recomputes"));
    this->allowPartialRecomputeAction->setStatusTip(tr(
        "Enables or disables the recomputating editing object when 'skip recomputation' is enabled"
    ));

    this->markRecomputeAction->setText(tr("Mark to Recompute"));
    this->markRecomputeAction->setStatusTip(tr("Marks this object to be recomputed"));
    this->markRecomputeAction->setIcon(BitmapFactory().iconFromTheme("Std_MarkToRecompute"));

    this->recomputeObjectAction->setText(tr("Recompute Object"));
    this->recomputeObjectAction->setStatusTip(tr("Recomputes the selected object"));
    this->recomputeObjectAction->setIcon(BitmapFactory().iconFromTheme("view-refresh"));
}

void TreeWidget::syncView(ViewProviderDocumentObject* vp)
{
    if (currentDocItem && TreeParams::getSyncView()) {
        bool focus = hasFocus();
        currentDocItem->document()->setActiveView(vp);
        if (focus) {
            setFocus();
        }
    }
}

void TreeWidget::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        setupText();
    }

    QTreeWidget::changeEvent(e);
}


void TreeWidget::updateVisibilityIcons()
{
    for (auto tree : TreeWidget::Instances) {
        QSignalBlocker blocker(tree);
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            auto item = *it;
            if (item->type() == ObjectType) {
                auto objitem = static_cast<DocumentObjectItem*>(item);
                objitem->testStatus(true);
            }
        }
        tree->resizeColumnToContents(0);
    }
}

QList<QTreeWidgetItem*> TreeWidget::childrenOfItem(const QTreeWidgetItem& item) const
{
    QList children = QList<QTreeWidgetItem*>();

    // check item is in this tree
    if (!this->indexFromItem(&item).isValid()) {
        return children;
    }

    for (int i = 0; i < item.childCount(); i++) {
        children.append(item.child(i));
    }
    return children;
}

void TreeWidget::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (column == 0 && isSelectionCheckBoxesEnabled()) {
        bool selected = item->isSelected();
        bool checked = item->checkState(0) == Qt::Checked;
        if (checked != selected) {
            item->setSelected(checked);
        }
    }
}

// ----------------------------------------------------------------------------

/* TRANSLATOR Gui::TreePanel */

// ----------------------------------------------------------------------------

/* TRANSLATOR Gui::TreeDockWidget */

// ----------------------------------------------------------------------------


void TreeWidget::slotDeleteDocument(const Gui::Document& Doc)
{
    NewObjects.erase(Doc.getDocument()->getName());
    auto it = DocumentMap.find(&Doc);
    if (it != DocumentMap.end()) {
        UpdateDisabler disabler(*this, updateBlocked);
        auto docItem = it->second;
        for (auto& v : docItem->ObjectMap) {
            for (auto item : v.second->items) {
                item->myOwner = nullptr;
            }
            auto obj = v.second->viewObject->getObject();
            if (obj->getDocument() == Doc.getDocument()) {
                _slotDeleteObject(*v.second->viewObject, docItem);
                continue;
            }
            auto it = ObjectTable.find(obj);
            assert(it != ObjectTable.end());
            assert(it->second.size() > 1);
            it->second.erase(v.second);
        }
        QTreeWidgetItem* parent = docItem->parent() ? docItem->parent() : this->rootItem;
        parent->takeChild(parent->indexOfChild(docItem));
        delete docItem;
        DocumentMap.erase(it);
    }
}

void TreeWidget::slotDeleteObject(const Gui::ViewProviderDocumentObject& view)
{
    _slotDeleteObject(view, nullptr);
}

void TreeWidget::_slotDeleteObject(const Gui::ViewProviderDocumentObject& view, DocumentItem* deletingDoc)
{
    auto obj = view.getObject();
    auto itEntry = ObjectTable.find(obj);
    if (itEntry == ObjectTable.end()) {
        return;
    }

    if (itEntry->second.empty()) {
        ObjectTable.erase(itEntry);
        return;
    }

    TREE_LOG("delete object " << obj->getFullName());

    // Block all selection signals during deletion to prevent cascading selection change events
    // during item creation or deletion
    bool lock = blockSelection(true);
    bool needUpdate = false;
    QTreeWidgetItem* newFocusItem = nullptr;
    bool hadFocus = (QApplication::focusWidget() == this);

    for (const auto& data : itEntry->second) {
        DocumentItem* docItem = data->docItem;
        if (docItem == deletingDoc) {
            continue;
        }

        auto doc = docItem->document()->getDocument();
        auto& items = data->items;

        if (obj->getDocument() == doc) {
            docItem->_ParentMap.erase(obj);
        }

        for (auto cit = items.begin(), citNext = cit; cit != items.end(); cit = citNext) {
            ++citNext;
            DocumentObjectItem* itemToDelete = *cit;

            // get next item based on currently deleted item to select it
            // as the next one
            if (currentItem() == itemToDelete && !newFocusItem) {
                QTreeWidgetItem* parent = itemToDelete->parent();
                int index = parent->indexOfChild(itemToDelete);
                if (index > 0) {
                    newFocusItem = parent->child(index - 1);
                }
                else if (parent->childCount() > 1) {
                    newFocusItem = parent->child(index + 1);
                }
                else {
                    // no siblings, move to parent
                    newFocusItem = parent;
                }
            }

            itemToDelete->myOwner = nullptr;
            delete itemToDelete;
        }

        // Check for any child of the deleted object that is not in the tree, and put it
        // under document item.
        for (auto child : data->children) {
            auto childVp = docItem->getViewProvider(child);
            if (!childVp || child->getDocument() != doc) {
                continue;
            }
            docItem->_ParentMap[child].erase(obj);
            auto cit = docItem->ObjectMap.find(child);
            if (cit == docItem->ObjectMap.end() || cit->second->items.empty()) {
                if (docItem->createNewItem(*childVp)) {
                    needUpdate = true;
                }
            }
            else {
                auto childItem = *cit->second->items.begin();
                if (childItem->requiredAtRoot(false)) {
                    if (docItem->createNewItem(*childItem->object(), docItem, -1, childItem->myData)) {
                        needUpdate = true;
                    }
                }
            }
            childVp->setShowable(docItem->isObjectShowable(child));
        }
        docItem->ObjectMap.erase(obj);
    }
    ObjectTable.erase(itEntry);

    // Restore signal state
    blockSelection(lock);

    // restore focus to the appropriate item after deletion
    if (newFocusItem) {
        setCurrentItem(newFocusItem);
        newFocusItem->setSelected(true);
    }

    // restore focus to the tree widget if it had focus before deletion
    if (hadFocus) {
        QTimer::singleShot(0, this, [this]() {
            setFocus(Qt::OtherFocusReason);
            activateWindow();
        });
    }

    if (needUpdate) {
        _updateStatus();
    }
}

void TreeWidget::slotChangeObject(const Gui::ViewProviderDocumentObject& view, const App::Property& prop)
{

    auto obj = view.getObject();
    if (!obj || !obj->isAttachedToDocument()) {
        return;
    }

    auto itEntry = ObjectTable.find(obj);
    if (itEntry == ObjectTable.end() || itEntry->second.empty()) {
        return;
    }

    _updateStatus();

    // Let's not waste time on the newly added Visibility property in
    // DocumentObject.
    if (&prop == &obj->Visibility) {
        return;
    }

    if (&prop == &obj->Label) {
        const char* label = obj->Label.getValue();
        auto firstData = *itEntry->second.begin();
        if (firstData->label != label) {
            for (const auto& data : itEntry->second) {
                data->label = label;
                auto displayName = QString::fromUtf8(label);
                for (auto item : data->items) {
                    item->setText(0, displayName);
                }
            }
        }
        return;
    }

    if (&prop == &obj->Label2) {
        const char* label = obj->Label2.getValue();
        auto firstData = *itEntry->second.begin();
        if (firstData->label2 != label) {
            for (const auto& data : itEntry->second) {
                data->label2 = label;
                auto displayName = QString::fromUtf8(label);
                for (auto item : data->items) {
                    item->setText(1, displayName);
                }
            }
        }
        return;
    }

    auto& s = ChangedObjects[obj];
    if (prop.testStatus(App::Property::Output) || prop.testStatus(App::Property::NoRecompute)) {
        s.set(CS_Output);
    }
}

void TreeWidget::updateChildren(
    App::DocumentObject* obj,
    const std::set<DocumentObjectDataPtr>& dataSet,
    bool propOutput,
    bool force
)
{
    bool childrenChanged = false;
    std::vector<App::DocumentObject*> children;
    bool removeChildrenFromRoot = true;

    DocumentObjectDataPtr found;
    for (auto data : dataSet) {
        if (!found) {
            found = data;
            childrenChanged = found->updateChildren(force);
            removeChildrenFromRoot = found->viewObject->canRemoveChildrenFromRoot();
            if (!childrenChanged && found->removeChildrenFromRoot == removeChildrenFromRoot) {
                return;
            }
        }
        else if (childrenChanged) {
            data->updateChildren(found);
        }
        data->removeChildrenFromRoot = removeChildrenFromRoot;
        DocumentItem* docItem = data->docItem;
        for (auto item : data->items) {
            docItem->populateItem(item, true);
        }
    }

    if (force) {
        return;
    }

    if (childrenChanged && propOutput) {
        // When a property is marked as output, it will not touch its object,
        // and thus, its property change will not be propagated through
        // recomputation. So we have to manually check for each links here.
        for (auto link : App::GetApplication().getLinksTo(obj, App::GetLinkRecursive)) {
            if (ChangedObjects.contains(link)) {
                continue;
            }
            std::vector<App::DocumentObject*> linkedChildren;
            DocumentObjectDataPtr found;
            auto it = ObjectTable.find(link);
            if (it == ObjectTable.end()) {
                continue;
            }
            for (auto data : it->second) {
                if (!found) {
                    found = data;
                    if (!found->updateChildren(false)) {
                        break;
                    }
                }
                data->updateChildren(found);
                DocumentItem* docItem = data->docItem;
                for (auto item : data->items) {
                    docItem->populateItem(item, true);
                }
            }
        }
    }

    if (childrenChanged) {
        if (!selectTimer->isActive()) {
            onSelectionChanged(SelectionChanges());
        }

        // if the item is in a GeoFeatureGroup we may need to update that too, as the claim children
        // of the geofeaturegroup depends on what the childs claim
        auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(obj);
        if (grp && !ChangedObjects.contains(grp)) {
            auto iter = ObjectTable.find(grp);
            if (iter != ObjectTable.end()) {
                updateChildren(grp, iter->second, true, false);
            }
        }
    }
}

// ----------------------------------------------------------------------------


#include "moc_Tree.cpp"
