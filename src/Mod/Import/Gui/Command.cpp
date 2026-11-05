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


#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/FileDialog.h>
#include <Gui/MainWindow.h>


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
using Gui::FileDialog;

//===========================================================================
// Import_Box
//===========================================================================
DEF_STD_CMD_A(FCCmdImportReadBREP)

FCCmdImportReadBREP::FCCmdImportReadBREP()
    : Command("Import_ReadBREP")
{
    sAppModule = "Import";
    sGroup = "Import";
    sMenuText = "Read BREP";
    sToolTipText = "Read a BREP file";
    sWhatsThis = "Import_ReadBREP";
    sStatusTip = sToolTipText;
    sPixmap = "Std_Tool1";
}

void FCCmdImportReadBREP::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    openCommand(QT_TRANSLATE_NOOP("Command", "Read BREP"));
    QString fn = Gui::FileDialog::getOpenFileName(
        Gui::getMainWindow(),
        QString(),
        QString(),
        Gui::FileDialog::FilterList {{QStringLiteral("BREP"), {"*.brep", "*.rle"}}}
    );
    if (fn.isEmpty()) {
        abortCommand();
        return;
    }

    const QByteArray fnUtf8 = fn.toUtf8();
    const std::string escaped = Base::Tools::escapeEncodeFilename(
        std::string(fnUtf8.constData(), fnUtf8.size())
    );
    doCommand(Doc, "TopoShape = Import.ReadBREP(\"%s\")", escaped.c_str());
    commitCommand();
}

bool FCCmdImportReadBREP::isActive()
{
    return getGuiApplication()->activeDocument() != nullptr;
}

void CreateImportCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    rcCmdMgr.addCommand(new FCCmdImportReadBREP());
}
