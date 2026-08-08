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
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <TopExp_Explorer.hxx>

#include <App/Datums.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/Application.h>
#include <Gui/MainWindow.h>
#include <Gui/MDIView.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureBase.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>

#include "Utils.h"


//===========================================================================
// PartDesign_MoveTip
//===========================================================================
DEF_STD_CMD_A(CmdPartDesignMoveTip)

CmdPartDesignMoveTip::CmdPartDesignMoveTip()
    : Command("PartDesign_MoveTip")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Set Tip");
    sToolTipText = QT_TR_NOOP("Moves the tip of the body to the selected feature");
    sWhatsThis = "PartDesign_MoveTip";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_MoveTip";
}

void CmdPartDesignMoveTip::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> features = getSelection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::erase_if(features, [](App::DocumentObject* o) { return !Part::hasShape(o); });
    App::DocumentObject* selFeature;
    PartDesign::Body* body = nullptr;

    if (features.size() == 1) {
        selFeature = features.front();
        if (selFeature->isDerivedFrom<PartDesign::Body>()) {
            body = static_cast<PartDesign::Body*>(selFeature);
        }
        else {
            body = PartDesignGui::getBodyFor(selFeature, /* messageIfNot =*/false);
        }
    }
    else {
        selFeature = nullptr;
    }

    if (!selFeature) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("Selection error"),
            QObject::tr("Select exactly one Part Design feature or a body.")
        );
        return;
    }
    else if (!body) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("Selection error"),
            QObject::tr(
                "Could not determine a body for the selected feature '%s'.",
                selFeature->Label.getValue()
            )
        );
        return;
    }
    else if (
        !selFeature->isDerivedFrom(PartDesign::Feature::getClassTypeId()) && selFeature != body
        && body->BaseFeature.getValue() != selFeature
    ) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("Selection error"),
            QObject::tr("Only a solid feature can be the tip of a body.")
        );
        return;
    }

    App::DocumentObject* oldTip = body->Tip.getValue();
    if (oldTip == selFeature) {  // it's not generally an error, so print only a console message
        Base::Console().message("%s is already the tip of the body\n", selFeature->getNameInDocument());
        return;
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Move tip to selected feature"));

    if (selFeature == body) {
        FCMD_OBJ_CMD(body, "Tip = None");
    }
    else {
        FCMD_OBJ_CMD(body, "Tip = " << getObjectCmd(selFeature));

        // Adjust visibility to show only the Tip feature
        FCMD_OBJ_SHOW(selFeature);
    }

    // TODO: Hide all datum features after the Tip feature? But the user might have already hidden
    // some and wants to see others, so we would have to remember their state somehow
    updateActive();
}

bool CmdPartDesignMoveTip::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// PartDesign_DuplicateSelection
//===========================================================================

DEF_STD_CMD_A(CmdPartDesignDuplicateSelection)

CmdPartDesignDuplicateSelection::CmdPartDesignDuplicateSelection()
    : Command("PartDesign_DuplicateSelection")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Duplicate &Object");
    sToolTipText = QT_TR_NOOP("Duplicates the selected object and adds it to the active body");
    sWhatsThis = "PartDesign_DuplicateSelection";
    sStatusTip = sToolTipText;
}

void CmdPartDesignDuplicateSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartDesign::Body* pcActiveBody = PartDesignGui::getBody(/*messageIfNot = */ false);

    std::vector<App::DocumentObject*> beforeFeatures = getDocument()->getObjects();

    openCommand(QT_TRANSLATE_NOOP("Command", "Duplicate a Part Design object"));
    doCommand(Doc, "FreeCADGui.runCommand('Std_DuplicateSelection')");

    if (pcActiveBody) {
        // Find the features that were added
        std::vector<App::DocumentObject*> afterFeatures = getDocument()->getObjects();
        std::vector<App::DocumentObject*> newFeatures;
        std::sort(beforeFeatures.begin(), beforeFeatures.end());
        std::sort(afterFeatures.begin(), afterFeatures.end());
        std::set_difference(
            afterFeatures.begin(),
            afterFeatures.end(),
            beforeFeatures.begin(),
            beforeFeatures.end(),
            std::back_inserter(newFeatures)
        );

        for (auto feature : newFeatures) {
            if (PartDesign::Body::isAllowed(feature)) {
                // If the feature already belongs to a body, don't re-home it into the active
                // body (issue #6278). Body membership is derived from the feature chain, not
                // Body.Group, so ask the reverse lookup rather than probing the dormant group.
                if (!PartDesign::Body::inAnyBody(feature)) {
                    FCMD_OBJ_CMD(pcActiveBody, "addFeature(" << getObjectCmd(feature) << ")");
                    FCMD_OBJ_HIDE(feature);
                }
            }
        }

        // Adjust visibility of features
        if (!newFeatures.empty()) {
            FCMD_OBJ_SHOW(newFeatures.back());
        }
    }

    updateActive();

    commitCommand();
}

bool CmdPartDesignDuplicateSelection::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// PartDesign_MoveFeature
//===========================================================================

DEF_STD_CMD_A(CmdPartDesignMoveFeature)

CmdPartDesignMoveFeature::CmdPartDesignMoveFeature()
    : Command("PartDesign_MoveFeature")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Move Object To…");
    sToolTipText = QT_TR_NOOP("Moves the selected object to another body");
    sWhatsThis = "PartDesign_MoveFeature";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_MoveFeature";
}

void CmdPartDesignMoveFeature::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> features = getSelection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::erase_if(features, [](App::DocumentObject* o) { return !Part::hasShape(o); });
    if (features.empty()) {
        return;
    }

    // Check if all features are valid to move
    if (std::any_of(std::begin(features), std::end(features), [](App::DocumentObject* obj) {
            return !PartDesignGui::isFeatureMovable(obj);
        })) {
        // show messagebox and cancel
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Features cannot be moved"),
            QObject::tr("Some of the selected features have dependencies in the source body")
        );
        return;
    }

    // Collect dependencies of the selected features
    std::vector<App::DocumentObject*> dependencies = PartDesignGui::collectMovableDependencies(
        features
    );
    if (!dependencies.empty()) {
        features.insert(std::end(features), std::begin(dependencies), std::end(dependencies));
    }

    // Create a list of all bodies in this part
    std::vector<App::DocumentObject*> bodies = getDocument()->getObjectsOfType(
        Part::BodyBase::getClassTypeId()
    );

    std::set<App::DocumentObject*> source_bodies;
    for (auto feat : features) {
        // Note: 'source' can be null which means that the feature doesn't belong to a body.
        PartDesign::Body* source = PartDesign::Body::findBodyOf(feat);
        source_bodies.insert(static_cast<App::DocumentObject*>(source));
    }

    if (source_bodies.size() != 1) {
        // show messagebox and cancel
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Features cannot be moved"),
            QObject::tr("Only features of a single source body can be moved")
        );
        return;
    }

    auto source_body = *source_bodies.begin();

    std::vector<App::DocumentObject*> target_bodies;
    for (auto body : bodies) {
        if (!source_bodies.count(body)) {
            target_bodies.push_back(body);
        }
    }

    if (target_bodies.empty()) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Features cannot be moved"),
            QObject::tr("There are no other bodies to move to")
        );
        return;
    }

    // Ask user to select the target body (remove source bodies from list)
    bool ok;
    QStringList items;
    for (auto body : target_bodies) {
        items.push_back(QString::fromUtf8(body->Label.getValue()));
    }
    QString text = QInputDialog::getItem(
        Gui::getMainWindow(),
        qApp->translate("PartDesign_MoveFeature", "Select Body"),
        qApp->translate("PartDesign_MoveFeature", "Select a body from the list"),
        items,
        0,
        false,
        &ok,
        Qt::MSWindowsFixedSizeDialogHint
    );
    if (!ok) {
        return;
    }
    int index = items.indexOf(text);
    if (index < 0) {
        return;
    }

    PartDesign::Body* target = static_cast<PartDesign::Body*>(target_bodies[index]);

    openCommand(QT_TRANSLATE_NOOP("Command", "Move an object"));

    std::stringstream stream;
    stream << "features_ = [" << getObjectCmd(features.back());
    features.pop_back();

    for (auto feat : features) {
        stream << ", " << getObjectCmd(feat);
    }

    stream << "]";
    runCommand(Doc, stream.str().c_str());
    FCMD_OBJ_CMD(source_body, "removeFeatures(features_)");
    FCMD_OBJ_CMD(target, "addFeatures(features_)");
    /*

        // Find body of this feature
        Part::BodyBase* source = PartDesign::Body::findBodyOf(feat);
        bool featureWasTip = false;

        if (source == target) continue;

        // Remove from the source body if the feature belonged to a body
        if (source) {
            featureWasTip = (source->Tip.getValue() == feat);
            doCommand(Doc,"App.activeDocument().%s.removeFeature(App.activeDocument().%s)",
                      source->getNameInDocument(), (feat)->getNameInDocument());
        }

        App::DocumentObject* targetOldTip = target->Tip.getValue();

        // Add to target body (always at the Tip)
        doCommand(Doc,"App.activeDocument().%s.addFeature(App.activeDocument().%s)",
                      target->getNameInDocument(), (feat)->getNameInDocument());
        // Recompute to update the shape
        doCommand(Gui,"App.activeDocument().recompute()");

        // Adjust visibility of features
        // TODO: May be something can be done in view provider (2015-08-05, Fat-Zer)
        // If we removed the tip of the source body, make the new tip visible
        if ( featureWasTip ) {
            App::DocumentObject * sourceNewTip = source->Tip.getValue();
            if (sourceNewTip)
                doCommand(Gui,"Gui.activeDocument().show(\"%s\")",
    sourceNewTip->getNameInDocument());
        }

        // Hide old tip and show new tip (the moved feature) of the target body
        App::DocumentObject* targetNewTip = target->Tip.getValue();
        if ( targetOldTip != targetNewTip ) {
            if ( targetOldTip ) {
                doCommand(Gui,"Gui.activeDocument().hide(\"%s\")",
    targetOldTip->getNameInDocument());
            }
            if (targetNewTip) {
                doCommand(Gui,"Gui.activeDocument().show(\"%s\")",
    targetNewTip->getNameInDocument());
            }
        }

        // Fix sketch support
        if (feat->isDerivedFrom<Sketcher::SketchObject>()) {
            Sketcher::SketchObject *sketch = static_cast<Sketcher::SketchObject*>(feat);
            try {
                PartDesignGui::fixSketchSupport(sketch);
            } catch (Base::Exception &) {
                QMessageBox::warning( Gui::getMainWindow(), QObject::tr("Sketch plane cannot be
    migrated"), QObject::tr("Please edit '%1' and redefine it to use a Base or Datum plane as the
    sketch plane."). arg( QString::fromLatin1( sketch->Label.getValue () ) ) );
            }
        }

        //relink origin for sketches and datums (coordinates)
        PartDesignGui::relinkToOrigin(feat, target);
    }*/

    updateActive();

    commitCommand();
}

bool CmdPartDesignMoveFeature::isActive()
{
    return hasActiveDocument();
}

DEF_STD_CMD_A(CmdPartDesignMoveFeatureInTree)

CmdPartDesignMoveFeatureInTree::CmdPartDesignMoveFeatureInTree()
    : Command("PartDesign_MoveFeatureInTree")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Move Feature After…");
    sToolTipText = QT_TR_NOOP("Moves the selected feature after another feature in the same body");
    sWhatsThis = "PartDesign_MoveFeatureInTree";
    sStatusTip = sToolTipText;
    sPixmap = "PartDesign_MoveFeatureInTree";
}

void CmdPartDesignMoveFeatureInTree::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> features = getSelection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::erase_if(features, [](App::DocumentObject* o) { return !Part::hasShape(o); });

    // also check and include datum objects, ie. plane, line, and point
    std::vector<App::DocumentObject*> datums = getSelection().getObjectsOfType(
        App::DatumElement::getClassTypeId()
    );
    features.insert(features.end(), datums.begin(), datums.end());

    std::vector<App::DocumentObject*> lcs = getSelection().getObjectsOfType(
        App::LocalCoordinateSystem::getClassTypeId()
    );
    features.insert(features.end(), lcs.begin(), lcs.end());

    if (features.empty()) {
        return;
    }

    PartDesign::Body* body = PartDesignGui::getBodyFor(features.front(), false);
    App::DocumentObject* bodyBase = nullptr;
    // sanity check
    bool allFeaturesFromSameBody = true;

    if (body) {
        bodyBase = body->BaseFeature.getValue();
        for (auto feat : features) {
            if (!PartDesign::Body::backsBody(feat, body)) {
                allFeaturesFromSameBody = false;
                break;
            }
            if (bodyBase == feat) {
                QMessageBox::warning(
                    nullptr,
                    QObject::tr("Selection error"),
                    QObject::tr("Impossible to move the base feature of a body.")
                );
                return;
            }
        }
    }
    if (!body || !allFeaturesFromSameBody) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("Selection error"),
            QObject::tr("Select one or more features from the same body.")
        );
        return;
    }

    // Create a list of all features in this body
    const std::vector<App::DocumentObject*>& model = body->getFullModel();

    // Ask user to select the target feature
    bool ok;
    QStringList items;
    if (bodyBase) {
        items.push_back(QString::fromUtf8(bodyBase->Label.getValue()));
    }
    else {
        items.push_back(QObject::tr("Beginning of the body"));
    }
    for (auto feat : model) {
        items.push_back(QString::fromUtf8(feat->Label.getValue()));
    }

    QString text = QInputDialog::getItem(
        Gui::getMainWindow(),
        qApp->translate("PartDesign_MoveFeatureInTree", "Move Feature After…"),
        qApp->translate("PartDesign_MoveFeatureInTree", "Select a feature from the list"),
        items,
        0,
        false,
        &ok,
        Qt::MSWindowsFixedSizeDialogHint
    );
    if (!ok) {
        return;
    }
    int index = items.indexOf(text);
    // first object is the beginning of the body
    App::DocumentObject* target = index != 0 ? model[index - 1] : nullptr;

    openCommand(QT_TRANSLATE_NOOP("Command", "Move a feature inside body"));

    App::DocumentObject* lastObject = target;
    for (auto feat : features) {
        if (feat == target) {
            continue;
        }

        // Remove and re-insert the feature to/from the Body, preserving their order.
        // TODO: if tip was moved the new position of tip is quite undetermined (2015-08-07, Fat-Zer)
        // TODO: warn the user if we are moving an object to some place before the object's link
        // (2015-08-07, Fat-Zer)
        FCMD_OBJ_CMD(body, "removeFeature(" << getObjectCmd(feat) << ")");
        FCMD_OBJ_CMD(
            body,
            "insertObject(" << getObjectCmd(feat) << "," << getObjectCmd(lastObject) << ", True)"
        );

        lastObject = feat;
    }

    // Dependency order check.
    // We must make sure the resulting objects of PartDesign::Feature do not
    // depend on later objects
    std::vector<App::DocumentObject*> bodyFeatures;
    std::map<App::DocumentObject*, size_t> orders;
    for (auto obj : body->getFullModel()) {
        if (obj->isDerivedFrom<PartDesign::Feature>()) {
            orders.emplace(obj, bodyFeatures.size());
            bodyFeatures.push_back(obj);
        }
    }
    bool failed = false;
    std::ostringstream ss;
    for (size_t i = 0; i < bodyFeatures.size(); ++i) {
        auto feat = bodyFeatures[i];
        for (auto obj : feat->getOutList()) {
            if (obj->isDerivedFrom<PartDesign::Feature>()) {
                continue;
            }
            for (auto dep : App::Document::getDependencyList({obj})) {
                auto it = orders.find(dep);
                if (it != orders.end() && it->second > i) {
                    ss << feat->Label.getValue() << ", " << obj->Label.getValue() << " -> "
                       << it->first->Label.getValue();
                    if (!failed) {
                        failed = true;
                    }
                    else {
                        ss << std::endl;
                    }
                }
            }
        }
    }
    if (failed) {
        QMessageBox::critical(
            nullptr,
            QObject::tr("Dependency violation"),
            QObject::tr("Early feature must not depend on later feature.\n\n")
                + QString::fromUtf8(ss.str().c_str())
        );
        abortCommand();
        return;
    }

    // If the selected objects have been moved after the current tip then ask the
    // user if they want the last object to be the new tip.
    // Only do this for features that can hold a tip (not for e.g. datums)
    if (lastObject != target && body->Tip.getValue() == target
        && lastObject->isDerivedFrom<PartDesign::Feature>()) {
        QMessageBox msgBox(Gui::getMainWindow());
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setWindowTitle(qApp->translate("PartDesign_MoveFeatureInTree", "Move Tip"));
        msgBox.setText(qApp->translate(
            "PartDesign_MoveFeatureInTree",
            "The moved feature appears after the currently set tip."
        ));
        msgBox.setInformativeText(
            qApp->translate("PartDesign_MoveFeatureInTree", "Set tip to last feature?")
        );
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        int ret = msgBox.exec();
        if (ret == QMessageBox::Yes) {
            FCMD_OBJ_CMD(body, "Tip = " << getObjectCmd(lastObject));
        }
    }

    updateActive();

    commitCommand();
}

bool CmdPartDesignMoveFeatureInTree::isActive()
{
    return hasActiveDocument();
}


//===========================================================================
// PartDesign_CheckInterference
//===========================================================================

DEF_STD_CMD_A(CmdPartDesignCheckInterference)

CmdPartDesignCheckInterference::CmdPartDesignCheckInterference()
    : Command("PartDesign_CheckInterference")
{
    sAppModule = "PartDesign";
    sGroup = QT_TR_NOOP("PartDesign");
    sMenuText = QT_TR_NOOP("Check spatial interference");
    sToolTipText = QT_TR_NOOP(
        "List pairs of bodies whose volumes overlap in space, and acknowledge intended overlaps"
    );
    sWhatsThis = "PartDesign_CheckInterference";
    sStatusTip = sToolTipText;
}

void CmdPartDesignCheckInterference::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    // Cruth §8.6: an on-demand, non-blocking check — the pairwise boolean is too costly to run
    // every recompute, and an overlap is valid geometry, never a failure. This dialog is the
    // interim surface until the Phase-5 state manifest carries the notice.
    App::Document* doc = getDocument();
    if (!doc) {
        return;
    }

    if (PartDesign::Body::liveInterferingPairs(doc).empty()) {
        QMessageBox::information(
            Gui::getMainWindow(),
            QObject::tr("Spatial interference"),
            QObject::tr("No unacknowledged overlaps: no two bodies share volume in space.")
        );
        return;
    }

    QDialog dialog(Gui::getMainWindow());
    dialog.setWindowTitle(QObject::tr("Spatial interference"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QObject::tr(
            "These bodies overlap in space without being merged. Select a pair to highlight "
            "it, then acknowledge if the overlap is intentional (keep them distinct)."
        ),
        &dialog
    ));
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* ackButton = buttons->addButton(
        QObject::tr("Acknowledge (keep distinct)"),
        QDialogButtonBox::ActionRole
    );
    ackButton->setEnabled(false);
    layout->addWidget(buttons);

    // Rebuild the row list from the current live pairs; each row remembers its two body names so a
    // later selection/acknowledge resolves them freshly (a body could have been retired meanwhile).
    const std::string docName = doc->getName();
    auto repopulate = [&]() {
        list->clear();
        for (const auto& pair : PartDesign::Body::liveInterferingPairs(doc)) {
            auto* item = new QListWidgetItem(
                QString::fromUtf8("%1  ∩  %2")
                    .arg(QString::fromUtf8(pair.first->Label.getValue()))
                    .arg(QString::fromUtf8(pair.second->Label.getValue())),
                list
            );
            item->setData(Qt::UserRole, QString::fromLatin1(pair.first->getNameInDocument()));
            item->setData(Qt::UserRole + 1, QString::fromLatin1(pair.second->getNameInDocument()));
        }
        ackButton->setEnabled(false);
        if (list->count() == 0) {
            dialog.accept();  // nothing left to resolve
        }
    };

    QObject::connect(list, &QListWidget::currentItemChanged, [&](QListWidgetItem* item) {
        ackButton->setEnabled(item != nullptr);
        Gui::Selection().clearSelection();
        if (!item) {
            return;
        }
        // Highlight both bodies of the selected pair in the 3D view.
        Gui::Selection().addSelection(
            docName.c_str(),
            item->data(Qt::UserRole).toString().toLatin1().constData()
        );
        Gui::Selection().addSelection(
            docName.c_str(),
            item->data(Qt::UserRole + 1).toString().toLatin1().constData()
        );
    });

    QObject::connect(ackButton, &QPushButton::clicked, [&]() {
        QListWidgetItem* item = list->currentItem();
        if (!item) {
            return;
        }
        auto* a = freecad_cast<PartDesign::Body*>(
            doc->getObject(item->data(Qt::UserRole).toString().toLatin1().constData())
        );
        auto* b = freecad_cast<PartDesign::Body*>(
            doc->getObject(item->data(Qt::UserRole + 1).toString().toLatin1().constData())
        );
        PartDesign::Body::dismissInterference(a, b);
        repopulate();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    repopulate();
    dialog.exec();
    Gui::Selection().clearSelection();
}

bool CmdPartDesignCheckInterference::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// Initialization
//===========================================================================

void CreatePartDesignBodyCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdPartDesignMoveTip());

    rcCmdMgr.addCommand(new CmdPartDesignDuplicateSelection());
    rcCmdMgr.addCommand(new CmdPartDesignMoveFeature());
    rcCmdMgr.addCommand(new CmdPartDesignMoveFeatureInTree());
    rcCmdMgr.addCommand(new CmdPartDesignCheckInterference());
}
