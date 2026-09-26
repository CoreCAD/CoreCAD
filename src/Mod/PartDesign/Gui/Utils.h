// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (C) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
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

#pragma once

/** \file PartDesign/Gui/Utils.h
 *  This file contains some utility function used over PartDesignGui module
 */
namespace PartDesign
{
class Body;
class Feature;
}  // namespace PartDesign

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Sketcher
{
class SketchObject;
}

namespace Gui
{
class Command;
}

namespace PartDesignGui
{

/// Activate edit mode of the given object
bool setEdit(App::DocumentObject* obj, PartDesign::Body* body = nullptr);

/// Return active body or show a warning message
PartDesign::Body* getBody(
    bool messageIfNot,
    bool autoActivate = true,
    bool assertModern = true,
    App::DocumentObject** topParent = nullptr,
    std::string* subname = nullptr
);

/// Display a dialog to select or create a Body object when none is active
PartDesign::Body* needActiveBodyMessage(App::Document* doc, const QString& infoText = QString());

/**
 * Set given body active, and return pointer to it.
 * \param body the pointer to the body
 * \param doc the pointer to the document in question
 * \param topParent and
 * \param subname to be passed under certain circumstances
 *        (currently only subshapebinder)
 */
PartDesign::Body* makeBodyActive(
    App::DocumentObject* body,
    App::Document* doc,
    App::DocumentObject** topParent = nullptr,
    std::string* subname = nullptr
);

/// Display error when there are existing Body objects, but none are active
void needActiveBodyError();

/// Finds the body the given feature belongs to, and shows a message if there is none.
PartDesign::Body* getBodyFor(const App::DocumentObject*, bool messageIfNot);

/**
 * The distinct bodies the current selection in \a doc points at: a Body picked directly, or
 * any feature or sub-shape resolved to its Body. Shows no message and asks nothing.
 */
std::vector<PartDesign::Body*> selectedBodies(const App::Document* doc);

/// The one body the selection in \a doc points at, or nullptr when it points at none or several.
PartDesign::Body* soleSelectedBody(const App::Document* doc);

/**
 * Cruth §8.5/§4.6: resolve the target Body a combinator (subtractive primitive, Boolean)
 * operates on, from the selection rather than an active-body session state. One body
 * selected → that body; several selected → warn and abort; nothing selected but a sole body
 * exists → that body; several bodies, none selected → the de-owned pickBody chooser; no
 * bodies → warn and abort. Shows the appropriate message and returns nullptr on any
 * no-target/cancel/ambiguous outcome, so a combinator is never told a target it did not pick.
 */
PartDesign::Body* resolveTargetBody(Gui::Command* cmd);

/**
 * Cruth §8.5/§4.6: resolve the target Body of a Boolean, whose selection names its TOOLS.
 * The target is the one body the selection leaves over: one unselected body → that body;
 * several → the pickBody chooser; every body selected → the chooser over the selected ones
 * (the rest stay tools). Never read from an active body. Returns nullptr on no-body/cancel.
 */
PartDesign::Body* resolveBooleanTarget(Gui::Command* cmd);

/// Fix sketch support after moving a free sketch into a body
void fixSketchSupport(Sketcher::SketchObject* sketch);

/// Check if feature is dependent on anything except movable sketches and datums
bool isFeatureMovable(App::DocumentObject* feature);
/// Collect dependencies of the features during the move. Dependencies should only be dependent on origin
std::vector<App::DocumentObject*> collectMovableDependencies(
    std::vector<App::DocumentObject*>& features
);

/**
 * Cruth §11 step 5e: author a new feature under de-ownership. Creates the object at the
 * document level (creation is the Document's responsibility, no longer the Body's) then
 * splices it into @p body's pipeline via Body::addFeature (the Tip/BaseFeature-chain edit).
 * Both steps are emitted to the command console for macro fidelity (P8). This is the single
 * replacement for the retired `body.newObject(type, name)` group-extension idiom.
 *
 * @return the created object, or nullptr on failure.
 */
App::DocumentObject* createFeature(PartDesign::Body* body, const char* type, const std::string& name);

}  // namespace PartDesignGui
