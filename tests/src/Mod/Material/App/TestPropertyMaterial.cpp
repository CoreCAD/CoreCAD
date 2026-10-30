// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2026 Sean Barton (Cruth)                                *
 *                                                                         *
 *   This file is part of the Cruth CAD development system, a fork of      *
 *   FreeCAD.                                                              *
 *                                                                         *
 *   Cruth is free software: you can redistribute it and/or modify it      *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   Cruth is distributed in the hope that it will be useful, but          *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with Cruth. If not, see                                 *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <QString>

#include <App/Application.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/UnitsApi.h>
#include <Base/Writer.h>
#include <Gui/MetaTypes.h>
#include <src/App/InitApplication.h>

#include <Mod/Material/App/ModelUuids.h>
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

// The name is the only record of what an unresolvable material was called, so it has to come back
// out of a save exactly as it went in. Decorating it on restore -- to mark that it did not
// resolve -- writes the decoration back on the next save and grows it on every open after that.
TEST_F(PropertyMaterialRestore, doesNotRewriteTheNameOfAMaterialItCannotResolve)  // NOLINT
{
    const std::string missing = "deadbeef-0000-0000-0000-000000000000";
    const std::string named = "AISI 1020 Steel";
    std::istringstream document(
        "<root><PropertyMaterial uuid=\"" + missing + "\" name=\"" + named + "\"/></root>"
    );

    Base::XMLReader reader("PropertyMaterial", document);
    Materials::PropertyMaterial property;
    ASSERT_NO_THROW(property.Restore(reader));

    EXPECT_EQ(property.getValue().getName().toStdString(), named)
        << "the author's name for the material was rewritten on the way in";

    Base::StringWriter writer;
    property.Save(writer);
    EXPECT_NE(writer.getString().find("name=\"" + named + "\""), std::string::npos)
        << "a save wrote back something other than the name the document came with: "
            + writer.getString();
}

// A document depends on a library value it did not author, so it carries a copy of that value as
// well as its identity (Amendment 18 Clause 18.3). Without the copy, a part opened on a machine
// whose library does not have the material reads back at the default material's values -- so it
// weighs what the default weighs and is drawn in the default's colour, while still naming the
// material the author chose. The document would be stating one thing and computing another.
TEST_F(PropertyMaterialRestore, carriesTheValuesOfAMaterialNoLibraryCanResolve)  // NOLINT
{
    const std::string missing = "deadbeef-0000-0000-0000-000000000000";

    Materials::Material authored;
    authored.setUUID(QString::fromStdString(missing));
    authored.setName(QStringLiteral("Shop Steel"));
    authored.addPhysical(Materials::ModelUUIDs::ModelUUID_Mechanical_Density);
    authored.setPhysicalValue(QStringLiteral("Density"), QStringLiteral("7900 kg/m^3"));
    ASSERT_EQ(authored.getPhysicalValueString(QStringLiteral("Density")).toStdString(), "7900 kg/m^3")
        << "the test never gave the material a value to lose";

    Materials::PropertyMaterial property;
    property.setValue(authored);

    Base::StringWriter writer;
    property.Save(writer);

    std::istringstream document("<root>" + writer.getString() + "</root>");
    Base::XMLReader reader("PropertyMaterial", document);
    Materials::PropertyMaterial reopened;
    ASSERT_NO_THROW(reopened.Restore(reader));

    ASSERT_TRUE(reopened.getValue().hasPhysicalProperty(QStringLiteral("Density")))
        << "the document kept the material's name and carried none of what it was made of";
    EXPECT_EQ(
        reopened.getValue().getPhysicalValueString(QStringLiteral("Density")).toStdString(),
        "7900 kg/m^3"
    ) << "the value came back as something other than the one the author chose";
}

// What the record looks like, read by a person. The identity stays an attribute of the property
// and the copy is a block beneath it, one line per value, so a change of density is one changed
// line in a diff rather than a rewritten blob.
TEST_F(PropertyMaterialRestore, statesTheCopyAsOneLinePerValue)  // NOLINT
{
    Materials::Material authored;
    authored.setUUID(QStringLiteral("deadbeef-0000-0000-0000-000000000000"));
    authored.setName(QStringLiteral("Shop Steel"));
    authored.setLicense(QStringLiteral("CC-BY-3.0"));
    authored.addPhysical(Materials::ModelUUIDs::ModelUUID_Mechanical_Density);
    authored.setPhysicalValue(QStringLiteral("Density"), QStringLiteral("7900 kg/m^3"));

    Materials::PropertyMaterial property;
    property.setValue(authored);
    Base::StringWriter writer;
    property.Save(writer);
    const std::string record = writer.getString();

    EXPECT_NE(record.find("license=\"CC-BY-3.0\""), std::string::npos)
        << "the terms the material may be used on did not travel with it: " + record;
    EXPECT_NE(record.find("<Physical name=\"Density\""), std::string::npos)
        << "the value is not stated where a person reading the file would find it: " + record;
}

// A record whose meaning depends on a setting of the machine that wrote it is not a record. The
// material's own values are rendered for display through whichever unit schema the person has
// chosen, so a document saved on a machine set to US customary units would have stated this
// density in pounds per cubic inch -- and every machine reading it afterwards would have had to
// know which setting was in force when it was written.
TEST_F(PropertyMaterialRestore, statesValuesInTheUnitTheModelDeclares)  // NOLINT
{
    Materials::Material authored;
    authored.setUUID(QStringLiteral("deadbeef-0000-0000-0000-000000000000"));
    authored.addPhysical(Materials::ModelUUIDs::ModelUUID_Mechanical_Density);
    authored.setPhysicalValue(QStringLiteral("Density"), QStringLiteral("7900 kg/m^3"));

    Materials::PropertyMaterial property;
    property.setValue(authored);

    Base::UnitsApi::setSchema(std::string("Imperial"));
    Base::StringWriter writer;
    property.Save(writer);
    Base::UnitsApi::setSchema(std::string("Internal"));

    EXPECT_NE(writer.getString().find("value=\"7900 kg/m^3\""), std::string::npos)
        << "the value was written through the unit schema of the machine that saved it: "
            + writer.getString();
}

// Where the library HAS the material and holds different values for it, which of the two governs
// is the one question Clause 18.3 reserves. So a save may not answer it: re-deriving the copy
// from the material in memory would write the library's values over the ones the document was
// authored with, and the next reader would find a document that had quietly changed its mind
// about what the part is made of, with nothing left to say it ever held anything else.
TEST_F(PropertyMaterialRestore, keepsTheCopyItCameWithWhenTheLibraryHoldsSomethingElse)  // NOLINT
{
    // Steel-Generic, from the library that ships with the program.
    const std::string known = "90bbd8ef-8623-4d78-b3bf-e0bdb9b74dd3";
    Materials::PropertyMaterial fromLibrary;
    {
        std::istringstream naming("<root><PropertyMaterial uuid=\"" + known + "\"/></root>");
        Base::XMLReader reader("PropertyMaterial", naming);
        ASSERT_NO_THROW(fromLibrary.Restore(reader));
    }
    ASSERT_EQ(fromLibrary.getValue().getUUID().toStdString(), known)
        << "the material this test is built on is not in this system's library";

    Base::StringWriter asAuthored;
    fromLibrary.Save(asAuthored);
    std::string document = asAuthored.getString();
    const std::string libraryValue = "value=\"7900 kg/m^3\"";
    const std::string authoredValue = "value=\"8100 kg/m^3\"";
    ASSERT_NE(document.find(libraryValue), std::string::npos)
        << "the library's steel is not the density this test was written against: " + document;
    document.replace(document.find(libraryValue), libraryValue.size(), authoredValue);

    std::istringstream stated("<root>" + document + "</root>");
    Base::XMLReader reader("PropertyMaterial", stated);
    Materials::PropertyMaterial reopened;
    ASSERT_NO_THROW(reopened.Restore(reader));

    Base::StringWriter resaved;
    reopened.Save(resaved);
    EXPECT_NE(resaved.getString().find(authoredValue), std::string::npos)
        << "a save replaced the copy the document was authored with by the library's own value: "
            + resaved.getString();
}

// A save that changed nothing must change nothing, or every open and close of a document is a
// commit in a file people diff.
TEST_F(PropertyMaterialRestore, writesBackWhatItRead)  // NOLINT
{
    const std::string known = "90bbd8ef-8623-4d78-b3bf-e0bdb9b74dd3";
    Materials::PropertyMaterial property;
    {
        std::istringstream naming("<root><PropertyMaterial uuid=\"" + known + "\"/></root>");
        Base::XMLReader reader("PropertyMaterial", naming);
        ASSERT_NO_THROW(property.Restore(reader));
    }
    Base::StringWriter first;
    property.Save(first);

    std::istringstream stated("<root>" + first.getString() + "</root>");
    Base::XMLReader reader("PropertyMaterial", stated);
    Materials::PropertyMaterial reopened;
    ASSERT_NO_THROW(reopened.Restore(reader));
    Base::StringWriter second;
    reopened.Save(second);

    EXPECT_EQ(second.getString(), first.getString())
        << "reading a record and writing it straight back produced a different record";
}
