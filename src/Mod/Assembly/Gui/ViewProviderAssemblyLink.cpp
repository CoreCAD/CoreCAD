// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 Ondsel <development@ondsel.com>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <QAction>
#include <QMenu>
#include <vector>
#include <sstream>
#include <iostream>


#include <App/Link.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Part.h>

#include <Gui/Action.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/MainWindow.h>
#include <Gui/ViewProviderLink.h>

#include <Inventor/SoFullPath.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/details/SoDetail.h>
#include <Inventor/nodes/SoSeparator.h>

#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/AssemblyLink.h>

#include "ViewProviderAssembly.h"
#include "ViewProviderAssemblyLink.h"


using namespace Assembly;
using namespace AssemblyGui;


PROPERTY_SOURCE(AssemblyGui::ViewProviderAssemblyLink, Gui::ViewProviderPart)

ViewProviderAssemblyLink::ViewProviderAssemblyLink()
{
    linkView = new Gui::LinkView;
}

ViewProviderAssemblyLink::~ViewProviderAssemblyLink()
{
    linkView->setInvalid();
}

void ViewProviderAssemblyLink::attach(App::DocumentObject* obj)
{
    ViewProviderPart::attach(obj);
    linkView->setOwner(this);
    // Render the linked assembly's geometry beneath this instance's root. pcRoot already
    // carries this AssemblyLink's Placement (via pcTransform), so the linked snapshot is
    // positioned by the instance placement — matching how getSubObject composes it.
    pcRoot->addChild(linkView->getLinkRoot());
    updateLinkView();
}

void ViewProviderAssemblyLink::updateData(const App::Property* prop)
{
    ViewProviderPart::updateData(prop);

    auto* link = freecad_cast<AssemblyLink*>(getObject());
    if (link && (prop == &link->LinkedObject || prop == &link->Rigid)) {
        updateLinkView();
    }
}

void ViewProviderAssemblyLink::finishRestoring()
{
    ViewProviderPart::finishRestoring();
    // On reload the linked view provider may not have existed when attach() ran; rebuild now.
    updateLinkView();
}

void ViewProviderAssemblyLink::updateLinkView()
{
    auto* link = freecad_cast<AssemblyLink*>(getObject());

    // A rigid sub-assembly owns no proxy geometry, so we render the linked assembly through the
    // reference. A flexible one keeps its owned proxy children (#63); leave it to the base class.
    if (!link || !link->isRigid()) {
        linkView->setLink(nullptr);
        return;
    }

    auto* assembly = link->getLinkedAssembly();
    if (!assembly) {
        linkView->setLink(nullptr);
        return;
    }

    // SnapshotVisible keeps the linked assembly's internal placements and defers position to our
    // pcTransform; SnapshotTransform would discard them.
    linkView->setNodeType(Gui::LinkView::SnapshotVisible);
    linkView->setLink(assembly);
}

bool ViewProviderAssemblyLink::getElementPicked(const SoPickedPoint* pp, std::string& subname) const
{
    // A pick on the referenced geometry resolves to its sub-shape (e.g. "Body.Face3"); nothing
    // is linked for a flexible sub-assembly, so defer to the base group behaviour.
    if (linkView->isLinked() && linkView->linkGetElementPicked(pp, subname)) {
        return true;
    }
    return ViewProviderPart::getElementPicked(pp, subname);
}

bool ViewProviderAssemblyLink::getDetailPath(
    const char* subname,
    SoFullPath* pPath,
    bool append,
    SoDetail*& det
) const
{
    // Reverse of getElementPicked: turn a referenced sub-name back into the Coin path under our
    // linked root, so selection highlighting reaches through the reference.
    if (linkView->isLinked()) {
        int len = pPath->getLength();
        if (append) {
            // The linked root hangs directly off pcRoot (not pcModeSwitch); LinkView appends the
            // rest of the path from there.
            pPath->append(pcRoot);
        }
        if (linkView->linkGetDetailPath(subname, pPath, det)) {
            return true;
        }
        pPath->truncate(len);
    }
    return ViewProviderPart::getDetailPath(subname, pPath, append, det);
}

QIcon ViewProviderAssemblyLink::getIcon() const
{
    auto* assembly = dynamic_cast<Assembly::AssemblyLink*>(getObject());
    if (assembly->isRigid()) {
        return Gui::BitmapFactory().pixmap("Assembly_AssemblyLinkRigid.svg");
    }
    else {
        return Gui::BitmapFactory().pixmap("Assembly_AssemblyLink.svg");
    }
}

bool ViewProviderAssemblyLink::setEdit(int mode)
{
    auto* assemblyLink = dynamic_cast<Assembly::AssemblyLink*>(getObject());

    if (!assemblyLink->isRigid() && mode == (int)ViewProvider::Transform) {
        Base::Console().userTranslatedNotification("Flexible sub-assemblies cannot be transformed.");
        return true;
    }

    return ViewProviderPart::setEdit(mode);
}

bool ViewProviderAssemblyLink::doubleClicked()
{
    auto* link = freecad_cast<AssemblyLink*>(getObject());
    if (!link) {
        return true;
    }
    auto* assembly = link->getLinkedAssembly();
    if (!assembly) {
        return true;
    }

    auto* vpa = freecad_cast<ViewProviderAssembly*>(
        Gui::Application::Instance->getViewProvider(assembly)
    );
    if (!vpa) {
        return true;
    }

    auto doc = assembly->getDocument();
    auto guiDoc = vpa->getDocument();
    if (!doc || !guiDoc) {
        return true;
    }

    Gui::MDIView* mdi = guiDoc->getActiveView();

    // Ensure the linked assembly document is fully loaded and has a view
    if (doc->testStatus(App::Document::PartialDoc) || !mdi) {
        Gui::Application::Instance->reopen(doc);

        // reopening invalidates the pointer.
        auto* assembly = link->getLinkedAssembly();
        if (!assembly) {
            return true;
        }

        vpa = freecad_cast<ViewProviderAssembly*>(
            Gui::Application::Instance->getViewProvider(assembly)
        );
        if (!vpa) {
            return true;
        }
    }

    return vpa->doubleClicked();
}

bool ViewProviderAssemblyLink::onDelete(const std::vector<std::string>& subNames)
{
    Q_UNUSED(subNames)

    Gui::Command::doCommand(
        Gui::Command::Doc,
        "App.getDocument(\"%s\").getObject(\"%s\").removeObjectsFromDocument()",
        getObject()->getDocument()->getName(),
        getObject()->getNameInDocument()
    );

    // getObject()->purgeTouched();

    return ViewProviderPart::onDelete(subNames);
}

void ViewProviderAssemblyLink::setupContextMenu(QMenu* menu, QObject* receiver, const char* member)
{
    auto func = new Gui::ActionFunction(menu);
    QAction* act;
    auto* assemblyLink = dynamic_cast<Assembly::AssemblyLink*>(getObject());
    if (assemblyLink->isRigid()) {
        act = menu->addAction(QObject::tr("Turn flexible"));
        act->setToolTip(
            QObject::tr("Your sub-assembly is currently rigid. This will make it flexible instead.")
        );
    }
    else {
        act = menu->addAction(QObject::tr("Turn rigid"));
        act->setToolTip(
            QObject::tr("Your sub-assembly is currently flexible. This will make it rigid instead.")
        );
    }

    func->trigger(act, [this]() {
        auto* assemblyLink = dynamic_cast<Assembly::AssemblyLink*>(getObject());
        getDocument()->openCommand(QT_TRANSLATE_NOOP("Command", "Toggle Rigid"));
        Gui::cmdAppObjectArgs(
            assemblyLink,
            "Rigid = %s",
            assemblyLink->Rigid.getValue() ? "False" : "True"
        );

        getDocument()->commitCommand();
        Gui::Selection().clearSelection();
    });

    Gui::CommandManager& mgr = Gui::Application::Instance->commandManager();
    Gui::Command* cmd = mgr.getCommandByName("Assembly_LinkSelectLinked");
    if (cmd) {
        QAction* action = cmd->getAction()->action();
        if (action) {
            menu->addAction(action);
        }
    }

    Q_UNUSED(receiver)
    Q_UNUSED(member)
}
