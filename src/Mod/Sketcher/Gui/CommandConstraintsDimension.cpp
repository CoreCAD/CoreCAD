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

#include <limits>
#include <cmath>

#include <Precision.hxx>
#include <Bnd_Box.hxx>
#include <QPainter>
#include <algorithm>
#include <sstream>

#include <BRepBndLib.hxx>

#include <boost/range/adaptor/reversed.hpp>

#include <App/Application.h>
#include <Base/Tools.h>
#include <Base/Tools2D.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/CommandT.h>
#include <Gui/Dialogs/DlgCheckableMessageBox.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/Notifications.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>
#include <Gui/Selection/SelectionObject.h>
#include <Mod/Sketcher/App/GeometryFacade.h>
#include <Mod/Part/App/ShapeExtension.h>
#include <Mod/Sketcher/App/SketchObject.h>
#include <Mod/Sketcher/App/SolverGeometryExtension.h>

#include "CommandConstraints.h"
#include "DrawSketchHandler.h"
#include "EditDatumDialog.h"
#include "Utils.h"
#include "ViewProviderSketch.h"
#include "ui_InsertDatum.h"
#include <Inventor/events/SoKeyboardEvent.h>
#include "SnapManager.h"

// clang-format off
using namespace std;
using namespace SketcherGui;
using namespace Sketcher;

// Dimension tool =======================================================

class GeomSelectionSizes
{
public:
    GeomSelectionSizes(size_t s_pts, size_t s_lns, size_t s_cir, size_t s_ell, size_t s_spl) :
        s_pts(s_pts), s_lns(s_lns), s_cir(s_cir), s_ell(s_ell), s_spl(s_spl) {}
    ~GeomSelectionSizes() {}

    bool hasPoints()        const { return s_pts > 0; }
    bool hasLines()         const { return s_lns > 0; }
    bool hasCirclesOrArcs() const { return s_cir > 0; }
    bool hasEllipseAndCo()  const { return s_ell > 0; }
    bool hasSplineAndCo()   const { return s_spl > 0; }

    bool has1Point()             const { return s_pts == 1 && s_lns == 0 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has2Points()            const { return s_pts == 2 && s_lns == 0 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has1Point1Line()        const { return s_pts == 1 && s_lns == 1 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has3Points()            const { return s_pts == 3 && s_lns == 0 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has4MorePoints()        const { return s_pts >= 4 && s_lns == 0 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has2Points1Line()       const { return s_pts == 2 && s_lns == 1 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has3MorePoints1Line()   const { return s_pts >= 3 && s_lns == 1 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has1Point1Circle()      const { return s_pts == 1 && s_lns == 0 && s_cir == 1 && s_ell == 0 && s_spl == 0; }
    bool has1MorePoint1Ellipse() const { return s_pts >= 1 && s_lns == 0 && s_cir == 0 && s_ell == 1 && s_spl == 0; }

    bool has1Line()              const { return s_pts == 0 && s_lns == 1 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has2Lines()             const { return s_pts == 0 && s_lns == 2 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has3MoreLines()         const { return s_pts == 0 && s_lns >= 3 && s_cir == 0 && s_ell == 0 && s_spl == 0; }
    bool has1Line1Circle()       const { return s_pts == 0 && s_lns == 1 && s_cir == 1 && s_ell == 0 && s_spl == 0; }
    bool has1Line2Circles()      const { return s_pts == 0 && s_lns == 1 && s_cir == 2 && s_ell == 0 && s_spl == 0; }
    bool has1Line1Ellipse()      const { return s_pts == 0 && s_lns == 1 && s_cir == 0 && s_ell == 1 && s_spl == 0; }

    bool has1Circle()            const { return s_pts == 0 && s_lns == 0 && s_cir == 1 && s_ell == 0 && s_spl == 0; }
    bool has2Circles()           const { return s_pts == 0 && s_lns == 0 && s_cir == 2 && s_ell == 0 && s_spl == 0; }
    bool has3MoreCircles()       const { return s_pts == 0 && s_lns == 0 && s_cir >= 3 && s_ell == 0 && s_spl == 0; }
    bool has1Circle1Ellipse()    const { return s_pts == 0 && s_lns == 0 && s_cir == 1 && s_ell == 1 && s_spl == 0; }

    bool has1Ellipse()           const { return s_pts == 0 && s_lns == 0 && s_cir == 0 && s_ell == 1 && s_spl == 0; }
    bool has2MoreEllipses()      const { return s_pts == 0 && s_lns == 0 && s_cir == 0 && s_ell >= 2 && s_spl == 0; }
    bool has1Point1Spline1MoreEdge()   const { return s_pts == 1 && s_spl >= 1 && (s_lns + s_cir + s_ell + s_spl) == 2; }

    size_t s_pts, s_lns, s_cir, s_ell, s_spl;
};

class DrawSketchHandlerDimension : public DrawSketchHandler
{
public:
    // Helper constants for hint texts
    static constexpr const char* PICK_EDGE = "%1 pick edge";
    static constexpr const char* PICK_POINT_OR_EDGE = "%1 pick point or edge";
    static constexpr const char* PICK_SECOND_POINT_OR_EDGE = "%1 pick second point or edge";
    static constexpr const char* PICK_SECOND_POINT_OR_EDGE_OR_CLICK_TO_FINISH = "%1 pick second point or edge, or click to finish";
    static constexpr const char* PLACE_DIMENSION = "%1 place dimension";
    static constexpr const char* MODE_HINT = "%1 switch mode";
    explicit DrawSketchHandlerDimension(std::vector<std::string> SubNames)
        : specialConstraint(SpecialConstraint::None)
        , availableConstraint(AvailableConstraint::FIRST)
        , previousOnSketchPos(Base::Vector2d(0.f, 0.f))
        , selPoints({})
        , selLine({})
        , selCircleArc({})
        , selEllipseAndCo({})
        , selSplineAndCo({})
        , initialSelection(std::move(SubNames))
        , cstrIndexes({})
    {
    }
    ~DrawSketchHandlerDimension() override
    {
    }

    enum class AvailableConstraint {
        FIRST,
        SECOND,
        THIRD,
        FOURTH,
        FIFTH,
        RESET
    };

    enum class SpecialConstraint {
        LineOr2PointsDistance,
        Block,
        None
    };

    void activated() override
    {
        openCommand(QT_TRANSLATE_NOOP("Command", "Dimension"));

        Obj = sketchgui->getSketchObject();

        // Constrain icon size in px
        qreal pixelRatio = devicePixelRatio();
        const unsigned long defaultCrosshairColor = 0xFFFFFF;
        unsigned long color = getCrosshairColor();
        auto colorMapping = std::map<unsigned long, unsigned long>();
        colorMapping[defaultCrosshairColor] = color;

        constexpr qreal fullIconWidth = 32;
        constexpr qreal iconWidth = 16;
        QPixmap cursorPixmap = Gui::BitmapFactory().pixmapFromSvg("Sketcher_Crosshair", QSizeF(fullIconWidth, fullIconWidth), colorMapping);
        QPixmap icon = Gui::BitmapFactory().pixmapFromSvg("Constraint_Dimension", QSizeF(iconWidth, iconWidth));
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

        handleInitialSelection();
    }

    void deactivated() override
    {
        abortCommand();
        if (availableConstraint != AvailableConstraint::FIRST) {
            Obj->solve();
        }
        sketchgui->draw(false, false); // Redraw
    }

    void registerPressedKey(bool pressed, int key) override
    {
        if (key == SoKeyboardEvent::M && pressed) {
            if (availableConstraint == AvailableConstraint::FIRST) {
                availableConstraint = AvailableConstraint::SECOND;
            }
            else if (availableConstraint == AvailableConstraint::SECOND) {
                availableConstraint = AvailableConstraint::THIRD;
            }
            else if (availableConstraint == AvailableConstraint::THIRD) {
                availableConstraint = AvailableConstraint::FOURTH;
            }
            else if (availableConstraint == AvailableConstraint::FOURTH) {
                availableConstraint = AvailableConstraint::FIFTH;
            }
            else if (availableConstraint == AvailableConstraint::FIFTH || availableConstraint == AvailableConstraint::RESET) {
                availableConstraint = AvailableConstraint::FIRST;
            }
            makeAppropriateConstraint(previousOnSketchPos);
        }
        else {
            DrawSketchHandler::registerPressedKey(pressed, key);
        }
    }

    void mouseMove(SnapManager::SnapHandle snapHandle) override
    {
        if (hasBeenAborted()) {
            resetTool();
            return;
        }

        //All snap Positions except of edges, because it results in jumps on angle constraints
        SnapType mask = static_cast<SnapType>(static_cast<int>(SnapType::All) & ~static_cast<int>(SnapType::Edge));
        Base::Vector2d onSketchPos = snapHandle.compute(mask);
        previousOnSketchPos = onSketchPos;

        //Change distance constraint based on position of mouse.
        if (specialConstraint == SpecialConstraint::LineOr2PointsDistance)
            updateDistanceType(onSketchPos);

        //Move constraints
        if (!cstrIndexes.empty()) {
            bool oneMoved = false;
            const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();
            int lastConstrIndex = static_cast<int>(ConStr.size()) - 1;
            for (int index : cstrIndexes) {
                if (ConStr[index]->isDimensional()) {
                    Base::Vector2d pointWhereToMove = onSketchPos;

                    if (specialConstraint == SpecialConstraint::Block) {
                        if (index == lastConstrIndex)
                            pointWhereToMove.y = Obj->getPoint(selPoints[0].GeoId, selPoints[0].PosId).y;
                        else
                            pointWhereToMove.x = Obj->getPoint(selPoints[0].GeoId, selPoints[0].PosId).x;
                    }
                    moveConstraint(index, pointWhereToMove, OffsetConstraint);
                    oneMoved = true;
                }
            }
            if (oneMoved)
                sketchgui->draw(false, false); // Redraw
        }
    }

    bool pressButton(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos)
        return true;
    }

    bool releaseButton(Base::Vector2d onSketchPos) override
    {
        Q_UNUSED(onSketchPos);
        availableConstraint = AvailableConstraint::FIRST;
        SelIdPair selIdPair;
        selIdPair.GeoId = GeoEnum::GeoUndef;
        selIdPair.PosId = Sketcher::PointPos::none;
        std::stringstream ss;
        Base::Type newselGeoType = Base::Type::BadType;

        int VtId = getPreselectPoint();
        int CrvId = getPreselectCurve();
        int CrsId = getPreselectCross();

        if (VtId >= 0) { //Vertex
            Obj->getGeoVertexIndex(VtId,
                selIdPair.GeoId, selIdPair.PosId);
            newselGeoType = Part::GeomPoint::getClassTypeId();
            ss << "Vertex" << VtId + 1;
        }
        else if (CrsId == 0) { //RootPoint
            selIdPair.GeoId = Sketcher::GeoEnum::RtPnt;
            selIdPair.PosId = Sketcher::PointPos::start;
            newselGeoType = Part::GeomPoint::getClassTypeId();
            ss << "RootPoint";
        }
        else if (CrsId == 1) { //H_Axis
            selIdPair.GeoId = Sketcher::GeoEnum::HAxis;
            newselGeoType = Part::GeomLineSegment::getClassTypeId();
            ss << "H_Axis";
        }
        else if (CrsId == 2) { //V_Axis
            selIdPair.GeoId = Sketcher::GeoEnum::VAxis;
            newselGeoType = Part::GeomLineSegment::getClassTypeId();
            ss << "V_Axis";
        }
        else if (CrvId >= 0 || CrvId <= Sketcher::GeoEnum::RefExt) { //Curves
            selIdPair.GeoId = CrvId;
            const Part::Geometry* geo = Obj->getGeometry(CrvId);

            newselGeoType = geo->getTypeId();

            if (CrvId >= 0) {
                ss << "Edge" << CrvId + 1;
            }
            else {
                ss << "ExternalEdge" << Sketcher::GeoEnum::RefExt + 1 - CrvId;
            }
        }

        if (selIdPair.GeoId == GeoEnum::GeoUndef) {
            // If mouse is released on "blank" space, finalize and start over
            finalizeCommand();
            return true;
        }

        std::vector<SelIdPair>& selVector = getSelectionVector(newselGeoType);

        if (notSelectedYet(selIdPair)) {
            //add the geometry to its type vector. Temporarily if not selAllowed
            selVector.push_back(selIdPair);

            bool selAllowed = makeAppropriateConstraint(onSketchPos);

            if (selAllowed) {
                // If mouse is released on something allowed, select it
                sketchgui->addSelection2(ss.str().c_str(), onSketchPos.x, onSketchPos.y, 0.f);
                sketchgui->draw(false, false); // Redraw
            }
            else {
                selVector.pop_back();
            }
        }
        else {
            //if it is already selected we unselect it.
            selVector.pop_back();
            if (!selectionEmpty()) {
                makeAppropriateConstraint(onSketchPos);
            }
            else {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Dimension"));
            }

            sketchgui->rmvSelection(ss.str().c_str());
            sketchgui->draw(false, false); // Redraw
        }

        updateHint();
        return true;
    }

    void quit() override
    {
        if (!cstrIndexes.empty()) {
            // if a constraint is being made, then we cancel the dimension but not the tool.
            resetTool();
            sketchgui->draw(false, false); // Redraw
        }
        else {
            DrawSketchHandler::quit();
        }
    }

std::list<Gui::InputHint> getToolHints() const override {
    if (selectionEmpty()) {
        return {{QObject::tr(PICK_POINT_OR_EDGE), {Gui::InputHint::UserInput::MouseLeft}},
                {QObject::tr(MODE_HINT), {Gui::InputHint::UserInput::KeyM}}};
    } else if (selPoints.size() == 1 && selLine.empty() && selCircleArc.empty() && selEllipseAndCo.empty() && selSplineAndCo.empty()) {
        // Single point - can add more points, lines, circles, etc.
        return {{QObject::tr(PICK_SECOND_POINT_OR_EDGE_OR_CLICK_TO_FINISH), {Gui::InputHint::UserInput::MouseLeft}},
                {QObject::tr(MODE_HINT), {Gui::InputHint::UserInput::KeyM}}};
    } else if (selLine.size() == 1 && selPoints.empty() && selCircleArc.empty() && selEllipseAndCo.empty() && selSplineAndCo.empty()) {
        // Single line - can add more points, lines, circles, etc.
        return {{QObject::tr(PICK_SECOND_POINT_OR_EDGE_OR_CLICK_TO_FINISH), {Gui::InputHint::UserInput::MouseLeft}},
                {QObject::tr(MODE_HINT), {Gui::InputHint::UserInput::KeyM}}};
    } else if (selCircleArc.size() == 1 && selPoints.empty() && selLine.empty() && selEllipseAndCo.empty() && selSplineAndCo.empty()) {
        // Single circle/arc - can add more points, lines, circles, etc.
        return {{QObject::tr(PICK_SECOND_POINT_OR_EDGE_OR_CLICK_TO_FINISH), {Gui::InputHint::UserInput::MouseLeft}},
                {QObject::tr(MODE_HINT), {Gui::InputHint::UserInput::KeyM}}};
    } else {
        // Multiple selections or complex combinations - check if more selections are possible
        // For now, assume more selections are possible unless we have a complete constraint
        return {{QObject::tr(PICK_SECOND_POINT_OR_EDGE_OR_CLICK_TO_FINISH), {Gui::InputHint::UserInput::MouseLeft}},
                {QObject::tr(MODE_HINT), {Gui::InputHint::UserInput::KeyM}}};
    }
}

protected:
    SpecialConstraint specialConstraint;
    AvailableConstraint availableConstraint;

    Base::Vector2d previousOnSketchPos;

    std::vector<SelIdPair> selPoints;
    std::vector<SelIdPair> selLine;
    std::vector<SelIdPair> selCircleArc;
    std::vector<SelIdPair> selEllipseAndCo;
    std::vector<SelIdPair> selSplineAndCo;

    std::vector<std::string> initialSelection;

    std::vector<int> cstrIndexes;

    Sketcher::SketchObject* Obj;

    void clearRefVectors()
    {
        selPoints.clear();
        selLine.clear();
        selCircleArc.clear();
        selEllipseAndCo.clear();
        selSplineAndCo.clear();
    }

    void handleInitialSelection()
    {
        if (initialSelection.size() == 0) {
            return;
        }

        availableConstraint = AvailableConstraint::FIRST;

        // Add the selected elements to their corresponding selection vectors
        for (auto& selElement : initialSelection) {
            SelIdPair selIdPair;
            getIdsFromName(selElement, Obj, selIdPair.GeoId, selIdPair.PosId);

            Base::Type newselGeoType = Base::Type::BadType;
            if (isEdge(selIdPair.GeoId, selIdPair.PosId)) {
                const Part::Geometry* geo = Obj->getGeometry(selIdPair.GeoId);
                newselGeoType = geo->getTypeId();
            }
            else if (isVertex(selIdPair.GeoId, selIdPair.PosId)) {
                newselGeoType = Part::GeomPoint::getClassTypeId();
            }

            std::vector<SelIdPair>& selVector = getSelectionVector(newselGeoType);

            //add the geometry to its type vector. Temporarily if not selAllowed
            selVector.push_back(selIdPair);
        }

        // Remove redundant coincident points from the selection
        // This ensures box-selecting a junction of multiple lines is treated as 1 point.
        filterCoincidentPoints();

        // See if the selection is valid
        bool selAllowed = makeAppropriateConstraint(Base::Vector2d(0.,0.));

        if (!selAllowed) {
            clearRefVectors();
        }
    }

    void filterCoincidentPoints()
    {
        if (selPoints.size() < 2) {
            return;
        }

        std::vector<SelIdPair> filteredPoints;

        for (const auto& p : selPoints) {
            bool isRedundant = false;
            for (const auto& kept : filteredPoints) {
                if (Obj->arePointsCoincident(p.GeoId, p.PosId, kept.GeoId, kept.PosId)) {
                    isRedundant = true;
                    break;
                }
            }

            if (!isRedundant) {
                filteredPoints.push_back(p);
            }
        }

        selPoints = std::move(filteredPoints);
    }

    void finalizeCommand()
    {
        if (hasBeenAborted()) {
            resetTool();
            return;
        }

        // Ask for the value of datum constraints
        ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Sketcher");
        bool show = hGrp->GetBool("ShowDialogOnDistanceConstraint", true);
        const std::vector<Sketcher::Constraint*>& ConStr = Obj->Constraints.getValues();

        bool commandHandledInEditDatum = false;
        for (int index : cstrIndexes | boost::adaptors::reversed) {
            if (show && ConStr[index]->isDimensional() && ConStr[index]->isDriving) {
                commandHandledInEditDatum = true;
                EditDatumDialog editDatumDialog(currentTransactionID, sketchgui, index);
                editDatumDialog.exec();
                if (!editDatumDialog.isSuccess()) {
                    break;
                }
            }
        }

        if (!commandHandledInEditDatum) {
            commitCommand();
        }

        // This code enables the continuous creation mode.
        bool continuousMode = hGrp->GetBool("ContinuousCreationMode", true);
        if (continuousMode) {
            resetTool();
        }
        else {
            sketchgui->purgeHandler(); // no code after this line, Handler get deleted in ViewProvider
        }
    }

    std::vector<SelIdPair>& getSelectionVector(Base::Type selGeoType)
    {
        if (selGeoType == Part::GeomPoint::getClassTypeId()) {
            return selPoints;
        }
        else if (selGeoType == Part::GeomLineSegment::getClassTypeId()) {
            return selLine;
        }
        else if (selGeoType == Part::GeomArcOfCircle::getClassTypeId() ||
            selGeoType == Part::GeomCircle::getClassTypeId()) {
            return selCircleArc;
        }
        else if (selGeoType == Part::GeomEllipse::getClassTypeId() ||
            selGeoType == Part::GeomArcOfEllipse::getClassTypeId() ||
            selGeoType == Part::GeomArcOfHyperbola::getClassTypeId() ||
            selGeoType == Part::GeomArcOfParabola::getClassTypeId()) {
            return selEllipseAndCo;
        }
        else if (selGeoType == Part::GeomBSplineCurve::getClassTypeId()) {
            return selSplineAndCo;
        }

        static std::vector<SelIdPair> emptyVector;
        return emptyVector;
    }

    bool notSelectedYet(const SelIdPair& elem)
    {
        auto contains = [&](const std::vector<SelIdPair>& vec, const SelIdPair& elem) {
            for (const auto& x : vec)
            {
                if (x.GeoId == elem.GeoId && x.PosId == elem.PosId)
                    return true;
            }
            return false;
        };

        return !contains(selPoints, elem)
            && !contains(selLine, elem)
            && !contains(selCircleArc, elem)
            && !contains(selEllipseAndCo, elem);
    }

    bool selectionEmpty() const
    {
        return selPoints.empty() && selLine.empty() && selCircleArc.empty() && selEllipseAndCo.empty();
    }

    bool makeAppropriateConstraint(Base::Vector2d onSketchPos) {
        bool selAllowed = false;

        GeomSelectionSizes selection(selPoints.size(), selLine.size(), selCircleArc.size(), selEllipseAndCo.size(), selSplineAndCo.size());

        if (selection.hasPoints()) {
            if (selection.has1Point()) { makeCts_1Point(selAllowed, onSketchPos); }
            else if (selection.has2Points()) { makeCts_2Point(selAllowed, onSketchPos); }
            else if (selection.has1Point1Line()) { makeCts_1Point1Line(selAllowed, onSketchPos); }
            else if (selection.has1Point1Spline1MoreEdge()) { makeCts_1Point1Spline1MoreEdge(selAllowed);}
            else if (selection.has3Points()) { makeCts_3Point(selAllowed, selection.s_pts); }
            else if (selection.has4MorePoints()) { makeCts_4MorePoint(selAllowed, selection.s_pts); }
            else if (selection.has2Points1Line()) { makeCts_2Point1Line(selAllowed, onSketchPos, selection.s_pts); }
            else if (selection.has3MorePoints1Line()) { makeCts_3MorePoint1Line(selAllowed, onSketchPos, selection.s_pts); }
            else if (selection.has1Point1Circle()) { makeCts_1Point1Circle(selAllowed, onSketchPos); }
            else if (selection.has1MorePoint1Ellipse()) { makeCts_1MorePoint1Ellipse(selAllowed); }
        }
        else if (selection.hasLines()) {
            if (selection.has1Line()) { makeCts_1Line(selAllowed, onSketchPos); }
            else if (selection.has2Lines()) { makeCts_2Line(selAllowed, onSketchPos); }
            else if (selection.has3MoreLines()) { makeCts_3MoreLine(selAllowed, selection.s_lns); }
            else if (selection.has1Line1Circle()) { makeCts_1Line1Circle(selAllowed, onSketchPos); }
            else if (selection.has1Line2Circles()) { makeCts_1Line2Circle(selAllowed); }
            else if (selection.has1Line1Ellipse()) { makeCts_1Line1Ellipse(selAllowed); }
        }
        else if (selection.hasCirclesOrArcs()) {
            if (selection.has1Circle()) { makeCts_1Circle(selAllowed, onSketchPos); }
            else if (selection.has2Circles()) { makeCts_2Circle(selAllowed, onSketchPos); }
            else if (selection.has3MoreCircles()) { makeCts_3MoreCircle(selAllowed, selection.s_cir); }
            else if (selection.has1Circle1Ellipse()) { makeCts_1Circle1Ellipse(selAllowed); }
        }
        else if (selection.hasEllipseAndCo()) {
            if (selection.has1Ellipse()) { makeCts_1Ellipse(selAllowed); }
            else if (selection.has2MoreEllipses()) { makeCts_2MoreEllipse(selAllowed, selection.s_ell); }
        }
        return selAllowed;
    }

    void makeCts_1Point(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //distance, lock
        if (availableConstraint == AvailableConstraint::FIRST) {
            selAllowed = true;
            if(selPoints[0].GeoId == Sketcher::GeoEnum::RtPnt) {
                // Cannot do distance to origin if origin selected
                return;
            }
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Distance to origin' constraint"));
            createDistanceConstrain(selPoints[0].GeoId, selPoints[0].PosId, Sketcher::GeoEnum::RtPnt, Sketcher::PointPos::start, onSketchPos);
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add lock constraint"));
            specialConstraint = SpecialConstraint::Block;
            createDistanceXYConstrain(Sketcher::DistanceX, selPoints[0].GeoId, selPoints[0].PosId, Sketcher::GeoEnum::RtPnt, Sketcher::PointPos::start, onSketchPos);
            createDistanceXYConstrain(Sketcher::DistanceY, selPoints[0].GeoId, selPoints[0].PosId, Sketcher::GeoEnum::RtPnt, Sketcher::PointPos::start, onSketchPos);
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_2Point(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //distance, horizontal, vertical
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Distance constraint"));
            createDistanceConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, onSketchPos);
            selAllowed = true;
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Horizontal' constraints"));
            createHorizontalConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId);
        }
        if (availableConstraint == AvailableConstraint::THIRD) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Vertical' constraints"));
            createVerticalConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId);
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Point1Line(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //distance, Symmetry
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add point to line Distance constraint"));
            createDistanceConstrain(selPoints[0].GeoId, selPoints[0].PosId, selLine[0].GeoId, selLine[0].PosId, onSketchPos); // point to be on first parameter
            selAllowed = true;
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Symmetry constraint"));
            createSymmetryConstrain(selLine[0].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, Sketcher::PointPos::end, selPoints[0].GeoId, selPoints[0].PosId);
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_3Point(bool& selAllowed, size_t s_pts)
    {
        //Horizontal, vertical, symmetry
        if (s_pts > 0 && availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Horizontal' constraints"));
            for (size_t i = 0; i < s_pts - 1; i++) {
                createHorizontalConstrain(selPoints[i].GeoId, selPoints[i].PosId, selPoints[i + 1].GeoId, selPoints[i + 1].PosId);
            }
            selAllowed = true;
        }
        if (s_pts > 0 && availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Vertical' constraints"));
            for (size_t i = 0; i < s_pts - 1; i++) {
                createVerticalConstrain(selPoints[i].GeoId, selPoints[i].PosId, selPoints[i + 1].GeoId, selPoints[i + 1].PosId);
            }
        }
        if (availableConstraint == AvailableConstraint::THIRD) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Symmetry constraints"));
            createSymmetryConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, selPoints[2].GeoId, selPoints[2].PosId);
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Point1Spline1MoreEdge(bool& /*selAllowed*/)
    {
        //angle
        if (availableConstraint == AvailableConstraint::FIRST) {
            // FIXME: Once everything is implemented uncomment restartCommand and setAllowed
            // restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Angle' constraint"));
            // TODO: Find the appropriate geoids and call createAngleConstrain
            // selAllowed = true;
        }
    }

    void makeCts_4MorePoint(bool& selAllowed, size_t s_pts)
    {
        //Horizontal, vertical
        if (s_pts > 0 && availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Horizontal' constraints"));
            for (size_t i = 0; i < s_pts - 1; i++) {
                createHorizontalConstrain(selPoints[i].GeoId, selPoints[i].PosId, selPoints[i + 1].GeoId, selPoints[i + 1].PosId);
            }
            selAllowed = true;
        }
        if (s_pts > 0 && availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add 'Vertical' constraints"));
            for (size_t i = 0; i < s_pts - 1; i++) {
                createVerticalConstrain(selPoints[i].GeoId, selPoints[i].PosId, selPoints[i + 1].GeoId, selPoints[i + 1].PosId);
            }
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_2Point1Line(bool& selAllowed, Base::Vector2d onSketchPos, size_t s_pts)
    {
        //symmetry, distances
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Symmetry constraint"));
            createSymmetryConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, selLine[0].GeoId, selLine[0].PosId);
            selAllowed = true;
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Distance constraints"));
            for (size_t i = 0; i < s_pts; i++) {
                createDistanceConstrain(selPoints[i].GeoId, selPoints[i].PosId, selLine[0].GeoId, selLine[0].PosId, onSketchPos);
            }
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_3MorePoint1Line(bool& selAllowed, Base::Vector2d onSketchPos, size_t s_pts)
    {
        //distances
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Distance constraints"));
            for (size_t i = 0; i < s_pts; i++) {
                createDistanceConstrain(selPoints[i].GeoId, selPoints[i].PosId, selLine[0].GeoId, selLine[0].PosId, onSketchPos);
            }
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Point1Circle(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //Distance
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            createDistanceConstrain(selPoints[0].GeoId, selPoints[0].PosId, selCircleArc[0].GeoId, selCircleArc[0].PosId, onSketchPos);
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1MorePoint1Ellipse(bool& selAllowed)
    {
        Q_UNUSED(selAllowed)
        //distance between 1 point and ellipse/arc of... not supported yet.
        if (availableConstraint == AvailableConstraint::FIRST) {
            //nothing yet
            //availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Line(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //axis can be selected but we don't want distance on axis!
        if ((selLine[0].GeoId != Sketcher::GeoEnum::VAxis && selLine[0].GeoId != Sketcher::GeoEnum::HAxis)) {
            //distance, horizontal, vertical, block
            if (availableConstraint == AvailableConstraint::FIRST) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
                createDistanceConstrain(selLine[0].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, Sketcher::PointPos::end, onSketchPos);
                selAllowed = true;
            }
            if (availableConstraint == AvailableConstraint::SECOND) {
                if (isHorizontalVerticalBlock(selLine[0].GeoId)) {
                    //if the line has a vertical horizontal or block constraint then we don't switch to other modes as they are horizontal, vertical and block.
                    availableConstraint = AvailableConstraint::RESET;
                }
                else {
                    restartCommand(QT_TRANSLATE_NOOP("Command", "Add Horizontal constraint"));
                    createHorizontalConstrain(selLine[0].GeoId, Sketcher::PointPos::none, GeoEnum::GeoUndef, Sketcher::PointPos::none);
                }
            }
            if (availableConstraint == AvailableConstraint::THIRD) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add Vertical constraint"));
                createVerticalConstrain(selLine[0].GeoId, Sketcher::PointPos::none, GeoEnum::GeoUndef, Sketcher::PointPos::none);
            }
            if (availableConstraint == AvailableConstraint::FOURTH) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add Block constraint"));
                createBlockConstrain(selLine[0].GeoId);
                availableConstraint = AvailableConstraint::RESET;
            }
        }
        else {
            //But axis can still be selected
            selAllowed = true;
        }
    }

    void makeCts_2Line(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //angle (if parallel: Distance (see in createAngleConstrain)), equal.
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Angle constraint"));
            createAngleConstrain(selLine[0].GeoId, selLine[1].GeoId, onSketchPos);
            selAllowed = true;
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            if (selLine[0].GeoId == Sketcher::GeoEnum::VAxis || selLine[1].GeoId == Sketcher::GeoEnum::VAxis
                || selLine[0].GeoId == Sketcher::GeoEnum::HAxis || selLine[1].GeoId == Sketcher::GeoEnum::HAxis) {
                //if one line is axis, then can't equal..
            }
            else {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add Equality constraint"));
                createEqualityConstrain(selLine[0].GeoId, selLine[1].GeoId);
            }
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_3MoreLine(bool& selAllowed, size_t s_lns)
    {
        //equality.
        if (s_lns > 0 && availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Equality constraints"));
            for (size_t i = 0; i < s_lns - 1; i++) {
                createEqualityConstrain(selLine[i].GeoId, selLine[i + 1].GeoId);
            }
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Line1Circle(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //Distance
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            createDistanceConstrain(selCircleArc[0].GeoId, selCircleArc[0].PosId, selLine[0].GeoId, selLine[0].PosId, onSketchPos); //Line second parameter
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Line2Circle(bool& selAllowed)
    {
        //symmetry.
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Symmetry constraints"));
            createSymmetryConstrain(selCircleArc[0].GeoId, Sketcher::PointPos::mid, selCircleArc[1].GeoId, Sketcher::PointPos::mid, selLine[0].GeoId, selLine[0].PosId);
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Line1Ellipse(bool& selAllowed)
    {
        Q_UNUSED(selAllowed)
        //TODO distance between line and ellipse/arc of... not supported yet.
        if (availableConstraint == AvailableConstraint::FIRST) {
            //selAllowed = true;
            //availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Circle(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        int geoId = selCircleArc[0].GeoId;
        bool reverseOrder = isRadiusDoF(geoId);

        if (reverseOrder) {
            if (availableConstraint == AvailableConstraint::FIRST) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add arc angle constraint"));
                createArcAngleConstrain(geoId, onSketchPos);
                selAllowed = true;
            }
            if (availableConstraint == AvailableConstraint::SECOND) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add arc length constraint"));
                createArcLengthConstrain(geoId, onSketchPos);
            }
            if (availableConstraint == AvailableConstraint::THIRD) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
                createRadiusDiameterConstrain(geoId, onSketchPos, true);
            }
            if (availableConstraint == AvailableConstraint::FOURTH) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
                createRadiusDiameterConstrain(geoId, onSketchPos, false);
                availableConstraint = AvailableConstraint::RESET;
            }
        }
        else {
            if (availableConstraint == AvailableConstraint::FIRST) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
                createRadiusDiameterConstrain(geoId, onSketchPos, true);
                selAllowed = true;
            }
            if (availableConstraint == AvailableConstraint::SECOND) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add radius constraint"));
                createRadiusDiameterConstrain(geoId, onSketchPos, false);
                if (!isArcOfCircle(*Obj->getGeometry(geoId))) {
                    //This way if key is pressed again it goes back to FIRST
                    availableConstraint = AvailableConstraint::RESET;
                }
            }
            if (availableConstraint == AvailableConstraint::THIRD) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add arc angle constraint"));
                createArcAngleConstrain(geoId, onSketchPos);
            }
            if (availableConstraint == AvailableConstraint::FOURTH) {
                restartCommand(QT_TRANSLATE_NOOP("Command", "Add arc length constraint"));
                createArcLengthConstrain(geoId, onSketchPos);
                availableConstraint = AvailableConstraint::RESET;
            }
        }

    }

    void makeCts_2Circle(bool& selAllowed, Base::Vector2d onSketchPos)
    {
        //Distance, radial distance, equality
        if (availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add length constraint"));
            createDistanceConstrain(selCircleArc[0].GeoId, selCircleArc[0].PosId, selCircleArc[1].GeoId, selCircleArc[1].PosId, onSketchPos);
            selAllowed = true;
        }
        if (availableConstraint == AvailableConstraint::SECOND) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add concentric and length constraint"));
            bool created = createCoincidenceConstrain(selCircleArc[0].GeoId, Sketcher::PointPos::mid, selCircleArc[1].GeoId, Sketcher::PointPos::mid);
            if (!created) { //Already concentric, so skip to next
                availableConstraint = AvailableConstraint::THIRD;
            }
            else {
                createDistanceConstrain(selCircleArc[0].GeoId, selCircleArc[0].PosId, selCircleArc[1].GeoId, selCircleArc[1].PosId, onSketchPos);
            }
        }
        if (availableConstraint == AvailableConstraint::THIRD) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Equality constraint"));
            createEqualityConstrain(selCircleArc[0].GeoId, selCircleArc[1].GeoId);
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_3MoreCircle(bool& selAllowed, size_t s_cir)
    {
        //equality.
        if (s_cir > 0 && availableConstraint == AvailableConstraint::FIRST) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Equality constraint"));
            for (size_t i = 0; i < s_cir - 1; i++) {
                createEqualityConstrain(selCircleArc[i].GeoId, selCircleArc[i + 1].GeoId);
            }
            selAllowed = true;
            availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Circle1Ellipse(bool& selAllowed)
    {
        Q_UNUSED(selAllowed)
        //TODO distance between circle and ellipse/arc of... not supported yet.
        if (availableConstraint == AvailableConstraint::FIRST) {
            //selAllowed = true;
            //availableConstraint = AvailableConstraint::RESET;
        }
    }

    void makeCts_1Ellipse(bool& selAllowed)
    {
        //One ellipse or arc of ellipse/hyperbola/parabola - no constrain to attribute
        selAllowed = true;
    }

    void makeCts_2MoreEllipse(bool& selAllowed, size_t s_ell)
    {
        //only ellipse or arc of of same kind, then equality of all radius.
        bool allTheSame = 1;
        const Part::Geometry* geom = Obj->getGeometry(selEllipseAndCo[0].GeoId);
        Base::Type typeOf = geom->getTypeId();
        for (size_t i = 1; i < s_ell; i++) {
            const Part::Geometry* geomi = Obj->getGeometry(selEllipseAndCo[i].GeoId);
            if (typeOf != geomi->getTypeId()) {
                allTheSame = 0;
            }
        }
        if (allTheSame) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Equality constraint"));
            for (size_t i = 1; i < s_ell; i++) {
                createEqualityConstrain(selEllipseAndCo[0].GeoId, selEllipseAndCo[i].GeoId);
            }
            selAllowed = true;
        }
    }

    void createDistanceConstrain(int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2, Base::Vector2d onSketchPos) {
        // If there's a point, it must be GeoId1. We could add a swap to make sure but as it's hardcoded it's not necessary.

        if (GeoId1 == GeoId2 || (PosId1 != Sketcher::PointPos::none && PosId2 != Sketcher::PointPos::none)) {
            specialConstraint = SpecialConstraint::LineOr2PointsDistance;
        }

        // Point-line case and point-circle/arc
        if (PosId1 != Sketcher::PointPos::none && PosId2 == Sketcher::PointPos::none) {
            Base::Vector3d pnt = Obj->getPoint(GeoId1, PosId1);
            double ActDist = 0.;
            const Part::Geometry* geom = Obj->getGeometry(GeoId2);

            if (isLineSegment(*geom)) {
                auto lineSeg = static_cast<const Part::GeomLineSegment*>(geom);
                Base::Vector3d pnt1 = lineSeg->getStartPoint();
                Base::Vector3d pnt2 = lineSeg->getEndPoint();
                Base::Vector3d d = pnt2 - pnt1;
                ActDist = std::abs(-pnt.x * d.y + pnt.y * d.x + pnt1.x * pnt2.y - pnt2.x * pnt1.y) / d.Length();
            }
            else if (isCircle(*geom)) {
                auto circle = static_cast<const Part::GeomCircle*>(geom);
                Base::Vector3d ct = circle->getCenter();
                Base::Vector3d di = ct - pnt;
                ActDist = std::abs(di.Length() - circle->getRadius());
            }
            else if (isArcOfCircle(*geom)) {
                auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
                Base::Vector3d ct = arc->getCenter();
                Base::Vector3d di = ct - pnt;
                ActDist = std::abs(di.Length() - arc->getRadius());
            }

            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%f)) ",
                GeoId1, static_cast<int>(PosId1), GeoId2, ActDist);
        }
        // Circle/arc - line, circle/arc - circle/arc cases
        else if (PosId1 == Sketcher::PointPos::none && PosId2 == Sketcher::PointPos::none) {
            const Part::Geometry* geo1 = Obj->getGeometry(GeoId1);
            const Part::Geometry* geo2 = Obj->getGeometry(GeoId2);
            double radius1 {};
            double radius2 {};
            Base::Vector3d center1, center2;
            if (isCircle(*geo1)) {
                auto conic = static_cast<const Part::GeomCircle*>(geo1);
                radius1 = conic->getRadius();
                center1 = conic->getCenter();
            }
            else if (isArcOfCircle(*geo1)) {
                auto conic = static_cast<const Part::GeomArcOfCircle*>(geo1);
                radius1 = conic->getRadius();
                center1 = conic->getCenter();
            }
            if (isCircle(*geo2)) {
                auto conic = static_cast<const Part::GeomCircle*>(geo2);
                radius2 = conic->getRadius();
                center2 = conic->getCenter();
            }
            else if (isArcOfCircle(*geo2)){
                auto conic = static_cast<const Part::GeomArcOfCircle*>(geo2);
                radius2 = conic->getRadius();
                center2 = conic->getCenter();
            }
            // Circle/arc - line case
            if ((isCircle(*geo1) || isArcOfCircle(*geo1)) && isLineSegment(*geo2)) {
                auto lineSeg = static_cast<const Part::GeomLineSegment*>(geo2);
                Base::Vector3d pnt1 = lineSeg->getStartPoint();
                Base::Vector3d pnt2 = lineSeg->getEndPoint();
                Base::Vector3d d = pnt2 - pnt1;
                double ActDist = std::abs(
                            std::abs(-center1.x * d.y + center1.y * d.x + pnt1.x * pnt2.y - pnt2.x * pnt1.y)
                            / d.Length()
                            - radius1);

                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Distance',%d,%d,%f))",
                                      GeoId1,
                                      GeoId2,
                                      ActDist);
            }
            // Circle/arc - circle/arc case
            else if ((isCircle(*geo1) || isArcOfCircle(*geo1))
                     && (isCircle(*geo2) || isArcOfCircle(*geo2))) {
                double ActDist = 0.;

                Base::Vector3d intercenter = center1 - center2;
                double intercenterdistance = intercenter.Length();

                if (intercenterdistance >= radius1 && intercenterdistance >= radius2) {

                    ActDist = intercenterdistance - radius1 - radius2;
                }
                else {
                    double bigradius = std::max(radius1, radius2);
                    double smallradius = std::min(radius1, radius2);

                    ActDist = bigradius - smallradius - intercenterdistance;
                }

                Gui::cmdAppObjectArgs(Obj,
                                      "addConstraint(Sketcher.Constraint('Distance',%d,%d,%f))",
                                      GeoId1,
                                      GeoId2,
                                      ActDist);
            }
        }
        else {  // both points
            Base::Vector3d pnt1 = Obj->getPoint(GeoId1, PosId1);
            Base::Vector3d pnt2 = Obj->getPoint(GeoId2, PosId2);

            Gui::cmdAppObjectArgs(Obj,
                                  "addConstraint(Sketcher.Constraint('Distance',%d,%d,%d,%d,%f)) ",
                                  GeoId1,
                                  static_cast<int>(PosId1),
                                  GeoId2,
                                  static_cast<int>(PosId2),
                                  (pnt2 - pnt1).Length());
        }

        finishDimensionCreation(GeoId1, GeoId2, onSketchPos);
    }

    void createDistanceXYConstrain(Sketcher::ConstraintType type, int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2, Base::Vector2d onSketchPos) {
        Base::Vector3d pnt1 = Obj->getPoint(GeoId1, PosId1);
        Base::Vector3d pnt2 = Obj->getPoint(GeoId2, PosId2);
        double ActLength = pnt2.x - pnt1.x;

        if (type == Sketcher::DistanceY) {
            ActLength = pnt2.y - pnt1.y;
        }

        //negative sign avoidance: swap the points to make value positive
        if (ActLength < -Precision::Confusion()) {
            std::swap(GeoId1, GeoId2);
            std::swap(PosId1, PosId2);
            std::swap(pnt1, pnt2);
            ActLength = -ActLength;
        }

        if (type == Sketcher::DistanceY) {
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('DistanceY',%d,%d,%d,%d,%f)) ",
                GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2), ActLength);
        }
        else {
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('DistanceX',%d,%d,%d,%d,%f)) ",
                GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2), ActLength);
        }

        finishDimensionCreation(GeoId1, GeoId2, onSketchPos);
    }

    void createRadiusDiameterConstrain(int GeoId, Base::Vector2d onSketchPos, bool firstCstr) {
        double radius = 0.0;
        bool isCircleGeom = true;

        const Part::Geometry* geom = Obj->getGeometry(GeoId);

        if(!geom)
            return;

        if (geom && isArcOfCircle(*geom)) {
            auto arc = static_cast<const Part::GeomArcOfCircle*>(geom);
            radius = arc->getRadius();
            isCircleGeom = false;
        }
        else if (geom && isCircle(*geom)) {
            auto circle = static_cast<const Part::GeomCircle*>(geom);
            radius = circle->getRadius();
        }

        if (isBsplinePole(geom)) {
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Weight',%d,%f)) ",
                GeoId, radius);
        }
        else {
            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Sketcher/dimensioning");
            bool dimensioningDiameter = hGrp->GetBool("DimensioningDiameter", true);
            bool dimensioningRadius = hGrp->GetBool("DimensioningRadius", true);

            if ((firstCstr && dimensioningRadius && !dimensioningDiameter) ||
                (!firstCstr && !dimensioningRadius && dimensioningDiameter) ||
                (firstCstr && dimensioningRadius && dimensioningDiameter && !isCircleGeom) ||
                (!firstCstr && dimensioningRadius && dimensioningDiameter && isCircleGeom) ) {
                Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Radius',%d,%f)) ",
                    GeoId, radius);
            }
            else {
                Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Diameter',%d,%f)) ",
                    GeoId, radius * 2);
            }
        }

        finishDimensionCreation(GeoId, GeoEnum::GeoUndef, onSketchPos);
    }

    bool createCoincidenceConstrain(int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2) {
        // check if the edge already has a Block constraint
        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            return false;
        }

        // check if this coincidence is already enforced (even indirectly)
        bool constraintExists = Obj->arePointsCoincident(GeoId1, PosId1, GeoId2, PosId2);
        if (!constraintExists && (GeoId1 != GeoId2)) {
            Gui::cmdAppObjectArgs(sketchgui->getObject(), "addConstraint(Sketcher.Constraint('Coincident', %d, %d, %d, %d)) ",
                GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2));

            addConstraintIndex();
            return true;
        }
        return false;
    }

    void createEqualityConstrain(int GeoId1, int GeoId2) {
        // check if the edge already has a Block constraint
        if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
            return;
        }

        const Part::Geometry* geo1 = Obj->getGeometry(GeoId1);
        const Part::Geometry* geo2 = Obj->getGeometry(GeoId2);

        if ((isLineSegment(*geo1) && ! isLineSegment(*geo2))
            || (isArcOfHyperbola(*geo1) && ! isArcOfHyperbola(*geo2))
            || (isArcOfParabola(*geo1) && ! isArcOfParabola(*geo2))
            || (isBsplinePole(geo1) && !isBsplinePole(geo2))
            || ((isCircle(*geo1) || isArcOfCircle(*geo1)) && !(isCircle(*geo2) || isArcOfCircle(*geo2)))
            || ((isEllipse(*geo1) || isArcOfEllipse(*geo1)) && !(isEllipse(*geo2) || isArcOfEllipse(*geo2)))) {

            Gui::TranslatedUserWarning(Obj,
                QObject::tr("Wrong selection"),
                QObject::tr("Select two or more edges of similar type."));
            return;
        }

        Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Equal',%d,%d)) ",
            GeoId1, GeoId2);
        addConstraintIndex();
    }

    void createAngleConstrain(int GeoId1, int GeoId2, Base::Vector2d onSketchPos) {
        Sketcher::PointPos PosId1 = Sketcher::PointPos::none;
        Sketcher::PointPos PosId2 = Sketcher::PointPos::none;
        double ActAngle;

        if (!calculateAngle(Obj, GeoId1, GeoId2, PosId1, PosId2, ActAngle)) {
            return;
        }

        if (ActAngle == 0.0) {
            //Here we are sure that GeoIds are lines. So 0.0 means that lines are parallel, we change to distance
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Distance constraint"));
            createDistanceConstrain(selLine[1].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, selLine[0].PosId, onSketchPos);
            return;
        }

        Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Angle',%d,%d,%d,%d,%f)) ",
            GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2), ActAngle);

        finishDimensionCreation(GeoId1, GeoId2, onSketchPos);
    }

    void createArcLengthConstrain(int GeoId, Base::Vector2d onSketchPos) {
        const Part::Geometry* geom = Obj->getGeometry(GeoId);
        if (!isArcOfCircle(*geom)) {
            return;
        }

        const auto* arc = static_cast<const Part::GeomArcOfCircle*>(geom);
        double ActLength = arc->getAngle(false) * arc->getRadius();

        Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Distance',%d,%f))",
            GeoId, ActLength);

        finishDimensionCreation(GeoId, GeoEnum::GeoUndef, onSketchPos);
    }

    void createArcAngleConstrain(int GeoId, Base::Vector2d onSketchPos) {
        const Part::Geometry* geom = Obj->getGeometry(GeoId);
        if (!isArcOfCircle(*geom)) {
            return;
        }

        const auto* arc = static_cast<const Part::GeomArcOfCircle*>(geom);
        double angle = arc->getAngle(/*EmulateCCWXY=*/true);

        Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Angle',%d,%f))",
            GeoId, angle);

        finishDimensionCreation(GeoId, GeoEnum::GeoUndef, onSketchPos);
    }

    void createVerticalConstrain(int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2) {
        if (selLine.size() == 1) {
            // If the line is horizontal (should be without constraint if we're here), then we need to modify
            // its point or we'll get a null line.
            const Part::Geometry* geo = Obj->getGeometry(GeoId1);
            if (!(geo->is<Part::GeomLineSegment>())) {
                return;
            }
            auto* line = static_cast<const Part::GeomLineSegment*>(geo);

            Base::Vector3d p1 = line->getStartPoint();
            Base::Vector3d p2 = line->getEndPoint();
            if (fabs(p1.y - p2.y) < Precision::Confusion()) { // effectively vertical
                p2 = p1 + (p2 - p1).Length() * Base::Vector3d(0.0, 1.0, 0.0);
                Gui::cmdAppObjectArgs(Obj, "moveGeometry(%d,2,App.Vector(%f, %f, 0),0) ", GeoId1, p2.x, p2.y);
            }
            Gui::cmdAppObjectArgs(sketchgui->getObject(), "addConstraint(Sketcher.Constraint('Vertical',%d)) ", GeoId1);
        }
        else { //2points
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                return;
            }
            Gui::cmdAppObjectArgs(sketchgui->getObject(), "addConstraint(Sketcher.Constraint('Vertical',%d,%d,%d,%d)) "
                , GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2));
        }
        addConstraintIndex();
        tryAutoRecompute(Obj);
    }
    void createHorizontalConstrain(int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2) {
        if (selLine.size() == 1) {
            // If the line is vertical (should be without constraint if we're here), then we need to modify
            // its point or we'll get a null line.
            const Part::Geometry* geo = Obj->getGeometry(GeoId1);
            if (!(geo->is<Part::GeomLineSegment>())) {
                return;
            }
            auto* line = static_cast<const Part::GeomLineSegment*>(geo);

            Base::Vector3d p1 = line->getStartPoint();
            Base::Vector3d p2 = line->getEndPoint();
            if (fabs(p1.x - p2.x) < Precision::Confusion()) { // effectively vertical
                p2 = p1 + (p2 - p1).Length() * Base::Vector3d(1.0, 0.0, 0.0);
                Gui::cmdAppObjectArgs(Obj, "moveGeometry(%d,2,App.Vector(%f, %f, 0),0) ", GeoId1, p2.x, p2.y);
            }
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Horizontal',%d)) ", GeoId1);
        }
        else { //2points
            if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                return;
            }
            Gui::cmdAppObjectArgs(sketchgui->getObject(), "addConstraint(Sketcher.Constraint('Horizontal',%d,%d,%d,%d)) "
                , GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2));
        }
        addConstraintIndex();
        tryAutoRecompute(Obj);
    }

    void createBlockConstrain(int GeoId) {
        Gui::cmdAppObjectArgs(sketchgui->getObject(), "addConstraint(Sketcher.Constraint('Block',%d)) ", GeoId);

        addConstraintIndex();
        tryAutoRecompute(Obj);
    }

    bool isHorizontalVerticalBlock(int GeoId) {
        const std::vector< Sketcher::Constraint* >& vals = Obj->Constraints.getValues();

        // check if the edge already has a Horizontal/Vertical/Block constraint
        for (const auto& constraint : vals) {
            if ((constraint->Type == Sketcher::Horizontal || constraint->Type == Sketcher::Vertical || constraint->Type == Sketcher::Block)
                && constraint->First == GeoId) {
                return true;
            }
        }
        return false;
    }

    void createSymmetryConstrain(int GeoId1, Sketcher::PointPos PosId1, int GeoId2, Sketcher::PointPos PosId2, int GeoId3, Sketcher::PointPos PosId3) {
        if (selPoints.size() == 2 && selLine.size() == 1) {
            if (isEdge(GeoId1, PosId1) && isVertex(GeoId3, PosId3)) {
                std::swap(GeoId1, GeoId3);
                std::swap(PosId1, PosId3);
            }
            else if (isEdge(GeoId2, PosId2) && isVertex(GeoId3, PosId3)) {
                std::swap(GeoId2, GeoId3);
                std::swap(PosId2, PosId3);
            }

            if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
                return;
            }

            const Part::Geometry* geom = Obj->getGeometry(GeoId3);

            if (isLineSegment(*geom)) {
                if (GeoId1 == GeoId2 && GeoId2 == GeoId3) {
                    Gui::TranslatedUserWarning(Obj,
                        QObject::tr("Wrong selection"),
                        QObject::tr("Cannot add a symmetry constraint between a line and its end points!"));
                    return;
                }

                Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d)) ",
                    GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2), GeoId3);

                addConstraintIndex();
                tryAutoRecompute(Obj);
            }
        }
        else {
            if (selPoints.size() == 1 && selLine.size() == 1) { //1line 1 point
                if (GeoId1 == GeoId3) {
                    Gui::TranslatedUserWarning(Obj,
                        QObject::tr("Wrong selection"),
                        QObject::tr("Cannot add a symmetry constraint between a line and its end points!"));
                    return;
                }
                if (areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2)) {
                    return;
                }
            }
            else {
                if (areAllPointsOrSegmentsFixed(Obj, GeoId1, GeoId2, GeoId3)) {
                    return;
                }
            }
            Gui::cmdAppObjectArgs(Obj, "addConstraint(Sketcher.Constraint('Symmetric',%d,%d,%d,%d,%d,%d)) ",
                GeoId1, static_cast<int>(PosId1), GeoId2, static_cast<int>(PosId2), GeoId3, static_cast<int>(PosId3));

            addConstraintIndex();
            tryAutoRecompute(Obj);
        }
    }

    void updateDistanceType(Base::Vector2d onSketchPos)
    {
        const std::vector< Sketcher::Constraint* >& vals = Obj->Constraints.getValues();
        Sketcher::ConstraintType type = vals[vals.size() - 1]->Type;

        Base::Vector3d pnt1, pnt2;
        bool addedOrigin = false;
        if (selPoints.size() == 1) {
            //Case of single point selected, for distance constraint. We add temporarily the origin in the vector.
            addedOrigin = true;
            SelIdPair selIdPair;
            selIdPair.GeoId = Sketcher::GeoEnum::RtPnt;
            selIdPair.PosId = Sketcher::PointPos::start;
            selPoints.push_back(selIdPair);
        }

        if (selLine.size() == 1) {
            pnt1 = Obj->getPoint(selLine[0].GeoId, Sketcher::PointPos::start);
            pnt2 = Obj->getPoint(selLine[0].GeoId, Sketcher::PointPos::end);
        }
        else {
            pnt1 = Obj->getPoint(selPoints[0].GeoId, selPoints[0].PosId);
            pnt2 = Obj->getPoint(selPoints[1].GeoId, selPoints[1].PosId);
        }

        bool isHorizontal = std::abs(pnt1.y - pnt2.y) < Precision::Confusion();
        bool isVertical = std::abs(pnt1.x - pnt2.x) < Precision::Confusion();

        double minX, minY, maxX, maxY;
        minX = min(pnt1.x, pnt2.x);
        maxX = max(pnt1.x, pnt2.x);
        minY = min(pnt1.y, pnt2.y);
        maxY = max(pnt1.y, pnt2.y);

        bool isInDistanceXZone = onSketchPos.x > minX && onSketchPos.x < maxX
            && (onSketchPos.y < minY || onSketchPos.y > maxY);
        bool isInDistanceYZone = onSketchPos.y > minY && onSketchPos.y < maxY
            && (onSketchPos.x < minX || onSketchPos.x > maxX);
        bool isInDistanceZone = ((onSketchPos.y < minY || onSketchPos.y > maxY) && (onSketchPos.x < minX || onSketchPos.x > maxX))
            || (onSketchPos.y > minY && onSketchPos.y < maxY && onSketchPos.x > minX && onSketchPos.x < maxX);

        if ((isHorizontal || isInDistanceXZone) && type != Sketcher::DistanceX) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add DistanceX constraint"));
            specialConstraint = SpecialConstraint::LineOr2PointsDistance;
            if (selLine.size() == 1) {
                createDistanceXYConstrain(Sketcher::DistanceX, selLine[0].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, Sketcher::PointPos::end, onSketchPos);
            }
            else {
                createDistanceXYConstrain(Sketcher::DistanceX, selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, onSketchPos);
            }
        }
        else if ((isVertical || isInDistanceYZone) && type != Sketcher::DistanceY) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add DistanceY constraint"));
            specialConstraint = SpecialConstraint::LineOr2PointsDistance;
            if (selLine.size() == 1) {
                createDistanceXYConstrain(Sketcher::DistanceY, selLine[0].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, Sketcher::PointPos::end, onSketchPos);
            }
            else {
                createDistanceXYConstrain(Sketcher::DistanceY, selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, onSketchPos);
            }
        }
        else if (isInDistanceZone && !isHorizontal && !isVertical && type != Sketcher::Distance) {
            restartCommand(QT_TRANSLATE_NOOP("Command", "Add Distance constraint"));
            if (selLine.size() == 1) {
                createDistanceConstrain(selLine[0].GeoId, Sketcher::PointPos::start, selLine[0].GeoId, Sketcher::PointPos::end, onSketchPos);
            }
            else {
                createDistanceConstrain(selPoints[0].GeoId, selPoints[0].PosId, selPoints[1].GeoId, selPoints[1].PosId, onSketchPos);
            }
        }

        if (addedOrigin) {
            //remove origin
            selPoints.pop_back();
        }
    }

    bool isRadiusDoF(int geoId)
    {
        const Part::Geometry* geo = Obj->getGeometry(geoId);
        if (!isArcOfCircle(*geo)) {
            return false;
        }

        //make sure we are not taking into account the constraint created in previous mode.
        abortCommand();
        Obj->solve();

        auto solvext = Obj->getSolvedSketch().getSolverExtension(geoId);

        if (solvext) {
            auto arcInfo = solvext->getArc();

            return !arcInfo.isRadiusDoF();
        }

        return false;
    }

    void finishDimensionCreation(int GeoId1, int GeoId2, Base::Vector2d onSketchPos)
    {
        bool fixed = GeoId2 == GeoEnum::GeoUndef ? isPointOrSegmentFixed(Obj, GeoId1) : areBothPointsOrSegmentsFixed(Obj, GeoId1, GeoId2);

        int index = Obj->Constraints.getValues().size() - 1;
        if (fixed || constraintCreationMode == Reference) {
            Gui::cmdAppObjectArgs(Obj, "setDriving(%i,%s)", index, "False");
        }

        addConstraintIndex();
        moveConstraint(index, onSketchPos);
    }

    void addConstraintIndex()
    {
        cstrIndexes.push_back(Obj->Constraints.getValues().size() - 1);
    }

    bool hasBeenAborted()
    {
        // User can abort the command with Undo (ctrl-Z)
        if (!cstrIndexes.empty()) {
            int lastConstrIndex = Obj->Constraints.getSize() - 1;
            if (cstrIndexes.back() != lastConstrIndex) {
                return true;
            }
        }

        return false;
    }

    void restartCommand(const char* cstrName) {
        specialConstraint = SpecialConstraint::None;
        abortCommand();
        Obj->solve();
        sketchgui->draw(false, false); // Redraw
        openCommand(cstrName);

        cstrIndexes.clear();
    }

    void resetTool()
    {
        abortCommand();
        Gui::Selection().clearSelection();
        openCommand(QT_TRANSLATE_NOOP("Command", "Dimension"));
        cstrIndexes.clear();
        specialConstraint = SpecialConstraint::None;
        previousOnSketchPos = Base::Vector2d(0.f, 0.f);
        clearRefVectors();
    }
};

DEF_STD_CMD_AU(CmdSketcherDimension)

CmdSketcherDimension::CmdSketcherDimension()
    : Command("Sketcher_Dimension")
{
    sAppModule = "Sketcher";
    sGroup = "Sketcher";
    sMenuText = QT_TR_NOOP("Dimension");
    sToolTipText = QT_TR_NOOP("Constrains contextually based on the selection. The type can be changed with the M key.");
    sWhatsThis = "Sketcher_Dimension";
    sStatusTip = sToolTipText;
    sPixmap = "Constraint_Dimension";
    sAccel = "D";
    eType = ForEdit;
}

void CmdSketcherDimension::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    // get the selection
    std::vector<Gui::SelectionObject> selection = getSelection().getSelectionEx();
    std::vector<std::string> SubNames = {};

    // only one sketch with its subelements are allowed to be selected
    if (selection.size() == 1 && selection[0].isObjectTypeOf(Sketcher::SketchObject::getClassTypeId())) {
        SubNames = selection[0].getSubNames();
    }

    ActivateHandler(getActiveGuiDocument(), std::make_unique<DrawSketchHandlerDimension>(SubNames));
}

void CmdSketcherDimension::updateAction(int mode)
{
    switch (mode) {
    case Reference:
        if (getAction())
            getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Dimension_Driven"));
        break;
    case Driving:
        if (getAction())
            getAction()->setIcon(Gui::BitmapFactory().iconFromTheme("Constraint_Dimension"));
        break;
    }
}

bool CmdSketcherDimension::isActive(void)
{
    return isCommandActive(getActiveGuiDocument());
}

void SketcherGui::addDimensionCommand(Gui::CommandManager& rcCmdMgr)
{
    rcCmdMgr.addCommand(new CmdSketcherDimension());
}
