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

#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <App/Document.h>
#include <Mod/Part/App/PartFeature.h>
#include <App/Link.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Selection/Selection.h>

#include "CommandSupport.h"


namespace PartGui
{


// True when the active document holds at least one shape -- there is real geometry to
// operate on. The document is the container (ARCHITECTURE §7.1); nothing needs to be active.
bool documentHasShapes()
{
    auto* doc = App::GetApplication().getActiveDocument();
    return doc && !Part::getShapeObjects(doc).empty();
}

bool checkForSolids(const TopoDS_Shape& shape)
{
    TopExp_Explorer xp;
    xp.Init(shape, TopAbs_FACE, TopAbs_SHELL);
    if (xp.More()) {
        return false;
    }
    xp.Init(shape, TopAbs_WIRE, TopAbs_FACE);
    if (xp.More()) {
        return false;
    }
    xp.Init(shape, TopAbs_EDGE, TopAbs_WIRE);
    if (xp.More()) {
        return false;
    }
    xp.Init(shape, TopAbs_VERTEX, TopAbs_EDGE);
    if (xp.More()) {
        return false;
    }

    return true;
}
/*
 * returns vector of Part::TopoShapes from selected Part::Feature derived objects,
 * or App::Links linked to Part::Features
 */
std::vector<Part::TopoShape> getShapesFromSelection()
{
    std::vector<App::DocumentObject*> objs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    std::vector<Part::TopoShape> shapes;
    for (auto it : objs) {
        Part::TopoShape shp = Part::Feature::getTopoShape(
            it,
            Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
        );
        if (!shp.isNull()) {
            shapes.push_back(shp);
        }
    }
    return shapes;
}
/*
 * returns true if selected objects contain valid Part::TopoShapes.
 * Objects can be Part::Features or App::Links
 */
bool hasShapesInSelection()
{
    bool hasShapes = false;
    std::vector<App::DocumentObject*> docobjs = Gui::Selection().getObjectsOfType(
        App::DocumentObject::getClassTypeId()
    );
    for (auto it : docobjs) {
        if (!Part::Feature::getTopoShape(it, Part::ShapeOption::ResolveLink).isNull()) {
            hasShapes = true;
            break;
        }
    }
    return hasShapes;
}

}  // namespace PartGui
