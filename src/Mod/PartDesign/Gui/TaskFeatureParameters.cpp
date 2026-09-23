// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (C) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>


#include <App/DocumentObserver.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/MainWindow.h>
#include <Gui/BitmapFactory.h>
#include <Mod/PartDesign/App/Feature.h>
#include <Mod/PartDesign/App/FeatureAddSub.h>
#include <Mod/PartDesign/App/Body.h>

#include "ui_TaskPreviewParameters.h"

#include "SketchPickDialog.h"
#include "TaskFeatureParameters.h"
#include "TaskSketchBasedParameters.h"

using namespace PartDesignGui;
using namespace Gui;

/*********************************************************************
 *                      Task Feature Parameters                      *
 *********************************************************************/

TaskPreviewParameters::TaskPreviewParameters(ViewProvider* vp, QWidget* parent)
    : TaskBox(BitmapFactory().pixmap("tree-pre-sel"), tr("Preview"), true, parent)
    , vp(vp)
    , ui(std::make_unique<Ui_TaskPreviewParameters>())
{
    vp->showPreviousFeature(!hGrp->GetBool("ShowFinal", true));
    vp->showPreview(hGrp->GetBool("ShowTransparentPreview", true));

    auto* proxy = new QWidget(this);
    ui->setupUi(proxy);

    ui->showFinalCheckBox->setChecked(vp->isVisible());
    ui->showTransparentPreviewCheckBox->setChecked(vp->isPreviewEnabled());

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    connect(
        ui->showTransparentPreviewCheckBox,
        &QCheckBox::checkStateChanged,
        this,
        &TaskPreviewParameters::onShowPreviewChanged
    );
    connect(
        ui->showFinalCheckBox,
        &QCheckBox::checkStateChanged,
        this,
        &TaskPreviewParameters::onShowFinalChanged
    );
#else
    connect(
        ui->showTransparentPreviewCheckBox,
        &QCheckBox::stateChanged,
        this,
        &TaskPreviewParameters::onShowPreviewChanged
    );
    connect(
        ui->showFinalCheckBox,
        &QCheckBox::stateChanged,
        this,
        &TaskPreviewParameters::onShowFinalChanged
    );
#endif

    groupLayout()->addWidget(proxy);
}

TaskPreviewParameters::~TaskPreviewParameters() = default;

void TaskPreviewParameters::onShowFinalChanged(bool show)
{
    vp->showPreviousFeature(!show);
}

void TaskPreviewParameters::onShowPreviewChanged(bool show)
{
    vp->showPreview(show);
}

/*********************************************************************
 *                   Task Merge Result Parameters                    *
 *********************************************************************/


TaskMergeResultParameters::TaskMergeResultParameters(ViewProvider* vp, QWidget* parent)
    : TaskBox(tr("Merge result"), true, parent)
    , vp(vp)
    , mergeCheckBox(new QCheckBox(tr("Merge with existing body"), this))
    , bodyLabel(new QLabel(this))
    , pickBodyButton(new QPushButton(tr("Extend a different body..."), this))
    , mergeTargetBody(nullptr)
{
    auto* feature = vp->getObject<PartDesign::FeatureAddSub>();

    // Cruth §8.5: the feature already has its spawn-vs-extend choice baked in by the
    // creating command — reflect it. BaseFeature set ⇒ extending a Body; null ⇒ own body.
    bool additive = feature && feature->getAddSubType() == PartDesign::FeatureAddSub::Additive;
    bool extending = feature && feature->BaseFeature.getValue() != nullptr;

    // Asked independently of `extending`, so the answer does not disappear the moment the
    // user unticks the box (#32). resolveMergeCandidate keeps to the non-throwing queries
    // on purpose: this runs in a dialog constructor and must degrade, never throw.
    mergeTargetBody = PartDesign::Body::resolveMergeCandidate(feature);

    mergeCheckBox->setChecked(extending);
    // The choice is only offered when the feature could go either way: additive (a cut
    // must extend something) and with a real Body to merge into (a bare-plane new body has
    // no target). Otherwise the checkbox stays as a read-only indicator of the fixed state.
    mergeCheckBox->setEnabled(additive && mergeTargetBody != nullptr);

    bodyLabel->setWordWrap(true);

    // §8.5's third gesture: the inferred target is a default, not a verdict. The picker
    // offers every other Body the feature could legally join, so an anchor chain that
    // inferred nothing — or inferred the wrong Body — is still one gesture from the right
    // answer. Additive only, for the same reason the checkbox is: a cut must extend the
    // thing it cuts, and re-aiming a cut is a Scope edit (§8.3), not a merge choice.
    pickBodyButton->setEnabled(additive && !candidateBodies().empty());

    connect(mergeCheckBox, &QCheckBox::toggled, this, &TaskMergeResultParameters::onMergeToggled);
    connect(pickBodyButton, &QPushButton::clicked, this, &TaskMergeResultParameters::onPickBody);

    auto* proxy = new QWidget(this);
    auto* layout = new QVBoxLayout(proxy);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mergeCheckBox);
    layout->addWidget(bodyLabel);
    layout->addWidget(pickBodyButton);
    groupLayout()->addWidget(proxy);

    refreshBodyLabel();
}

TaskMergeResultParameters::~TaskMergeResultParameters() = default;

void TaskMergeResultParameters::onMergeToggled(bool merge)
{
    auto* feature = vp->getObject<PartDesign::Feature>();
    if (!feature) {
        return;
    }

    // Moving the last feature out of a Body retires it (§4.7), so the target captured when
    // the dialog opened can be gone by the time the box is ticked again. Confirm it is still
    // live rather than splicing onto a dangling pointer.
    if (mergeTargetBody && !mergeTargetBody->isAttachedToDocument()) {
        mergeTargetBody = nullptr;
        mergeCheckBox->setEnabled(false);
    }

    // Merge on ⇒ splice back onto the remembered target Body; off ⇒ spawn a fresh Body.
    // Both branches re-home the feature by rewiring the BaseFeature chain and Tips; the
    // recompute below refreshes the preview and downstream. We are inside the dialog's
    // edit transaction, so Cancel rolls the move back (that is what makes it reversible).
    PartDesign::Body::moveFeatureToBody(feature, merge ? mergeTargetBody : nullptr);
    feature->getDocument()->recompute();

    // The move can retire the Body it left (§4.7), so what is still pickable has changed.
    pickBodyButton->setEnabled(mergeCheckBox->isEnabled() && !candidateBodies().empty());
    refreshBodyLabel();
}

std::vector<PartDesign::Body*> TaskMergeResultParameters::candidateBodies() const
{
    // The list itself is an App-layer question (§8.5, P8): the picker and the Python API
    // must offer the same set, and the cycle rule belongs beside the pipeline it protects.
    return PartDesign::Body::mergeCandidates(vp ? vp->getObject<PartDesign::Feature>() : nullptr);
}

void TaskMergeResultParameters::onPickBody()
{
    auto* feature = vp ? vp->getObject<PartDesign::Feature>() : nullptr;
    if (!feature) {
        return;
    }

    std::vector<PartDesign::Body*> candidates = candidateBodies();
    if (candidates.empty()) {
        pickBodyButton->setEnabled(false);
        return;
    }

    PartDesign::Body* chosen = PartDesignGui::pickBody(candidates);
    if (!chosen) {
        return;  // cancelled — the feature stays where it is
    }

    // An explicit pick overrides the inferred candidate and becomes the target the checkbox
    // returns to, so unticking and re-ticking after a pick comes back to the Body the user
    // chose, not the one the anchor chain guessed.
    PartDesign::Body::moveFeatureToBody(feature, chosen);
    mergeTargetBody = chosen;

    QSignalBlocker block(mergeCheckBox);  // the move is already done; do not re-run it
    mergeCheckBox->setChecked(true);
    mergeCheckBox->setEnabled(true);

    feature->getDocument()->recompute();

    pickBodyButton->setEnabled(!candidateBodies().empty());
    refreshBodyLabel();
}

void TaskMergeResultParameters::refreshBodyLabel()
{
    auto* feature = vp->getObject<App::DocumentObject>();
    PartDesign::Body* body = feature ? PartDesign::Body::findBodyOf(feature) : nullptr;
    if (body) {
        bodyLabel->setText(tr("Result body: %1").arg(QString::fromUtf8(body->Label.getValue())));
    }
    else {
        bodyLabel->clear();
    }
}

TaskFeatureParameters::TaskFeatureParameters(
    PartDesignGui::ViewProvider* vp,
    QWidget* parent,
    const std::string& pixmapname,
    const QString& parname
)
    : TaskBox(Gui::BitmapFactory().pixmap(pixmapname.c_str()), parname, true, parent)
    , vp(vp)
    , blockUpdate(false)
{
    Gui::Document* doc = vp->getDocument();
    this->attachDocument(doc);
}

void TaskFeatureParameters::slotDeletedObject(const Gui::ViewProviderDocumentObject& Obj)
{
    if (this->vp == &Obj) {
        this->vp = nullptr;
    }
}

void TaskFeatureParameters::onUpdateView(bool on)
{
    blockUpdate = !on;
    recomputeFeature();
}

void TaskFeatureParameters::recomputeFeature()
{
    if (!blockUpdate) {
        auto* feature = getObject<PartDesign::Feature>();
        assert(feature);

        feature->recomputeFeature();
        feature->recomputePreview();
    }
}

/*********************************************************************
 *                            Task Dialog                            *
 *********************************************************************/
TaskDlgFeatureParameters::TaskDlgFeatureParameters(PartDesignGui::ViewProvider* vp)
    : preview(new TaskPreviewParameters(vp))
    , vp(vp)
{
    assert(vp);
}

TaskDlgFeatureParameters::~TaskDlgFeatureParameters() = default;

bool TaskDlgFeatureParameters::accept()
{
    App::DocumentObject* feature = getObject();
    bool isUpdateBlocked = false;
    try {
        // Iterate over parameter dialogs and apply all parameters from them
        for (QWidget* wgt : Content) {
            TaskFeatureParameters* param = qobject_cast<TaskFeatureParameters*>(wgt);
            if (!param) {
                continue;
            }

            param->saveHistory();
            param->apply();
            isUpdateBlocked |= param->isUpdateBlocked();
        }
        // Make sure the feature is what we are expecting
        // Should be fine but you never know...
        if (!feature->isDerivedFrom<PartDesign::Feature>()) {
            throw Base::TypeError("Bad object processed in the feature dialog.");
        }

        if (isUpdateBlocked) {
            Gui::cmdAppDocument(feature, "recompute()");
        }
        else {
            // object was already computed, nothing more to do with it...
            Gui::cmdAppDocument(feature, "purgeTouched()");

            if (!feature->isValid()) {
                throw Base::RuntimeError(getObject()->getStatusString());
            }

            // ...but touch parents to signal the change...
            for (auto obj : feature->getInList()) {
                obj->touch();
            }
            // ...and recompute them
            Gui::cmdAppDocument(feature->getDocument(), "recompute()");
        }

        if (!feature->isValid()) {
            throw Base::RuntimeError(getObject()->getStatusString());
        }

        App::DocumentObject* previous = static_cast<PartDesign::Feature*>(feature)->getBaseObject(
            /* silent = */ true
        );
        Gui::cmdAppObjectHide(previous);

        // detach the task panel from the selection to avoid to invoke
        // eventually onAddSelection when the selection changes
        std::vector<QWidget*> subwidgets = getDialogContent();
        for (auto it : subwidgets) {
            TaskSketchBasedParameters* param = qobject_cast<TaskSketchBasedParameters*>(it);
            if (param) {
                param->detachSelection();
            }
        }

        Gui::cmdGuiDocument(feature, "resetEdit()");
        feature->getDocument()->commitTransaction();
    }
    catch (const Base::Exception& e) {
        QString errorText = QString::fromUtf8(e.what());
        QString statusText = QString::fromUtf8(getObject()->getStatusString());

        // generic, fallback error message
        if (errorText == QStringLiteral("Error") || errorText.isEmpty()) {
            if (!statusText.isEmpty() && statusText != QStringLiteral("Error")) {
                errorText = statusText;
            }
            else {
                errorText = tr(
                    "The feature could not be created with the given parameters.\n"
                    "The geometry may be invalid or the parameters may be incompatible.\n"
                    "Please adjust the parameters and try again."
                );
            }
        }
        Base::Console().error("%s\n", errorText.toUtf8().constData());
        return false;
    }
    return true;
}

bool TaskDlgFeatureParameters::reject()
{
    auto feature = getObject<PartDesign::Feature>();
    App::DocumentObjectWeakPtrT weakptr(feature);
    App::Document* document = feature->getDocument();

    PartDesign::Body* body = PartDesign::Body::findBodyOf(feature);

    // Find out previous feature we won't be able to do it after abort
    // (at least in the body case)
    App::DocumentObject* previous = feature->getBaseObject(/* silent = */ true);

    // detach the task panel from the selection to avoid to invoke
    // eventually onAddSelection when the selection changes
    std::vector<QWidget*> subwidgets = getDialogContent();
    for (auto it : subwidgets) {
        TaskSketchBasedParameters* param = qobject_cast<TaskSketchBasedParameters*>(it);
        if (param) {
            param->detachSelection();
        }
    }

    // roll back the done things which may delete the feature
    document->abortTransaction();

    // if abort command deleted the object make the previous feature visible again
    if (weakptr.expired()) {
        // Make the tip or the previous feature visible again with preference to the previous one
        // TODO: ViewProvider::onDelete has the same code. May be this one is excess?
        if (previous && Gui::Application::Instance->getViewProvider(previous)) {
            Gui::Application::Instance->getViewProvider(previous)->show();
        }
        else if (body) {
            App::DocumentObject* tip = body->Tip.getValue();
            if (tip && Gui::Application::Instance->getViewProvider(tip)) {
                Gui::Application::Instance->getViewProvider(tip)->show();
            }
        }
    }

    Gui::cmdAppDocument(document, "recompute()");
    Gui::cmdGuiDocument(document, "resetEdit()");

    return true;
}

#include "moc_TaskFeatureParameters.cpp"
