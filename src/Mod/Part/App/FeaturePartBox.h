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

#pragma once

#include <memory>

#include <App/PropertyStandard.h>
#include <App/PropertyGeo.h>

#include <Mod/Part/PartGlobal.h>

#include "PrimitiveFeature.h"


namespace Part
{

/** First concrete feature to route its shape query through the composed
 * Part::ShapeExtension capability (#79).
 *
 * Box carries the shape-source capability from the Part::ShapeFeature base (which
 * composes Part::ShapeExtension for the whole lineage) and overrides getSubObject
 * to delegate to the App base so the query dispatches to the extension rather than
 * the inherited ShapeFeature in-line resolution — proving the write/ownership side
 * of Amendment 17: a stored-backed feature can source its element-mapped shape from
 * the capability with byte-identical results, transform path included.
 */
class PartExport Box: public Part::Primitive
{
    PROPERTY_HEADER_WITH_OVERRIDE(Part::Box);

public:
    Box();

    App::PropertyLength Length, Height, Width;


    /** @name methods override feature */
    //@{
    /// recalculate the Feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    /// returns the type name of the ViewProvider
    const char* getViewProviderName() const override
    {
        return "PartGui::ViewProviderBox";
    }

    /// Route the shape-source query through the composed Part::ShapeExtension
    /// (App::DocumentObject::getSubObject dispatches to it) instead of the
    /// inherited Part::ShapeFeature override.
    App::DocumentObject* getSubObject(
        const char* subname,
        PyObject** pyObj,
        Base::Matrix4D* mat,
        bool transform,
        int depth
    ) const override;

protected:
    /** @name Reading a file older than the words this feature uses now
     *
     * Cruth: reading a property is the reader's work, and a feature that does it again is a
     * second reader that drifts from the first -- this one had no error handling, so a value it
     * could not read ended the object's read where it stood and every size stated after it came
     * back at its default, silently. What is left here is only what the reader cannot know: which
     * of this feature's own older words a name in the file means. The reader asks; the feature
     * answers.
     */
    //@{
    /// A name this feature no longer has: the sizes and positions of the 0.7 and 0.8 releases.
    void handleChangedPropertyName(
        Base::XMLReader& reader,
        const char* TypeName,
        const char* PropName
    ) override;
    /// A name this feature still has, said in a type it no longer uses.
    void handleChangedPropertyType(
        Base::XMLReader& reader,
        const char* TypeName,
        App::Property* prop
    ) override;
    /// The reader's work first; then what those older words add up to.
    void Restore(Base::XMLReader& reader) override;
    //@}

    /// get called by the container when a property has changed
    void onChanged(const App::Property* prop) override;
    //@}

private:
    /** What an older file stated in words this build no longer uses, gathered as it is read.
     *
     * Absent -- and never built -- for an ordinary file. A size or a position only means something
     * once the whole block has been read, so it is held until then and applied in one place.
     */
    struct OlderWords
    {
        bool sizes {false};
        bool positionXyz {false};
        bool positionAxis {false};
        App::PropertyDistance length, width, height;
        App::PropertyDistance x, y, z;
        App::PropertyVector axis, location;
    };
    std::unique_ptr<OlderWords> _olderWords;

    /// The older words being gathered, made on first use.
    OlderWords& olderWords();
    /// Restore into `into` only if the file states the type it is -- the check the shared reader
    /// makes for every other property, kept here because these properties are not the container's.
    static bool restoreLegacy(Base::XMLReader& reader, const char* TypeName, App::Property& into);
};

}  // namespace Part
