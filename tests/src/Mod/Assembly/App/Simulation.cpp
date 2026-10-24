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
#include <App/DocumentObjectGroup.h>

#include <src/App/InitApplication.h>

#include <Mod/Assembly/App/AssemblyObject.h>
#include <Mod/Assembly/App/Motion.h>
#include <Mod/Assembly/App/Simulation.h>
#include <Mod/Assembly/App/SimulationGroup.h>

class SimulationTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        auto* doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");

        _assembly = doc->addObject<Assembly::AssemblyObject>();
        // A simulation is never held by the assembly directly: it lives inside the
        // assembly's SimulationGroup, which is how the workbench creates it.
        _simGroup = _assembly->addObject<Assembly::SimulationGroup>("simulations");
        _simulation = _simGroup->addObject<Assembly::Simulation>("simulation");
        _motion = _simulation->addObject<Assembly::Motion>("motion");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    Assembly::AssemblyObject* _assembly {nullptr};
    Assembly::SimulationGroup* _simGroup {nullptr};
    Assembly::Simulation* _simulation {nullptr};
    Assembly::Motion* _motion {nullptr};

private:
    std::string _docName;
};

// A simulation sits one container below the assembly. Looking only at its direct
// parents finds the SimulationGroup and stops, which is what the former Python version
// did: it never found the assembly, so a double-click on a simulation did nothing.
TEST_F(SimulationTest, findsTheAssemblyThroughItsContainer)  // NOLINT
{
    ASSERT_NE(_simulation->getAssembly(), nullptr);
    EXPECT_EQ(_simulation->getAssembly(), _assembly);
}

// A motion is two containers below the assembly: motion -> simulation -> group.
TEST_F(SimulationTest, motionFindsTheAssemblyThroughTwoContainers)  // NOLINT
{
    ASSERT_NE(_motion->getAssembly(), nullptr);
    EXPECT_EQ(_motion->getAssembly(), _assembly);
}

// The former version found the owning simulation by asking every object above it
// whether its Python proxy happened to own a method called
// "setMotionsChangedCallback" -- which would have matched any object carrying that
// name. The type answers the question directly.
TEST_F(SimulationTest, motionFindsItsSimulationByType)  // NOLINT
{
    EXPECT_EQ(_motion->getSimulation(), _simulation);
}

TEST_F(SimulationTest, holdsItsMotionsInOrder)  // NOLINT
{
    auto* second = _simulation->addObject<Assembly::Motion>("motion2");

    const auto motions = _simulation->getMotions();
    ASSERT_EQ(motions.size(), 2);
    EXPECT_EQ(motions[0], _motion);
    EXPECT_EQ(motions[1], second);
}

// A simulation's group may hold something that is not a motion; the solver must be
// handed motions only.
TEST_F(SimulationTest, reportsOnlyMotionsAsMotions)  // NOLINT
{
    _simulation->addObject<App::DocumentObjectGroup>("stray");

    const auto motions = _simulation->getMotions();
    ASSERT_EQ(motions.size(), 1);
    EXPECT_EQ(motions[0], _motion);
}

// One object, one group. A motion used to be created on the assembly and then also
// listed in the simulation's group; the Python group extension never ran this check,
// so the shipped code silently held a motion in two groups at once.
TEST_F(SimulationTest, aMotionBelongsToOneGroupOnly)  // NOLINT
{
    auto* otherGroup = _assembly->addObject<Assembly::SimulationGroup>("otherGroup");
    otherGroup->addObject(_motion);

    EXPECT_FALSE(_simulation->hasObject(_motion));
    EXPECT_TRUE(otherGroup->hasObject(_motion));
    EXPECT_EQ(_simulation->getMotions().size(), 0);
}

// The solver reads these directly; a default that drifts from what the panel showed
// would change every simulation nobody had touched.
TEST_F(SimulationTest, defaultsAreTheSolverSettings)  // NOLINT
{
    EXPECT_DOUBLE_EQ(_simulation->TimeStart.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(_simulation->TimeEnd.getValue(), 1.0);
    EXPECT_DOUBLE_EQ(_simulation->TimeStepOutput.getValue(), 1.0e-2);
    EXPECT_DOUBLE_EQ(_simulation->GlobalErrorTolerance.getValue(), 1.0e-6);
    EXPECT_EQ(_simulation->FramesPerSecond.getValue(), 30);
}

TEST_F(SimulationTest, motionReportsWhatItDrives)  // NOLINT
{
    _motion->MotionType.setValue("Angular");
    EXPECT_TRUE(_motion->isAngular());

    _motion->MotionType.setValue("Linear");
    EXPECT_FALSE(_motion->isAngular());
}
