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

#include <QString>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Document.h>
#include <Mod/Part/App/PartFeature.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionObject.h>
#include <Mod/Part/App/Part2DObject.h>

#include "CommandSupport.h"
#include "CrossSections.h"
#include "DlgExtrusion.h"
#include "DlgFilletEdges.h"
#include "DlgProjectionOnSurface.h"
#include "DlgRevolution.h"
#include "DlgScale.h"
#include "Mirroring.h"
#include "TaskLoft.h"
#include "TaskShapeBuilder.h"
#include "TaskSweep.h"


//===========================================================================
// Part_Compound
//===========================================================================
DEF_STD_CMD_A(CmdPartCompound)

CmdPartCompound::CmdPartCompound()
    : Command("Part_Compound")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Compound");
    sToolTipText = QT_TR_NOOP("Compounds the selected shapes");
    sWhatsThis = "Part_Compound";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Compound";
}

void CmdPartCompound::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    unsigned int n = getSelection().countObjectsOfType<App::DocumentObject>(
        nullptr,
        Gui::ResolveMode::FollowLink
    );
    if (n < 1) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select at least one shape")
        );
        return;
    }

    std::string FeatName = getUniqueObjectName("Compound");

    std::vector<Gui::SelectionSingleton::SelObj> Sel = getSelection().getSelection();
    std::stringstream str;

    // avoid duplicates without changing the order
    std::set<std::string> tempSelNames;
    str << "App.activeDocument()." << FeatName << ".Links = [";
    for (const auto& it : Sel) {
        auto pos = tempSelNames.insert(it.FeatName);
        if (pos.second) {
            str << "App.activeDocument()." << it.FeatName << ",";
        }
    }
    str << "]";

    openCommand(QT_TRANSLATE_NOOP("Command", "Compound"));
    doCommand(Doc, "App.activeDocument().addObject(\"Part::Compound\",\"%s\")", FeatName.c_str());
    runCommand(Doc, str.str().c_str());
    updateActive();
    commitCommand();
}

bool CmdPartCompound::isActive()
{
    return getSelection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
        >= 1;
}

//===========================================================================
// Part_MakeSolid
//===========================================================================
DEF_STD_CMD_A(CmdPartMakeSolid)

CmdPartMakeSolid::CmdPartMakeSolid()
    : Command("Part_MakeSolid")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Convert to Solid");
    sToolTipText = QT_TR_NOOP("Converts the selected shell or compound to a solid");
    sWhatsThis = "Part_MakeSolid";
    sStatusTip = sToolTipText;
    sPixmap = "Part_MakeSolid";
}

void CmdPartMakeSolid::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> objs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId(),
        nullptr,
        Gui::ResolveMode::FollowLink
    );
    runCommand(Doc, "import Part");
    for (auto it : objs) {
        const TopoDS_Shape& shape = Part::Feature::getShape(
            it,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!shape.IsNull()) {
            TopAbs_ShapeEnum type = shape.ShapeType();
            QString str;
            if (type == TopAbs_SOLID) {
                Base::Console().message(
                    "%s is ignored because it is already a solid.\n",
                    it->Label.getValue()
                );
            }
            else if (type == TopAbs_COMPOUND || type == TopAbs_COMPSOLID) {
                str = QStringLiteral(
                          "__s__=App.ActiveDocument.%1.Shape.Faces\n"
                          "__s__=Part.Solid(Part.Shell(__s__))\n"
                          "__o__=App.ActiveDocument.addObject(\"Part::Feature\",\"%1_solid\")\n"
                          "__o__.Label=\"%2 (Solid)\"\n"
                          "__o__.Shape=__s__\n"
                          "del __s__, __o__"
                )
                          .arg(
                              QLatin1String(it->getNameInDocument()),
                              QLatin1String(it->Label.getValue())
                          );
            }
            else if (type == TopAbs_SHELL) {
                str = QStringLiteral(
                          "__s__=App.ActiveDocument.%1.Shape\n"
                          "__s__=Part.Solid(__s__)\n"
                          "__o__=App.ActiveDocument.addObject(\"Part::Feature\",\"%1_solid\")\n"
                          "__o__.Label=\"%2 (Solid)\"\n"
                          "__o__.Shape=__s__\n"
                          "del __s__, __o__"
                )
                          .arg(
                              QLatin1String(it->getNameInDocument()),
                              QLatin1String(it->Label.getValue())
                          );
            }
            else {
                Base::Console().message(
                    "%s is ignored because it is neither a shell nor a compound.\n",
                    it->Label.getValue()
                );
            }

            try {
                if (!str.isEmpty()) {
                    runCommand(Doc, str.toLatin1());
                }
            }
            catch (const Base::Exception& e) {
                Base::Console().error("Cannot convert %s because %s.\n", it->Label.getValue(), e.what());
            }
        }
    }
}

bool CmdPartMakeSolid::isActive()
{
    return true;
}

//===========================================================================
// Part_ReverseShape
//===========================================================================
DEF_STD_CMD_A(CmdPartReverseShape)

CmdPartReverseShape::CmdPartReverseShape()
    : Command("Part_ReverseShape")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Reverse Shapes");
    sToolTipText = QT_TR_NOOP("Reverses the orientation of the selected shapes");
    sWhatsThis = "Part_ReverseShape";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Reverse_Shape";
}

void CmdPartReverseShape::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> objs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    openCommand(QT_TRANSLATE_NOOP("Command", "Reverse"));
    for (auto it : objs) {
        const TopoDS_Shape& shape = Part::Feature::getShape(
            it,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!shape.IsNull()) {
            std::string name = it->getNameInDocument();
            name += "_rev";
            name = getUniqueObjectName(name.c_str());

            QString str = QStringLiteral(
                              "__o__=App.ActiveDocument.addObject(\"Part::Reverse\",\"%1\")\n"
                              "__o__.Source=App.ActiveDocument.%2\n"
                              "__o__.Label=\"%3 (Rev)\"\n"
                              "del __o__"
            )
                              .arg(
                                  QString::fromLatin1(name.c_str()),
                                  QString::fromLatin1(it->getNameInDocument()),
                                  QString::fromLatin1(it->Label.getValue())
                              );

            try {
                runCommand(Doc, str.toLatin1());
                copyVisual(name.c_str(), "ShapeAppearance", it->getNameInDocument());
                copyVisual(name.c_str(), "LineColor", it->getNameInDocument());
                copyVisual(name.c_str(), "PointColor", it->getNameInDocument());
            }
            catch (const Base::Exception& e) {
                Base::Console().error("Cannot convert %s because %s.\n", it->Label.getValue(), e.what());
            }
        }
    }

    commitCommand();
    updateActive();
}

bool CmdPartReverseShape::isActive()
{
    return PartGui::hasShapesInSelection();
}

//===========================================================================
// Part_Extrude
//===========================================================================
DEF_STD_CMD_A(CmdPartExtrude)

CmdPartExtrude::CmdPartExtrude()
    : Command("Part_Extrude")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Extrude");
    sToolTipText = QT_TR_NOOP("Extrudes the selected sketch or profile");
    sWhatsThis = "Part_Extrude";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Extrude";
}

void CmdPartExtrude::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskExtrusion());
}

bool CmdPartExtrude::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Scale
//===========================================================================
DEF_STD_CMD_A(CmdPartScale)

CmdPartScale::CmdPartScale()
    : Command("Part_Scale")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Scale");
    sToolTipText = QT_TR_NOOP("Scales the selected shape");
    sWhatsThis = "Part_Scale";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Scale";
}

void CmdPartScale::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    Gui::Control().showDialog(new PartGui::TaskScale());
}

bool CmdPartScale::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_MakeFace
//===========================================================================
DEF_STD_CMD_A(CmdPartMakeFace)

CmdPartMakeFace::CmdPartMakeFace()
    : Command("Part_MakeFace")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Face From Wires");
    sToolTipText = QT_TR_NOOP("Creates a face from the selected wires (e.g. from a sketch)");
    sWhatsThis = "Part_MakeFace";
    sStatusTip = sToolTipText;
    sPixmap = "Part_MakeFace";
}

void CmdPartMakeFace::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    auto sketches = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId(),
        nullptr,
        Gui::ResolveMode::FollowLink
    );
    if (sketches.empty()) {
        return;
    }
    openCommand(QT_TRANSLATE_NOOP("Command", "Make face"));

    try {
        App::DocumentT doc(sketches.front()->getDocument());
        std::stringstream str;
        str << doc.getDocumentPython() << R"(.addObject("Part::Face", "Face").Sources = ()";
        for (auto& obj : sketches) {
            str << App::DocumentObjectT(obj).getObjectPython() << ", ";
        }

        str << ")";

        runCommand(Doc, str.str().c_str());
        commitCommand();
        updateActive();
    }
    catch (...) {
        abortCommand();
        throw;
    }
}

bool CmdPartMakeFace::isActive()
{
    return (
        Gui::Selection().countObjectsOfType<App::DocumentObject>(nullptr, Gui::ResolveMode::FollowLink)
            > 0
        && !Gui::Control().activeDialog()
    );
}

//===========================================================================
// Part_Revolve
//===========================================================================
DEF_STD_CMD_A(CmdPartRevolve)

CmdPartRevolve::CmdPartRevolve()
    : Command("Part_Revolve")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Revolve");
    sToolTipText = QT_TR_NOOP("Revolves the selected shape");
    sWhatsThis = "Part_Revolve";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Revolve";
}

void CmdPartRevolve::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskRevolution());
}

bool CmdPartRevolve::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Fillet
//===========================================================================
DEF_STD_CMD_A(CmdPartFillet)

CmdPartFillet::CmdPartFillet()
    : Command("Part_Fillet")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Fillet");
    sToolTipText = QT_TR_NOOP("Fillets the selected edges of a shape");
    sWhatsThis = "Part_Fillet";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Fillet";
}

void CmdPartFillet::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskFilletEdges(nullptr));
}

bool CmdPartFillet::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Chamfer
//===========================================================================
DEF_STD_CMD_A(CmdPartChamfer)

CmdPartChamfer::CmdPartChamfer()
    : Command("Part_Chamfer")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Chamfer");
    sToolTipText = QT_TR_NOOP("Chamfers the selected edges of a shape");
    sWhatsThis = "Part_Chamfer";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Chamfer";
}

void CmdPartChamfer::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskChamferEdges(nullptr));
}

bool CmdPartChamfer::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Mirror
//===========================================================================
DEF_STD_CMD_A(CmdPartMirror)

CmdPartMirror::CmdPartMirror()
    : Command("Part_Mirror")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Mirror");
    sToolTipText = QT_TR_NOOP("Mirrors the selected shape");
    sWhatsThis = "Part_Mirror";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Mirror";
}

void CmdPartMirror::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskMirroring());
}

bool CmdPartMirror::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_CrossSections
//===========================================================================
DEF_STD_CMD_A(CmdPartCrossSections)

CmdPartCrossSections::CmdPartCrossSections()
    : Command("Part_CrossSections")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Cross-Sections");
    sToolTipText = QT_TR_NOOP("Creates cross-sections");
    sWhatsThis = "Part_CrossSections";
    sStatusTip = sToolTipText;
    sPixmap = "Part_CrossSections";
}

void CmdPartCrossSections::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::TaskView::TaskDialog* dlg = Gui::Control().activeDialog(getDocument());
    if (!dlg) {
        std::vector<Part::TopoShape> shapes = PartGui::getShapesFromSelection();
        Base::BoundBox3d bbox;
        for (const auto& it : shapes) {
            bbox.Add(it.getBoundBox());
        }
        dlg = new PartGui::TaskCrossSections(bbox);
    }
    Gui::Control().showDialog(dlg);
}

bool CmdPartCrossSections::isActive()
{
    bool hasShapes = PartGui::hasShapesInSelection();
    return (hasShapes && !Gui::Control().activeDialog(getDocument()));
}

//===========================================================================
// Part_Builder
//===========================================================================

DEF_STD_CMD_A(CmdPartBuilder)

CmdPartBuilder::CmdPartBuilder()
    : Command("Part_Builder")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Shape Builder");

    sToolTipText = QT_TR_NOOP("Advanced utility to create shapes");
    sWhatsThis = "Part_Builder";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Shapebuilder";
}

void CmdPartBuilder::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskShapeBuilder());
}

bool CmdPartBuilder::isActive()
{
    if (Gui::Control().activeDialog()) {
        return false;
    }
    return hasActiveDocument();
}

//===========================================================================
// Part_Loft
//===========================================================================

DEF_STD_CMD_A(CmdPartLoft)

CmdPartLoft::CmdPartLoft()
    : Command("Part_Loft")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Loft");
    sToolTipText = QT_TR_NOOP("Lofts the selected profiles");
    sWhatsThis = "Part_Loft";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Loft";
}

void CmdPartLoft::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskLoft());
}

bool CmdPartLoft::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Sweep
//===========================================================================

DEF_STD_CMD_A(CmdPartSweep)

CmdPartSweep::CmdPartSweep()
    : Command("Part_Sweep")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Sweep");
    sToolTipText = QT_TR_NOOP("Sweeps profiles along a wire");
    sWhatsThis = "Part_Sweep";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Sweep";
}

void CmdPartSweep::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Control().showDialog(new PartGui::TaskSweep());
}

bool CmdPartSweep::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_Offset
//===========================================================================

DEF_STD_CMD_A(CmdPartOffset)

CmdPartOffset::CmdPartOffset()
    : Command("Part_Offset")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("3D Offset");
    sToolTipText = QT_TR_NOOP("Offsets shapes in 3D");
    sWhatsThis = "Part_Offset";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Offset";
}

void CmdPartOffset::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::vector<App::DocumentObject*> shapes;
    for (auto it : docobjs) {
        if (!Part::Feature::getTopoShape(it, Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform)
                 .isNull()) {
            shapes.push_back(it);
        }
    }
    if (shapes.size() != 1) {
        return;
    }
    App::DocumentObject* shape = shapes.front();
    std::string offset = getUniqueObjectName("Offset");

    openCommand(QT_TRANSLATE_NOOP("Command", "Make Offset"));
    doCommand(Doc, "App.ActiveDocument.addObject(\"Part::Offset\",\"%s\")", offset.c_str());
    doCommand(
        Doc,
        "App.ActiveDocument.%s.Source = App.ActiveDocument.%s",
        offset.c_str(),
        shape->getNameInDocument()
    );
    doCommand(Doc, "App.ActiveDocument.%s.Value = 1.0", offset.c_str());
    updateActive();

    doCommand(Gui, "Gui.ActiveDocument.setEdit('%s')", offset.c_str());

    if (!shape->isDerivedFrom<Part::Part2DObject>()) {
        copyVisual(offset.c_str(), "ShapeAppearance", shape->getNameInDocument());
        copyVisual(offset.c_str(), "LineColor", shape->getNameInDocument());
        copyVisual(offset.c_str(), "PointColor", shape->getNameInDocument());
    }
}

bool CmdPartOffset::isActive()
{
    bool hasShapes = PartGui::hasShapesInSelection();
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    return (hasShapes && !Gui::Control().activeDialog() && docobjs.size() == 1);
}


//===========================================================================
// Part_Offset2D
//===========================================================================

DEF_STD_CMD_A(CmdPartOffset2D)

CmdPartOffset2D::CmdPartOffset2D()
    : Command("Part_Offset2D")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("2D Offset");
    sToolTipText = QT_TR_NOOP("Offsets planar shapes in 2D");
    sWhatsThis = "Part_Offset2D";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Offset2D";
}

void CmdPartOffset2D::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::vector<App::DocumentObject*> shapes;

    for (auto it : docobjs) {
        if (!Part::Feature::getTopoShape(it, Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform)
                 .isNull()) {
            shapes.push_back(it);
        }
    }
    if (shapes.size() != 1) {
        return;
    }
    App::DocumentObject* shape = shapes.front();
    std::string offset = getUniqueObjectName("Offset2D");

    openCommand(QT_TRANSLATE_NOOP("Command", "Make 2D Offset"));
    doCommand(Doc, "App.ActiveDocument.addObject(\"Part::Offset2D\",\"%s\")", offset.c_str());
    doCommand(
        Doc,
        "App.ActiveDocument.%s.Source = App.ActiveDocument.%s",
        offset.c_str(),
        shape->getNameInDocument()
    );
    doCommand(Doc, "App.ActiveDocument.%s.Value = 1.0", offset.c_str());
    updateActive();
    doCommand(Gui, "Gui.ActiveDocument.setEdit('%s')", offset.c_str());

    if (!shape->isDerivedFrom<Part::Part2DObject>()) {
        copyVisual(offset.c_str(), "ShapeAppearance", shape->getNameInDocument());
        copyVisual(offset.c_str(), "LineColor", shape->getNameInDocument());
        copyVisual(offset.c_str(), "PointColor", shape->getNameInDocument());
    }
}

bool CmdPartOffset2D::isActive()
{
    bool hasShapes = PartGui::hasShapesInSelection();
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    return (hasShapes && !Gui::Control().activeDialog() && docobjs.size() == 1);
}

//===========================================================================
// Part_CompOffset (dropdown toolbar button for Offset features)
//===========================================================================

DEF_STD_CMD_ACL(CmdPartCompOffset)

CmdPartCompOffset::CmdPartCompOffset()
    : Command("Part_CompOffset")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Offset");
    sToolTipText = QT_TR_NOOP("Tools to offset shapes (construct parallel shapes)");
    sWhatsThis = "Part_CompOffset";
    sStatusTip = sToolTipText;
}

void CmdPartCompOffset::activated(int iMsg)
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();
    if (iMsg == 0) {
        rcCmdMgr.runCommandByName("Part_Offset");
    }
    else if (iMsg == 1) {
        rcCmdMgr.runCommandByName("Part_Offset2D");
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

Gui::Action* CmdPartCompOffset::createAction()
{
    Gui::ActionGroup* pcAction = new Gui::ActionGroup(this, Gui::getMainWindow());
    pcAction->setDropDownMenu(true);
    applyCommandData(this->className(), pcAction);

    QAction* cmd0 = pcAction->addAction(QString());
    cmd0->setIcon(Gui::BitmapFactory().iconFromTheme("Part_Offset"));
    QAction* cmd1 = pcAction->addAction(QString());
    cmd1->setIcon(Gui::BitmapFactory().iconFromTheme("Part_Offset2D"));

    _pcAction = pcAction;
    languageChange();

    pcAction->setIcon(cmd0->icon());
    int defaultId = 0;
    pcAction->setProperty("defaultAction", QVariant(defaultId));

    return pcAction;
}

void CmdPartCompOffset::languageChange()
{
    Command::languageChange();

    if (!_pcAction) {
        return;
    }

    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    Gui::ActionGroup* pcAction = qobject_cast<Gui::ActionGroup*>(_pcAction);
    QList<QAction*> a = pcAction->actions();

    Gui::Command* cmdOffset = rcCmdMgr.getCommandByName("Part_Offset");
    if (cmdOffset) {
        QAction* cmd0 = a[0];
        cmd0->setText(QApplication::translate(cmdOffset->className(), cmdOffset->getMenuText()));
        cmd0->setToolTip(QApplication::translate(cmdOffset->className(), cmdOffset->getToolTipText()));
        cmd0->setStatusTip(QApplication::translate(cmdOffset->className(), cmdOffset->getStatusTip()));
    }

    Gui::Command* cmdOffset2D = rcCmdMgr.getCommandByName("Part_Offset2D");
    if (cmdOffset2D) {
        QAction* cmd1 = a[1];
        cmd1->setText(QApplication::translate(cmdOffset2D->className(), cmdOffset2D->getMenuText()));
        cmd1->setToolTip(
            QApplication::translate(cmdOffset2D->className(), cmdOffset2D->getToolTipText())
        );
        cmd1->setStatusTip(
            QApplication::translate(cmdOffset2D->className(), cmdOffset2D->getStatusTip())
        );
    }
}

bool CmdPartCompOffset::isActive()
{
    bool hasShapes = PartGui::hasShapesInSelection();
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    return (hasShapes && !Gui::Control().activeDialog() && docobjs.size() == 1);
}
//===========================================================================
// Part_Thickness
//===========================================================================

DEF_STD_CMD_A(CmdPartThickness)

CmdPartThickness::CmdPartThickness()
    : Command("Part_Thickness")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Thickness");
    sToolTipText = QT_TR_NOOP(
        "Removes the selected faces and offsets the remaining shape outward to add thickness"
    );
    sWhatsThis = "Part_Thickness";
    sStatusTip = sToolTipText;
    sPixmap = "Part_Thickness";
}

void CmdPartThickness::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    const App::DocumentObject* obj = nullptr;
    std::string selection;
    const std::vector<Gui::SelectionObject> selobjs = Gui::Selection().getSelectionEx();
    std::vector<Part::TopoShape> subShapes;
    Part::TopoShape topoShape = Part::TopoShape();

    bool ok = true;
    if (selobjs.size() == 1) {
        selection = selobjs[0].getAsPropertyLinkSubString();
        const std::vector<std::string>& subnames = selobjs[0].getSubNames();
        obj = selobjs[0].getObject();
        topoShape = Part::Feature::getTopoShape(
            obj,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!topoShape.isNull()) {
            for (std::vector<std::string>::const_iterator it = subnames.begin(); it != subnames.end();
                 ++it) {
                subShapes.emplace_back(topoShape.getSubShape(subnames[0].c_str()));
            }
            for (const auto& it : subShapes) {
                TopoDS_Shape dsShape = it.getShape();
                if (dsShape.IsNull()
                    || dsShape.ShapeType() != TopAbs_FACE) {  // only face selection allowed
                    ok = false;
                }
            }
        }
        else {  // could be not a part::feature or app:link to non-part::feature or app::part
                // without a visible part::feature
            ok = false;
        }
    }
    else {  // not just one object selected
        ok = false;
    }

    int countSolids = 0;
    TopExp_Explorer xp;
    if (!topoShape.isNull()) {
        xp.Init(topoShape.getShape(), TopAbs_SOLID);
        for (; xp.More(); xp.Next()) {
            countSolids++;
        }
    }
    if (countSolids != 1 || !ok) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QApplication::translate("CmdPartThickness", "Wrong selection"),
            QApplication::translate("CmdPartThickness", "Selected shape is not a solid")
        );
        return;
    }

    std::string thick = getUniqueObjectName("Thickness");

    openCommand(QT_TRANSLATE_NOOP("Command", "Make Thickness"));
    doCommand(Doc, "App.ActiveDocument.addObject(\"Part::Thickness\",\"%s\")", thick.c_str());
    doCommand(Doc, "App.ActiveDocument.%s.Faces = %s", thick.c_str(), selection.c_str());
    doCommand(Doc, "App.ActiveDocument.%s.Value = 1.0", thick.c_str());
    updateActive();
    if (isActiveObjectValid()) {
        doCommand(
            App,
            "App.getDocument(\"%s\").getObject(\"%s\").ViewObject.Visibility = False",
            obj->getDocument()->getName(),
            obj->getNameInDocument()
        );
    }
    doCommand(Gui, "Gui.ActiveDocument.setEdit('%s')", thick.c_str());

    copyVisual(thick.c_str(), "ShapeAppearance", obj->getNameInDocument());
    copyVisual(thick.c_str(), "LineColor", obj->getNameInDocument());
    copyVisual(thick.c_str(), "PointColor", obj->getNameInDocument());
}

bool CmdPartThickness::isActive()
{
    bool objectsSelected = Gui::Selection().countObjectsOfType<Part::ShapeFeature>(
                               nullptr,
                               Gui::ResolveMode::FollowLink
                           )
        > 0;
    return (objectsSelected && !Gui::Control().activeDialog());
}

//===========================================================================
// Part_RuledSurface
//===========================================================================

DEF_STD_CMD_A(CmdPartRuledSurface)

CmdPartRuledSurface::CmdPartRuledSurface()
    : Command("Part_RuledSurface")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Ruled Surface");
    sToolTipText = QT_TR_NOOP("Creates a ruled surface between 2 selected wires");
    sWhatsThis = "Part_RuledSurface";
    sStatusTip = sToolTipText;
    sPixmap = "Part_RuledSurface";
}

void CmdPartRuledSurface::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    bool ok = true;
    TopoDS_Shape curve1, curve2;
    std::string link1, link2, obj1, obj2;
    const std::vector<Gui::SelectionObject> selobjs = Gui::Selection().getSelectionEx();
    const App::DocumentObject* docobj1 = nullptr;
    const App::DocumentObject* docobj2 = nullptr;

    if (selobjs.size() != 1 && selobjs.size() != 2) {
        ok = false;
    }

    if (ok && selobjs.size() <= 2) {
        if (!selobjs.empty()) {
            const std::vector<std::string>& subnames1 = selobjs[0].getSubNames();
            docobj1 = selobjs[0].getObject();
            obj1 = docobj1->getNameInDocument();
            obj2 = obj1;  // changed later if 2 objects were selected
            const Part::TopoShape& shape1 = Part::Feature::getTopoShape(
                docobj1,
                Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
            );
            if (shape1.isNull()) {
                ok = false;
            }
            if (ok && subnames1.size() <= 2) {
                if (!subnames1.empty()) {
                    curve2 = Part::Feature::getTopoShape(
                                 docobj1,
                                 Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                                     | Part::ShapeOption::Transform,
                                 subnames1[0].c_str()
                    )
                                 .getShape();

                    link1 = subnames1[0];
                }
                if (subnames1.size() == 2) {
                    curve2 = Part::Feature::getTopoShape(
                                 docobj1,
                                 Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                                     | Part::ShapeOption::Transform,
                                 subnames1[1].c_str()
                    )
                                 .getShape();
                    link2 = subnames1[1];
                }
                if (subnames1.empty()) {
                    curve1 = shape1.getShape();
                }
            }
            else {
                ok = false;
            }
        }
        if (selobjs.size() == 2) {
            const std::vector<std::string>& subnames2 = selobjs[1].getSubNames();
            docobj2 = selobjs[1].getObject();
            obj2 = docobj2->getNameInDocument();

            const Part::TopoShape& shape2 = Part::Feature::getTopoShape(
                docobj2,
                Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
            );
            if (shape2.isNull()) {
                ok = false;
            }
            if (ok && subnames2.size() == 1) {
                curve2 = Part::Feature::getTopoShape(
                             docobj2,
                             Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
                                 | Part::ShapeOption::Transform,
                             subnames2[0].c_str()
                )
                             .getShape();

                link2 = subnames2[0];
            }
            else {
                if (subnames2.empty()) {
                    curve2 = shape2.getShape();
                }
            }
        }
        if (!curve1.IsNull() && !curve2.IsNull()) {
            if ((curve1.ShapeType() == TopAbs_EDGE || curve1.ShapeType() == TopAbs_WIRE)
                && (curve2.ShapeType() == TopAbs_EDGE || curve2.ShapeType() == TopAbs_WIRE)) {
                ok = true;
            }
        }
    }

    if (!ok) {
        QMessageBox::warning(
            Gui::getMainWindow(),
            QObject::tr("Wrong Selection"),
            QObject::tr("Select either 2 edges or 2 wires.")
        );
        return;
    }

    openCommand(QT_TRANSLATE_NOOP("Command", "Create ruled surface"));
    doCommand(Doc, "FreeCAD.ActiveDocument.addObject('Part::RuledSurface', 'Ruled Surface')");
    doCommand(
        Doc,
        "FreeCAD.ActiveDocument.ActiveObject.Curve1=(FreeCAD.ActiveDocument.%s,['%s'])",
        obj1.c_str(),
        link1.c_str()
    );
    doCommand(
        Doc,
        "FreeCAD.ActiveDocument.ActiveObject.Curve2=(FreeCAD.ActiveDocument.%s,['%s'])",
        obj2.c_str(),
        link2.c_str()
    );
    commitCommand();
    updateActive();
}

bool CmdPartRuledSurface::isActive()
{
    return PartGui::documentHasShapes();
}

//===========================================================================
// Part_ProjectionOnSurface
//===========================================================================
DEF_STD_CMD_A(CmdPartProjectionOnSurface)

CmdPartProjectionOnSurface::CmdPartProjectionOnSurface()
    : Command("Part_ProjectionOnSurface")
{
    sAppModule = "Part";
    sGroup = QT_TR_NOOP("Part");
    sMenuText = QT_TR_NOOP("Project on Surface");
    sToolTipText = QT_TR_NOOP(
        "Projects edges, wires, or faces of one shape\n"
        "onto a face of another shape.\n"
        "The camera view determines the direction\n"
        "of the projection."
    );
    sWhatsThis = "Part_ProjectionOnSurface";
    sStatusTip = sToolTipText;
    sPixmap = "Part_ProjectionOnSurface";
}

void CmdPartProjectionOnSurface::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    auto dlg = new PartGui::TaskProjectOnSurface(getDocument(nullptr));
    Gui::Control().showDialog(dlg);
}

bool CmdPartProjectionOnSurface::isActive()
{
    return (PartGui::documentHasShapes() && !Gui::Control().activeDialog());
}

void CreatePartShapeToolCommands(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdPartMakeSolid());
    rcCmdMgr.addCommand(new CmdPartReverseShape());
    rcCmdMgr.addCommand(new CmdPartExtrude());
    rcCmdMgr.addCommand(new CmdPartScale());
    rcCmdMgr.addCommand(new CmdPartMakeFace());
    rcCmdMgr.addCommand(new CmdPartMirror());
    rcCmdMgr.addCommand(new CmdPartRevolve());
    rcCmdMgr.addCommand(new CmdPartCrossSections());
    rcCmdMgr.addCommand(new CmdPartFillet());
    rcCmdMgr.addCommand(new CmdPartChamfer());
    rcCmdMgr.addCommand(new CmdPartCompound());
    rcCmdMgr.addCommand(new CmdPartRuledSurface());
    rcCmdMgr.addCommand(new CmdPartBuilder());
    rcCmdMgr.addCommand(new CmdPartLoft());
    rcCmdMgr.addCommand(new CmdPartSweep());
    rcCmdMgr.addCommand(new CmdPartOffset());
    rcCmdMgr.addCommand(new CmdPartOffset2D());
    rcCmdMgr.addCommand(new CmdPartCompOffset());
    rcCmdMgr.addCommand(new CmdPartThickness());
    rcCmdMgr.addCommand(new CmdPartProjectionOnSurface());
}
