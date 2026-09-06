// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                *
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

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <QString>

#include <App/Application.h>
#include <Base/Reader.h>
#include <Gui/MetaTypes.h>
#include <src/App/InitApplication.h>

#include <Mod/Material/App/PropertyMaterial.h>

class PropertyMaterialRestore: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (App::Application::GetARGC() == 0) {
            tests::initApplication();
        }
    }
};

// A document stores nothing about its material but the identifier. Opening it on a machine whose
// library does not have that identifier used to leave the default material in its place, and the
// next save wrote the default's own identifier over the original -- so the part quietly stopped
// recording what it was made of, with nothing anywhere to say what it had been.
TEST_F(PropertyMaterialRestore, keepsAReferenceNoLibraryCanResolve)  // NOLINT
{
    const std::string missing = "deadbeef-0000-0000-0000-000000000000";
    // Wrapped in a root element, as the document itself would be, so the reader has
    // something to enter before it looks for the property.
    std::istringstream document("<root><PropertyMaterial uuid=\"" + missing + "\"/></root>");

    Base::XMLReader reader("PropertyMaterial", document);
    Materials::PropertyMaterial property;

    ASSERT_NO_THROW(property.Restore(reader))
        << "a material this system does not have is not a damaged document";

    EXPECT_EQ(property.getValue().getUUID().toStdString(), missing)
        << "the reference was replaced rather than kept, so saving would destroy it";
}
