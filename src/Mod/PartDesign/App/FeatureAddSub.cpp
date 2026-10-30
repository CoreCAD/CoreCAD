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


#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>


#include <App/FeaturePythonPyImp.h>
#include <Mod/Part/App/modelRefine.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>

#include "FeatureAddSub.h"
#include "FeaturePy.h"

#include <Mod/Part/App/Tools.h>


using namespace PartDesign;

namespace PartDesign
{

extern bool getPDRefineModelParameter();

PROPERTY_SOURCE(PartDesign::FeatureAddSub, PartDesign::FeatureRefine)

FeatureAddSub::FeatureAddSub()
{
    ADD_PROPERTY(AddSubShape, (TopoDS_Shape()));
}

void FeatureAddSub::onChanged(const App::Property* property)
{
    Feature::onChanged(property);
}

bool FeatureAddSub::producesContentOf(const App::Property& prop) const
{
    // The tool shape is built from the profile on every recompute; storing it as source would make
    // each save differ by the kernel's rebuild alone.
    return &prop == &AddSubShape || PartDesign::Feature::producesContentOf(prop);
}

FeatureAddSub::Type FeatureAddSub::getAddSubType()
{
    return addSubType;
}

short FeatureAddSub::mustExecute() const
{
    if (Refine.isTouched()) {
        return 1;
    }
    return PartDesign::Feature::mustExecute();
}

void FeatureAddSub::getAddSubShape(Part::TopoShape& addShape, Part::TopoShape& subShape)
{
    if (addSubType == Additive) {
        addShape = AddSubShape.getShape();
    }
    else if (addSubType == Subtractive) {
        subShape = AddSubShape.getShape();
    }
}

Part::TopoShape FeatureAddSub::makeAddSubHalves(
    const Part::TopoShape& addShape,
    const Part::TopoShape& subShape
)
{
    const auto orEmpty = [](const Part::TopoShape& half) {
        if (!half.isNull()) {
            return half;
        }
        BRep_Builder builder;
        TopoDS_Compound empty;
        builder.MakeCompound(empty);
        return Part::TopoShape(empty);
    };
    return Part::TopoShape().makeElementCompound({orEmpty(addShape), orEmpty(subShape)});
}

void FeatureAddSub::splitAddSubHalves(
    const Part::TopoShape& halves,
    Part::TopoShape& addShape,
    Part::TopoShape& subShape
)
{
    if (halves.getShape().ShapeType() != TopAbs_COMPOUND) {
        addShape = halves;
        return;
    }
    const int count = halves.countSubShapes(TopAbs_SHAPE);
    if (count == 0) {
        throw Part::NullShapeException("Null AddSub shape");
    }
    Part::TopoShape added = halves.getSubTopoShape(TopAbs_SHAPE, 1);
    if (!added.isNull() && added.hasSubShape(TopAbs_SOLID)) {
        addShape = added;
    }
    if (count > 1) {
        Part::TopoShape cut = halves.getSubTopoShape(TopAbs_SHAPE, 2);
        if (!cut.isNull() && cut.hasSubShape(TopAbs_SOLID)) {
            subShape = cut;
        }
    }
}
void FeatureAddSub::updatePreviewShape()
{
    const auto notifyWarning = [](const QString& message) {
        Base::Console().translatedUserWarning(
            "Preview",
            tr("Failure while computing removed volume preview: %1").arg(message).toUtf8()
        );
    };

    // for subtractive shapes we want to also showcase removed volume, not only the tool
    if (addSubType == Subtractive) {
        TopoShape base = getBaseTopoShape(true).moved(getLocation().Inverted());
        const TopoShape& tool = AddSubShape.getShape();

        if (!tool.isEmpty()) {
            try {
                // Compute removed volume preview (for display)
                TopoShape common;
                common.makeElementBoolean(
                    Part::OpCodes::Common,
                    {base, tool},
                    "Preview",
                    Precision::Confusion()
                );

                // does CUT change volume?
                GProp_GProps propsBefore, propsAfter;
                BRepGProp::VolumeProperties(base.getShape(), propsBefore);

                TopoShape cut;
                cut.makeElementBoolean(
                    Part::OpCodes::Cut,
                    {base, tool},
                    "PreviewCheck",
                    Precision::Confusion()
                );

                BRepGProp::VolumeProperties(cut.getShape(), propsAfter);

                const double removed = propsBefore.Mass() - propsAfter.Mass();

                if (removed <= Precision::Confusion()) {
                    notifyWarning(
                        tr("Resulting shape is empty. That may indicate that no material will be "
                           "removed or a problem with the model.")
                    );
                }
                PreviewShape.setValue(common);
                return;
            }
            catch (Standard_Failure& e) {
                notifyWarning(QString::fromUtf8(e.GetMessageString()));
            }
            catch (Base::Exception& e) {
                notifyWarning(QString::fromStdString(e.getMessage()));
            }
            PreviewShape.setValue(base);
            return;
        }
    }

    PreviewShape.setValue(AddSubShape.getShape());
}

}  // namespace PartDesign

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PartDesign::FeatureAddSubPython, PartDesign::FeatureAddSub)
template<>
const char* PartDesign::FeatureAddSubPython::getViewProviderName() const
{
    return "PartDesignGui::ViewProviderPython";
}
template<>
PyObject* PartDesign::FeatureAddSubPython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<PartDesign::FeaturePy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class PartDesignExport FeaturePythonT<PartDesign::FeatureAddSub>;
}  // namespace App


namespace PartDesign
{

PROPERTY_SOURCE(PartDesign::FeatureAdditivePython, PartDesign::FeatureAddSubPython)

FeatureAdditivePython::FeatureAdditivePython()
{
    addSubType = Additive;
}

FeatureAdditivePython::~FeatureAdditivePython() = default;


PROPERTY_SOURCE(PartDesign::FeatureSubtractivePython, PartDesign::FeatureAddSubPython)

FeatureSubtractivePython::FeatureSubtractivePython()
{
    addSubType = Subtractive;
}

FeatureSubtractivePython::~FeatureSubtractivePython() = default;

}  // namespace PartDesign
