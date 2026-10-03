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

#include <QFileInfo>
#include <QPointer>
#include <QString>

#include <App/Document.h>
#include <Mod/Part/App/PartFeature.h>
#include <Gui/MDIView.h>
#include <Base/Tools.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/FileDialog.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/WaitCursor.h>
#include <Mod/Part/App/Datums.h>

#include "BoxSelection.h"
#include "CommandSupport.h"
#include "DlgPrimitives.h"
#include "SectionCutting.h"
#include "TaskCheckGeometry.h"
#include "ViewProvider.h"


static bool documentHasVisibleShapes()
{
    auto* doc = App::GetApplication().getActiveDocument();
    if (!doc) {
        return false;
    }
    for (auto* obj : Part::getShapeObjects(doc)) {
        if (obj->Visibility.getValue()) {
            return true;
        }
    }
    return false;
}

//===========================================================================
// Part_PickCurveNet
//===========================================================================
DEF_STD_CMD(CmdPartPickCurveNet)

CmdPartPickCurveNet::CmdPartPickCurveNet()
    : Command("Part_PickCurveNet")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Pick Curve Network");
    sToolTipText = QT_TR_NOOP("Picks a curve network");
    sWhatsThis = "Part_PickCurveNet";
    sStatusTip = sToolTipText;
    sPixmap = "Test1";
}

void CmdPartPickCurveNet::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

//===========================================================================
// Part_NewDoc
//===========================================================================
DEF_STD_CMD(CmdPartNewDoc)

CmdPartNewDoc::CmdPartNewDoc()
    : Command("Part_NewDoc")
{
    sAppModule = "Part";
    sGroup = "Part";
    sMenuText = "New Document";
    sToolTipText = "Creates an Empty Part Document";
    sWhatsThis = "Part_NewDoc";
    sStatusTip = sToolTipText;
    sPixmap = "New";
}

void CmdPartNewDoc::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    doCommand(Doc, "d = App.New()");
    updateActive();
}

//===========================================================================
// Part_Box2
//===========================================================================
DEF_STD_CMD_A(CmdPartBox2)

CmdPartBox2::CmdPartBox2()
    : Command("Part_Box2")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Box Fix 1");
    sToolTipText = QT_TR_NOOP("Creates a solid box");
    sWhatsThis = "Part_Box2";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Box_Parametric";
}

void CmdPartBox2::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    openCommand(QT_TRANSLATE_NOOP("Command", "Part Box Create"));
    doCommand(Doc, "from FreeCAD import Base");
    doCommand(Doc, "import Part");
    doCommand(Doc, "__fb__ = App.ActiveDocument.addObject(\"Part::Box\",\"PartBox\")");
    doCommand(Doc, "__fb__.Location = Base.Vector(0.0,0.0,0.0)");
    doCommand(Doc, "__fb__.Length = 100.0");
    doCommand(Doc, "__fb__.Width = 100.0");
    doCommand(Doc, "__fb__.Height = 100.0");
    doCommand(Doc, "del __fb__");
    commitCommand();
    updateActive();
}

bool CmdPartBox2::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}

//===========================================================================
// Part_Box3
//===========================================================================
DEF_STD_CMD_A(CmdPartBox3)

CmdPartBox3::CmdPartBox3()
    : Command("Part_Box3")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Box Fix 2");
    sToolTipText = QT_TR_NOOP("Creates a solid box");
    sWhatsThis = "Part_Box3";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Box_Parametric";
}

void CmdPartBox3::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    openCommand(QT_TRANSLATE_NOOP("Command", "Part Box Create"));
    doCommand(Doc, "from FreeCAD import Base");
    doCommand(Doc, "import Part");
    doCommand(Doc, "__fb__ = App.ActiveDocument.addObject(\"Part::Box\",\"PartBox\")");
    doCommand(Doc, "__fb__.Location = Base.Vector(50.0,50.0,50.0)");
    doCommand(Doc, "__fb__.Length = 100.0");
    doCommand(Doc, "__fb__.Width = 100.0");
    doCommand(Doc, "__fb__.Height = 100.0");
    doCommand(Doc, "del __fb__");
    commitCommand();
    updateActive();
}

bool CmdPartBox3::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}

//===========================================================================
// Part_Primitives
//===========================================================================
DEF_STD_CMD_A(CmdPartPrimitives)

CmdPartPrimitives::CmdPartPrimitives()
    : Command("Part_Primitives")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Primitive");

    sToolTipText = QT_TR_NOOP("Creates solid geometric primitives parametrically");
    sWhatsThis = "Part_Primitives";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Primitives";
}

void CmdPartPrimitives::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    if (!ensureActiveDocument()) {
        return;
    }
    PartGui::TaskPrimitives* dlg = new PartGui::TaskPrimitives();
    Gui::Control().showDialog(dlg);
}

bool CmdPartPrimitives::isActive()
{
    return !Gui::Control().activeDialog();
}

//===========================================================================
// CmdPartImport
//===========================================================================
DEF_STD_CMD_A(CmdPartImport)

CmdPartImport::CmdPartImport()
    : Command("Part_Import")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Import CAD File");

    sToolTipText = QT_TR_NOOP("Imports a CAD file");
    sWhatsThis = "Part_Import";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Import";
}

void CmdPartImport::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::FileDialog::FilterList filter {
        {QStringLiteral("STEP"), {"*.stp", "*.step"}},
        {QObject::tr("STEP with colors"), {"*.stp", "*.step"}},
        {QStringLiteral("IGES"), {"*.igs", "*.iges"}},
        {QObject::tr("IGES with colors"), {"*.igs", "*.iges"}},
        {QStringLiteral("BREP"), {"*.brp", "*.brep"}},
    };

    // Default to the first filter. getOpenFileName reads this as the initially
    // selected filter index and overwrites it with the user's choice; leaving it
    // uninitialized crashes the non-native dialog path (filters[garbage]).
    qsizetype select = 0;
    QString fn
        = Gui::FileDialog::getOpenFileName(Gui::getMainWindow(), QString(), QString(), filter, &select);
    if (!fn.isEmpty()) {
        Gui::WaitCursor wc;
        fn = QString::fromStdString(Base::Tools::escapeEncodeFilename(fn.toStdString()));

        App::Document* pDoc = getDocument();

        // Ensure we have a document to import into.
        if (!pDoc) {
            doCommand(Doc, "App.newDocument(type=App.DocTypePart)");
            pDoc = getDocument();
            if (!pDoc) {
                return;
            }
        }

        // Run the format-appropriate import command into pDoc.
        auto runImport = [&]() {
            if (select == 4) {  // BREP
                doCommand(Doc, "import Part");
                doCommand(Doc, "Part.insert(\"%s\",\"%s\")", (const char*)fn.toUtf8(), pDoc->getName());
            }
            else {
                doCommand(Doc, "import ImportGui");
                doCommand(
                    Doc,
                    "ImportGui.insert(\"%s\",\"%s\")",
                    (const char*)fn.toUtf8(),
                    pDoc->getName()
                );
            }
        };

        // The document is the container (ARCHITECTURE §7.1): imported geometry lands at its
        // root and needs no wrapper to hold it.
        openCommand(QT_TRANSLATE_NOOP("Command", "Import Part"));
        runImport();
        commitCommand();

        std::list<Gui::MDIView*> views = getActiveGuiDocument()->getMDIViewsOfType(
            Gui::View3DInventor::getClassTypeId()
        );
        for (auto view : views) {
            view->viewAll();
        }
    }
}

bool CmdPartImport::isActive()
{
    return true;
}

//===========================================================================
// CmdPartExport
//===========================================================================
DEF_STD_CMD_A(CmdPartExport)

CmdPartExport::CmdPartExport()
    : Command("Part_Export")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Export CAD File");

    sToolTipText = QT_TR_NOOP("Exports to a CAD file");
    sWhatsThis = "Part_Export";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Export";
}

void CmdPartExport::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::FileDialog::FilterList filter {
        {QStringLiteral("STEP"), {"*.stp", "*.step"}},
        {QObject::tr("STEP with colors"), {"*.stp", "*.step"}},
        {QStringLiteral("IGES"), {"*.igs", "*.iges"}},
        {QObject::tr("IGES with colors"), {"*.igs", "*.iges"}},
        {QStringLiteral("BREP"), {"*.brp", "*.brep"}},
    };

    qsizetype select;
    QString fn
        = Gui::FileDialog::getSaveFileName(Gui::getMainWindow(), QString(), QString(), filter, &select);
    if (!fn.isEmpty()) {
        App::Document* pDoc = getDocument();
        if (!pDoc) {  // no document
            return;
        }
        if (select == 1 || select == 3) {
            Gui::Application::Instance->exportTo((const char*)fn.toUtf8(), pDoc->getName(), "ImportGui");
        }
        else {
            Gui::Application::Instance->exportTo((const char*)fn.toUtf8(), pDoc->getName(), "Part");
        }
    }
}

bool CmdPartExport::isActive()
{
    return Gui::Selection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        > 0;
}

//===========================================================================
// PartImportCurveNet
//===========================================================================
DEF_STD_CMD_A(CmdPartImportCurveNet)

CmdPartImportCurveNet::CmdPartImportCurveNet()
    : Command("Part_ImportCurveNet")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Import Curve Network");

    sToolTipText = QT_TR_NOOP("Imports a curve network");
    sWhatsThis = "Part_ImportCurveNet";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Box_Parametric";
}

void CmdPartImportCurveNet::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const Gui::FileDialog::FilterList filter {
        {QObject::tr("All CAD Files"), {"*.stp", "*.step", "*.igs", "*.iges", "*.brp", "*.brep"}},
        {QStringLiteral("STEP"), {"*.stp", "*.step"}},
        {QStringLiteral("IGES"), {"*.igs", "*.iges"}},
        {QStringLiteral("BREP"), {"*.brp", "*.brep"}},
        Gui::FileDialog::Filter::AllFiles(),
    };

    QString fn = Gui::FileDialog::getOpenFileName(Gui::getMainWindow(), QString(), QString(), filter);
    if (!fn.isEmpty()) {
        QFileInfo fi;
        fi.setFile(fn);
        openCommand(QT_TRANSLATE_NOOP("Command", "Import Curve Net"));
        doCommand(
            Doc,
            "f = App.activeDocument().addObject(\"Part::CurveNet\",\"%s\")",
            (const char*)fi.baseName().toLatin1()
        );
        doCommand(Doc, "f.FileName = \"%s\"", (const char*)fn.toLatin1());
        commitCommand();
        updateActive();
    }
}

bool CmdPartImportCurveNet::isActive()
{
    if (getActiveGuiDocument()) {
        return true;
    }
    else {
        return false;
    }
}

//===========================================================================
// Part_ShapeInfo
//===========================================================================

DEF_STD_CMD_A(CmdShapeInfo)

CmdShapeInfo::CmdShapeInfo()
    : Command("Part_ShapeInfo")
{
    sAppModule = "Part";
    sGroup = "Part";
    sMenuText = "Shape Info";

    sToolTipText = "Displays information about the selected shape";
    sWhatsThis = "Part_ShapeInfo";
    sStatusTip = sToolTipText;
    sPixmap = "Part_ShapeInfo";
}

void CmdShapeInfo::activated(int iMsg)
{
    Q_UNUSED(iMsg);
}

bool CmdShapeInfo::isActive()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc || doc->countObjectsOfType<Part::ShapeFeature>() == 0) {
        return false;
    }

    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom<Gui::View3DInventor>()) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>(view)->getViewer();
        return !viewer->isEditing();
    }

    return false;
}

//===========================================================================
// Part_CheckGeometry
//===========================================================================

DEF_STD_CMD_A(CmdCheckGeometry)

CmdCheckGeometry::CmdCheckGeometry()
    : Command("Part_CheckGeometry")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Check Geometry");
    sToolTipText = QT_TR_NOOP("Analyzes the selected shapes for errors");
    sWhatsThis = "Part_CheckGeometry";
    sStatusTip = sToolTipText;
    sPixmap = "Part_CheckGeometry";
}

void CmdCheckGeometry::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog();
    if (!dlg) {
        dlg = new PartGui::TaskCheckGeometryDialog();
    }
    Gui::Control().showDialog(dlg);
}

bool CmdCheckGeometry::isActive()
{
    bool hasShapes = PartGui::hasShapesInSelection();
    return (hasShapes && !Gui::Control().activeDialog(getDocument()));
}

//===========================================================================
// Part_ColorPerFace
//===========================================================================

DEF_STD_CMD_A(CmdColorPerFace)

CmdColorPerFace::CmdColorPerFace()
    : Command("Part_ColorPerFace")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Appearance per &Face");
    sToolTipText = QT_TR_NOOP("Sets the appearance of individual faces of the selected object");
    sStatusTip = sToolTipText;
    sWhatsThis = "Part_ColorPerFace";
    sPixmap = "Part_ColorFace";
}

void CmdColorPerFace::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    if (getActiveGuiDocument()->getInEdit()) {
        getActiveGuiDocument()->resetEdit();
    }
    std::vector<App::DocumentObject*> sel = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::erase_if(sel, [](App::DocumentObject* o) { return !Part::hasShape(o); });
    if (sel.empty()) {
        return;
    }
    PartGui::ViewProviderPartExt* vp = dynamic_cast<PartGui::ViewProviderPartExt*>(
        Gui::Application::Instance->getViewProvider(sel.front())
    );
    if (vp) {
        vp->changeFaceAppearances();
    }
}

bool CmdColorPerFace::isActive()
{
    bool objectSelected = Gui::Selection().countObjectsOfType<Part::ShapeFeature>() == 1;
    return (hasActiveDocument() && !Gui::Control().activeDialog(getDocument()) && objectSelected);
}

//===========================================================================
// Part_BoxSelection
//===========================================================================

DEF_STD_CMD_A(CmdBoxSelection)

CmdBoxSelection::CmdBoxSelection()
    : Command("Part_BoxSelection")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Box Selection");
    sToolTipText = QT_TR_NOOP("Selects elements in the 3D view using a box selection");
    sWhatsThis = "Part_BoxSelection";
    sStatusTip = QT_TR_NOOP("Box selection");
    sPixmap = "Part_BoxSelection";
}

void CmdBoxSelection::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    PartGui::BoxSelection* sel = new PartGui::BoxSelection();
    sel->setAutoDelete(true);
    sel->start(TopAbs_FACE);
}

bool CmdBoxSelection::isActive()
{
    return hasActiveDocument();
}

//===========================================================================
// Part_SectionCut
//===========================================================================

DEF_STD_CMD_AC(CmdPartSectionCut)

CmdPartSectionCut::CmdPartSectionCut()
    : Command("Part_SectionCut")
{
    sAppModule = "Part";
    sGroup = "View";
    sMenuText = QT_TR_NOOP("Persiste&nt Section Cut");
    sToolTipText = QT_TR_NOOP(
        "Creates a new object as a boolean intersection of all visible "
        "shapes and the selected axis planes"
    );
    sWhatsThis = "Part_SectionCut";
    sStatusTip = sToolTipText;
    sPixmap = "Part_SectionCut";
    eType = AlterDoc | Alter3DView;
}

Gui::Action* CmdPartSectionCut::createAction()
{
    Gui::Action* pcAction = Gui::Command::createAction();
    return pcAction;
}

void CmdPartSectionCut::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    static QPointer<PartGui::SectionCut> sectionCut = nullptr;
    if (!sectionCut) {
        sectionCut = PartGui::SectionCut::makeDockWidget(Gui::getMainWindow());
    }
}

bool CmdPartSectionCut::isActive()
{
    return documentHasVisibleShapes();
}

void CreatePartCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    CreatePartBooleanCommands(rcCmdMgr);
    CreatePartShapeToolCommands(rcCmdMgr);
    CreatePartDatumCommands(rcCmdMgr);

    rcCmdMgr.addCommand(new CmdPartPrimitives());

    rcCmdMgr.addCommand(new CmdPartImport());
    rcCmdMgr.addCommand(new CmdPartExport());
    rcCmdMgr.addCommand(new CmdPartImportCurveNet());
    rcCmdMgr.addCommand(new CmdPartPickCurveNet());
    rcCmdMgr.addCommand(new CmdShapeInfo());
    rcCmdMgr.addCommand(new CmdCheckGeometry());
    rcCmdMgr.addCommand(new CmdColorPerFace());
    rcCmdMgr.addCommand(new CmdBoxSelection());
    rcCmdMgr.addCommand(new CmdPartSectionCut());
}
