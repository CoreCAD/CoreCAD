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

#include <App/Document.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/Selection/Selection.h>
#include <Mod/Part/App/Attacher.h>
#include <Mod/Part/App/BodyBase.h>
#include <Mod/Part/App/Datums.h>

#include "CommandSupport.h"


//===========================================================================
// Part_CoordinateSystem
//===========================================================================

namespace
{
// Give a freshly created datum the best-fit attachment for the current selection, so
// "select a face -> create datum" yields an already-attached datum in one step. Ported from
// the retired PartDesign UnifiedDatumCommand (Cruth datum consolidation, issue #45): the
// surviving Part datum command is the union of both halves -- no active-body gate (kept) plus
// auto-attach-from-selection (restored here). If nothing is selected, or the selection fits no
// attachment mode, the datum is left loose for the user to attach through the dialog that opens.
void applyAttachmentFromSelection(App::DocumentObject* obj)
{
    if (!obj) {
        return;
    }
    App::PropertyLinkSubList support;
    Gui::Selection().getAsPropertyLinkSubList(support);
    // A face pick on a Body's solid resolves to the Body marker; re-anchor it to the Tip
    // feature so the datum attaches to the feature, not the tip-tracking Body (ARCHITECTURE §8).
    Part::BodyBase::rebaseBodySubReferencesToTip(support);
    support.removeValue(obj);
    if (support.getSize() == 0) {
        return;
    }

    auto* attach = obj->getExtensionByType<Part::AttachExtension>();
    if (!attach) {
        return;
    }
    attach->attacher().setReferences(support);
    Attacher::SuggestResult sugr;
    attach->attacher().suggestMapModes(sugr);
    if (sugr.message != Attacher::SuggestResult::srOK) {
        return;
    }
    FCMD_OBJ_CMD(obj, "AttachmentSupport = " << support.getPyReprString());
    FCMD_OBJ_CMD(obj, "MapMode = '" << Attacher::AttachEngine::getModeName(sugr.bestFitMode) << "'");
    Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
}
}  // namespace

DEF_STD_CMD_A(CmdPartCoordinateSystem)

CmdPartCoordinateSystem::CmdPartCoordinateSystem()
    : Command("Part_CoordinateSystem")
{
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Coordinate System");
    sToolTipText = QT_TR_NOOP("Creates a coordinate system that can be attached to other objects");
    sWhatsThis = "Part_CoordinateSystem";
    sStatusTip = sToolTipText;
    sPixmap = "Std_CoordinateSystem";
}

void CmdPartCoordinateSystem::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Add coordinate system"));

    std::string name = getUniqueObjectName("LCS");
    doCommand(
        Doc,
        "obj = App.activeDocument().addObject('Part::LocalCoordinateSystem','%s')",
        name.c_str()
    );
    applyAttachmentFromSelection(getDocument()->getObject(name.c_str()));
    doCommand(Doc, "obj.Visibility = True");
    doCommand(Doc, "obj.ViewObject.doubleClicked()");
}

bool CmdPartCoordinateSystem::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// Part_DatumPlane
//===========================================================================
DEF_STD_CMD_A(CmdPartDatumPlane)

CmdPartDatumPlane::CmdPartDatumPlane()
    : Command("Part_DatumPlane")
{
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Datum Plane");
    sToolTipText = QT_TR_NOOP("Creates a datum plane that can be attached to other objects");
    sWhatsThis = "Part_DatumPlane";
    sStatusTip = sToolTipText;
    sPixmap = "Std_Plane";
}

void CmdPartDatumPlane::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Add datum plane"));

    std::string name = getUniqueObjectName("DatumPlane");
    doCommand(Doc, "obj = App.activeDocument().addObject('Part::DatumPlane','%s')", name.c_str());
    applyAttachmentFromSelection(getDocument()->getObject(name.c_str()));
    doCommand(Doc, "obj.ViewObject.doubleClicked()");
}

bool CmdPartDatumPlane::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// Part_DatumLine
//===========================================================================
DEF_STD_CMD_A(CmdPartDatumLine)

CmdPartDatumLine::CmdPartDatumLine()
    : Command("Part_DatumLine")
{
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Datum Line");
    sToolTipText = QT_TR_NOOP("Creates a datum line that can be attached to other objects");
    sWhatsThis = "Part_DatumLine";
    sStatusTip = sToolTipText;
    sPixmap = "Std_Axis";
}

void CmdPartDatumLine::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Add datum line"));

    std::string name = getUniqueObjectName("DatumLine");
    doCommand(Doc, "obj = App.activeDocument().addObject('Part::DatumLine','%s')", name.c_str());
    applyAttachmentFromSelection(getDocument()->getObject(name.c_str()));
    doCommand(Doc, "obj.ViewObject.doubleClicked()");
}

bool CmdPartDatumLine::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// Part_DatumPoint
//===========================================================================
DEF_STD_CMD_A(CmdPartDatumPoint)

CmdPartDatumPoint::CmdPartDatumPoint()
    : Command("Part_DatumPoint")
{
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Datum Point");
    sToolTipText = QT_TR_NOOP("Creates a datum point that can be attached to other objects");
    sWhatsThis = "Part_DatumPoint";
    sStatusTip = sToolTipText;
    sPixmap = "Std_Point";
}

void CmdPartDatumPoint::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    openCommand(QT_TRANSLATE_NOOP("Command", "Add datum point"));

    std::string name = getUniqueObjectName("DatumPoint");
    doCommand(Doc, "obj = App.activeDocument().addObject('Part::DatumPoint','%s')", name.c_str());
    applyAttachmentFromSelection(getDocument()->getObject(name.c_str()));
    doCommand(Doc, "obj.ViewObject.doubleClicked()");
}

bool CmdPartDatumPoint::isActive()
{
    return hasActiveDocument();
}


//===========================================================================
// Part_Datums
//===========================================================================
class CmdPartDatums: public Gui::GroupCommand
{
public:
    CmdPartDatums()
        : GroupCommand("Part_Datums")
    {
        sGroup = QT_TR_NOOP("Part");
        sMenuText = QT_TR_NOOP("Datums");
        sToolTipText
            = QT_TR_NOOP("Creates a datum object (coordinate system, plane, line, or point) that can be attached to other objects");
        sWhatsThis = "Part_Datums";
        sStatusTip = sToolTipText;

        setCheckable(false);

        addCommand("Part_CoordinateSystem");
        addCommand("Part_DatumPlane");
        addCommand("Part_DatumLine");
        addCommand("Part_DatumPoint");
    }

    const char* className() const override
    {
        return "CmdPartDatums";
    }

    bool isActive() override
    {
        return hasActiveDocument();
    }
};
//---------------------------------------------------------------

void CreatePartDatumCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartCoordinateSystem());
    rcCmdMgr.addCommand(new CmdPartDatumPlane());
    rcCmdMgr.addCommand(new CmdPartDatumLine());
    rcCmdMgr.addCommand(new CmdPartDatumPoint());
    rcCmdMgr.addCommand(new CmdPartDatums());
}
