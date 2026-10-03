// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <QMessageBox>
#include <QString>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Document.h>
#include <Mod/Part/App/PartFeature.h>
#include <Base/Tools.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionObject.h>

#include "CommandSupport.h"
#include "DlgBooleanOperation.h"


//===========================================================================
// Part_Cut
//===========================================================================
DEF_STD_CMD_A(CmdPartCut)

CmdPartCut::CmdPartCut()
    : Command("Part_Cut")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Cut");
    sToolTipText = QT_TR_NOOP("Cuts 2 selected shapes");
    sWhatsThis = "Part_Cut";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Cut";
}

void CmdPartCut::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<Gui::SelectionObject> Sel = getSelection().getSelectionEx(
        nullptr,
        App::DocumentObject::getClassTypeId(),
        Gui::ResolveMode::FollowLink
    );
    if (Sel.size() != 2) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select 2 shapes")
        );
        return;
    }

    bool askUser = false;
    std::vector<std::string> names;
    for (const auto& it : Sel) {
        const App::DocumentObject* obj = it.getObject();
        const TopoDS_Shape& shape = Part::Feature::getShape(
            obj,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!PartGui::checkForSolids(shape) && !askUser) {
            int ret = QMessageBox::warning(
                Gui::getMainWindow(),
                QObject::tr("Non-solids selected"),
                QObject::tr(
                    "The use of non-solids for boolean operations may lead to unexpected results.\n"
                    "Continue?"
                ),
                QMessageBox::Yes,
                QMessageBox::No
            );
            if (ret == QMessageBox::No) {
                return;
            }
            askUser = true;
        }

        names.push_back(Base::Tools::quoted(it.getFeatName()));
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Part Cut"));
    doCommand(Doc, "from BOPTools import BOPFeatures");
    doCommand(Doc, "bp = BOPFeatures.BOPFeatures(App.activeDocument())");
    doCommand(Doc, "bp.make_cut([%s])", Base::Tools::joinList(names).c_str());
    updateActive();
    commitCommand();
}

bool CmdPartCut::isActive()
{
    return getSelection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        == 2;
}

//===========================================================================
// Part_Common
//===========================================================================
DEF_STD_CMD_A(CmdPartCommon)

CmdPartCommon::CmdPartCommon()
    : Command("Part_Common")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Intersection");
    sToolTipText = QT_TR_NOOP("Intersects the selected shapes");
    sWhatsThis = "Part_Common";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Common";
}

void CmdPartCommon::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<Gui::SelectionObject> Sel = getSelection().getSelectionEx(
        nullptr,
        App::DocumentObject::getClassTypeId(),
        Gui::ResolveMode::FollowLink
    );

    if (Sel.empty()) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select at least 2 shapes. Alternatively, select 1 compound containing 2 or more shapes to compute the intersection between.")
        );
        return;
    }

    bool askUser = false;
    std::vector<std::string> names;
    for (const auto& it : Sel) {
        const App::DocumentObject* obj = it.getObject();
        const TopoDS_Shape& shape = Part::Feature::getShape(
            obj,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!PartGui::checkForSolids(shape) && !askUser) {
            int ret = QMessageBox::warning(
                Gui::getMainWindow(),
                QObject::tr("Non-solids selected"),
                QObject::tr(
                    "The use of non-solids for boolean operations may lead to unexpected results.\n"
                    "Continue?"
                ),
                QMessageBox::Yes,
                QMessageBox::No
            );
            if (ret == QMessageBox::No) {
                return;
            }
            askUser = true;
        }

        names.push_back(Base::Tools::quoted(it.getFeatName()));
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Common"));
    doCommand(Doc, "from BOPTools import BOPFeatures");
    doCommand(Doc, "bp = BOPFeatures.BOPFeatures(App.activeDocument())");
    doCommand(Doc, "bp.make_multi_common([%s])", Base::Tools::joinList(names).c_str());
    updateActive();
    commitCommand();
}

bool CmdPartCommon::isActive()
{
    return getSelection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        >= 1;
}

//===========================================================================
// Part_Fuse
//===========================================================================
DEF_STD_CMD_A(CmdPartFuse)

CmdPartFuse::CmdPartFuse()
    : Command("Part_Fuse")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Union");
    sToolTipText = QT_TR_NOOP("Unites the selected shapes");
    sWhatsThis = "Part_Fuse";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Fuse";
}

void CmdPartFuse::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<Gui::SelectionObject> Sel = getSelection().getSelectionEx(
        nullptr,
        App::DocumentObject::getClassTypeId(),
        Gui::ResolveMode::FollowLink
    );

    // test if selected object is a compound, and if it is, look how many children it has...
    std::size_t numShapes = 0;
    if (Sel.size() == 1) {
        numShapes = 1;  // to be updated later in code
        Gui::SelectionObject selobj = Sel[0];
        TopoDS_Shape sh = Part::Feature::getShape(
            selobj.getObject(),
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        while (numShapes == 1 && sh.ShapeType() == TopAbs_COMPOUND) {
            numShapes = 0;
            TopoDS_Iterator it(sh);
            TopoDS_Shape last;
            for (; it.More(); it.Next()) {
                ++numShapes;
                last = it.Value();
            }
            sh = last;
        }
    }
    else {
        numShapes = Sel.size();
    }
    if (numShapes < 2) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select at least 2 shapes. Alternatively, select 1 compound containing 2 or more shapes to be fused.")
        );
        return;
    }

    bool askUser = false;
    std::vector<std::string> names;
    for (const auto& it : Sel) {
        const App::DocumentObject* obj = it.getObject();
        const TopoDS_Shape& shape = Part::Feature::getShape(
            obj,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!PartGui::checkForSolids(shape) && !askUser) {
            int ret = QMessageBox::warning(
                Gui::getMainWindow(),
                QObject::tr("Non-solids selected"),
                QObject::tr(
                    "The use of non-solids for boolean operations may lead to unexpected results.\n"
                    "Continue?"
                ),
                QMessageBox::Yes,
                QMessageBox::No
            );
            if (ret == QMessageBox::No) {
                return;
            }
            askUser = true;
        }

        names.push_back(Base::Tools::quoted(it.getFeatName()));
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Fusion"));
    doCommand(Doc, "from BOPTools import BOPFeatures");
    doCommand(Doc, "bp = BOPFeatures.BOPFeatures(App.activeDocument())");
    doCommand(Doc, "bp.make_multi_fuse([%s])", Base::Tools::joinList(names).c_str());
    updateActive();
    commitCommand();
}

bool CmdPartFuse::isActive()
{
    return getSelection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        >= 1;
}

//===========================================================================
// Part_CompJoinFeatures (dropdown toolbar button for Connect, Embed and Cutout)
//===========================================================================

DEF_STD_CMD_ACL(CmdPartCompJoinFeatures)

CmdPartCompJoinFeatures::CmdPartCompJoinFeatures()
    : Command("Part_CompJoinFeatures")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Join Shapes");

    sToolTipText = QT_TR_NOOP("Joins the selected walled shapes");
    sWhatsThis = "Part_CompJoinFeatures";
    sStatusTip = sToolTipText;
}

void CmdPartCompJoinFeatures::activated(int iMsg)
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    if (iMsg == 0) {
        rcCmdMgr.runCommandByName("Part_JoinConnect");
    }
    else if (iMsg == 1) {
        rcCmdMgr.runCommandByName("Part_JoinEmbed");
    }
    else if (iMsg == 2) {
        rcCmdMgr.runCommandByName("Part_JoinCutout");
    }
    else {
        return;
    }

    // Since the default icon is reset when enabling/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action* CmdPartCompJoinFeatures::createAction()
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    QAction* cmd0 = pcAction->addAction(QString());
    cmd0->setIcon(Gui::BitmapFactory().iconFromTheme("Part_JoinConnect"));
    QAction* cmd1 = pcAction->addAction(QString());
    cmd1->setIcon(Gui::BitmapFactory().iconFromTheme("Part_JoinEmbed"));
    QAction* cmd2 = pcAction->addAction(QString());
    cmd2->setIcon(Gui::BitmapFactory().iconFromTheme("Part_JoinCutout"));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(cmd0->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdPartCompJoinFeatures::languageChange()
{
    Command::languageChange();

    if (!_pcAction) {
        return;
    }

    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    Gui::Command* joinConnect = rcCmdMgr.getCommandByName("Part_JoinConnect");
    if (joinConnect) {
        QAction* cmd0 = a[0];
        cmd0->setText(QApplication::translate("Part_JoinFeatures", joinConnect->getMenuText()));
        cmd0->setToolTip(QApplication::translate("Part_JoinFeatures", joinConnect->getToolTipText()));
        cmd0->setStatusTip(QApplication::translate("Part_JoinFeatures", joinConnect->getStatusTip()));
    }

    Gui::Command* joinEmbed = rcCmdMgr.getCommandByName("Part_JoinEmbed");
    if (joinEmbed) {
        QAction* cmd1 = a[1];
        cmd1->setText(QApplication::translate("Part_JoinFeatures", joinEmbed->getMenuText()));
        cmd1->setToolTip(QApplication::translate("Part_JoinFeatures", joinEmbed->getToolTipText()));
        cmd1->setStatusTip(QApplication::translate("Part_JoinFeatures", joinEmbed->getStatusTip()));
    }

    Gui::Command* joinCutout = rcCmdMgr.getCommandByName("Part_JoinCutout");
    if (joinCutout) {
        QAction* cmd2 = a[2];
        cmd2->setText(QApplication::translate("Part_JoinFeatures", joinCutout->getMenuText()));
        cmd2->setToolTip(QApplication::translate("Part_JoinFeatures", joinCutout->getToolTipText()));
        cmd2->setStatusTip(QApplication::translate("Part_JoinFeatures", joinCutout->getStatusTip()));
    }
}

bool CmdPartCompJoinFeatures::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}

//===========================================================================
// Part_CompSplitFeatures (dropdown toolbar button for BooleanFragments, Slice)
//===========================================================================

DEF_STD_CMD_ACL(CmdPartCompSplitFeatures)

CmdPartCompSplitFeatures::CmdPartCompSplitFeatures()
    : Command("Part_CompSplitFeatures")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Split Shapes");

    sToolTipText = QT_TR_NOOP("Shape splitting and compsolid creation tools");
    sWhatsThis = "Part_CompSplitFeatures";
    sStatusTip = sToolTipText;
}

void CmdPartCompSplitFeatures::activated(int iMsg)
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    if (iMsg == 0) {
        rcCmdMgr.runCommandByName("Part_BooleanFragments");
    }
    else if (iMsg == 1) {
        rcCmdMgr.runCommandByName("Part_SliceApart");
    }
    else if (iMsg == 2) {
        rcCmdMgr.runCommandByName("Part_Slice");
    }
    else if (iMsg == 3) {
        rcCmdMgr.runCommandByName("Part_XOR");
    }
    else {
        return;
    }

    // Since the default icon is reset when enabling/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action* CmdPartCompSplitFeatures::createAction()
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    QAction* cmd0 = pcAction->addAction(QString());
    cmd0->setIcon(Gui::BitmapFactory().iconFromTheme("Part_BooleanFragments"));
    QAction* cmd1 = pcAction->addAction(QString());
    cmd1->setIcon(Gui::BitmapFactory().iconFromTheme("Part_SliceApart"));
    QAction* cmd2 = pcAction->addAction(QString());
    cmd2->setIcon(Gui::BitmapFactory().iconFromTheme("Part_Slice"));
    QAction* cmd3 = pcAction->addAction(QString());
    cmd3->setIcon(Gui::BitmapFactory().iconFromTheme("Part_XOR"));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(cmd0->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdPartCompSplitFeatures::languageChange()
{
    Command::languageChange();

    if (!_pcAction) {
        return;
    }

    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    Gui::Command* splitBoolFragments = rcCmdMgr.getCommandByName("Part_BooleanFragments");
    if (splitBoolFragments) {
        QAction* cmd0 = a[0];
        cmd0->setText(QApplication::translate("Part_SplitFeatures", splitBoolFragments->getMenuText()));
        cmd0->setToolTip(
            QApplication::translate("Part_SplitFeatures", splitBoolFragments->getToolTipText())
        );
        cmd0->setStatusTip(
            QApplication::translate("Part_SplitFeatures", splitBoolFragments->getStatusTip())
        );
    }

    Gui::Command* splitSliceApart = rcCmdMgr.getCommandByName("Part_SliceApart");
    if (splitSliceApart) {
        QAction* cmd1 = a[1];
        cmd1->setText(QApplication::translate("Part_SplitFeatures", splitSliceApart->getMenuText()));
        cmd1->setToolTip(
            QApplication::translate("Part_SplitFeatures", splitSliceApart->getToolTipText())
        );
        cmd1->setStatusTip(
            QApplication::translate("Part_SplitFeatures", splitSliceApart->getStatusTip())
        );
    }

    Gui::Command* splitSlice = rcCmdMgr.getCommandByName("Part_Slice");
    if (splitSlice) {
        QAction* cmd1 = a[2];
        cmd1->setText(QApplication::translate("Part_SplitFeatures", splitSlice->getMenuText()));
        cmd1->setToolTip(QApplication::translate("Part_SplitFeatures", splitSlice->getToolTipText()));
        cmd1->setStatusTip(QApplication::translate("Part_SplitFeatures", splitSlice->getStatusTip()));
    }

    Gui::Command* splitXOR = rcCmdMgr.getCommandByName("Part_XOR");
    if (splitXOR) {
        QAction* cmd2 = a[3];
        cmd2->setText(QApplication::translate("Part_SplitFeatures", splitXOR->getMenuText()));
        cmd2->setToolTip(QApplication::translate("Part_SplitFeatures", splitXOR->getToolTipText()));
        cmd2->setStatusTip(QApplication::translate("Part_SplitFeatures", splitXOR->getStatusTip()));
    }
}

bool CmdPartCompSplitFeatures::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}

//===========================================================================
// Part_CompCompoundTools (dropdown toolbar button for BooleanFragments, Slice)
//===========================================================================

DEF_STD_CMD_ACL(CmdPartCompCompoundTools)

CmdPartCompCompoundTools::CmdPartCompCompoundTools()
    : Command("Part_CompCompoundTools")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Compound Tools");
    sToolTipText = QT_TR_NOOP("Compound tools for working with multiple shapes");
    sWhatsThis = "Part_CompCompoundTools";
    sStatusTip = sToolTipText;
}

void CmdPartCompCompoundTools::activated(int iMsg)
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    if (iMsg == 0) {
        rcCmdMgr.runCommandByName("Part_Compound");
    }
    else if (iMsg == 1) {
        rcCmdMgr.runCommandByName("Part_ExplodeCompound");
    }
    else if (iMsg == 2) {
        rcCmdMgr.runCommandByName("Part_CompoundFilter");
    }
    else {
        return;
    }

    // Since the default icon is reset when enabling/disabling the command we have
    // to explicitly set the icon of the used command.
    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    assert(iMsg < a.size());
    pcAction->setIcon(a[iMsg]->icon());
}

Gui::Action* CmdPartCompCompoundTools::createAction()
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    QAction* cmd0 = pcAction->addAction(QString());
    cmd0->setIcon(Gui::BitmapFactory().iconFromTheme("Part_Compound"));
    QAction* cmd1 = pcAction->addAction(QString());
    cmd1->setIcon(Gui::BitmapFactory().iconFromTheme("Part_ExplodeCompound"));
    QAction* cmd2 = pcAction->addAction(QString());
    cmd2->setIcon(Gui::BitmapFactory().iconFromTheme("Part_CompoundFilter"));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(cmd0->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdPartCompCompoundTools::languageChange()
{
    Command::languageChange();

    if (!_pcAction) {
        return;
    }

    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    Gui::Command* cmdCompound = rcCmdMgr.getCommandByName("Part_Compound");
    if (cmdCompound) {
        QAction* cmd0 = a[0];
        cmd0->setText(QApplication::translate("CmdPartCompound", cmdCompound->getMenuText()));
        cmd0->setToolTip(QApplication::translate("CmdPartCompound", cmdCompound->getToolTipText()));
        cmd0->setStatusTip(QApplication::translate("CmdPartCompound", cmdCompound->getStatusTip()));
    }

    Gui::Command* cmdExplode = rcCmdMgr.getCommandByName("Part_ExplodeCompound");
    if (cmdExplode) {
        QAction* cmd1 = a[1];
        cmd1->setText(QApplication::translate("Part_CompoundTools", cmdExplode->getMenuText()));
        cmd1->setToolTip(QApplication::translate("Part_CompoundTools", cmdExplode->getToolTipText()));
        cmd1->setStatusTip(QApplication::translate("Part_CompoundTools", cmdExplode->getStatusTip()));
    }

    Gui::Command* cmdCompoundFilter = rcCmdMgr.getCommandByName("Part_CompoundFilter");
    if (cmdCompoundFilter) {
        QAction* cmd2 = a[2];
        cmd2->setText(QApplication::translate("Part_CompoundTools", cmdCompoundFilter->getMenuText()));
        cmd2->setToolTip(
            QApplication::translate("Part_CompoundTools", cmdCompoundFilter->getToolTipText())
        );
        cmd2->setStatusTip(
            QApplication::translate("Part_CompoundTools", cmdCompoundFilter->getStatusTip())
        );
    }
}

bool CmdPartCompCompoundTools::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}


//===========================================================================
// Part_Section
//===========================================================================
DEF_STD_CMD_A(CmdPartSection)

CmdPartSection::CmdPartSection()
    : Command("Part_Section")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Section");
    sToolTipText = QT_TR_NOOP("Sections 2 selected shapes");
    sWhatsThis = "Part_Section";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Section";
}

void CmdPartSection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<Gui::SelectionObject> Sel = getSelection().getSelectionEx(
        nullptr,
        App::DocumentObject::getClassTypeId(),
        Gui::ResolveMode::FollowLink
    );
    if (Sel.size() != 2) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select 2 shapes")
        );
        return;
    }

    std::string FeatName = getUniqueObjectName("Section");
    std::string BaseName = Sel[0].getFeatName();
    std::string ToolName = Sel[1].getFeatName();

    openCommand(QT_TRANSLATE_NOOP("Command", "Section"));
    doCommand(Doc, "App.activeDocument().addObject(\"Part::Section\",\"%s\")", FeatName.c_str());
    doCommand(
        Doc,
        "App.activeDocument().%s.Base = App.activeDocument().%s",
        FeatName.c_str(),
        BaseName.c_str()
    );
    doCommand(
        Doc,
        "App.activeDocument().%s.Tool = App.activeDocument().%s",
        FeatName.c_str(),
        ToolName.c_str()
    );
    doCommand(Gui, "Gui.activeDocument().hide('%s')", BaseName.c_str());
    doCommand(Gui, "Gui.activeDocument().hide('%s')", ToolName.c_str());
    doCommand(
        Gui,
        "Gui.activeDocument().%s.LineMaterial = Gui.activeDocument().%s.ShapeAppearance[0]",
        FeatName.c_str(),
        BaseName.c_str()
    );
    updateActive();
    commitCommand();
}

bool CmdPartSection::isActive()
{
    return getSelection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        == 2;
}

//===========================================================================
// Part_Boolean
//===========================================================================
DEF_STD_CMD_A(CmdPartBoolean)

CmdPartBoolean::CmdPartBoolean()
    : Command("Part_Boolean")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Boolean Operation");
    sToolTipText = QT_TR_NOOP("Applies a boolean operations with the selected shapes");
    sWhatsThis = "Part_Boolean";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Booleans";
}

void CmdPartBoolean::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog(getDocument());
    if (!dlg) {
        dlg = new PartGui::TaskBooleanOperation();
    }
    Gui::Control().showDialog(dlg);
}

bool CmdPartBoolean::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

void CreatePartBooleanCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartBoolean());
    rcCmdMgr.addCommand(new CmdPartCommon());
    rcCmdMgr.addCommand(new CmdPartCut());
    rcCmdMgr.addCommand(new CmdPartFuse());
    rcCmdMgr.addCommand(new CmdPartCompJoinFeatures());
    rcCmdMgr.addCommand(new CmdPartCompSplitFeatures());
    rcCmdMgr.addCommand(new CmdPartCompCompoundTools());
    rcCmdMgr.addCommand(new CmdPartSection());
}
