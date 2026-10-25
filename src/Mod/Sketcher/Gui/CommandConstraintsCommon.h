// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <functional>
#include <vector>

#include <Gui/Command.h>
#include <Gui/Selection/SelectionFilter.h>

#include "CommandConstraints.h"
#include "DrawSketchHandler.h"
#include "Utils.h"

// clang-format off
namespace SketcherGui
{
/**
 * @brief The SelType enum
 * Types of sketch elements that can be (pre)selected. The root/origin and the
 * axes are put up separately so that they can be specifically disallowed, for
 * example, when in lock, horizontal, or vertical constraint modes.
 */
enum SelType
{
    SelUnknown = 0,
    SelVertex = 1,
    SelRoot = 2,
    SelVertexOrRoot = SelVertex | SelRoot,
    SelEdge = 4,
    SelHAxis = 8,
    SelVAxis = 16,
    SelEdgeOrAxis = SelEdge | SelHAxis | SelVAxis,
    SelExternalEdge = 32
};
/**
 * @brief The GenericConstraintSelection class
 * SelectionFilterGate with changeable filters. In a constraint creation mode
 * like point on object, if the first selection object can be a point, the next
 * has to be a curve for the constraint to make sense. Thus filters are
 * changeable so that same filter can be kept on while in one mode.
 */
class GenericConstraintSelection: public Gui::SelectionFilterGate
{
    App::DocumentObject* object;

public:
    explicit GenericConstraintSelection(App::DocumentObject* obj)
        : Gui::SelectionFilterGate(nullPointer())
        , object(obj)
        , allowedSelTypes(0)
    {}

    bool allow(App::Document*, App::DocumentObject* pObj, const char* sSubName) override
    {
        if (pObj != this->object) {
            return false;
        }
        if (Base::Tools::isNullOrEmpty(sSubName)) {
            return false;
        }
        std::string element(sSubName);
        if ((allowedSelTypes & SelRoot && element.substr(0, 9) == "RootPoint")
            || (allowedSelTypes & SelVertex && element.substr(0, 6) == "Vertex")
            || (allowedSelTypes & SelEdge && element.substr(0, 4) == "Edge")
            || (allowedSelTypes & SelHAxis && element.substr(0, 6) == "H_Axis")
            || (allowedSelTypes & SelVAxis && element.substr(0, 6) == "V_Axis")
            || (allowedSelTypes & SelExternalEdge && element.substr(0, 12) == "ExternalEdge")) {
            return true;
        }

        return false;
    }

    void setAllowedSelTypes(unsigned int types)
    {
        if (types < 256) {
            allowedSelTypes = types;
        }
    }

protected:
    int allowedSelTypes;
};
}  // namespace SketcherGui

void finishDatumConstraint(Gui::Command* cmd,
                           Sketcher::SketchObject* sketch,
                           bool isDriving = true,
                           unsigned int numberofconstraints = 1);
bool removeRedundantPointOnObject(Sketcher::SketchObject* Obj, int GeoId1, int GeoId2, int GeoId3);
void showNoConstraintBetweenExternal(const App::DocumentObject* obj);
void showNoConstraintBetweenFixedGeometry(const App::DocumentObject* obj);
bool isGeoConcentricCompatible(const Part::Geometry* geo);
bool addConstraintSafely(Sketcher::SketchObject* obj, std::function<void()> constraintadditionfunction);

namespace SketcherGui
{

/**
 * @brief The CmdSketcherConstraint class
 * Superclass for all sketcher constraints to ease generation of constraint
 * creation modes.
 */
class CmdSketcherConstraint: public Gui::Command
{
    friend class DrawSketchHandlerGenConstraint;

public:
    explicit CmdSketcherConstraint(const char* name)
        : Command(name)
    {}

    ~CmdSketcherConstraint() override
    {}

    const char* className() const override
    {
        return "CmdSketcherConstraint";
    }

protected:
    /**
     * @brief allowedSelSequences
     * Each element is a vector representing sequence of selections allowable.
     * DrawSketchHandlerGenConstraint will use these to filter elements and
     * generate sequences to be passed to applyConstraint().
     * Whenever any sequence is completed, applyConstraint() is called, so it's
     * best to keep them prefix-free.
     *
     * TODO: Introduce structs to allow keeping first selection
     */
    std::vector<std::vector<SketcherGui::SelType>> allowedSelSequences;

    virtual void applyConstraint(std::vector<SelIdPair>&, int)
    {}
    void activated(int /*iMsg*/) override;
    bool isActive() override
    {
        return isCommandActive(getActiveGuiDocument());
    }
};

class DrawSketchHandlerGenConstraint: public DrawSketchHandler
{
public:
    // Helper constants for all hint texts
    static constexpr const char* PICK_EDGE_OR_FIRST_POINT = "%1 pick edge or first point";
    static constexpr const char* PICK_EDGE_OR_SECOND_POINT = "%1 pick edge or second point";
    static constexpr const char* PICK_FIRST_EDGE = "%1 pick first edge";
    static constexpr const char* PICK_SECOND_EDGE = "%1 pick second edge";
    static constexpr const char* PICK_SECOND_LINE_OR_POINT = "%1 pick second line or point";
    static constexpr const char* PICK_SECOND_EDGE_OR_POINT = "%1 pick second edge or point";
    static constexpr const char* PICK_SYMMETRY_POINT = "%1 pick symmetry point";
    static constexpr const char* PICK_SYMMETRY_LINE_OR_POINT = "%1 pick symmetry line or point";
    static constexpr const char* PICK_SYMMETRY_LINE = "%1 pick symmetry line";
    static constexpr const char* PICK_SECOND_LINE = "%1 pick second line";
    static constexpr const char* PICK_SECOND_POINT_OR_EDGE = "%1 pick second point or edge";
    static constexpr const char* PICK_POINT_OR_EDGE = "%1 pick point or edge";
    static constexpr const char* PICK_SECOND_POINT = "%1 pick second point";
    static constexpr const char* PICK_EDGE = "%1 pick edge";
    static constexpr const char* PICK_CIRCLE_OR_ARC = "%1 pick circle or arc";
    static constexpr const char* PICK_EDGE_TO_BLOCK = "%1 pick edge to block";
    static constexpr const char* PICK_POINT_TO_LOCK = "%1 pick point to lock";
    static constexpr const char* PICK_POINT_OR_CURVE = "%1 pick point or curve";
    static constexpr const char* PICK_SECOND_POINT_OR_CURVE = "%1 pick second point or curve";
    static constexpr const char* PICK_OPTIONAL_TANGENT_POINT = "%1 pick optional tangent point";
    static constexpr const char* PICK_OPTIONAL_PERPENDICULAR_POINT = "%1 pick optional perpendicular point";
    static constexpr const char* PICK_LINE = "%1 pick line";
    static constexpr const char* PICK_POINT = "%1 pick point";
    static constexpr const char* PLACE_DIMENSION = "%1 place dimension";
    static constexpr const char* MODE_HINT = "%1 switch mode";

    explicit DrawSketchHandlerGenConstraint(CmdSketcherConstraint* _cmd)
        : cmd(_cmd)
        , seqIndex(0)
    {}
    ~DrawSketchHandlerGenConstraint() override
    {
        Gui::Selection().rmvSelectionGate();
    }

    void mouseMove(SnapManager::SnapHandle /*snapHandle*/) override
    {
    }

    bool pressButton(Base::Vector2d /*onSketchPos*/) override
    {
        return true;
    }

    bool releaseButton(Base::Vector2d onSketchPos) override
    {
        SelIdPair selIdPair;
        selIdPair.GeoId = Sketcher::GeoEnum::GeoUndef;
        selIdPair.PosId = Sketcher::PointPos::none;
        std::stringstream ss;
        SelType newSelType = SelUnknown;

        // For each SelType allowed, check if button is released there and assign it to selIdPair
        int VtId = getPreselectPoint();
        int CrvId = getPreselectCurve();
        int CrsId = getPreselectCross();
        if (allowedSelTypes & SelRoot && CrsId == 0) {
            selIdPair.GeoId = Sketcher::GeoEnum::RtPnt;
            selIdPair.PosId = Sketcher::PointPos::start;
            newSelType = SelRoot;
            ss << "RootPoint";
        }
        else if (allowedSelTypes & SelVertex && VtId >= 0) {
            sketchgui->getSketchObject()->getGeoVertexIndex(VtId, selIdPair.GeoId, selIdPair.PosId);
            newSelType = SelVertex;
            ss << "Vertex" << VtId + 1;
        }
        else if (allowedSelTypes & SelEdge && CrvId >= 0) {
            selIdPair.GeoId = CrvId;
            newSelType = SelEdge;
            ss << "Edge" << CrvId + 1;
        }
        else if (allowedSelTypes & SelHAxis && CrsId == 1) {
            selIdPair.GeoId = Sketcher::GeoEnum::HAxis;
            newSelType = SelHAxis;
            ss << "H_Axis";
        }
        else if (allowedSelTypes & SelVAxis && CrsId == 2) {
            selIdPair.GeoId = Sketcher::GeoEnum::VAxis;
            newSelType = SelVAxis;
            ss << "V_Axis";
        }
        else if (allowedSelTypes & SelExternalEdge && CrvId <= Sketcher::GeoEnum::RefExt) {
            // TODO: Figure out how this works
            selIdPair.GeoId = CrvId;
            newSelType = SelExternalEdge;
            ss << "ExternalEdge" << Sketcher::GeoEnum::RefExt + 1 - CrvId;
        }

        if (selIdPair.GeoId == Sketcher::GeoEnum::GeoUndef) {
            // If mouse is released on "blank" space, start over
            selSeq.clear();
            resetOngoingSequences();
            Gui::Selection().clearSelection();
        }
        else {
            // If mouse is released on something allowed, select it and move forward
            selSeq.push_back(selIdPair);
            Gui::Selection().addSelection(sketchgui->getSketchObject()->getDocument()->getName(),
                                          sketchgui->getSketchObject()->getNameInDocument(),
                                          ss.str().c_str(),
                                          onSketchPos.x,
                                          onSketchPos.y,
                                          0.f);
            _tempOnSequences.clear();
            allowedSelTypes = 0;
            for (std::set<int>::iterator token = ongoingSequences.begin();
                 token != ongoingSequences.end();
                 ++token) {
                if ((cmd->allowedSelSequences).at(*token).at(seqIndex) & newSelType) {
                    if (seqIndex == (cmd->allowedSelSequences).at(*token).size() - 1) {
                        // One of the sequences is completed. Pass to cmd->applyConstraint
                        cmd->applyConstraint(selSeq, *token);// replace arg 2 by ongoingToken

                        selSeq.clear();
                        resetOngoingSequences();

                        // Re-arm hint for next operation
                        updateHint();

                        return true;
                    }
                    _tempOnSequences.insert(*token);
                    allowedSelTypes =
                        allowedSelTypes | (cmd->allowedSelSequences).at(*token).at(seqIndex + 1);
                }
            }

            // Progress to next seqIndex
            std::swap(_tempOnSequences, ongoingSequences);
            seqIndex++;
            selFilterGate->setAllowedSelTypes(allowedSelTypes);
        }
        updateHint();

        return true;
    }

    std::list<Gui::InputHint> getToolHints() const override {
    const std::string commandName = cmd->getName();
    const int selectionStep = seqIndex;

    // Special case for Sketcher_ConstrainPointOnObject to generate dynamic step hint
    if (commandName == "Sketcher_ConstrainPointOnObject") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                return {{QObject::tr(PICK_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                return {{QObject::tr(PICK_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainAngle to generate context-aware hints
    if (commandName == "Sketcher_ConstrainAngle") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_FIRST_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Could be Line + Line or Edge + Point + Edge workflow
                // Tell user what they can actually pick next
                return {{QObject::tr(PICK_SECOND_LINE_OR_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        } else if (selectionStep == 2 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else if (isVertex(selSeq[1].GeoId, selSeq[1].PosId)) {
                // Edge + Point + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainPerpendicular to generate context-aware hints
    if (commandName == "Sketcher_ConstrainPerpendicular") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_FIRST_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Edge + Edge or Edge + Point + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE_OR_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        } else if (selectionStep == 2 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else if (isVertex(selSeq[1].GeoId, selSeq[1].PosId)) {
                // Edge + Point + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainTangent to generate context-aware hints
    if (commandName == "Sketcher_ConstrainTangent") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_FIRST_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Could be Edge + Edge or Edge + Point + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE_OR_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        } else if (selectionStep == 2 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            } else if (isVertex(selSeq[1].GeoId, selSeq[1].PosId)) {
                // Edge + Point + Edge workflow
                return {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainDistanceX to generate context-aware hints
    if (commandName == "Sketcher_ConstrainDistanceX") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Point workflow
                return {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Edge workflow - no second selection needed
                return {{QObject::tr(PLACE_DIMENSION), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainDistanceY to generate context-aware hints
    if (commandName == "Sketcher_ConstrainDistanceY") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Point workflow
                return {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Edge workflow - no second selection needed
                return {{QObject::tr(PLACE_DIMENSION), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // Special case for Sketcher_ConstrainSymmetric to generate context-aware hints
    if (commandName == "Sketcher_ConstrainSymmetric") {
        if (selectionStep == 0) {
            return {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
        } else if (selectionStep == 1 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId)) {
                // Point + Edge + Point or Point + Point + Edge/Point workflow
                return {{QObject::tr(PICK_EDGE_OR_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            } else {
                // Edge + Point workflow
                return {{QObject::tr(PICK_SYMMETRY_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        } else if (selectionStep == 2 && !selSeq.empty()) {
            if (isVertex(selSeq[0].GeoId, selSeq[0].PosId) && isVertex(selSeq[1].GeoId, selSeq[1].PosId)) {
                // Point + Point + Edge workflow
                return {{QObject::tr(PICK_SYMMETRY_LINE_OR_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            } else if (isVertex(selSeq[0].GeoId, selSeq[0].PosId) && !isVertex(selSeq[1].GeoId, selSeq[1].PosId)) {
                // Point + Edge + Point workflow
                return {{QObject::tr(PICK_POINT), {Gui::InputHint::UserInput::MouseLeft}}};
            }
        }
    }

    // For everything else, use the static table
    return lookupConstraintHints(commandName, selectionStep);
}

private:
    struct ConstraintHintEntry {
        std::string commandName;    // FreeCAD command name (e.g., "Sketcher_ConstrainSymmetric")
        int selectionStep;          // 0-indexed step in the selection sequence
        std::list<Gui::InputHint> hints;  // Hint text and input types for this step
    };

    using ConstraintHintTable = std::vector<ConstraintHintEntry>;

    // Constraint hint lookup table
    // Format: {command_name, selection_step, {hint_text, input_types}}
    // Steps are 0-indexed and correspond to DrawSketchHandlerGenConstraint::seqIndex
    // Each step provides contextual guidance for what the user should select next
    static ConstraintHintTable getConstraintHintTable() {
        return {
            // Coincident
            {.commandName = "Sketcher_ConstrainCoincidentUnified",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainCoincidentUnified",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Distance X/Y
            {.commandName = "Sketcher_ConstrainDistanceX",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainDistanceX",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainDistanceY",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainDistanceY",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Horizontal/Vertical
            {.commandName = "Sketcher_ConstrainHorizontal",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainHorizontal",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainVertical",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainVertical",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainHorVer",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainHorVer",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Block/Lock
            {.commandName = "Sketcher_ConstrainBlock",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE_TO_BLOCK), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainLock",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_TO_LOCK), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Coincident (individual)
            {.commandName = "Sketcher_ConstrainCoincident",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_OR_CURVE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainCoincident",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT_OR_CURVE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainEqual",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainEqual",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Radius/Diameter
            {.commandName = "Sketcher_ConstrainRadius",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_CIRCLE_OR_ARC), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainDiameter",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_CIRCLE_OR_ARC), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainRadiam",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_CIRCLE_OR_ARC), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Angle
            {.commandName = "Sketcher_ConstrainAngle",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE_OR_FIRST_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainAngle",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainAngle",
            .selectionStep = 2,
            .hints = {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Symmetry
            {.commandName = "Sketcher_ConstrainSymmetric",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainSymmetric",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainSymmetric",
            .selectionStep = 2,
            .hints = {{QObject::tr(PICK_SYMMETRY_LINE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Tangent
            {.commandName = "Sketcher_ConstrainTangent",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainTangent",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainTangent",
            .selectionStep = 2,
            .hints = {{QObject::tr(PICK_OPTIONAL_TANGENT_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Perpendicular
            {.commandName = "Sketcher_ConstrainPerpendicular",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainPerpendicular",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainPerpendicular",
            .selectionStep = 2,
            .hints = {{QObject::tr(PICK_OPTIONAL_PERPENDICULAR_POINT), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Parallel
            {.commandName = "Sketcher_ConstrainParallel",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_LINE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainParallel",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_LINE), {Gui::InputHint::UserInput::MouseLeft}}}},

            // Distance
            {.commandName = "Sketcher_ConstrainDistance",
            .selectionStep = 0,
            .hints = {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},

            {.commandName = "Sketcher_ConstrainDistance",
            .selectionStep = 1,
            .hints = {{QObject::tr(PICK_SECOND_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}}}},
        };
    }

    static std::list<Gui::InputHint> lookupConstraintHints(const std::string& commandName, int selectionStep) {
        const auto constraintHintTable = getConstraintHintTable();
        auto it = std::ranges::find_if(constraintHintTable,
                                [&commandName, selectionStep](const ConstraintHintEntry& entry) {
                                    return entry.commandName == commandName && entry.selectionStep == selectionStep;
                                });

        return (it != constraintHintTable.end()) ? it->hints : std::list<Gui::InputHint>{};
    }

    void activated() override
    {
        selFilterGate = new GenericConstraintSelection(sketchgui->getObject());

        resetOngoingSequences();

        selSeq.clear();

        Gui::Selection().rmvSelectionGate();
        Gui::Selection().addSelectionGate(selFilterGate);

        // Constrain icon size in px
        qreal pixelRatio = devicePixelRatio();
        const unsigned long defaultCrosshairColor = 0xFFFFFF;
        unsigned long color = getCrosshairColor();
        auto colorMapping = std::map<unsigned long, unsigned long>();
        colorMapping[defaultCrosshairColor] = color;

        constexpr qreal fullIconWidth = 32;
        constexpr qreal iconWidth = 16;
        QPixmap cursorPixmap =
                    Gui::BitmapFactory().pixmapFromSvg("Sketcher_Crosshair",
                                                       QSizeF(fullIconWidth, fullIconWidth),
                                                       colorMapping),
                icon = Gui::BitmapFactory().pixmapFromSvg(cmd->getPixmap(),
                                                          QSizeF(iconWidth, iconWidth));
        QPainter cursorPainter;
        cursorPainter.begin(&cursorPixmap);
        cursorPainter.drawPixmap(16, 16, icon);
        cursorPainter.end();
        int hotX = 8;
        int hotY = 8;
        // only X11 needs hot point coordinates to be scaled
        if (qGuiApp->platformName() == QLatin1String("xcb")) {
            hotX *= pixelRatio;
            hotY *= pixelRatio;
        }
        setCursor(cursorPixmap, hotX, hotY, false);
    }

protected:
    CmdSketcherConstraint* cmd;

    GenericConstraintSelection* selFilterGate = nullptr;

    std::vector<SelIdPair> selSeq;
    unsigned int allowedSelTypes = 0;

    /// indices of currently ongoing sequences in cmd->allowedSequences
    std::set<int> ongoingSequences, _tempOnSequences;
    /// Index within the selection sequences active
    unsigned int seqIndex;

    void resetOngoingSequences()
    {
        ongoingSequences.clear();
        for (unsigned int i = 0; i < cmd->allowedSelSequences.size(); i++) {
            ongoingSequences.insert(i);
        }
        seqIndex = 0;

        // Estimate allowed selections from the first types in allowedSelTypes
        allowedSelTypes = 0;
        for (std::vector<std::vector<SelType>>::const_iterator it =
                 cmd->allowedSelSequences.begin();
             it != cmd->allowedSelSequences.end();
             ++it) {
            allowedSelTypes = allowedSelTypes | (*it).at(seqIndex);
        }
        selFilterGate->setAllowedSelTypes(allowedSelTypes);

        Gui::Selection().clearSelection();
    }
};

}  // namespace SketcherGui
