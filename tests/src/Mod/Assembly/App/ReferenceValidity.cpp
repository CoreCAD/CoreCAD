// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Cruth contributors

/****************************************************************************
 *   Copyright (c) 2026 Cruth contributors                                  *
 *                                                                          *
 *   This file is part of the Cruth CAD development system, a fork of       *
 *   FreeCAD.                                                               *
 *                                                                          *
 *   Cruth is free software: you can redistribute it and/or modify it       *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   Cruth is distributed in the hope that it will be useful, but           *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with Cruth. If not, see                                  *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/Part.h>

#include <src/App/InitApplication.h>

#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/AssemblyUtils.h>
#include <Mod/Assembly/App/Joint.h>
#include <Mod/Assembly/App/JointGroup.h>

/**
 * What a reference means when it cannot be resolved.
 *
 * The rule -- a "?" written into a sub-element name by the topological-naming
 * layer marks a reference that no longer finds what it named -- was written out
 * twice, once here and once privately inside Joint.cpp. Two copies of a rule drift.
 */
class ReferenceValidityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        doc = App::GetApplication().newDocument("refValidity");
        assembly = doc->addObject<Assembly::AssemblyObject>();
        component = doc->addObject<App::Part>("component");
        jointGroup = assembly->addObject<Assembly::JointGroup>("joints");
        joint = jointGroup->addObject<Assembly::Joint>("joint");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }

    App::Document* doc {};
    Assembly::AssemblyObject* assembly {};
    App::Part* component {};
    Assembly::JointGroup* jointGroup {};
    Assembly::Joint* joint {};
};

TEST_F(ReferenceValidityTest, anUnsetReferenceIsNotBroken)  // NOLINT
{
    // A joint half nobody has filled in yet is unfinished, not damaged: saying
    // otherwise would fail the recompute of every joint being made.
    EXPECT_FALSE(Assembly::hasBrokenReference(&joint->Reference1));
    EXPECT_FALSE(Assembly::isRefValid(&joint->Reference1));
}

TEST_F(ReferenceValidityTest, aReferenceToAnElementThatIsStillThereIsUsable)  // NOLINT
{
    joint->Reference1.setValue(component, {"Face1"});

    EXPECT_FALSE(Assembly::hasBrokenReference(&joint->Reference1));
    EXPECT_TRUE(Assembly::isRefValid(&joint->Reference1));
}

TEST_F(ReferenceValidityTest, aReferenceTheNamingLayerCouldNotResolveIsBroken)  // NOLINT
{
    joint->Reference1.setValue(component, {"?Face1"});

    EXPECT_TRUE(Assembly::hasBrokenReference(&joint->Reference1));
    EXPECT_FALSE(Assembly::isRefValid(&joint->Reference1));
}

TEST_F(ReferenceValidityTest, aJointRefusesToRecomputeOverABrokenReference)  // NOLINT
{
    joint->Reference1.setValue(component, {"?Face1"});

    App::DocumentObjectExecReturn* result = joint->execute();

    ASSERT_NE(result, App::DocumentObject::StdReturn);
    EXPECT_NE(std::string(result->Why).find("Reference1"), std::string::npos);
    delete result;  // NOLINT(cppcoreguidelines-owning-memory)
}
