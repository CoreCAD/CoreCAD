// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <QPointer>


#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>

#include "DlgInspectAppearance.h"
#include "DlgInspectMaterial.h"
#include "MaterialSave.h"
#include "MaterialsEditor.h"
#include "ModelSelect.h"
#include "TaskMigrateExternal.h"


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//===========================================================================
// Material_Edit
//===========================================================================
DEF_STD_CMD_A(CmdMaterialEdit)

CmdMaterialEdit::CmdMaterialEdit()
    : Command("Material_Edit")
{
    sAppModule = "Material";
    sGroup = QT_TR_NOOP("Material");
    sMenuText = QT_TR_NOOP("Material Library");
    sToolTipText = QT_TR_NOOP("Adds and changes materials in the library");
    sWhatsThis = "Material_Edit";
    sStatusTip = sToolTipText;
    sPixmap = "Material_Edit";
}

void CmdMaterialEdit::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    static QPointer<QDialog> dlg = nullptr;
    if (!dlg) {
        dlg = new MatGui::MaterialsEditor(Gui::getMainWindow());
    }
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

bool CmdMaterialEdit::isActive()
{
    // return (hasActiveDocument() && !Gui::Control().activeDialog());
    return true;
}

//===========================================================================
// Materials_InspectAppearance
//===========================================================================
DEF_STD_CMD_A(CmdInspectAppearance)

CmdInspectAppearance::CmdInspectAppearance()
    : Command("Materials_InspectAppearance")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Inspect Appearance");
    sToolTipText = QT_TR_NOOP("Inspects the appearance properties of the selected object");
    sWhatsThis = "Materials_InspectAppearance";
    sStatusTip = QT_TR_NOOP("Inspect the appearance properties of the selected object");
    // sPixmap = "Material_Edit";
}

void CmdInspectAppearance::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskInspectAppearance());
}

bool CmdInspectAppearance::isActive()
{
    return (Gui::Control().activeDialog() == nullptr);
}

//===========================================================================
// Materials_InspectMaterial
//===========================================================================
DEF_STD_CMD_A(CmdInspectMaterial)

CmdInspectMaterial::CmdInspectMaterial()
    : Command("Materials_InspectMaterial")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Inspect Material");
    sToolTipText = QT_TR_NOOP("Inspects the material properties of the selected object");
    sWhatsThis = "Materials_InspectMaterial";
    sStatusTip = QT_TR_NOOP("Inspect the material properties of the selected object");
    // sPixmap = "Material_Edit";
}

void CmdInspectMaterial::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new MatGui::TaskInspectMaterial());
}

bool CmdInspectMaterial::isActive()
{
    return (Gui::Control().activeDialog() == nullptr);
}

//===========================================================================
// Materials_MigrateToDatabase
//===========================================================================

#if defined(BUILD_MATERIAL_EXTERNAL)
DEF_STD_CMD_A(CmdMigrateToExternal)

CmdMigrateToExternal::CmdMigrateToExternal()
    : Command("Materials_MigrateToExternal")
{
    sGroup = "Standard-View";
    sMenuText = QT_TR_NOOP("Migrate");
    sToolTipText = QT_TR_NOOP("Migrates the materials to the external materials manager");
    sWhatsThis = "Materials_MigrateToDatabase";
    sStatusTip = QT_TR_NOOP("Migrate existing materials to the external materials manager");
    // sPixmap = "Materials_Edit";
}

void CmdMigrateToExternal::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    MatGui::TaskMigrateExternal* dlg = new MatGui::TaskMigrateExternal();
    Gui::Control().showDialog(dlg);
}

bool CmdMigrateToExternal::isActive()
{
    return true;
}
#endif

//---------------------------------------------------------------

void CreateMaterialCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    rcCmdMgr.addCommand(new CmdMaterialEdit());
    rcCmdMgr.addCommand(new CmdInspectAppearance());
    rcCmdMgr.addCommand(new CmdInspectMaterial());
#if defined(BUILD_MATERIAL_EXTERNAL)
    rcCmdMgr.addCommand(new CmdMigrateToExternal());
#endif
}
