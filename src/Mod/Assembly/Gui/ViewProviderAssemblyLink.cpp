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

#include <boost/dynamic_bitset.hpp>


#include <App/Link.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

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
#include <Mod/Assembly/App/AssemblyUtils.h>

#include "ViewProviderAssembly.h"
#include "ViewProviderAssemblyLink.h"


using namespace Assembly;
using namespace AssemblyGui;


PROPERTY_SOURCE_WITH_EXTENSIONS(AssemblyGui::ViewProviderAssemblyLink, Gui::ViewProviderGeometryObject)

ViewProviderAssemblyLink::ViewProviderAssemblyLink()
{
    Gui::ViewProviderGeoFeatureGroupExtension::initExtension(this);
    linkView = new Gui::LinkView;
}

ViewProviderAssemblyLink::~ViewProviderAssemblyLink()
{
    linkView->setInvalid();
}

void ViewProviderAssemblyLink::attach(App::DocumentObject* obj)
{
    ViewProviderGeometryObject::attach(obj);
    linkView->setOwner(this);
    // Render the linked assembly's geometry beneath this instance's root. pcRoot already
    // carries this AssemblyLink's Placement (via pcTransform), so the linked snapshot is
    // positioned by the instance placement — matching how getSubObject composes it.
    pcRoot->addChild(linkView->getLinkRoot());
    updateLinkView();
}

void ViewProviderAssemblyLink::updateData(const App::Property* prop)
{
    ViewProviderGeometryObject::updateData(prop);

    auto* link = freecad_cast<AssemblyLink*>(getObject());
    if (link && (prop == &link->LinkedObject || prop == &link->Rigid)) {
        updateLinkView();
    }
}

void ViewProviderAssemblyLink::finishRestoring()
{
    ViewProviderGeometryObject::finishRestoring();
    // On reload the linked view provider may not have existed when attach() ran; rebuild now.
    updateLinkView();
}

void ViewProviderAssemblyLink::updateLinkView()
{
    auto* link = freecad_cast<AssemblyLink*>(getObject());

    // A rigid sub-assembly owns no proxy geometry, so we render the linked assembly through the
    // reference. A flexible one keeps its owned proxy children (#63); this view provider must
    // contribute nothing. Clear the child snapshot array as well as the direct link: a prior
    // rigid state (or the transient rigid pass during import) populates the array via
    // setChildren, and setLink(nullptr) alone leaves it in place -- double-drawing every leaf.
    if (!link || !link->isRigid()) {
        linkView->setChildren({}, {});
        linkView->setLink(nullptr);
        return;
    }

    auto* assembly = link->getLinkedAssembly();
    if (!assembly) {
        linkView->setChildren({}, {});
        return;
    }

    // The linked AssemblyObject is a plain group whose view-provider node does not aggregate
    // its members' geometry (they render as separate top-level objects in the linked document),
    // so a snapshot of that node is empty. Instead, render the assembly's components directly:
    // getAssemblyComponents is the App-layer notion of the renderable parts (cross-document
    // links, nested sub-assembly links, expanded arrays), excluding joints and datums. Each
    // component's snapshot carries its own placement within the sub-assembly; our pcTransform
    // then positions the whole by the instance placement -- matching getSubObject composition.
    // A nested sub-assembly component is itself an AssemblyLink whose view-provider renders
    // through its own reference, so the recursion is handled per component.
    std::vector<App::DocumentObject*> components = Assembly::getAssemblyComponents(assembly);

    boost::dynamic_bitset<> vis(components.size());
    for (size_t i = 0; i < components.size(); ++i) {
        vis[i] = components[i] && components[i]->Visibility.getValue();
    }

    // SnapshotVisible keeps each component's internal placement and defers position to our
    // pcTransform; SnapshotTransform would discard them.
    linkView->setNodeType(Gui::LinkView::SnapshotVisible);
    linkView->setChildren(components, vis, Gui::LinkView::SnapshotVisible);
}

bool ViewProviderAssemblyLink::getElementPicked(const SoPickedPoint* pp, std::string& subname) const
{
    // A pick on the referenced geometry resolves to its sub-shape (e.g. "Body.Face3"). The
    // rigid sub-assembly renders its components through the reference (setChildren), so query
    // the link view; a flexible sub-assembly renders nothing here (getSize() == 0), so defer
    // to the base group behaviour.
    if (linkView->getSize() > 0 && linkView->linkGetElementPicked(pp, subname)) {
        return true;
    }
    return ViewProviderGeometryObject::getElementPicked(pp, subname);
}

bool ViewProviderAssemblyLink::getDetailPath(
    const char* subname,
    SoFullPath* pPath,
    bool append,
    SoDetail*& det
) const
{
    // Reverse of getElementPicked: turn a referenced sub-name back into the Coin path under our
    // linked root, so selection highlighting reaches through the reference. Rigid renders its
    // components through the reference (getSize() > 0); flexible defers to the base group.
    if (linkView->getSize() > 0) {
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
    return ViewProviderGeometryObject::getDetailPath(subname, pPath, append, det);
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

    return ViewProviderGeometryObject::setEdit(mode);
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

    return ViewProviderGeometryObject::onDelete(subNames);
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
