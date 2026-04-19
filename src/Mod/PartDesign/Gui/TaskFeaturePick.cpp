// SPDX-License-Identifier: LGPL-2.1-or-later

/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer                                       *
 *                                      <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include <QListIterator>
#include <QListWidgetItem>
#include <QTimer>


#include <ranges>

#include <App/Document.h>
#include <App/Origin.h>
#include <App/Datums.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Control.h>
#include <Gui/ViewProviderCoordinateSystem.h>
#include <Mod/PartDesign/App/Body.h>

#include "ui_TaskFeaturePick.h"
#include "TaskFeaturePick.h"
#include "Utils.h"

#include <Gui/ViewParams.h>


using namespace PartDesignGui;

// TODO Do ve should snap here to App:Part or GeoFeatureGroup/DocumentObjectGroup ? (2015-09-04,
// Fat-Zer)
const QString TaskFeaturePick::getFeatureStatusString(const featureStatus st)
{
    switch (st) {
        case validFeature:
            return tr("Valid");
        case invalidShape:
            return tr("Invalid shape");
        case noWire:
            return tr("No wire in sketch");
        case isUsed:
            return tr("Sketch already used by other feature");
        case otherBody:
            return tr("Belongs to another body");
        case otherPart:
            return tr("Belongs to another part");
        case notInBody:
            return tr("Doesn't belong to any body");
        case basePlane:
            return tr("Base plane");
        case afterTip:
            return tr("Feature is located after the tip of the body");
    }

    return QString();
}

TaskFeaturePick::TaskFeaturePick(
    std::vector<App::DocumentObject*>& objects,
    const std::vector<featureStatus>& status,
    bool singleFeatureSelect,
    QWidget* parent
)
    : TaskBox(Gui::BitmapFactory().pixmap("edit-select-all"), tr("Select Attachment"), true, parent)
    , ui(new Ui_TaskFeaturePick)
    , doSelection(false)
{

    proxy = new QWidget(this);
    ui->setupUi(proxy);

    // Phase 2: external features are always valid; hide the copy/xref controls
    ui->checkExternal->setVisible(false);

    // clang-format off
    connect(ui->checkUsed, &QCheckBox::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->checkOtherBody, &QCheckBox::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->checkOtherPart, &QCheckBox::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->radioIndependent, &QRadioButton::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->radioDependent, &QRadioButton::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->radioXRef, &QRadioButton::toggled, this, &TaskFeaturePick::onUpdate);
    connect(ui->listWidget, &QListWidget::itemSelectionChanged, this, &TaskFeaturePick::onItemSelectionChanged);
    connect(ui->listWidget, &QListWidget::itemDoubleClicked, this, &TaskFeaturePick::onDoubleClick);
    // clang-format on


    if (!singleFeatureSelect) {
        ui->listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }

    // NOTE: generally there shouldn't be more then one origin
    std::map<App::Origin*, Gui::DatumElements> originVisStatus;

    auto statusIt = status.cbegin();
    auto objIt = objects.begin();
    assert(status.size() == objects.size());

    bool attached = false;
    for (; statusIt != status.end(); ++statusIt, ++objIt) {
        QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(
            QString::fromUtf8((*objIt)->Label.getValue()),
            getFeatureStatusString(*statusIt)
        ));
        item->setData(Qt::UserRole, QString::fromLatin1((*objIt)->getNameInDocument()));
        ui->listWidget->addItem(item);

        App::Document* pDoc = (*objIt)->getDocument();
        documentName = pDoc->getName();
        if (!attached) {
            attached = true;
            attachDocument(Gui::Application::Instance->getDocument(pDoc));
        }

        // check if we need to set any origin in temporary visibility mode
        auto* datum = dynamic_cast<App::DatumElement*>(*objIt);
        if ((*statusIt == validFeature || *statusIt == basePlane) && datum) {
            auto* origin = dynamic_cast<App::Origin*>(datum->getLCS());
            if (origin) {
                if ((*objIt)->isDerivedFrom<App::Plane>()) {
                    originVisStatus[origin].setFlag(Gui::DatumElement::Planes, true);
                }
                else if ((*objIt)->isDerivedFrom<App::Line>()) {
                    originVisStatus[origin].setFlag(Gui::DatumElement::Axes, true);
                }
            }
        }
    }

    // Setup the origin's temporary visibility
    for (const auto& originPair : originVisStatus) {
        const auto& origin = originPair.first;

        auto* vpo = static_cast<Gui::ViewProviderCoordinateSystem*>(
            Gui::Application::Instance->getViewProvider(origin)
        );
        if (vpo) {
            vpo->setTemporaryVisibility(originVisStatus[origin]);
            vpo->setTemporaryScale(Gui::ViewParams::instance()->getDatumTemporaryScaleFactor());
            vpo->setPlaneLabelVisibility(true);
            origins.push_back(vpo);
        }
    }

    // TODO may be update origin API to show only some objects (2015-08-31, Fat-Zer)

    groupLayout()->addWidget(proxy);
    statuses = status;
    updateList();
}

TaskFeaturePick::~TaskFeaturePick()
{
    for (Gui::ViewProviderCoordinateSystem* vpo : origins) {
        vpo->resetTemporaryVisibility();
        vpo->resetTemporarySize();
        vpo->setPlaneLabelVisibility(false);
    }
}

void TaskFeaturePick::updateList()
{
    int index = 0;

    for (auto status : statuses) {
        QListWidgetItem* item = ui->listWidget->item(index);

        switch (status) {
            case validFeature:
                item->setHidden(false);
                break;
            case invalidShape:
                item->setHidden(true);
                break;
            case isUsed:
                item->setHidden(!ui->checkUsed->isChecked());
                break;
            case noWire:
                item->setHidden(true);
                break;
            case otherBody:  // Phase 2: cross-body always visible
            case otherPart:  // Phase 2: cross-part always visible
            case notInBody:  // Phase 2: free features always visible
                item->setHidden(false);
                break;
            case basePlane:
                item->setHidden(false);
                break;
            case afterTip:
                item->setHidden(true);
                break;
        }

        index++;
    }
}

void TaskFeaturePick::onUpdate(bool)
{
    bool enable = false;
    if (ui->checkOtherBody->isChecked() || ui->checkOtherPart->isChecked()) {
        enable = true;
    }

    ui->radioDependent->setEnabled(enable);
    ui->radioIndependent->setEnabled(enable);
    ui->radioXRef->setEnabled(enable);

    updateList();
}

std::vector<App::DocumentObject*> TaskFeaturePick::getFeatures()
{
    features.clear();
    QListIterator<QListWidgetItem*> i(ui->listWidget->selectedItems());
    while (i.hasNext()) {

        auto item = i.next();
        if (item->isHidden()) {
            continue;
        }

        QString t = item->data(Qt::UserRole).toString();
        features.push_back(t);
    }

    std::vector<App::DocumentObject*> result;

    for (const auto& feature : features) {
        result.push_back(
            App::GetApplication().getDocument(documentName.c_str())->getObject(feature.toLatin1().data())
        );
    }

    return result;
}

std::vector<App::DocumentObject*> TaskFeaturePick::buildFeatures()
{
    int index = 0;
    std::vector<App::DocumentObject*> result;
    try {
        auto activeBody = PartDesignGui::getBody(false);
        if (!activeBody) {
            return result;
        }

        for (auto status : statuses) {
            Q_UNUSED(status)
            QListWidgetItem* item = ui->listWidget->item(index);

            if (item->isSelected() && !item->isHidden()) {
                QString t = item->data(Qt::UserRole).toString();
                auto obj = App::GetApplication()
                               .getDocument(documentName.c_str())
                               ->getObject(t.toLatin1().data());

                // CoreCAD Phase 2: cross-Body references are valid. Use obj directly.
                result.push_back(obj);
            }

            index++;
        }
    }
    catch (const Base::Exception& e) {
        e.reportException();
    }
    catch (Py::Exception& e) {
        // reported by code analyzers
        e.clear();
        Base::Console().warning("Unexpected PyCXX exception\n");
    }
    catch (const boost::exception&) {
        // reported by code analyzers
        Base::Console().warning("Unexpected boost exception\n");
    }

    return result;
}

bool TaskFeaturePick::isSingleSelectionEnabled() const
{
    ParameterGrp::handle hGrp = App::GetApplication()
                                    .GetUserParameter()
                                    .GetGroup("BaseApp")
                                    ->GetGroup("Preferences")
                                    ->GetGroup("Selection");
    return hGrp->GetBool("singleClickFeatureSelect", true);
}

void TaskFeaturePick::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (doSelection) {
        return;
    }
    doSelection = true;
    ui->listWidget->clearSelection();
    for (Gui::SelectionSingleton::SelObj obj : Gui::Selection().getSelection()) {
        for (int row = 0; row < ui->listWidget->count(); row++) {
            QListWidgetItem* item = ui->listWidget->item(row);
            QString t = item->data(Qt::UserRole).toString();
            if (t.compare(QString::fromLatin1(obj.FeatName)) == 0) {
                item->setSelected(true);

                if (msg.Type == Gui::SelectionChanges::AddSelection) {
                    std::string docNameCopy = documentName;
                    if (isSingleSelectionEnabled()) {
                        QMetaObject::invokeMethod(
                            qobject_cast<Gui::ControlSingleton*>(&Gui::Control()),
                            [docNameCopy] {
                                Gui::Control().accept(
                                    Gui::Application::Instance->getDocument(docNameCopy.c_str())
                                        ->getDocument()
                                );
                            },
                            Qt::QueuedConnection
                        );
                    }
                }
            }
        }
    }
    doSelection = false;
}

void TaskFeaturePick::onItemSelectionChanged()
{
    if (doSelection) {
        return;
    }
    doSelection = true;
    ui->listWidget->blockSignals(true);
    Gui::Selection().clearSelection();
    for (int row = 0; row < ui->listWidget->count(); row++) {
        QListWidgetItem* item = ui->listWidget->item(row);
        QString t = item->data(Qt::UserRole).toString();
        if (item->isSelected()) {
            Gui::Selection().addSelection(documentName.c_str(), t.toLatin1());
        }
    }
    ui->listWidget->blockSignals(false);
    doSelection = false;
}

void TaskFeaturePick::onDoubleClick(QListWidgetItem* item)
{
    if (doSelection) {
        return;
    }
    doSelection = true;
    QString t = item->data(Qt::UserRole).toString();
    Gui::Selection().addSelection(documentName.c_str(), t.toLatin1());
    doSelection = false;

    std::string docNameCopy = documentName;
    QMetaObject::invokeMethod(
        qobject_cast<Gui::ControlSingleton*>(&Gui::Control()),
        [docNameCopy] {
            Gui::Control().accept(
                Gui::Application::Instance->getDocument(docNameCopy.c_str())->getDocument()
            );
        },
        Qt::QueuedConnection
    );
}

void TaskFeaturePick::slotDeletedObject(const Gui::ViewProviderDocumentObject& Obj)
{
    if (const auto it = std::ranges::find(origins, &Obj); it != origins.end()) {
        origins.erase(it);
    }
}

void TaskFeaturePick::slotUndoDocument(const Gui::Document& doc)
{
    if (origins.empty()) {
        QTimer::singleShot(100, [&doc]() { Gui::Control().closeDialog(doc.getDocument()); });
    }
}

void TaskFeaturePick::slotDeleteDocument(const Gui::Document& doc)
{
    origins.clear();
    App::Document* docPtr = doc.getDocument();
    QTimer::singleShot(100, [docPtr]() { Gui::Control().closeDialog(docPtr); });
}

void TaskFeaturePick::showExternal(bool val)
{
    ui->checkOtherBody->setChecked(val);
    ui->checkOtherPart->setChecked(val);
    updateList();
}


//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgFeaturePick::TaskDlgFeaturePick(
    std::vector<App::DocumentObject*>& objects,
    const std::vector<TaskFeaturePick::featureStatus>& status,
    std::function<bool(std::vector<App::DocumentObject*>)> afunc,
    std::function<void(std::vector<App::DocumentObject*>)> wfunc,
    bool singleFeatureSelect,
    std::function<void(void)> abortfunc /* = NULL */
)
    : TaskDialog()
    , accepted(false)
{
    pick = new TaskFeaturePick(objects, status, singleFeatureSelect);
    Content.push_back(pick);

    acceptFunction = afunc;
    workFunction = wfunc;
    abortFunction = abortfunc;
}

TaskDlgFeaturePick::~TaskDlgFeaturePick()
{
    // do the work now as before in accept() the dialog is still open, hence the work
    // function could not open another dialog
    if (accepted) {
        try {
            workFunction(pick->buildFeatures());
        }
        catch (...) {
        }
    }
    else if (abortFunction) {

        // Get rid of the TaskFeaturePick before the TaskDialog dtor does. The
        // TaskFeaturePick holds pointers to things (ie any implicitly created
        // Body objects) that might be modified/removed by abortFunction.
        for (auto it : Content) {
            delete it;
        }
        Content.clear();

        try {
            abortFunction();
        }
        catch (...) {
        }
    }
}

//==== calls from the TaskView ===============================================================


void TaskDlgFeaturePick::open()
{}

void TaskDlgFeaturePick::clicked(int)
{}

bool TaskDlgFeaturePick::accept()
{
    accepted = acceptFunction(pick->getFeatures());
    return accepted;
}

bool TaskDlgFeaturePick::reject()
{
    accepted = false;
    return true;
}

void TaskDlgFeaturePick::showExternal(bool val)
{
    pick->showExternal(val);
}


#include "moc_TaskFeaturePick.cpp"
