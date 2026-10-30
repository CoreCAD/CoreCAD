// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Cylinder.hxx>
#include <BRep_Builder.hxx>
#include <Mod/Part/App/FCBRepAlgoAPI_Cut.h>
#include <Mod/Part/App/FCBRepAlgoAPI_Fuse.h>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom_Circle.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Standard_Version.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp.hxx>

#include <App/Application.h>
#include <App/DocumentObject.h>
#include <Base/Placement.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Mod/Part/App/FaceMakerCheese.h>
#include <Mod/Part/App/TopoShapeMapper.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/Tools.h>

#include "FeatureHole.h"
#include "json.hpp"

#include <numbers>

FC_LOG_LEVEL_INIT("PartDesign", true, true);

namespace PartDesign
{

/* TRANSLATOR PartDesign::Hole */

std::vector<std::string> getThreadDesignations(const int threadType)
{
    std::vector<std::string> designations;
    for (const auto& thread : Hole::threadDescription[threadType]) {
        designations.push_back(thread.designation);
    }
    return designations;
}

PROPERTY_SOURCE(PartDesign::Hole, PartDesign::ProfileBased)

const App::PropertyAngle::Constraints Hole::floatAngle = {
    Base::toDegrees<double>(Precision::Angular()),
    180.0 - Base::toDegrees<double>(Precision::Angular()),
    1.0
};
// OCC can only create holes with a min diameter of 10 times the Precision::Confusion()
const App::PropertyQuantityConstraint::Constraints diameterRange
    = {10 * Precision::Confusion(), std::numeric_limits<float>::max(), 1.0};

// Custom clearance can be negative or positive to adjust for manufacturing
const App::PropertyQuantityConstraint::Constraints clearanceRange
    = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 0.1};

Hole::Hole()
{
    addSubType = FeatureAddSub::Subtractive;

    readCutDefinitions();

    ADD_PROPERTY_TYPE(Threaded, (false), "Hole", App::Prop_None, "Threaded");

    ADD_PROPERTY_TYPE(ModelThread, (false), "Hole", App::Prop_None, "Model actual thread");

    ADD_PROPERTY_TYPE(CosmeticThread, (true), "Hole", App::Prop_None, "Texture the thread");

    ADD_PROPERTY_TYPE(ThreadType, (0L), "Hole", App::Prop_None, "Thread type");
    ThreadType.setEnums(ThreadTypeEnums);

    ADD_PROPERTY_TYPE(ThreadSize, (0L), "Hole", App::Prop_None, "Thread size");
    ThreadSize.setEnums(getThreadDesignations(ThreadType.getValue()));

    ADD_PROPERTY_TYPE(ThreadClass, (0L), "Hole", App::Prop_None, "Thread class");
    ThreadClass.setEnums(ThreadClass_None_Enums);

    ADD_PROPERTY_TYPE(ThreadFit, (0L), "Hole", App::Prop_None, "Clearance hole fit");
    ThreadFit.setEnums(ClearanceMetricEnums);

    ADD_PROPERTY_TYPE(Diameter, (6.0), "Hole", App::Prop_None, "Diameter");
    Diameter.setConstraints(&diameterRange);

    ADD_PROPERTY_TYPE(ThreadDiameter, (0.0), "Hole", App::Prop_None, "Thread major diameter");
    ThreadDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(ThreadDirection, (0L), "Hole", App::Prop_None, "Thread direction");
    ThreadDirection.setEnums(ThreadDirectionEnums);
    ThreadDirection.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutType, (0L), "Hole", App::Prop_None, "Head cut type");
    HoleCutType.setEnums(HoleCutType_None_Enums);

    ADD_PROPERTY_TYPE(HoleCutCustomValues, (false), "Hole", App::Prop_None, "Custom cut values");
    HoleCutCustomValues.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDiameter, (0.0), "Hole", App::Prop_None, "Head cut diameter");
    HoleCutDiameter.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutDepth, (0.0), "Hole", App::Prop_None, "Head cut depth");
    HoleCutDepth.setReadOnly(true);

    ADD_PROPERTY_TYPE(HoleCutCountersinkAngle, (90.0), "Hole", App::Prop_None, "Head cut countersink angle");
    HoleCutCountersinkAngle.setConstraints(&floatAngle);
    HoleCutCountersinkAngle.setReadOnly(true);

    ADD_PROPERTY_TYPE(DepthType, (0L), "Hole", App::Prop_None, "Type");
    DepthType.setEnums(DepthTypeEnums);

    ADD_PROPERTY_TYPE(Depth, (25.0), "Hole", App::Prop_None, "Length");

    ADD_PROPERTY_TYPE(DrillPoint, (1L), "Hole", App::Prop_None, "Drill point type");
    DrillPoint.setEnums(DrillPointEnums);

    ADD_PROPERTY_TYPE(DrillPointAngle, (118.0), "Hole", App::Prop_None, "Drill point angle");
    DrillPointAngle.setConstraints(&floatAngle);
    ADD_PROPERTY_TYPE(
        DrillForDepth,
        ((long)0),
        "Hole",
        App::Prop_None,
        "The size of the drill point will be taken into\n account for the depth of blind holes"
    );

    ADD_PROPERTY_TYPE(Tapered, (false), "Hole", App::Prop_None, "Tapered");

    ADD_PROPERTY_TYPE(TaperedAngle, (90.0), "Hole", App::Prop_None, "Tapered angle");
    TaperedAngle.setConstraints(&floatAngle);

    ADD_PROPERTY_TYPE(ThreadDepthType, (0L), "Hole", App::Prop_None, "Thread depth type");
    ThreadDepthType.setEnums(ThreadDepthTypeEnums);

    ADD_PROPERTY_TYPE(ThreadDepth, (23.5), "Hole", App::Prop_None, "Thread Length");  // default is
                                                                                      // assuming an M1

    ADD_PROPERTY_TYPE(
        UseCustomThreadClearance,
        (false),
        "Hole",
        App::Prop_None,
        "Use custom thread clearance"
    );

    ADD_PROPERTY_TYPE(
        CustomThreadClearance,
        (0.0),
        "Hole",
        App::Prop_None,
        "Custom thread clearance (overrides ThreadClass)"
    );
    CustomThreadClearance.setConstraints(&clearanceRange);

    // Defaults to circles & arcs so that older files are kept intact
    // while new file get points, circles and arcs set in setupObject()
    ADD_PROPERTY_TYPE(
        BaseProfileType,
        (BaseProfileTypeOptions::OnCirclesArcs),
        "Hole",
        App::Prop_None,
        "Which profile feature to base the holes on"
    );
}

void Hole::updateHoleCutParams()
{
    std::string holeCutTypeStr = HoleCutType.getValueAsString();

    // there is no cut, thus return
    if (holeCutTypeStr == "None") {
        return;
    }

    if (ThreadType.getValue() < 0) {
        throw Base::IndexError("Thread type out of range");
        return;
    }

    // get diameter and size
    double diameterVal = Diameter.getValue();

    // handle thread types
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
        if (ThreadSize.getValue() < 0) {
            throw Base::IndexError("Thread size out of range");
            return;
        }

        std::string threadSizeStr = ThreadSize.getValueAsString();

        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we take in this case the values from the norm ISO 4762 or ISO 10642
        if (holeCutTypeStr == "Counterbore") {
            // read ISO 4762 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 4762");
            const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                // there is no norm defining counterbores for all sizes, thus we need to use the
                // same fallback as for the case HoleCutTypeMap.count(key)
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                    HoleCutDepth.setValue(dimen.depth);
                }
                else {
                    calculateAndSetCounterbore();
                }
            }
            if (HoleCutDepth.getValue() == 0.0) {
                HoleCutDepth.setValue(dimen.depth);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink" || holeCutTypeStr == "Counterdrill") {
            // read ISO 10642 values
            const CutDimensionSet& counter = find_cutDimensionSet(threadTypeStr, "ISO 10642");
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                HoleCutCountersinkAngle.setValue(counter.angle);
                if (dimen.diameter != 0.0) {
                    HoleCutDiameter.setValue(dimen.diameter);
                }
                else {
                    calculateAndSetCountersink();
                }
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(counter.angle);
            }
            if (HoleCutDepth.getValue() == 0.0) {
                if (holeCutTypeStr == "Counterdrill") {
                    HoleCutDepth.setValue(1.0);
                }
                else {
                    ProfileBased::onChanged(&HoleCutDiameter);
                }
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }

        // Tag: MIGRATION
        // handle since FreeCAD 0.18 deprecated types that were
        // removed after FreeCAD 0.20
        if (holeCutTypeStr == "Cheesehead (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.6);
            HoleCutDepth.setValue(diameterVal * 0.6);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink socket screw (deprecated)") {
            HoleCutType.setValue("Countersink");
            holeCutTypeStr = "Countersink";
            HoleCutDiameter.setValue(diameterVal * 2.0);
            HoleCutDepth.setValue(diameterVal * 0.0);
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(90.0);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Cap screw (deprecated)") {
            HoleCutType.setValue("Counterbore");
            holeCutTypeStr = "Counterbore";
            HoleCutDiameter.setValue(diameterVal * 1.5);
            HoleCutDepth.setValue(diameterVal * 1.25);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }

        // cut definition
        CutDimensionKey key {threadTypeStr, holeCutTypeStr};
        if (HoleCutTypeMap.count(key)) {
            const CutDimensionSet& counter = find_cutDimensionSet(key);
            if (counter.cut_type == CutDimensionSet::Counterbore) {
                // disable HoleCutCountersinkAngle and reset it to ISO's default
                HoleCutCountersinkAngle.setValue(90.0);
                HoleCutCountersinkAngle.setReadOnly(true);
                const CounterBoreDimension& dimen = counter.get_bore(threadSizeStr);
                if (dimen.thread == "None") {
                    calculateAndSetCounterbore();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of
                    // updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and
                    // read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDepth.setValue(dimen.depth);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly()) {
                            HoleCutCustomValues.setValue(false);
                        }
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
            else if (counter.cut_type == CutDimensionSet::Countersink) {
                const CounterSinkDimension& dimen = counter.get_sink(threadSizeStr);
                if (dimen.thread == "None") {
                    // there might be an angle of zero (if no norm exists for the size)
                    if (HoleCutCountersinkAngle.getValue() == 0.0) {
                        HoleCutCountersinkAngle.setValue(counter.angle);
                    }
                    calculateAndSetCountersink();
                    // we force custom values since there are no normed ones
                    HoleCutCustomValues.setReadOnly(true);
                    // important to set only if not already true, to avoid loop call of
                    // updateHoleCutParams()
                    if (!HoleCutCustomValues.getValue()) {
                        HoleCutCustomValues.setValue(true);
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                }
                else {
                    // set normed values if not overwritten or if previously there
                    // were no normed values available and thus HoleCutCustomValues is checked and
                    // read-only
                    if (!HoleCutCustomValues.getValue()
                        || (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly())) {
                        HoleCutDiameter.setValue(dimen.diameter);
                        HoleCutDiameter.setReadOnly(true);
                        HoleCutDepth.setReadOnly(true);
                        HoleCutCountersinkAngle.setValue(counter.angle);
                        HoleCutCountersinkAngle.setReadOnly(true);
                        if (HoleCutCustomValues.getValue() && HoleCutCustomValues.isReadOnly()) {
                            HoleCutCustomValues.setValue(false);
                        }
                    }
                    else {
                        HoleCutDiameter.setReadOnly(false);
                        HoleCutDepth.setReadOnly(false);
                        HoleCutCountersinkAngle.setReadOnly(false);
                    }
                    HoleCutCustomValues.setReadOnly(false);
                }
            }
        }
    }
    else {
        // we don't update for these settings but we need to set a value for new holes
        // furthermore we must assure the hole cut diameter is not <= the hole diameter
        // if we have a cut but the values are zero, we assume it is a new hole
        // we use rules of thumbs as proposal
        if (holeCutTypeStr == "Counterbore") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.6);
                HoleCutDepth.setValue(diameterVal * 0.9);
            }
            if (HoleCutDepth.getValue() == 0.0) {
                HoleCutDepth.setValue(diameterVal * 0.9);
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
        }
        else if (holeCutTypeStr == "Countersink" || holeCutTypeStr == "Counterdrill") {
            if (HoleCutDiameter.getValue() == 0.0 || HoleCutDiameter.getValue() <= diameterVal) {
                HoleCutDiameter.setValue(diameterVal * 1.7);
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutCountersinkAngle.getValue() == 0.0) {
                HoleCutCountersinkAngle.setValue(getCountersinkAngle());
            }
            if (HoleCutDepth.getValue() == 0.0) {
                if (holeCutTypeStr == "Counterdrill") {
                    HoleCutDepth.setValue(1.0);
                }
                else {
                    ProfileBased::onChanged(&HoleCutDiameter);
                }
            }
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
    }
}

double Hole::getCountersinkAngle() const
{
    std::string threadTypeStr = ThreadType.getValueAsString();
    if (threadTypeStr == "BSW" || threadTypeStr == "BSF") {
        return 100.0;
    }
    if (threadTypeStr == "UNC" || threadTypeStr == "UNF" || threadTypeStr == "UNEF") {
        return 82.0;
    }
    return 90.0;
}

double Hole::getThreadClassClearance() const
{
    double pitch = getThreadPitch();

    // Calculate how much clearance to add based on Thread tolerance class and pitch
    if (ThreadClass.getValueAsString()[1] == 'G') {
        for (auto it : ThreadClass_ISOmetric_data) {
            double p = it[0];
            if (pitch <= p) {
                return it[1];
            }
        }
    }

    return 0.0;
}

// Calculates the distance between the bottom hole and the bottom most thread.
// This is defined in DIN 76-1, there are 3 possibilities:
// mode=1 (default), For normal cases e1 is wanted.
// mode=2, In cases where shorter thread runout is necessary
// mode=3, In cases where longer thread runout is necessary
double Hole::getThreadRunout(int mode) const
{
    double pitch = getThreadPitch();

    double sf = 1.0;  // scale factor
    switch (mode) {
        case 1:
            sf = 1.0;
            break;
        case 2:
            sf = 0.625;
            break;
        case 3:
            sf = 1.6;
            break;
        default:
            throw Base::ValueError("Unsupported argument");
    }
    for (auto it : ThreadRunout) {
        double p = it[0];
        if (pitch <= p) {
            return sf * it[1];
        }
    }

    // For non-standard pitch we fall back on general engineering rule of thumb of 4*pitch.
    return 4 * pitch;
}

double Hole::getThreadPitch() const
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        throw Base::IndexError("Thread type out of range");
    }
    if (threadSize < 0) {
        throw Base::IndexError("Thread size out of range");
    }
    return threadDescription[threadType][threadSize].pitch;
}

void Hole::updateThreadDepthParam()
{
    std::string ThreadMethod(ThreadDepthType.getValueAsString());
    std::string HoleDepth(DepthType.getValueAsString());
    if (HoleDepth == "Dimension") {
        if (ThreadMethod == "Hole Depth") {
            ThreadDepth.setValue(Depth.getValue());
        }
        else if (ThreadMethod == "Dimension") {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > Depth.getValue()) {
                ThreadDepth.setValue(Depth.getValue());
            }
            else {
                ThreadDepth.setValue(ThreadDepth.getValue());
            }
        }
        else if (ThreadMethod == "Tapped (DIN76)") {
            ThreadDepth.setValue(Depth.getValue() - getThreadRunout());
        }
        else {
            throw Base::RuntimeError("Unsupported thread depth type \n");
        }
    }
    else if (HoleDepth == "ThroughAll") {
        if (ThreadMethod != "Dimension") {
            ThreadDepth.setValue(getThroughAllLength());
        }
        else {
            // the thread cannot be longer than the hole depth
            if (ThreadDepth.getValue() > getThroughAllLength()) {
                ThreadDepth.setValue(getThroughAllLength());
            }
            else {
                ThreadDepth.setValue(ThreadDepth.getValue());
            }
        }
    }
    else {
        throw Base::RuntimeError("Unsupported depth type \n");
    }
}

std::optional<double> Hole::determineDiameter() const
{
    // Diameter parameter depends on Threaded, ThreadType, ThreadSize, and ThreadFit

    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType < 0) {
        // When restoring the feature it might be in an inconsistent state.
        // So, just silently ignore it instead of throwing an exception.
        if (isRestoring()) {
            return std::nullopt;
        }
        throw Base::IndexError("Thread type out of range");
    }
    if (threadSize < 0) {
        // When restoring the feature it might be in an inconsistent state.
        // So, just silently ignore it instead of throwing an exception.
        if (isRestoring()) {
            return std::nullopt;
        }
        throw Base::IndexError("Thread size out of range");
    }

    if (threadType == 0) {
        return std::nullopt;
    }

    double diameter = threadDescription[threadType][threadSize].diameter;
    double pitch = threadDescription[threadType][threadSize].pitch;
    double clearance = 0.0;

    if (Threaded.getValue()) {

        if (ModelThread.getValue()) {
            if (UseCustomThreadClearance.getValue()) {
                clearance = CustomThreadClearance.getValue();
            }
            else {
                clearance = getThreadClassClearance();
            }
        }

        // use normed diameters if possible
        std::string threadTypeStr = ThreadType.getValueAsString();
        if (threadDescription[threadType][threadSize].TapDrill > 0) {
            diameter = threadDescription[threadType][threadSize].TapDrill + clearance;
        }  // if nothing is available, we must calculate
        else if (threadTypeStr == "BSP" || threadTypeStr == "BSW" || threadTypeStr == "BSF") {
            double thread = 2 * (0.640327 * pitch);
            // truncation is allowed by ISO-228 and BS 84
            diameter = diameter - thread * 0.75 + clearance;
        }
        else if (threadTypeStr == "NPT") {
            double thread = 2 * (0.8 * pitch);
            diameter = diameter - thread * 0.75 + clearance;
        }
        else {
            // this fits exactly the definition for ISO metric fine
            diameter = diameter - pitch + clearance;
        }
    }
    else {  // we have a clearance hole
        bool found = false;
        std::string threadTypeStr = ThreadType.getValueAsString();
        // UTS and metric have a different clearance hole set
        if (threadTypeStr == "ISOMetricProfile" || threadTypeStr == "ISOMetricFineProfile") {
            int MatrixRowSizeMetric = sizeof(metricHoleDiameters) / sizeof(metricHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
                case 0: /* standard fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][2];
                            found = true;
                            break;
                        }
                    }
                    // if nothing is defined (e.g. for M2.2, M9 and M11), we must calculate
                    // we use the factors defined for M5 in the metricHoleDiameters list
                    if (!found) {
                        diameter = diameter * 1.10;
                    }
                    break;
                case 1: /* close fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][1];
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.06;
                    }
                    break;
                case 2: /* wide fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeMetric; i++) {
                        if (metricHoleDiameters[i][0] == diameter) {
                            diameter = metricHoleDiameters[i][3];
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.16;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
        else if (threadTypeStr == "UNC" || threadTypeStr == "UNF" || threadTypeStr == "UNEF") {
            std::string ThreadSizeString = ThreadSize.getValueAsString();
            int MatrixRowSizeUTS = sizeof(UTSHoleDiameters) / sizeof(UTSHoleDiameters[0]);
            switch (ThreadFit.getValue()) {
                case 0: /* normal fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].normal;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found (if "#12" or "9/16"), we must calculate
                    // // we use the factors defined for "3/8" in the UTSHoleDiameters list
                    if (!found) {
                        diameter = diameter * 1.08;
                    }
                    break;
                case 1: /* close fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].close;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.04;
                    }
                    break;
                case 2: /* loose fit */
                    // read diameter out of matrix
                    for (int i = 0; i < MatrixRowSizeUTS; i++) {
                        if (UTSHoleDiameters[i].designation == ThreadSizeString) {
                            diameter = UTSHoleDiameters[i].loose;
                            found = true;
                            break;
                        }
                    }
                    // if nothing was found, we must calculate
                    if (!found) {
                        diameter = diameter * 1.12;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
        else {
            switch (ThreadFit.getValue()) {
                case 0: /* normal fit */
                    // we must calculate
                    if (!found) {
                        diameter = diameter * 1.1;
                    }
                    break;
                case 1: /* close fit */
                    if (!found) {
                        diameter = diameter * 1.05;
                    }
                    break;
                case 2: /* loose fit */
                    if (!found) {
                        diameter = diameter * 1.15;
                    }
                    break;
                default:
                    throw Base::IndexError("Thread fit out of range");
            }
        }
    }

    return std::optional<double> {diameter};
}

void Hole::updateDiameterParam()
{
    int threadType = ThreadType.getValue();
    int threadSize = ThreadSize.getValue();
    if (threadType > 0 && threadSize > 0) {
        ThreadDiameter.setValue(threadDescription[threadType][threadSize].diameter);
    }
    if (auto opt = determineDiameter()) {
        Diameter.setValue(opt.value());
    }
}

double Hole::getThreadProfileAngle()
{
    // Both ISO 7-1 and ASME B1.20.1 define the same angle
    return 90 - 1.79;
}

void Hole::findClosestDesignation()
{
    int threadType = ThreadType.getValue();
    const int numTypes = static_cast<int>(std::size(threadDescription));

    if (threadType < 0 || threadType >= numTypes) {
        throw Base::IndexError(QT_TRANSLATE_NOOP("Exception", "Thread type is invalid"));
    }

    double diameter = ThreadDiameter.getValue();
    if (diameter == 0.0) {
        diameter = Diameter.getValue();
    }

    int oldSizeIndex = ThreadSize.getValue();
    const auto& options = threadDescription[threadType];
    double targetPitch = 0.0;
    if (oldSizeIndex >= 0 && oldSizeIndex < static_cast<int>(options.size())) {
        targetPitch = options[oldSizeIndex].pitch;
    }
    size_t bestIndex = 0;
    if (targetPitch == 0.0) {
        // If pitch is unknown, prioritize the closest diameter
        double bestDiameterDiff = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < options.size(); ++i) {
            double dDiff = std::abs(options[i].diameter - diameter);
            if (dDiff < bestDiameterDiff) {
                bestDiameterDiff = dDiff;
                bestIndex = i;
            }
        }
    }
    else {
        // Scan all entries to find the minimal (Δdiameter, Δpitch) Euclidean distance
        double bestMetric = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < options.size(); ++i) {
            double dDiff = options[i].diameter - diameter;
            double pDiff = options[i].pitch - targetPitch;
            double metric = std::hypot(dDiff, pDiff);
            if (metric < bestMetric) {
                bestMetric = metric;
                bestIndex = i;
            }
        }
    }

    ThreadSize.setValue(static_cast<int>(bestIndex));
}

void Hole::onChanged(const App::Property* prop)
{
    if (prop == &ThreadType) {
        std::string type;

        if (ThreadType.isValid()) {
            type = ThreadType.getValueAsString();
            ThreadSize.setEnums(getThreadDesignations(ThreadType.getValue()));
            if (type != "None") {
                findClosestDesignation();
            }
        }

        if (type == "None") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
            Threaded.setValue(false);
            ModelThread.setValue(false);
            UseCustomThreadClearance.setValue(false);
            ThreadFit.setEnums(ClearanceNoneEnums);
        }
        else if (type == "ISOMetricProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetric_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetric_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "ISOMetricFineProfile") {
            ThreadClass.setEnums(ThreadClass_ISOmetricfine_Enums);
            HoleCutType.setEnums(HoleCutType_ISOmetricfine_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "UNC") {
            ThreadClass.setEnums(ThreadClass_UNC_Enums);
            HoleCutType.setEnums(HoleCutType_UNC_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "UNF") {
            ThreadClass.setEnums(ThreadClass_UNF_Enums);
            HoleCutType.setEnums(HoleCutType_UNF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "UNEF") {
            ThreadClass.setEnums(ThreadClass_UNEF_Enums);
            HoleCutType.setEnums(HoleCutType_UNEF_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "BSP") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_BSP_Enums);
            ThreadFit.setEnums(ClearanceMetricEnums);
        }
        else if (type == "NPT") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_NPT_Enums);
            ThreadFit.setEnums(ClearanceUTSEnums);
        }
        else if (type == "BSW") {
            ThreadClass.setEnums(ThreadClass_BSW_Enums);
            HoleCutType.setEnums(HoleCutType_BSW_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
        }
        else if (type == "BSF") {
            ThreadClass.setEnums(ThreadClass_BSF_Enums);
            HoleCutType.setEnums(HoleCutType_BSF_Enums);
            ThreadFit.setEnums(ClearanceOtherEnums);
        }
        else if (type == "ISOTyre") {
            ThreadClass.setEnums(ThreadClass_None_Enums);
            HoleCutType.setEnums(HoleCutType_None_Enums);
        }

        bool isNone = type == "None";
        bool isThreaded = Threaded.getValue();

        Diameter.setReadOnly(!isNone);
        Threaded.setReadOnly(isNone);
        ThreadSize.setReadOnly(isNone);
        ThreadFit.setReadOnly(isNone || isThreaded);
        ThreadClass.setReadOnly(isNone || !isThreaded);
        ThreadDepthType.setReadOnly(isNone || !isThreaded);
        ThreadDepth.setReadOnly(isNone || !isThreaded);
        ModelThread.setReadOnly(isNone || !isThreaded);
        UseCustomThreadClearance.setReadOnly(isNone || !isThreaded || !ModelThread.getValue());
        CustomThreadClearance.setReadOnly(
            !UseCustomThreadClearance.getValue() || UseCustomThreadClearance.isReadOnly()
        );

        std::string holeCutTypeStr;
        if (HoleCutType.isValid()) {
            std::string holeCutTypeStr = HoleCutType.getValueAsString();
        }
        if (holeCutTypeStr == "None") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(true);
            HoleCutDepth.setReadOnly(true);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Counterbore") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(true);
        }
        else if (holeCutTypeStr == "Countersink") {
            HoleCutCustomValues.setReadOnly(true);
            HoleCutDiameter.setReadOnly(false);
            HoleCutDepth.setReadOnly(false);
            HoleCutCountersinkAngle.setReadOnly(false);
        }
        else {  // screw definition
            HoleCutCustomValues.setReadOnly(false);
            if (HoleCutCustomValues.getValue()) {
                HoleCutDiameter.setReadOnly(false);
                HoleCutDepth.setReadOnly(false);
                // we must not set HoleCutCountersinkAngle here because the info if this can
                // be enabled is first available in updateHoleCutParams and thus handled there
                updateHoleCutParams();
            }
            else {
                HoleCutDiameter.setReadOnly(true);
                HoleCutDepth.setReadOnly(true);
                HoleCutCountersinkAngle.setReadOnly(true);
            }
        }

        // Signal changes to these
        ProfileBased::onChanged(&ThreadSize);
        ProfileBased::onChanged(&ThreadClass);
        ProfileBased::onChanged(&HoleCutType);
        ProfileBased::onChanged(&Threaded);

        // Diameter parameter depends on this
        if (type != "None") {
            updateDiameterParam();
        }
    }
    else if (prop == &Threaded) {
        std::string type(ThreadType.getValueAsString());

        // thread class and direction are only sensible if threaded
        // fit only sensible if not threaded
        if (Threaded.getValue()) {
            CosmeticThread.setReadOnly(false);
            ThreadClass.setReadOnly(false);
            ThreadDirection.setReadOnly(false);
            ThreadFit.setReadOnly(true);
            ModelThread.setReadOnly(false);
            UseCustomThreadClearance.setReadOnly(false);
            CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
            ThreadDepthType.setReadOnly(false);
            ThreadDepth.setReadOnly(std::string(ThreadDepthType.getValueAsString()) != "Dimension");
            if (Tapered.getValue() && TaperedAngle.getValue() == 90) {
                TaperedAngle.setValue(getThreadProfileAngle());
            }
        }
        else {
            CosmeticThread.setValue(false);
            CosmeticThread.setReadOnly(true);
            ThreadClass.setReadOnly(true);
            ThreadDirection.setReadOnly(true);
            if (type == "None") {
                ThreadFit.setReadOnly(true);
            }
            else {
                ThreadFit.setReadOnly(false);
            }
            ModelThread.setReadOnly(true);
            UseCustomThreadClearance.setReadOnly(true);
            CustomThreadClearance.setReadOnly(true);
            ThreadDepthType.setReadOnly(true);
            ThreadDepth.setReadOnly(true);
        }

        // Diameter parameter depends on this
        updateDiameterParam();
    }
    else if (prop == &ModelThread) {
        // Diameter parameter depends on this
        updateDiameterParam();
        UseCustomThreadClearance.setReadOnly(!ModelThread.getValue());
        if (CosmeticThread.getValue() && ModelThread.getValue()) {
            CosmeticThread.setValue(false);
        }
    }
    else if (prop == &CosmeticThread) {
        if (CosmeticThread.getValue() && ModelThread.getValue()) {
            ModelThread.setValue(false);
        }
    }
    else if (prop == &DrillPoint) {
        if (DrillPoint.getValue() == 1) {
            DrillPointAngle.setReadOnly(false);
            DrillForDepth.setReadOnly(false);
        }
        else {
            DrillPointAngle.setReadOnly(true);
            DrillForDepth.setReadOnly(true);
        }
    }
    else if (prop == &Tapered) {
        if (Tapered.getValue()) {
            TaperedAngle.setReadOnly(false);
            if (Threaded.getValue() && TaperedAngle.getValue() == 90) {
                TaperedAngle.setValue(getThreadProfileAngle());
            }
        }
        else {
            TaperedAngle.setValue(90);
            TaperedAngle.setReadOnly(true);
        }
    }
    else if (prop == &ThreadSize) {
        updateDiameterParam();
        if (!isRestoring()) {
            updateThreadDepthParam();
        }
        // updateHoleCutParams() will later automatically be called because
    }
    else if (prop == &ThreadFit) {
        updateDiameterParam();
    }
    else if (prop == &Diameter) {
        // a changed diameter means we also need to check the hole cut
        // because the hole cut diameter must not be <= than the diameter
        updateHoleCutParams();
        if (ThreadType.getValue() == 0) {
            // Profile is None but this is needed to find the closest
            // designation if the user switch to threaded
            ThreadDiameter.setValue(Diameter.getValue());
        }
    }
    else if (prop == &HoleCutType) {
        // the read-only states are set in updateHoleCutParams()
        updateHoleCutParams();
        ProfileBased::onChanged(&HoleCutDiameter);
        ProfileBased::onChanged(&HoleCutDepth);
        ProfileBased::onChanged(&HoleCutCountersinkAngle);
    }
    else if (prop == &HoleCutCustomValues) {
        // when going back to standardized values, we must recalculate
        // also to find out if HoleCutCountersinkAngle can be ReadOnly
        // both an also the read-only states is done in updateHoleCutParams()
        updateHoleCutParams();
    }
    else if (prop == &HoleCutDiameter || prop == &HoleCutCountersinkAngle) {
        // Recalculate depth if Countersink
        const std::string holeCutTypeString = HoleCutType.getValueAsString();
        const std::string threadTypeString = ThreadType.getValueAsString();
        if (!(holeCutTypeString == "Countersink"
              || isDynamicCountersink(threadTypeString, holeCutTypeString))) {
            return;
        }
        auto angle = Base::toRadians(HoleCutCountersinkAngle.getValue());
        constexpr double fallback = 2.0;
        constexpr double EPSILON = 1e-6;
        if (angle <= 0.0 || angle >= std::numbers::pi) {
            HoleCutDepth.setValue(fallback);
        }
        else {
            double tanHalfAngle = std::tan(angle / 2.0);
            if (std::abs(tanHalfAngle) < EPSILON) {
                // Avoid near-zero division
                HoleCutDepth.setValue(fallback);
            }
            else {
                double diameter = HoleCutDiameter.getValue();
                HoleCutDepth.setValue((diameter / 2.0) / tanHalfAngle);
            }
        }
        ProfileBased::onChanged(&HoleCutDepth);
    }
    else if (prop == &DepthType) {
        std::string DepthMode(DepthType.getValueAsString());
        bool isNotDimension = (DepthMode != "Dimension");

        Depth.setReadOnly(isNotDimension);
        DrillPoint.setReadOnly(isNotDimension);
        DrillPointAngle.setReadOnly(isNotDimension);
        DrillForDepth.setReadOnly(isNotDimension);

        if (!isRestoring()) {
            if (isNotDimension) {
                // if through all, set the depth accordingly
                Depth.setValue(getThroughAllLength());
            }
            updateThreadDepthParam();
        }
    }
    else if (prop == &Depth) {
        if (!isRestoring()) {
            // the depth cannot be greater than the through-all length
            if (Depth.getValue() > getThroughAllLength()) {
                Depth.setValue(getThroughAllLength());
            }
        }

        if (std::string(ThreadDepthType.getValueAsString()) != "Dimension") {
            updateDiameterParam();  // make sure diameter and pitch are updated.
        }

        if (!isRestoring()) {
            updateThreadDepthParam();
        }
    }
    else if (prop == &ThreadDepthType) {
        if (!isRestoring()) {
            updateThreadDepthParam();
        }
        ThreadDepth.setReadOnly(
            Threaded.getValue() && std::string(ThreadDepthType.getValueAsString()) != "Dimension"
        );
    }
    else if (prop == &ThreadDepth) {
        // the thread depth cannot be greater than the hole depth
        if (ThreadDepth.getValue() > Depth.getValue()) {
            ThreadDepth.setValue(Depth.getValue());
        }
    }
    else if (prop == &UseCustomThreadClearance) {
        updateDiameterParam();
        CustomThreadClearance.setReadOnly(!UseCustomThreadClearance.getValue());
    }
    else if (prop == &CustomThreadClearance) {
        updateDiameterParam();
    }

    ProfileBased::onChanged(prop);
}
void Hole::setupObject()
{
    // Set the BaseProfileType to the user defined value
    // here so that new objects use points, but older files
    // keep the default value of "Circles and Arcs"

    Base::Reference<ParameterGrp> hGrp = App::GetApplication()
                                             .GetUserParameter()
                                             .GetGroup("BaseApp")
                                             ->GetGroup("Preferences")
                                             ->GetGroup("Mod/PartDesign");

    BaseProfileType.setValue(baseProfileOption_idxToBitmask(hGrp->GetInt("defaultBaseTypeHole", 1)));

    ProfileBased::setupObject();
}

/**
 * Computes 2D intersection between the lines (pa1, pa2) and (pb1, pb2).
 * The lines are assumed to be crossing, and it is an error
 * to specify parallel lines.
 * Only the x and y coordinates of the points are used to compute the 2D intersection.
 *
 * The result are the x and y coordinate of the intersection point.
 */
static void computeIntersection(gp_Pnt pa1, gp_Pnt pa2, gp_Pnt pb1, gp_Pnt pb2, double& x, double& y)
{
    double vx1 = pa1.X() - pa2.X();
    double vy1 = pa1.Y() - pa2.Y();
    double vx2 = pb1.X() - pb2.X();
    double vy2 = pb1.Y() - pb2.Y();
    double x1 = pa1.X();
    double y1 = pa1.Y();
    double x2 = pb1.X();
    double y2 = pb1.Y();

    /* Solve the system
        x1 + t1 * vx1 = x2 + t2 * vx2
        y1 + t1 * vy1 = y2 + t2 * vy2

        =>

       [ vx1    -vx2 ] [ t1 ] = [ x2 - x1 ]
       [ vy1    -vy2 ] [ t2 ] = [ y2 - y1 ]

       =>

       [ t1 ] = f * [ -vy2  vx2 ] [ x2 - x1 ]
       [ t2 ] =     [ -vy1  vx1 ] [ y2 - y1 ]

     */

    assert(((vx1 * -vy2) - (-vx2 * vy1)) != 0);

    double f = 1 / ((vx1 * -vy2) - (-vx2 * vy1));

    double t1 = -vy2 * f * (x2 - x1) + vx2 * f * (y2 - y1);

#ifdef _DEBUG
    double t2 = -vy1 * f * (x2 - x1) + vx1 * f * (y2 - y1);

    assert((x1 + t1 * vx1) - (x2 + t2 * vx2) < 1e-6);
    assert((y1 + t1 * vy1) - (y2 + t2 * vy2) < 1e-6);
#endif

    x = x1 + t1 * vx1;
    y = y1 + t1 * vy1;
}

short Hole::mustExecute() const
{
    if (ThreadType.isTouched() || Threaded.isTouched() || ThreadSize.isTouched()
        || ThreadClass.isTouched() || ThreadFit.isTouched() || Diameter.isTouched()
        || ThreadDirection.isTouched() || HoleCutType.isTouched() || HoleCutDiameter.isTouched()
        || HoleCutDepth.isTouched() || HoleCutCountersinkAngle.isTouched() || DepthType.isTouched()
        || Depth.isTouched() || DrillPoint.isTouched() || DrillPointAngle.isTouched()
        || Tapered.isTouched() || TaperedAngle.isTouched() || ModelThread.isTouched()
        || UseCustomThreadClearance.isTouched() || CustomThreadClearance.isTouched()
        || ThreadDepthType.isTouched() || ThreadDepth.isTouched() || BaseProfileType.isTouched()) {
        return 1;
    }
    return ProfileBased::mustExecute();
}

void Hole::Restore(Base::XMLReader& reader)
{
    ProfileBased::Restore(reader);
    updateProps();
}

void Hole::updateProps()
{
    onChanged(&Threaded);
    onChanged(&ThreadType);
    onChanged(&ThreadSize);
    onChanged(&ThreadClass);
    onChanged(&ThreadFit);
    onChanged(&Diameter);
    onChanged(&ThreadDirection);
    onChanged(&HoleCutType);
    onChanged(&HoleCutDiameter);
    onChanged(&HoleCutDepth);
    onChanged(&HoleCutCountersinkAngle);
    onChanged(&DepthType);
    onChanged(&Depth);
    onChanged(&DrillPoint);
    onChanged(&DrillPointAngle);
    onChanged(&Tapered);
    onChanged(&TaperedAngle);
    onChanged(&ModelThread);
    onChanged(&UseCustomThreadClearance);
    onChanged(&CustomThreadClearance);
    onChanged(&ThreadDepthType);
    onChanged(&ThreadDepth);
    onChanged(&BaseProfileType);
}

static gp_Pnt toPnt(gp_Vec dir)
{
    return {dir.X(), dir.Y(), dir.Z()};
}

App::DocumentObjectExecReturn* Hole::execute()
{
    TopoShape profileshape = getProfileShape(
        Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform | Part::ShapeOption::DontSimplifyCompound
    );

    // Find the base shape
    TopoShape base;
    try {
        base = getBaseTopoShape();
    }
    catch (const Base::Exception&) {
        std::string text(QT_TRANSLATE_NOOP(
            "Exception",
            "The requested feature cannot be created. The reason may be that:\n"
            "  - the active Body does not contain a base shape, so there is no\n"
            "  material to be removed;\n"
            "  - the selected sketch does not belong to the active Body."
        ));
        return new App::DocumentObjectExecReturn(text);
    }

    try {
        if (Diameter.getValue() < diameterRange.LowerBound) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Diameter too small")
            );
        }
        std::string method(DepthType.getValueAsString());
        double length = 0.0;

        /* Build the prototype hole */

        // Get vector normal to profile
        Base::Vector3d SketchVector = guessNormalDirection(profileshape);

        if (Reversed.getValue()) {
            SketchVector *= -1.0;
        }

        // Define this as zDir
        gp_Vec zDir(SketchVector.x, SketchVector.y, SketchVector.z);
        gp_Vec xDir = computePerpendicular(zDir);

        if (method == "Dimension") {
            length = Depth.getValue();
        }
        else if (method == "UpToFirst") {
            /* TODO */
        }
        else if (method == "ThroughAll") {
            length = getThroughAllLength();
        }
        else {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Unsupported length specification")
            );
        }

        if (length <= 0.0) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid hole depth")
            );
        }

        BRepBuilderAPI_MakeWire mkWire;
        const std::string holeCutType = HoleCutType.getValueAsString();
        const std::string threadType = ThreadType.getValueAsString();
        bool isCountersink
            = (holeCutType == "Countersink" || isDynamicCountersink(threadType, holeCutType));
        bool isCounterbore
            = (holeCutType == "Counterbore" || isDynamicCounterbore(threadType, holeCutType));
        bool isCounterdrill = (holeCutType == "Counterdrill");

        double TaperedAngleVal = Tapered.getValue() ? Base::toRadians(TaperedAngle.getValue())
                                                    : Base::toRadians(90.0);
        double radiusBottom = Diameter.getValue() / 2.0 - length / tan(TaperedAngleVal);

        double radius = Diameter.getValue() / 2.0;
        gp_Pnt firstPoint(0, 0, 0);
        gp_Pnt lastPoint(0, 0, 0);
        double lengthCounter = 0.0;
        double xPosCounter = 0.0;
        double zPosCounter = 0.0;

        if (TaperedAngleVal <= 0.0 || TaperedAngleVal > Base::toRadians(180.0)) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid taper angle")
            );
        }

        if (isCountersink || isCounterbore || isCounterdrill) {
            double holeCutRadius = HoleCutDiameter.getValue() / 2.0;
            double holeCutDepth = HoleCutDepth.getValue();
            double countersinkAngle = Base::toRadians(HoleCutCountersinkAngle.getValue() / 2.0);

            if (isCounterbore) {
                // Counterbore is rendered the same way as a countersink, but with a hardcoded
                // angle of 90deg
                countersinkAngle = Base::toRadians(90.0);
            }

            if (isCountersink) {
                holeCutDepth = 0.0;
                // We cannot recalculate the HoleCutDiameter because the previous HoleCutDepth
                // is unknown. Therefore we cannot know with what HoleCutDepth the current
                // HoleCutDiameter was calculated.
            }

            if (holeCutRadius < radius) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Hole cut diameter too small")
                );
            }

            if (holeCutDepth > length) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                    "Exception",
                    "Hole error: Hole cut depth must be less than hole depth"
                ));
            }

            if (holeCutDepth < 0.0) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                    "Exception",
                    "Hole error: Hole cut depth must be greater or equal to zero"
                ));
            }

            // Top point
            gp_Pnt newPoint = toPnt(holeCutRadius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            // Bottom of counterbore
            if (holeCutDepth > 0.0) {
                newPoint = toPnt(holeCutRadius * xDir - holeCutDepth * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }

            // Compute intersection of tapered edge and line at bottom of counterbore hole
            computeIntersection(
                gp_Pnt(holeCutRadius, -holeCutDepth, 0),
                gp_Pnt(holeCutRadius - sin(countersinkAngle), -cos(countersinkAngle) - holeCutDepth, 0),
                gp_Pnt(radius, 0, 0),
                gp_Pnt(radiusBottom, -length, 0),
                xPosCounter,
                zPosCounter
            );

            if (-length > zPosCounter) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid countersink")
                );
            }

            lengthCounter = zPosCounter;
            newPoint = toPnt(xPosCounter * xDir + zPosCounter * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else {
            gp_Pnt newPoint = toPnt(radius * xDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
            lengthCounter = 0.0;
        }

        std::string drillPoint = DrillPoint.getValueAsString();
        double xPosDrill = 0.0;
        double zPosDrill = 0.0;
        if (drillPoint == "Flat") {
            gp_Pnt newPoint = toPnt(radiusBottom * xDir + -length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;

            newPoint = toPnt(-length * zDir);
            mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
            lastPoint = newPoint;
        }
        else if (drillPoint == "Angled") {
            double drillPointAngle = Base::toRadians((180.0 - DrillPointAngle.getValue()) / 2.0);
            gp_Pnt newPoint;
            bool isDrillForDepth = DrillForDepth.getValue();

            // the angle is in any case > 0 and < 90 but nevertheless this safeguard:
            if (drillPointAngle <= 0.0 || drillPointAngle >= Base::toRadians(180.0)) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point angle")
                );
            }

            // if option to take drill point size into account
            // the next wire point is the intersection of the drill edge and the hole edge
            if (isDrillForDepth) {
                computeIntersection(
                    gp_Pnt(0, -length, 0),
                    gp_Pnt(radius, radius * tan(drillPointAngle) - length, 0),
                    gp_Pnt(radius, 0, 0),
                    gp_Pnt(radiusBottom, -length, 0),
                    xPosDrill,
                    zPosDrill
                );
                if (zPosDrill > 0 || zPosDrill >= lengthCounter) {
                    return new App::DocumentObjectExecReturn(
                        QT_TRANSLATE_NOOP("Exception", "Hole error: Invalid drill point")
                    );
                }

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                newPoint = toPnt(-length * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
            else {
                xPosDrill = radiusBottom;
                zPosDrill = -length;

                newPoint = toPnt(xPosDrill * xDir + zPosDrill * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;

                // the end point is the size of the drill tip
                newPoint = toPnt((-length - radius * tan(drillPointAngle)) * zDir);
                mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, newPoint));
                lastPoint = newPoint;
            }
        }

        mkWire.Add(BRepBuilderAPI_MakeEdge(lastPoint, firstPoint));

        TopoDS_Wire wire = mkWire.Wire();

        TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

        double angle = Base::toRadians<double>(360.0);
        BRepPrimAPI_MakeRevol RevolMaker(face, gp_Ax1(firstPoint, zDir), angle);
        if (!RevolMaker.IsDone()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Could not revolve sketch")
            );
        }

        TopoDS_Shape protoHole = RevolMaker.Shape();
        if (protoHole.IsNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Resulting shape is empty")
            );
        }

        // Make thread
        if (Threaded.getValue() && ModelThread.getValue()) {
            TopoDS_Shape protoThread = makeThread(xDir, zDir, length);

            TopoDS_Compound holeWithThread;
            holeWithThread.Nullify();

            BRep_Builder builder;
            builder.MakeCompound(holeWithThread);
            builder.Add(holeWithThread, protoHole);
            builder.Add(holeWithThread, protoThread);

            // we reuse the name protoHole (only now it is threaded)
            protoHole = holeWithThread;
        }
        std::vector<TopoShape> holes;
        auto compound = findHoles(holes, profileshape, protoHole);
        if (holes.empty()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Hole error: Finding axis failed")
            );
        }

        TopoShape result(0);

        // set the subtractive shape property for later usage in e.g. pattern
        this->AddSubShape.setValue(compound);

        if (base.isNull()) {
            Shape.setValue(compound);
            return App::DocumentObject::StdReturn;
        }

        // First try cutting with compound which will be faster as it is done in
        // parallel
        bool retry = true;
        const char* maker;
        switch (getAddSubType()) {
            case Additive:
                maker = Part::OpCodes::Fuse;
                break;
            default:
                maker = Part::OpCodes::Cut;
        }
        try {
            if (base.isNull()) {
                result = compound;
            }
            else {
                result.makeElementBoolean(
                    maker,
                    {base, compound},
                    getNameInDocument(),
                    Precision::Confusion()
                );
            }
            retry = false;
        }
        catch (Standard_Failure& e) {
            FC_WARN(
                getFullName() << ": boolean operation with compound failed ("
                              << e.GetMessageString() << "), retry…"
            );
        }
        catch (Base::Exception& e) {
            FC_WARN(
                getFullName() << ": boolean operation with compound failed (" << e.what() << "), retry…"
            );
        }

        if (retry) {
            int i = 0;
            for (auto& hole : holes) {
                ++i;
                try {
                    result.makeElementBoolean(
                        maker,
                        {base, hole},
                        getNameInDocument(),
                        Precision::Confusion()
                    );
                }
                catch (Standard_Failure&) {
                    std::string msg(
                        QT_TRANSLATE_NOOP("Exception", "Boolean operation failed on profile Edge")
                    );
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
                catch (Base::Exception& e) {
                    e.reportException();
                    std::string msg(
                        QT_TRANSLATE_NOOP("Exception", "Boolean operation failed on profile Edge")
                    );
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
                base = result;
                if (base.isNull()) {
                    std::string msg(QT_TRANSLATE_NOOP(
                        "Exception",
                        "Boolean operation produced non-solid on profile Edge"
                    ));
                    msg += std::to_string(i);
                    return new App::DocumentObjectExecReturn(msg.c_str());
                }
            }
            result = base;
        }
        result = refineShapeIfActive(result);

        this->Shape.setValue(result);

        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        if (std::string(e.GetMessageString()) == "TopoDS::Face"
            && (std::string(DepthType.getValueAsString()) == "UpToFirst"
                || std::string(DepthType.getValueAsString()) == "UpToFace")) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                "Exception",
                "Could not create face from sketch.\n"
                "Intersecting sketch entities or multiple faces in a sketch are not allowed "
                "for making a pocket up to a face."
            ));
        }
        else {
            return new App::DocumentObjectExecReturn(e.GetMessageString());
        }
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
}

Base::Vector3d Hole::guessNormalDirection(const TopoShape& profileshape) const
{
    // If trying to build a hole from a cylinder face
    // we must try to find the direction ourselves as
    // getProfileNormal() will try to find the normal to
    // the middle of the face
    if (profileshape.hasSubShape(TopAbs_FACE)) {
        BRepAdaptor_Surface sf(TopoDS::Face(profileshape.getSubShape(TopAbs_FACE, 1)));

        if (sf.GetType() == GeomAbs_Cylinder) {
            return Base::convertTo<Base::Vector3d>(sf.Cylinder().Axis().Direction());
        }
    }

    return getProfileNormal();
}

TopoShape Hole::findHoles(
    std::vector<TopoShape>& holes,
    const TopoShape& profileshape,
    const TopoDS_Shape& protoHole
) const
{
    TopoShape result(0);

    auto addHole = [&](Part::TopoShape const& baseshape, gp_Pnt loc) {
        gp_Trsf localSketchTransformation;
        localSketchTransformation.SetTranslation(gp_Pnt(0, 0, 0), gp_Pnt(loc.X(), loc.Y(), loc.Z()));

        Part::ShapeMapper mapper;
        mapper.populate(
            Part::MappingStatus::Modified,
            baseshape,
            TopoShape(protoHole).getSubTopoShapes(TopAbs_FACE)
        );

        TopoShape hole(-getID());
        hole.makeShapeWithElementMap(protoHole, mapper, {baseshape});

        // transform and generate element map.
        hole = hole.makeElementTransform(localSketchTransformation);
        holes.push_back(hole);
    };

    int baseProfileType = BaseProfileType.getValue();

    // Iterate over edges and filter out non-circle/non-arc types
    if (baseProfileType & BaseProfileTypeOptions::OnCircles
        || baseProfileType & BaseProfileTypeOptions::OnArcs) {
        for (const auto& profileEdge : profileshape.getSubTopoShapes(TopAbs_EDGE)) {
            TopoDS_Edge edge = TopoDS::Edge(profileEdge.getShape());
            BRepAdaptor_Curve adaptor(edge);

            // Circle base?
            if (adaptor.GetType() != GeomAbs_Circle) {
                continue;
            }
            // Filter for circles
            if (!(baseProfileType & BaseProfileTypeOptions::OnCircles) && adaptor.IsClosed()) {
                continue;
            }

            // Filter for arcs
            if (!(baseProfileType & BaseProfileTypeOptions::OnArcs) && !adaptor.IsClosed()) {
                continue;
            }

            gp_Circ circle = adaptor.Circle();
            addHole(profileEdge, circle.Axis().Location());
        }
    }

    // To avoid breaking older files which where not made with
    // holes on points
    if (baseProfileType & BaseProfileTypeOptions::OnPoints) {
        // Iterate over vertices while avoiding edges so that curve handles are ignored
        for (const auto& profileVertex : profileshape.getSubTopoShapes(TopAbs_VERTEX, TopAbs_EDGE)) {
            TopoDS_Vertex vertex = TopoDS::Vertex(profileVertex.getShape());

            addHole(profileVertex, BRep_Tool::Pnt(vertex));
        }
    }
    return TopoShape().makeElementCompound(holes);
}

int Hole::baseProfileOption_idxToBitmask(int index)
{
    // Translate combobox index to bitmask value
    // More options could be made available
    if (index == 0) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnCirclesArcs;
    }
    if (index == 1) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnPointsCirclesArcs;
    }
    if (index == 2) {
        return PartDesign::Hole::BaseProfileTypeOptions::OnPoints;
    }
    Base::Console().error("Unexpected hole base profile combobox index: %i", index);
    return 0;
}
int Hole::baseProfileOption_bitmaskToIdx(int bitmask)
{
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnCirclesArcs) {
        return 0;
    }
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnPointsCirclesArcs) {
        return 1;
    }
    if (bitmask == PartDesign::Hole::BaseProfileTypeOptions::OnPoints) {
        return 2;
    }

    Base::Console().error("Unexpected hole base profile bitmask: %i", bitmask);
    return -1;
}


}  // namespace PartDesign
