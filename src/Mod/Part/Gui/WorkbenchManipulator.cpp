// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include <cstring>

#include "WorkbenchManipulator.h"
#include <Gui/MenuManager.h>
#include <Gui/ToolBarManager.h>

using namespace PartGui;

void WorkbenchManipulator::modifyMenuBar(Gui::MenuItem* menuBar)
{
    addAppearanceAfter(menuBar, "Std_ToggleNavigation");
}

void WorkbenchManipulator::modifyContextMenu(const char* recipient, Gui::MenuItem* menuBar)
{
    if (strcmp(recipient, "View") == 0) {
        addAppearanceAfter(menuBar, "Std_TreeSelection");
    }
    else if (strcmp(recipient, "Tree") == 0) {
        if (!addAppearanceAfter(menuBar, "Std_RandomColor")) {
            addAppearanceAfter(menuBar, "Std_ToggleVisibility");
        }
    }
}

void WorkbenchManipulator::modifyToolBars(Gui::ToolBarItem* toolBar)
{
    addSelectionFilter(toolBar);
    addDatums(toolBar);
}

void WorkbenchManipulator::modifyDockWindows([[maybe_unused]] Gui::DockWindowItems* dockWindow)
{}

bool WorkbenchManipulator::addAppearanceAfter(Gui::MenuItem* menuBar, const char* reference)
{
    Gui::MenuItem* parent = menuBar->findParentOf(reference);
    if (!parent) {
        return false;
    }
    auto* item = new Gui::MenuItem();  // NOLINT
    item->setCommand("Part_EditAppearance");
    if (Gui::MenuItem* next = parent->afterItem(parent->findItem(reference))) {
        parent->insertItem(next, item);
    }
    else {
        parent->appendItem(item);
    }
    return true;
}

void WorkbenchManipulator::addSelectionFilter(Gui::ToolBarItem* toolBar)
{
    if (auto view = toolBar->findItem("View")) {
        auto add = new Gui::ToolBarItem();  // NOLINT
        add->setCommand("Part_SelectFilter");
        auto item = view->findItem("Std_TreeViewActions");
        if (item) {
            view->insertItem(item, add);
        }
        else {
            view->appendItem(add);
        }
    }
}

void WorkbenchManipulator::addDatums(Gui::ToolBarItem* toolBar)
{
    if (auto view = toolBar->findItem("Structure")) {
        auto add = new Gui::ToolBarItem();  // NOLINT
        add->setCommand("Part_Datums");
        auto item = view->findItem("Std_Group");
        if (item) {
            view->insertItem(item, add);
        }
        else {
            view->appendItem(add);
        }
    }
}
